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

#include "client.hpp"
#include <giomm.h>
#include <glibmm.h>
#include <filesystem>
#include <format>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <glib-unix.h>

namespace saddle::ipc {

std::string get_socket_path() {
    auto* user_cache = g_get_user_cache_dir();
    auto dir = std::filesystem::path(user_cache) / "saddle";
    std::filesystem::create_directories(dir);
    return (dir / "socket").string();
}

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
    disconnect();
}

bool IpcClient::connect() {
    if (is_connected()) return true;

    auto socket_path = get_socket_path();
    
    m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_fd < 0) {
        g_warning("Failed to create socket: %s", g_strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(m_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        g_warning("Failed to connect to %s: %s", socket_path.c_str(), g_strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

    m_watch_id = g_unix_fd_add(m_fd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
        [](gint, GIOCondition condition, gpointer data) -> gboolean {
            return static_cast<IpcClient*>(data)->on_io_watch(static_cast<Glib::IOCondition>(condition));
        }, this);

    g_message("Connected to daemon");
    return true;
}

void IpcClient::disconnect() {
    if (m_watch_id > 0) {
        g_source_remove(m_watch_id);
        m_watch_id = 0;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_buffer.clear();
}

void IpcClient::send_message(const nlohmann::json& msg) {
    if (!is_connected()) return;

    auto data = msg.dump();
    uint32_t len = static_cast<uint32_t>(data.size());

    std::string packet;
    packet.append(reinterpret_cast<const char*>(&len), sizeof(len));
    packet += data;

    ::send(m_fd, packet.data(), packet.size(), 0);
}

bool IpcClient::on_io_watch(Glib::IOCondition condition) {
    auto cond = static_cast<GIOCondition>(condition);
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        disconnect();
        return false;
    }

    if (cond & G_IO_IN) {
        read_message();
    }

    return is_connected();
}

void IpcClient::read_message() {
    char buf[4096];
    auto n = ::recv(m_fd, buf, sizeof(buf), 0);
    
    if (n <= 0) {
        if (n < 0 && errno == EAGAIN) return;
        disconnect();
        return;
    }

    m_buffer.append(buf, n);

    while (m_buffer.size() >= 4) {
        uint32_t size;
        std::memcpy(&size, m_buffer.data(), sizeof(size));

        if (size > 1024 * 1024) {
            g_warning("Message too large: %u bytes", size);
            disconnect();
            return;
        }

        if (m_buffer.size() < 4 + size) return;

        auto msg_str = m_buffer.substr(4, size);
        m_buffer.erase(0, 4 + size);

        try {
            auto msg = nlohmann::json::parse(msg_str);
            m_signal_received.emit(msg);
        } catch (const nlohmann::json::parse_error& e) {
            g_warning("JSON parse error: %s", e.what());
        }
    }
}

} // namespace saddle::ipc
