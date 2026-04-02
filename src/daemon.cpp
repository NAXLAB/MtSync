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

#include "daemon.hpp"
#include "rclone/cron_utils.hpp"
#include "ipc/protocol.hpp"
#include <format>
#include <iostream>
#include <glibmm.h>

using json = nlohmann::json;

namespace {

json make_response(saddle::ipc::ResponseType type, const json& payload,
                   const json& request = json{}) {
    json resp = {
        {"type", type},
        {"payload", payload}
    };
    if (request.contains("request_id")) {
        resp["request_id"] = request["request_id"];
    }
    return resp;
}

} // namespace

namespace saddle {

SaddleDaemon::SaddleDaemon() {
    auto config_dir = fs::path(g_get_user_config_dir()) / "saddle";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "jobs.json").string();

    load_jobs();

    m_tray = std::make_unique<TrayIcon>();
    m_tray->set_tooltip("Saddle - rclone GUI");
    m_tray->signal_show_window().connect([this]() {
        if (m_ipc_server->client_count() > 0) {
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::ShowWindow, {}));
        } else {
            // No GUI connected — launch one
            auto exe = Glib::find_program_in_path("saddle");
            if (exe.empty()) exe = "/proc/self/exe";
            try {
                Glib::spawn_async({}, {exe});
            } catch (const Glib::Error& e) {
                g_warning("Failed to launch GUI: %s", e.what());
            }
        }
    });
    m_tray->signal_quit().connect([this]() {
        stop();
    });

    m_ipc_server = std::make_unique<ipc::IpcServer>();
    m_ipc_server->signal_message().connect([this](const json& msg) {
        auto type_str = msg.value("type", "");
        auto payload = msg.value("payload", json{});

        try {
            if (type_str == "get_jobs") {
                json response_payload = {{"jobs", m_jobs}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobsList, response_payload, msg));

            } else if (type_str == "add_job") {
                m_jobs.push_back(payload.get<rclone::Job>());
                save_jobs();
                schedule_all_jobs();
                json response_payload = {{"index", m_jobs.size() - 1}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobAdded, response_payload, msg));

            } else if (type_str == "update_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    m_jobs[index] = payload.value("job", rclone::Job{});
                    save_jobs();
                    schedule_all_jobs();
                    json response_payload = {{"index", index}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobUpdated, response_payload, msg));
                }

            } else if (type_str == "delete_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    m_jobs.erase(m_jobs.begin() + index);
                    save_jobs();
                    json response_payload = {{"index", index}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobDeleted, response_payload, msg));
                }

            } else if (type_str == "run_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    on_run_job(index);
                    json response_payload = {{"index", index}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload, msg));
                }

            } else if (type_str == "stop_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_job_ids.size() && m_job_ids[index] >= 0) {
                    m_manager.rc().job_stop(m_job_ids[index], [this, index, msg](auto) {
                        if (index < m_poll_timers.size()) {
                            m_poll_timers[index].disconnect();
                        }
                        json response_payload = {{"index", index}};
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStopped, response_payload, msg));
                    });
                }

            } else if (type_str == "get_remotes") {
                m_manager.cli().list_remotes([this, msg](std::expected<std::vector<rclone::RemoteInfo>, std::string> result) {
                    json response_payload;
                    if (result.has_value()) {
                        response_payload = {{"remotes", json::array()}};
                        for (auto& remote : result.value()) {
                            response_payload["remotes"].push_back({
                                {"name", remote.name},
                                {"type", remote.type}
                            });
                        }
                    } else {
                        response_payload = {{"error", result.error()}};
                    }
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::RemotesList, response_payload, msg));
                });

            } else if (type_str == "quit") {
                stop();

            } else {
                json response_payload = {{"error", "Unknown request type: " + type_str}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::Error, response_payload, msg));
            }
        } catch (const std::exception& e) {
            json response_payload = {{"error", std::string(e.what())}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::Error, response_payload, msg));
        }
    });

    schedule_all_jobs();
}

SaddleDaemon::~SaddleDaemon() {
    stop();
}

void SaddleDaemon::stop() {
    m_running = false;

    for (auto& conn : m_poll_timers) conn.disconnect();
    for (auto& conn : m_sched_timers) conn.disconnect();
    m_poll_timers.clear();
    m_sched_timers.clear();

    m_ipc_server.reset();
    m_tray.reset();

    m_manager.rc().stop_daemon();
}

void SaddleDaemon::run() {
    if (!m_ipc_server->start()) {
        g_error("Failed to start IPC server");
        return;
    }

    g_message("Saddle daemon started");

    while (m_running) {
        g_main_context_iteration(nullptr, true);
    }

    g_message("Saddle daemon stopped");
}

