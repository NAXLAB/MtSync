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

int DaemonProxy::allocate_request_id() {
    return m_next_request_id++;
}

void DaemonProxy::send_request(const std::string& type, const nlohmann::json& payload) {
    if (!m_client || !m_client->is_connected()) {
        return;
    }

    nlohmann::json msg = {
        {"type", type},
        {"payload", payload}
    };
    m_client->send_message(msg);
}

void DaemonProxy::on_message(const nlohmann::json& msg) {
    m_signal_message.emit(msg);

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

    nlohmann::json msg = {
        {"type", "get_jobs"},
        {"payload", {}}
    };
    m_client->send_message(msg);

    callback(std::vector<rclone::Job>{});
}

void DaemonProxy::add_job(const rclone::Job& job, IndexCallback callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "add_job"},
        {"payload", job}
    };
    m_client->send_message(msg);

    callback(0);
}

void DaemonProxy::update_job(size_t index, const rclone::Job& job, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "update_job"},
        {"payload", {
            {"index", index},
            {"job", job}
        }}
    };
    m_client->send_message(msg);

    callback({});
}

void DaemonProxy::delete_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "delete_job"},
        {"payload", {{"index", index}}}
    };
    m_client->send_message(msg);

    callback({});
}

void DaemonProxy::run_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "run_job"},
        {"payload", {{"index", index}}}
    };
    m_client->send_message(msg);

    callback({});
}

void DaemonProxy::stop_job(size_t index, std::function<void(std::expected<void, std::string>)> callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "stop_job"},
        {"payload", {{"index", index}}}
    };
    m_client->send_message(msg);

    callback({});
}

void DaemonProxy::get_remotes(RemoteCallback callback) {
    if (!is_connected()) {
        callback(std::unexpected("Not connected to daemon"));
        return;
    }

    nlohmann::json msg = {
        {"type", "get_remotes"},
        {"payload", {}}
    };
    m_client->send_message(msg);

    callback(std::vector<rclone::RemoteInfo>{});
}

} // namespace saddle
