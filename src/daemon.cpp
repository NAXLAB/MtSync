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

#include "daemon.hpp"
#include "rclone/cron_utils.hpp"
#include "ipc/protocol.hpp"
#include "settings.hpp"
#include <format>
#include <iostream>
#include <set>
#include <sstream>
#include <glibmm.h>

using json = nlohmann::json;

namespace {

json make_response(mtsync::ipc::ResponseType type, const json& payload,
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

std::string format_duration(double seconds) {
    auto total_secs = static_cast<int64_t>(seconds);
    if (total_secs < 60) return std::format("{}s", total_secs);
    auto mins = total_secs / 60;
    auto secs = total_secs % 60;
    if (mins < 60) return std::format("{}m {}s", mins, secs);
    auto hrs = mins / 60;
    auto rem_mins = mins % 60;
    return std::format("{}h {}m {}s", hrs, rem_mins, secs);
}

void append_log(const std::string& line) {
    auto dir = fs::path(g_get_user_state_dir()) / "mtsync";
    fs::create_directories(dir);
    std::ofstream f(dir / "mtsync.log", std::ios::app);
    if (!f) return;
    auto ts = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    f << "[" << ts.raw() << "] " << line << "\n";
}

} // namespace

namespace mtsync {

MtSyncDaemon::MtSyncDaemon() {
    auto config_dir = fs::path(g_get_user_config_dir()) / "mtsync";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "jobs.json").string();

    load_jobs();

    m_tray = std::make_unique<TrayIcon>();
    m_tray->set_tooltip("Mt. Sync - rclone GUI");
    m_tray->signal_show_window().connect([this]() {
        if (m_ipc_server->client_count() > 0) {
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::ShowWindow, {}));
        } else {
            // No GUI connected — launch one
            auto exe = Glib::find_program_in_path("mtsync");
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
                    // If a non-mount job is actively running, its in-flight HTTP callbacks
                    // will be ignored (UUID mismatch after erasure), so adjust the counter now.
                    if (m_jobs[index].type != rclone::JobType::Mount
                        && index < m_job_ids.size() && m_job_ids[index] >= 0) {
                        m_running_job_count--;
                        if (m_running_job_count < 0) m_running_job_count = 0;
                        update_tray_animation();
                    }
                    // Disconnect timers and clean up all parallel vectors before erasing
                    if (index < m_sched_timers.size()) m_sched_timers[index].disconnect();
                    if (index < m_poll_timers.size())  m_poll_timers[index].disconnect();
                    auto erase_at = [](auto& vec, size_t i) {
                        if (i < vec.size()) vec.erase(vec.begin() + i);
                    };
                    m_jobs.erase(m_jobs.begin() + index);
                    erase_at(m_job_ids,        index);
                    erase_at(m_job_submitting, index);
                    erase_at(m_poll_timers,    index);
                    erase_at(m_last_stats,     index);
                    erase_at(m_sched_timers,   index);
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
                    std::string job_uuid = m_jobs[index].id;
                    m_manager.rc().unmount_async(m_jobs[index].destination, [this, index, job_uuid](auto) {
                        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                        m_jobs[index].active = false;
                        json response_payload = {{"index", index}, {"success", true}};
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    });
                } else if (index < m_job_ids.size() && m_job_ids[index] >= 0) {
                    std::string job_uuid = m_jobs[index].id;
                    m_manager.rc().job_stop(m_job_ids[index], [this, index, msg, job_uuid](auto) {
                        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                        if (index >= m_job_ids.size() || m_job_ids[index] < 0) return;
                        if (index < m_poll_timers.size()) {
                            m_poll_timers[index].disconnect();
                        }
                        m_job_ids[index] = -1;
                        m_jobs[index].running = false;
                        save_jobs();
                        m_running_job_count--;
                        if (m_running_job_count < 0) m_running_job_count = 0;
                        update_tray_animation();
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
                // Defer stop() to the next event-loop tick so we don't destroy
                // the IpcServer (and its m_clients map) while still inside
                // IpcServer::read_from_client() — which would be use-after-free.
                Glib::signal_idle().connect_once([this]() { stop(); });

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
        if (!m_running) return;
        if (!result.has_value()) return;

        // First, check which mount points are already active (e.g. from a previous session)
        m_manager.rc().list_mounts([this](auto result) {
            if (!m_running) return;
            std::set<std::string> active_mounts;
            if (result.has_value()) {
                active_mounts = std::set<std::string>(result.value().begin(), result.value().end());
            }

            // For each mount job, verify if it's actually mounted or needs auto-start
            for (size_t i = 0; i < m_jobs.size(); ++i) {
                if (m_jobs[i].type != rclone::JobType::Mount) continue;

                // Extract the mount point (destination) from the job
                std::string mount_point = m_jobs[i].destination;

                if (active_mounts.count(mount_point)) {
                    // Mount already exists — mark as running/active
                    m_jobs[i].running = true;
                    m_jobs[i].active = true;
                } else if (m_jobs[i].mount_at_startup) {
                    // Not mounted but flagged for auto-mount — start it
                    on_run_job(i);
                }
            }

            save_jobs();

            // Broadcast updated job list so any connected GUI sees the correct state
            json response_payload = {{"jobs", m_jobs}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobsList, response_payload));
        });
    });

