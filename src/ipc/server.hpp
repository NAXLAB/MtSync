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

#pragma once

#include <glib.h>
#include <sigc++/sigc++.h>
#include <nlohmann/json.hpp>
#include <map>

namespace saddle::ipc {

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    bool start();
    void stop();

    void send_to_all(const nlohmann::json& msg);
    void send_to(int client_fd, const nlohmann::json& msg);
    size_t client_count() const { return m_clients.size(); }

    sigc::signal<void()>& signal_client_connected() { return m_signal_client_connected; }
    sigc::signal<void()>& signal_client_disconnected() { return m_signal_client_disconnected; }
    sigc::signal<void(const nlohmann::json&)>& signal_message() { return m_signal_message; }

private:
    struct ClientData {
        std::string buffer;
        guint watch_id;
    };

    static gboolean on_server_watch_static(GIOChannel*, GIOCondition, gpointer);
    gboolean on_server_watch(GIOChannel*, GIOCondition);
    void accept_client();

    static gboolean on_client_watch_static(gint, GIOCondition, gpointer);
    gboolean on_client_watch(int fd, GIOCondition condition);
    void read_from_client(int fd);
    void remove_client(int fd);

    int m_server_fd = -1;
    guint m_server_watch_id = 0;
    std::map<int, ClientData> m_clients;
    sigc::signal<void()> m_signal_client_connected;
    sigc::signal<void()> m_signal_client_disconnected;
    sigc::signal<void(const nlohmann::json&)> m_signal_message;
};

} // namespace saddle::ipc