void SaddleDaemon::load_jobs() {
    if (fs::exists(m_config_path)) {
        try {
            std::ifstream f(m_config_path);
            auto j = json::parse(f);
            if (j.contains("jobs")) {
                for (auto& p : j["jobs"])
                    m_jobs.push_back(p.get<rclone::Job>());
            }
        } catch (const std::exception& e) {
            g_warning("Failed to load jobs: %s", e.what());
        }
    }
}

void SaddleDaemon::save_jobs() {
    json j;
    j["jobs"] = json::array();
    for (auto& job : m_jobs)
        j["jobs"].push_back(job);

    fs::create_directories(fs::path(m_config_path).parent_path());
    std::ofstream f(m_config_path);
    f << j.dump(2);
}

void SaddleDaemon::schedule_all_jobs() {
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].schedule_enabled) {
            schedule_job(i);
        }
    }
}

void SaddleDaemon::schedule_job(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];
    if (!job.schedule_enabled) return;

    GDateTime* now = g_date_time_new_now_local();
    GDateTime* next = cron::next_occurrence(job, now);
    g_date_time_unref(now);

    if (!next) return;

    GDateTime* now2 = g_date_time_new_now_local();
    gint64 diff_us = g_date_time_difference(next, now2);
    g_date_time_unref(now2);
    g_date_time_unref(next);

    if (diff_us <= 0) {
        on_run_job(index);
        return;
    }

    constexpr gint64 MAX_DELAY_MS = 24LL * 3600 * 1000;
    auto delay_ms = static_cast<unsigned int>(std::min(diff_us / 1000, MAX_DELAY_MS));

    if (index >= m_sched_timers.size()) {
        m_sched_timers.resize(index + 1);
    }

    m_sched_timers[index] = Glib::signal_timeout().connect(
        [this, index]() -> bool {
            on_run_job(index);
            return false;
        }, delay_ms);
}

void SaddleDaemon::on_run_job(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];

    // Ensure daemon is running
    m_manager.rc().ensure_daemon([this, index, job](auto result) {
        if (!result.has_value()) {
            g_warning("Failed to start rclone daemon: %s", result.error().c_str());
            return;
        }

        nlohmann::json opts;
        if (job.dry_run) opts["_config"] = {{"DryRun", true}};
        if (!job.bandwidth.empty()) opts["_config"]["BwLimit"] = job.bandwidth;
        if (!job.includes.empty()) {
            json filter = json::array();
            for (auto& inc : job.includes) {
                filter.push_back({{"IncludeRule", {{"Pattern", inc}}}});
            }
            opts["_config"]["Filter"] = filter;
        }

        auto done_cb = [this, index](auto result) {
            if (!result.has_value()) {
                g_warning("Job %zu failed: %s", index, result.error().c_str());
                on_job_completed(index, false);
                return;
            }

            if (index >= m_job_ids.size()) m_job_ids.resize(index + 1, -1);
            m_job_ids[index] = result.value();

            // Start polling for progress
            if (index >= m_poll_timers.size()) {
                m_poll_timers.resize(index + 1);
            }

            m_poll_timers[index] = Glib::signal_timeout().connect(
                [this, index]() -> bool {
                    m_manager.rc().job_status(m_job_ids[index], [this, index](auto status) {
                        if (status.has_value() && status->finished) {
                            on_job_completed(index, status->success);
                            return;
                        }
                    });
                    m_manager.rc().get_stats([this, index](auto) {
                        // Could emit progress updates here
                    });
                    return true;
                }, 500);
        };

        switch (job.type) {
            case rclone::JobType::Sync:
                m_manager.rc().sync_async(job.source, job.destination, opts, done_cb);
                break;
            case rclone::JobType::Copy:
                m_manager.rc().copy_async(job.source, job.destination, opts, done_cb);
                break;
            case rclone::JobType::Move:
                m_manager.rc().move_async(job.source, job.destination, opts, done_cb);
                break;
        }
    });
}

void SaddleDaemon::on_job_completed(size_t index, bool success) {
    if (index < m_poll_timers.size()) {
        m_poll_timers[index].disconnect();
    }

    auto now = Glib::DateTime::create_now_local().format_iso8601();
    if (index < m_jobs.size()) {
        auto& job = m_jobs[index];
        job.last_status = success ? "success" : "error";
        job.last_run = now;
        save_jobs();

        std::string title = success ? "Sync Complete" : "Sync Failed";
        std::string body = job.source + " → " + job.destination;
        send_notification(title, body);

        if (job.schedule_enabled) {
            schedule_job(index);
        }
    }

    m_tray->set_attention(success);
}

} // namespace saddle
