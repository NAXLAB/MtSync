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

#include "rclone/rclone_manager.hpp"
#include "tray.hpp"
#include "notification.hpp"
#include "ipc/server.hpp"
#include "ipc/protocol.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sigc++/sigc++.h>

namespace fs = std::filesystem;
namespace rclone = saddle::rclone;

namespace saddle {

class SaddleDaemon {
public:
    SaddleDaemon();
    ~SaddleDaemon();

    void run();
    void stop();

private:
    void load_jobs();
    void save_jobs();
    void schedule_all_jobs();
    void schedule_job(size_t index);
    void on_run_job(size_t index);
    void on_job_completed(size_t index, bool success);
    bool any_job_running() const;

    rclone::RcloneManager m_manager;
    std::vector<rclone::Job> m_jobs;
    std::string m_config_path;

    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<ipc::IpcServer> m_ipc_server;

    std::vector<sigc::connection> m_poll_timers;
    std::vector<sigc::connection> m_sched_timers;
    std::vector<int64_t> m_job_ids;
    std::vector<int>     m_retry_counts;
    std::vector<rclone::SyncStats> m_last_stats;

    bool m_running = true;
};

} // namespace saddle
