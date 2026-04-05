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
#include "settings.hpp"
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

const char* type_str(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return "SYNC";
        case rclone::JobType::Copy:  return "COPY";
        case rclone::JobType::Move:  return "MOVE";
        case rclone::JobType::Mount: return "MOUNT";
    }
    return "?";
}

std::string format_bytes(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string format_speed(double bps) {
    if (bps < 1024) return std::format("{:.0f} B/s", bps);
    if (bps < 1024 * 1024) return std::format("{:.1f} KB/s", bps / 1024);
    if (bps < 1024 * 1024 * 1024) return std::format("{:.1f} MB/s", bps / (1024.0 * 1024));
    return std::format("{:.1f} GB/s", bps / (1024.0 * 1024 * 1024));
}

void append_log(const std::string& line) {
    auto dir = fs::path(g_get_user_state_dir()) / "saddle";
    fs::create_directories(dir);
    std::ofstream f(dir / "saddle.log", std::ios::app);
    if (!f) return;
    auto ts = Glib::DateTime::create_now_local().format_iso8601();
    f << "[" << ts.raw() << "] " << line << "\n";
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
                    // Disconnect timers and clean up all parallel vectors before erasing
                    if (index < m_sched_timers.size()) m_sched_timers[index].disconnect();
                    if (index < m_poll_timers.size())  m_poll_timers[index].disconnect();
                    auto erase_at = [](auto& vec, size_t i) {
                        if (i < vec.size()) vec.erase(vec.begin() + i);
                    };
                    m_jobs.erase(m_jobs.begin() + index);
                    erase_at(m_job_ids,      index);
                    erase_at(m_poll_timers,  index);
                    erase_at(m_last_stats,   index);
                    erase_at(m_sched_timers, index);
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
                if (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount) {
                    m_manager.rc().unmount_async(m_jobs[index].destination, [this, index](auto) {
                        if (index < m_jobs.size()) m_jobs[index].active = false;
                        json response_payload = {{"index", index}, {"success", true}};
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    });
                } else if (index < m_job_ids.size() && m_job_ids[index] >= 0) {
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

    m_manager.rc().ensure_daemon([this](auto result) {
        if (!result.has_value()) return;

        // Auto-mount jobs flagged with mount_at_startup
        for (size_t i = 0; i < m_jobs.size(); ++i) {
            if (m_jobs[i].type == rclone::JobType::Mount && m_jobs[i].mount_at_startup)
                on_run_job(i);
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

    // Skip if a previous instance is still running
    bool already_running = job.type == rclone::JobType::Mount
        ? job.active
        : (index < m_job_ids.size() && m_job_ids[index] >= 0);
    if (already_running) {
        append_log(std::format("SKIPPED   {} [{}] previous instance still running",
            job.id, type_str(job.type)));
        return;
    }

    append_log(std::format("STARTED   {} [{}] {} -> {}",
        job.id, type_str(job.type), job.source, job.destination));

    m_tray->start_animation();

    if (job.type == rclone::JobType::Mount) {
        m_jobs[index].active = true;
        json response_payload = {{"index", index}};
        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload));
        m_manager.rc().mount_async(job.source, job.destination,
            [this, index](auto result) {
                json response_payload = {{"index", index}};
                if (result.has_value()) {
                    response_payload["success"] = true;
                    if (index < m_jobs.size()) m_jobs[index].active = true;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    if (index < m_jobs.size())
                        append_log(std::format("COMPLETED {} [MOUNT] SUCCESS",
                            m_jobs[index].id));
                } else {
                    response_payload["success"] = false;
                    if (index < m_jobs.size()) m_jobs[index].active = false;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    if (index < m_jobs.size())
                        append_log(std::format("COMPLETED {} [MOUNT] FAILED -- {}",
                            m_jobs[index].id, result.error()));
                    g_warning("Mount %zu failed: %s", index, result.error().c_str());
                }
            });
        return;
    }

    json response_payload = {{"index", index}};
    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload));

    nlohmann::json opts;
    if (job.dry_run) opts["_config"] = {{"DryRun", true}};
    if (!job.bandwidth.empty()) opts["_config"]["BwLimit"] = job.bandwidth;
    if (job.ignore_checksum) opts["_config"]["IgnoreChecksum"] = true;
    {
        int pt = job.parallel_transfers > 0
                 ? job.parallel_transfers
                 : load_settings().parallel_transfers;
        if (pt > 0) opts["_config"]["Transfers"] = pt;
    }
    if (job.type == rclone::JobType::Sync) {
        opts["createEmptySrcDirs"] = true;
    }
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
        if (index >= m_poll_timers.size()) m_poll_timers.resize(index + 1);
        if (index >= m_last_stats.size()) m_last_stats.resize(index + 1);

        m_poll_timers[index] = Glib::signal_timeout().connect(
            [this, index]() -> bool {
                if (index >= m_job_ids.size() || m_job_ids[index] < 0) return false;
                m_manager.rc().job_status(m_job_ids[index], [this, index](auto status) {
                    if (status.has_value() && status->finished) {
                        on_job_completed(index, status->success);
                        return;
                    }
                });
                m_manager.rc().get_stats([this, index](auto result) {
                    if (!result.has_value()) return;
                    auto& stats = result.value();
                    if (index < m_last_stats.size()) m_last_stats[index] = stats;
                    if (stats.bytes == 0 && stats.total_bytes == 0 && stats.transfers == 0) return;
                    json response_payload = {
                        {"index", index},
                        {"bytes", stats.bytes},
                        {"total_bytes", stats.total_bytes},
                        {"transfers", stats.transfers},
                        {"total_transfers", stats.total_transfers},
                        {"speed", stats.speed}
                    };
                    if (stats.eta) response_payload["eta"] = *stats.eta;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobProgress, response_payload));
                });
                return true;
            }, 500);
    };

    switch (job.type) {
        case rclone::JobType::Sync:
            if (job.bisync)
                m_manager.rc().bisync_async(job.source, job.destination, opts, done_cb);
            else
                m_manager.rc().sync_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Copy:
            m_manager.rc().copy_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Move:
            m_manager.rc().move_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Mount:
            break; // handled by early-return above
    }
}