    schedule_all_jobs();
}

MtSyncDaemon::~MtSyncDaemon() {
    stop();
}

void MtSyncDaemon::stop() {
    m_running = false;

    for (auto& conn : m_poll_timers) conn.disconnect();
    for (auto& conn : m_sched_timers) conn.disconnect();
    m_poll_timers.clear();
    m_sched_timers.clear();

    m_ipc_server.reset();
    m_tray.reset();

    m_manager.rc().stop_daemon();
}

void MtSyncDaemon::run() {
    if (!m_ipc_server->start()) {
        g_error("Failed to start IPC server");
        return;
    }

    g_message("Mt. Sync daemon started");

    while (m_running) {
        g_main_context_iteration(nullptr, true);
    }

    g_message("Mt. Sync daemon stopped");
}

void MtSyncDaemon::load_jobs() {
    try {
        std::ifstream f(m_config_path);
        if (f) {
            auto j = json::parse(f);
            if (j.contains("jobs")) {
                for (auto& p : j["jobs"])
                    m_jobs.push_back(p.get<rclone::Job>());
            }
        }
    } catch (const std::exception& e) {
        g_warning("Failed to load jobs: %s", e.what());
    }
    // In-flight state is not valid across daemon restarts — reset unconditionally.
    for (auto& job : m_jobs) {
        if (job.type == rclone::JobType::Mount)
            job.active = false;
        job.running = false;
    }
}

void MtSyncDaemon::save_jobs() {
    json j;
    j["jobs"] = json::array();
    for (auto& job : m_jobs)
        j["jobs"].push_back(job);

    auto target = fs::path(m_config_path);
    fs::create_directories(target.parent_path());
    auto tmp = target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream f(tmp);
        if (!f) { g_warning("save_jobs: cannot write to %s", tmp.c_str()); return; }
        f << j.dump(2);
        if (!f.good()) { g_warning("save_jobs: write error on %s", tmp.c_str()); fs::remove(tmp); return; }
    }
    try { fs::rename(tmp, target); }
    catch (const fs::filesystem_error& e) {
        g_warning("save_jobs: rename failed: %s", e.what());
        fs::remove(tmp);
    }
}

void MtSyncDaemon::schedule_all_jobs() {
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].schedule_enabled) {
            schedule_job(i);
        }
    }
}

