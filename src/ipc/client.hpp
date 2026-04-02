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

#include <glibmm.h>
#include <giomm.h>
#include <sigc++/sigc++.h>
#include <nlohmann/json.hpp>

namespace saddle::ipc {

std::string get_socket_path();

class IpcClient : public sigc::trackable {
public:
    IpcClient();
    ~IpcClient();

    bool connect();
    void disconnect();
    bool is_connected() const { return m_fd >= 0; }

    void send_message(const nlohmann::json& msg);
    sigc::signal<void(const nlohmann::json&)>& signal_received() { return m_signal_received; }

private:
    bool on_io_watch(Glib::IOCondition condition);
    void read_message();

    int m_fd = -1;
    guint m_watch_id = 0;
    std::string m_buffer;
    sigc::signal<void(const nlohmann::json&)> m_signal_received;
};

} // namespace saddle::ipc