void SaddleDaemon::on_job_completed(size_t index, bool success) {
    if (index < m_poll_timers.size()) {
        m_poll_timers[index].disconnect();
    }
    if (index < m_job_ids.size()) {
        m_job_ids[index] = -1;
    }

    if (!success && index < m_jobs.size()) {
        int max_retries = (m_jobs[index].retries >= 0)
            ? m_jobs[index].retries
            : load_settings().retries;
        if (index >= m_retry_counts.size()) m_retry_counts.resize(index + 1, 0);
        if (m_retry_counts[index] < max_retries) {
            m_retry_counts[index]++;
            append_log(std::format("RETRYING  {} [{}] attempt {}/{}",
                m_jobs[index].id, type_str(m_jobs[index].type),
                m_retry_counts[index], max_retries));
            on_run_job(index);
            return;
        }
        m_retry_counts[index] = 0;
    }

    auto now = Glib::DateTime::create_now_local().format_iso8601();
    if (index < m_jobs.size()) {
        auto& job = m_jobs[index];
        if (index < m_retry_counts.size()) m_retry_counts[index] = 0;
        job.active = false;
        job.last_status = success ? "success" : "error";
        job.last_run = now;
        save_jobs();

        json response_payload = {{"index", index}, {"success", success}};
        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));

        if (index < m_last_stats.size()) {
            auto& s = m_last_stats[index];
            std::string detail = success
                ? std::format("SUCCESS -- {} files, {}, {}",
                    s.transfers, format_bytes(s.bytes), format_speed(s.speed))
                : "FAILED";
            append_log(std::format("COMPLETED {} [{}] {}",
                job.id, type_str(job.type), detail));
        }

        std::string title = success ? "Sync Complete" : "Sync Failed";
        std::string body = job.source + " → " + job.destination;
        send_notification(title, body);

        if (job.schedule_enabled) {
            schedule_job(index);
        }
    }

    m_tray->set_attention(success);

    if (!any_job_running())
        m_tray->stop_animation();
}

bool SaddleDaemon::any_job_running() const {
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].type == rclone::JobType::Mount && m_jobs[i].active)
            return true;
        if (i < m_job_ids.size() && m_job_ids[i] >= 0)
            return true;
    }
    return false;
}

} // namespace saddle