void MtSyncDaemon::schedule_job(size_t index) {
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

void MtSyncDaemon::on_run_job(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];

    // Skip if a previous instance is still running or we're still waiting
    // for rclone to acknowledge a start request (prevents duplicate increments
    // to m_running_job_count from rapid scheduler / IPC calls)
    bool already_running = job.type == rclone::JobType::Mount
        ? (job.active || (index < m_job_submitting.size() && m_job_submitting[index]))
        : ((index < m_job_ids.size() && m_job_ids[index] >= 0)
           || (index < m_job_submitting.size() && m_job_submitting[index]));
    if (already_running) {
        append_log(std::format("SKIPPED   {} [{}] previous instance still running",
            job.id, type_str(job.type)));
        return;
    }

    if (index >= m_job_submitting.size()) m_job_submitting.resize(index + 1);
    m_job_submitting[index] = true;
    // Capture the job's stable ID so async callbacks can detect if the job was
    // deleted and a different job shifted into this index slot.
    std::string job_uuid = job.id;

    auto start_time = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    if (index < m_jobs.size()) {
        m_jobs[index].last_start = start_time;
        m_jobs[index].running = true;
    }
    save_jobs();

    append_log(std::format("STARTED   {} [{}] {} -> {}",
        job.id, type_str(job.type), job.source, job.destination));

    if (load_settings().notify_on_start)
        send_notification("Job Started", job.source + " → " + job.destination);

    m_tray->set_attention(false);

    if (job.type == rclone::JobType::Mount) {
        // Mount jobs are persistent state, not active transfers — don't count them
        m_jobs[index].active = true;
        json response_payload = {{"index", index}};
        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload));
        m_manager.rc().mount_async(job.source, job.destination, job.vfs_cache_mode,
            [this, index, job_uuid](auto result) {
                if (index < m_job_submitting.size()) m_job_submitting[index] = false;
                if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                json response_payload = {{"index", index}};
                if (result.has_value()) {
                    response_payload["success"] = true;
                    m_jobs[index].active = true;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    append_log(std::format("COMPLETED {} [MOUNT] SUCCESS", m_jobs[index].id));
                } else {
                    response_payload["success"] = false;
                    m_jobs[index].active = false;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    append_log(std::format("COMPLETED {} [MOUNT] FAILED -- {}",
                        m_jobs[index].id, result.error()));
                    g_warning("Mount %zu failed: %s", index, result.error().c_str());
                }
            });
        return;
    }

    // Sync/Copy/Move jobs are active transfers — track them for tray animation
    m_running_job_count++;
    update_tray_animation();

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

    auto to_pascal = [](const std::string& s) {
        std::string result;
        bool cap_next = true;
        for (char c : s) {
            if (c == '-') { cap_next = true; continue; }
            result += cap_next ? (char)std::toupper((unsigned char)c) : c;
            cap_next = false;
        }
        return result;
    };

    auto inject_flags = [&](const std::string& flags) {
        std::vector<std::string> tokens;
        std::istringstream iss(flags);
        std::string tok;
        while (iss >> tok) tokens.push_back(std::move(tok));
        for (size_t i = 0; i < tokens.size(); ) {
            const auto& t = tokens[i];
            if (t.size() < 3 || t[0] != '-' || t[1] != '-') { ++i; continue; }
            std::string key_raw = t.substr(2);
            if (auto eq = key_raw.find('='); eq != std::string::npos) {
                std::string key = to_pascal(key_raw.substr(0, eq));
                std::string val = key_raw.substr(eq + 1);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = val;
                ++i;
            } else if (i + 1 < tokens.size() && tokens[i + 1][0] != '-') {
                std::string key = to_pascal(key_raw);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = tokens[i + 1];
                i += 2;
            } else {
                std::string key = to_pascal(key_raw);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = true;
                ++i;
            }
        }
    };

    // Inject per-job extra flags (higher priority than global flags)
    if (!job.extra_flags.empty()) inject_flags(job.extra_flags);

    // Inject global rclone flags (lower priority — do not overwrite per-job opts)
    if (!load_settings().global_rclone_flags.empty())
        inject_flags(load_settings().global_rclone_flags);

    auto done_cb = [this, index, job_uuid](auto result) {
        if (index < m_job_submitting.size()) m_job_submitting[index] = false;
        if (!result.has_value()) {
            // Job never received an RC job ID — on_job_completed() would return early
            // on its m_job_ids guard, leaking job.running and m_running_job_count.
            // Clean up directly instead.
            g_warning("Job %zu failed to submit: %s", index, result.error().c_str());
            if (index < m_jobs.size() && m_jobs[index].id == job_uuid) {
                m_jobs[index].running = false;
                m_jobs[index].last_status = "error";
                save_jobs();
            }
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            json response_payload = {{"index", index}, {"success", false}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
            return;
        }

        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) {
            // Job was deleted while the submission was in-flight.
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            return;
        }

        if (index >= m_job_ids.size()) m_job_ids.resize(index + 1, -1);
        m_job_ids[index] = result.value();

        // Start polling for progress
        if (index >= m_poll_timers.size()) m_poll_timers.resize(index + 1);
        if (index >= m_last_stats.size()) m_last_stats.resize(index + 1);

        m_poll_timers[index] = Glib::signal_timeout().connect(
            [this, index, job_uuid]() -> bool {
                if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return false;
                if (index >= m_job_ids.size() || m_job_ids[index] < 0) return false;

                int64_t current_job_id = m_job_ids[index];
                // Check job status first; if finished, stop polling immediately
                m_manager.rc().job_status(current_job_id, [this, index, job_uuid](auto status) {
                    // Guard against stale index (job deleted) or duplicate completion
                    if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                    if (index >= m_job_ids.size() || m_job_ids[index] < 0) return;
                    if (!status.has_value()) {
                        // rclone rcd is unreachable or doesn't know this job ID
                        // (e.g. rcd crashed or was restarted). Treat as failure so the
                        // stale job ID is cleared and the job can run again.
                        on_job_completed(index, false);
                        return;
                    }
                    if (status->finished) {
                        on_job_completed(index, status->success);
                        return;
                    }
                });
                m_manager.rc().get_stats(current_job_id, [this, index, job_uuid](auto result) {
                    if (!result.has_value()) return;
                    if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
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

void MtSyncDaemon::on_job_completed(size_t index, bool success) {
    // Atomically claim ownership: if the job_id is already cleared, another
    // completion path beat us here (duplicate from overlapping poll cycles).
    if (index >= m_job_ids.size() || m_job_ids[index] < 0) return;
    if (index >= m_jobs.size()) return;
    std::string job_uuid = m_jobs[index].id;
    int64_t job_id = m_job_ids[index];
    // Clear the job_id FIRST so subsequent callbacks see it as done
    m_job_ids[index] = -1;

    if (index < m_poll_timers.size()) {
        m_poll_timers[index].disconnect();
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

    // Fetch final stats from rclone to ensure the log has the most up-to-date
    // transfer count (the cached m_last_stats may be from a previous poll cycle).
    m_manager.rc().get_stats(job_id, [this, index, success, job_id, job_uuid](auto result) {
        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) {
            // Job was deleted while the stats fetch was in-flight; just clean up the counter.
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            return;
        }
        if (result.has_value() && index < m_last_stats.size())
            m_last_stats[index] = result.value();

        auto now = Glib::DateTime::create_now_local().format_iso8601();
        if (index < m_jobs.size()) {
            auto& job = m_jobs[index];
            if (index < m_retry_counts.size()) m_retry_counts[index] = 0;
            job.active = false;
            job.running = false;
            job.last_status = success ? "success" : "error";
            job.last_run = now;
            save_jobs();

            json response_payload = {{"index", index}, {"success", success}};

            // Include stats text for GUI display
            if (index < m_last_stats.size()) {
                auto& s = m_last_stats[index];
                if (success) {
                    response_payload["stats_text"] = std::format("{} files, {}, {}",
                        s.transfers, format_bytes(s.bytes), format_speed(s.speed));
                }
            }

            m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));

            if (index < m_last_stats.size()) {
                auto& s = m_last_stats[index];
                std::string detail = success
                    ? std::format("SUCCESS -- {} files, {}, {}, ran for {}",
                        s.transfers, format_bytes(s.bytes), format_speed(s.speed),
                        format_duration(s.elapsed_time))
                    : "FAILED";
                append_log(std::format("COMPLETED {} [{}] {}",
                    job.id, type_str(job.type), detail));
            }

            if (success) {
                if (load_settings().notify_on_completion)
                    send_notification("Sync Complete", job.source + " → " + job.destination);
            } else {
                if (load_settings().notify_on_errors)
                    send_notification("Sync Failed", job.source + " → " + job.destination);
            }

            if (job.schedule_enabled) {
                schedule_job(index);
            }
        }

        m_running_job_count--;
        if (m_running_job_count < 0) m_running_job_count = 0;
        update_tray_animation();

        // Only update attention state when all jobs are truly done.
        // Calling set_attention() mid-run emits NewStatus which can cause
        // some desktop environments (GNOME AppIndicator extension) to reset
        // and re-cache the icon, breaking the spinner display.
        if (m_running_job_count == 0)
            m_tray->set_attention(success);
    });
}

void MtSyncDaemon::update_tray_animation() {
    if (m_running_job_count > 0)
        m_tray->start_animation();
    else
        m_tray->stop_animation();
}

} // namespace mtsync
