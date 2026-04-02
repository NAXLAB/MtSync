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

#include "daemon_proxy.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace saddle {

DaemonProxy::DaemonProxy() = default;

DaemonProxy::~DaemonProxy() {
    disconnect();
}

bool DaemonProxy::connect() {
    if (m_client && m_client->is_connected()) {
        return true;
    }

    m_client = std::make_unique<ipc::IpcClient>();
    m_client->signal_received().connect(sigc::mem_fun(*this, &DaemonProxy::on_message));

    if (!m_client->connect()) {
        m_client.reset();
        return false;
    }

    return true;
}

void DaemonProxy::disconnect() {
    if (m_client) {
        m_client->disconnect();
        m_client.reset();
    }
    m_pending_callbacks.clear();
}

void DaemonProxy::send_request(ipc::RequestType type, const json& payload,
                                std::function<void(const json&)> on_response) {
    if (!m_client || !m_client->is_connected()) {
        return;
    }

    int req_id = m_next_request_id++;

    json msg = {
        {"type", type},
        {"request_id", req_id},
        {"payload", payload}
    };

    if (on_response) {
        m_pending_callbacks[req_id] = std::move(on_response);
    }

    m_client->send_message(msg);
}

void DaemonProxy::on_message(const json& msg) {
    // Always broadcast for signal-based listeners (e.g. JobView)
    m_signal_message.emit(msg);

    // If the response carries a request_id, invoke the matching pending callback
    auto it = msg.find("request_id");
    if (it != msg.end() && it.value().is_number()) {
        int req_id = it.value().get<int>();
        auto cb_it = m_pending_callbacks.find(req_id);
        if (cb_it != m_pending_callbacks.end()) {
            cb_it->second(msg);
            m_pending_callbacks.erase(cb_it);
        }
    }
}

void DaemonProxy::get_jobs(JobCallback callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::GetJobs, {}, [callback = std::move(callback)](const json& msg) {
        auto payload = msg.value("payload", json{});
        if (payload.contains("error")) {
            callback(std::unexpected(payload["error"].get<std::string>()));
            return;
        }
        std::vector<rclone::Job> jobs;
        if (payload.contains("jobs")) {
            for (auto& j : payload["jobs"])
                jobs.push_back(j.get<rclone::Job>());
        }
        callback(std::move(jobs));
    });
}

void DaemonProxy::add_job(const rclone::Job& job, IndexCallback callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::AddJob, job, [callback = std::move(callback)](const json& msg) {
        auto payload = msg.value("payload", json{});
        if (payload.contains("error")) {
            callback(std::unexpected(payload["error"].get<std::string>()));
            return;
        }
        callback(payload.value("index", size_t{0}));
    });
}

void DaemonProxy::update_job(size_t index, const rclone::Job& job, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    json payload = {{"index", index}, {"job", job}};
    send_request(ipc::RequestType::UpdateJob, payload, [callback = std::move(callback)](const json& msg) {
        auto p = msg.value("payload", json{});
        if (p.contains("error")) {
            callback(std::unexpected(p["error"].get<std::string>()));
            return;
        }
        callback({});
    });
}

void DaemonProxy::delete_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::DeleteJob, {{"index", index}}, [callback = std::move(callback)](const json& msg) {
        auto p = msg.value("payload", json{});
        if (p.contains("error")) {
            callback(std::unexpected(p["error"].get<std::string>()));
            return;
        }
        callback({});
    });
}

void DaemonProxy::run_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::RunJob, {{"index", index}}, [callback = std::move(callback)](const json& msg) {
        auto p = msg.value("payload", json{});
        if (p.contains("error")) {
            callback(std::unexpected(p["error"].get<std::string>()));
            return;
        }
        callback({});
    });
}

void DaemonProxy::stop_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::StopJob, {{"index", index}}, [callback = std::move(callback)](const json& msg) {
        auto p = msg.value("payload", json{});
        if (p.contains("error")) {
            callback(std::unexpected(p["error"].get<std::string>()));
            return;
        }
        callback({});
    });
}

void DaemonProxy::get_remotes(RemoteCallback callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    send_request(ipc::RequestType::GetRemotes, {}, [callback = std::move(callback)](const json& msg) {
        auto payload = msg.value("payload", json{});
        if (payload.contains("error")) {
            callback(std::unexpected(payload["error"].get<std::string>()));
            return;
        }
        std::vector<rclone::RemoteInfo> remotes;
        if (payload.contains("remotes")) {
            for (auto& r : payload["remotes"]) {
                remotes.push_back({r.value("name", ""), r.value("type", "")});
            }
        }
        callback(std::move(remotes));
    });
}

void DaemonProxy::quit() {
    send_request(ipc::RequestType::Quit, {});
}

} // namespace saddle
