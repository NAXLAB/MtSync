/*
 * Saddle — GTK4 frontend to rclone
 * Copyright (C) 2026  Saddle contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "server.hpp"
#include "protocol.hpp"
#include <glib.h>
#include <glib/giochannel.h>
#include <glib-unix.h>
#include <format>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace saddle::ipc {

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    auto socket_path = get_socket_path();
    
    fs::path dir(fs::path(socket_path).parent_path());
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    
    if (fs::exists(socket_path)) {
        fs::remove(socket_path);
    }

    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        g_warning("Failed to create server socket: %s", g_strerror(errno));
        return false;
    }

    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        g_warning("Socket path too long (%zu bytes)", socket_path.size());
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        g_warning("Failed to bind socket: %s", g_strerror(errno));
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    ::chmod(socket_path.c_str(), 0600);

    if (::listen(m_server_fd, 5) < 0) {
        g_warning("Failed to listen: %s", g_strerror(errno));
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    int flags = fcntl(m_server_fd, F_GETFL, 0);
    fcntl(m_server_fd, F_SETFL, flags | O_NONBLOCK);

    GIOChannel* channel = g_io_channel_unix_new(m_server_fd);
    m_server_watch_id = g_io_add_watch(channel, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR),
        &IpcServer::on_server_watch_static, this);
    g_io_channel_unref(channel);

    g_message("IPC server listening on %s", socket_path.c_str());
    return true;
}

void IpcServer::stop() {
    if (m_server_watch_id > 0) {
        g_source_remove(m_server_watch_id);
        m_server_watch_id = 0;
    }
    
    for (auto& [fd, data] : m_clients) {
        if (data.watch_id > 0) {
            g_source_remove(data.watch_id);
        }
        ::close(fd);
    }
    m_clients.clear();

    if (m_server_fd >= 0) {
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    
    auto socket_path = get_socket_path();
    if (fs::exists(socket_path)) {
        fs::remove(socket_path);
    }
}

void IpcServer::send_to(int client_fd, const nlohmann::json& msg) {
    auto data = msg.dump();
    uint32_t len = static_cast<uint32_t>(data.size());

    std::string packet;
    packet.append(reinterpret_cast<const char*>(&len), sizeof(len));
    packet += data;

    ::send(client_fd, packet.data(), packet.size(), 0);
}

void IpcServer::send_to_all(const nlohmann::json& msg) {
    for (auto& [fd, data] : m_clients) {
        send_to(fd, msg);
    }
}

gboolean IpcServer::on_server_watch_static(GIOChannel*, GIOCondition condition, gpointer data) {
    return static_cast<IpcServer*>(data)->on_server_watch(nullptr, condition);
}

gboolean IpcServer::on_server_watch(GIOChannel*, GIOCondition condition) {
    auto cond = static_cast<GIOCondition>(condition);
    
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        return FALSE;
    }

    if (cond & G_IO_IN) {
        accept_client();
    }

    return TRUE;
}

void IpcServer::accept_client() {
    int client_fd = ::accept(m_server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            g_warning("accept failed: %s", g_strerror(errno));
        }
        return;
    }

    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    ClientData data;
    data.watch_id = g_unix_fd_add(client_fd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
        &IpcServer::on_client_watch_static, this);
    m_clients[client_fd] = std::move(data);

    m_signal_client_connected.emit();
    g_message("Client connected (fd=%d), total clients: %zu", client_fd, m_clients.size());
}

gboolean IpcServer::on_client_watch_static(gint fd, GIOCondition condition, gpointer data) {
    return static_cast<IpcServer*>(data)->on_client_watch(fd, condition);
}

gboolean IpcServer::on_client_watch(int fd, GIOCondition condition) {
    auto cond = static_cast<GIOCondition>(condition);
    
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        remove_client(fd);
        return FALSE;
    }
    
    if (cond & G_IO_IN) {
        read_from_client(fd);
    }

    return TRUE;
}

void IpcServer::read_from_client(int fd) {
    char buf[4096];
    auto n = ::recv(fd, buf, sizeof(buf), 0);
    
    if (n <= 0) {
        if (n < 0 && errno == EAGAIN) return;
        remove_client(fd);
        return;
    }

    auto it = m_clients.find(fd);
    if (it == m_clients.end()) return;
    
    it->second.buffer.append(buf, n);

    while (it->second.buffer.size() >= 4) {
        uint32_t size;
        std::memcpy(&size, it->second.buffer.data(), sizeof(size));

        if (size > 1024 * 1024) {
            g_warning("Message too large: %u bytes", size);
            remove_client(fd);
            return;
        }

        if (it->second.buffer.size() < 4 + size) return;

        auto msg_str = it->second.buffer.substr(4, size);
        it->second.buffer.erase(0, 4 + size);

        try {
            auto msg = nlohmann::json::parse(msg_str);
            m_signal_message.emit(msg);
        } catch (const nlohmann::json::parse_error& e) {
            g_warning("JSON parse error: %s", e.what());
        }
    }
}

void IpcServer::remove_client(int fd) {
    auto it = m_clients.find(fd);
    if (it != m_clients.end()) {
        if (it->second.watch_id > 0) {
            g_source_remove(it->second.watch_id);
        }
        ::close(fd);
        m_clients.erase(it);
        m_signal_client_disconnected.emit();
        g_message("Client disconnected (fd=%d), remaining: %zu", fd, m_clients.size());
    }
}

} // namespace saddle::ipc
