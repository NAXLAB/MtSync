/*
 * Mt. Sync — GTK4 frontend to rclone
 * Copyright (C) 2026  Mt. Sync contributors
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

namespace mtsync::ipc {

static bool write_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = ::send(fd, buf, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    auto socket_path = get_socket_path();
    
    fs::create_directories(fs::path(socket_path).parent_path());

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
        if (errno == EADDRINUSE) {
            // Test whether a live daemon already owns this socket
            int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe < 0) {
                g_warning("Failed to create probe socket: %s", g_strerror(errno));
                ::close(m_server_fd);
                m_server_fd = -1;
                return false;
            }
            bool live = (::connect(probe, (struct sockaddr*)&addr, sizeof(addr)) == 0);
            ::close(probe);
            if (live) {
                g_warning("Another daemon instance is already running at %s", socket_path.c_str());
                ::close(m_server_fd);
                m_server_fd = -1;
                return false;
            }
            // Stale socket — remove and retry
            ::unlink(socket_path.c_str());
            if (::bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                g_warning("Failed to bind socket after removing stale socket: %s", g_strerror(errno));
                ::close(m_server_fd);
                m_server_fd = -1;
                return false;
            }
        } else {
            g_warning("Failed to bind socket: %s", g_strerror(errno));
            ::close(m_server_fd);
            m_server_fd = -1;
            return false;
        }
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
    
    ::unlink(get_socket_path().c_str());
}

void IpcServer::send_to(int client_fd, const std::string& serialized) {
    if (serialized.size() > 1024 * 1024) {
        g_warning("Outbound message too large: %zu bytes, dropping", serialized.size());
        return;
    }
    uint32_t len = static_cast<uint32_t>(serialized.size());
    if (!write_all(client_fd, reinterpret_cast<const char*>(&len), sizeof(len)) ||
        !write_all(client_fd, serialized.data(), serialized.size()))
        g_warning("send to client fd=%d: %s (will be removed on next HUP/ERR)", client_fd, g_strerror(errno));
}

void IpcServer::send_to(int client_fd, const nlohmann::json& msg) {
    send_to(client_fd, msg.dump());
}

void IpcServer::send_to_all(const nlohmann::json& msg) {
    auto serialized = msg.dump();
    for (auto& [fd, _] : m_clients) {
        send_to(fd, serialized);
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
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        remove_client(fd);
        return;
    }

    auto it = m_clients.find(fd);
    if (it == m_clients.end()) return;

    auto& cbuf = it->second.buffer;
    auto& pos  = it->second.read_pos;

    cbuf.append(buf, n);

    while (cbuf.size() - pos >= 4) {
        uint32_t size;
        std::memcpy(&size, cbuf.data() + pos, sizeof(size));

        if (size > 1024 * 1024) {
            g_warning("Message too large: %u bytes", size);
            remove_client(fd);
            return;
        }

        if (cbuf.size() - pos < 4 + size) break;

        auto msg_sv = std::string_view(cbuf.data() + pos + 4, size);
        pos += 4 + size;

        try {
            auto msg = nlohmann::json::parse(msg_sv);
            m_signal_message.emit(msg);
        } catch (const nlohmann::json::parse_error& e) {
            g_warning("JSON parse error: %s", e.what());
        }
    }

    // Compact consumed bytes: O(1) when fully drained, O(remaining) otherwise
    if (pos > 0) {
        if (pos == cbuf.size()) {
            cbuf.clear();
        } else {
            cbuf.erase(0, pos);
        }
        pos = 0;
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

} // namespace mtsync::ipc
