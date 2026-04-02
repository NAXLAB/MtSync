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

#include "ipc/client.hpp"
#include "ipc/protocol.hpp"
#include "rclone/rclone_types.hpp"
#include <sigc++/sigc++.h>
#include <vector>
#include <map>
#include <functional>

namespace saddle {

class DaemonProxy : public sigc::trackable {
public:
    using JobCallback = std::function<void(std::expected<std::vector<rclone::Job>, std::string>)>;
    using RemoteCallback = std::function<void(std::expected<std::vector<rclone::RemoteInfo>, std::string>)>;
    using IndexCallback = std::function<void(std::expected<size_t, std::string>)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    DaemonProxy();
    ~DaemonProxy();

    bool connect();
    void disconnect();
    bool is_connected() const { return m_client && m_client->is_connected(); }

    void get_jobs(JobCallback callback);
    void add_job(const rclone::Job& job, IndexCallback callback);
    void update_job(size_t index, const rclone::Job& job, std::function<void(std::expected<void, std::string>)> callback);
    void delete_job(size_t index, std::function<void(std::expected<void, std::string>)> callback);
    void run_job(size_t index, std::function<void(std::expected<void, std::string>)> callback);
    void stop_job(size_t index, std::function<void(std::expected<void, std::string>)> callback);
    void get_remotes(RemoteCallback callback);
    void quit();

    sigc::signal<void(const nlohmann::json&)>& signal_message() { return m_signal_message; }

private:
    void on_message(const nlohmann::json& msg);
    void send_request(ipc::RequestType type, const nlohmann::json& payload,
                      std::function<void(const nlohmann::json&)> on_response = nullptr);

    std::unique_ptr<ipc::IpcClient> m_client;
    int m_next_request_id = 1;
    std::map<int, std::function<void(const nlohmann::json&)>> m_pending_callbacks;
    sigc::signal<void(const nlohmann::json&)> m_signal_message;
};

} // namespace saddle
