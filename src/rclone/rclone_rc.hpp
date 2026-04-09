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

#include "rclone_types.hpp"
#include <libsoup/soup.h>
#include <nlohmann/json.hpp>
#include <giomm.h>
#include <string>

namespace saddle::rclone {

class RcloneRc {
public:
    RcloneRc(std::string addr = "localhost", int port = 5571);
    ~RcloneRc();

    // Daemon lifecycle
    void ensure_daemon(AsyncCallback<std::monostate> callback);
    void stop_daemon();
    bool is_daemon_running() const { return m_daemon_pid != 0; }

    // Async RC API calls
    void mount_async  (const std::string& src, const std::string& mountpoint,
                       AsyncCallback<std::monostate> callback);
    void mount_async  (const std::string& src, const std::string& mountpoint,
                       const std::string& vfs_cache_mode,
                       AsyncCallback<std::monostate> callback);
    void unmount_async(const std::string& mountpoint,
                       AsyncCallback<std::monostate> callback);

    void sync_async(const std::string& src_fs, const std::string& dst_fs,
                    const nlohmann::json& opts,
                    AsyncCallback<int64_t> callback);
    void copy_async(const std::string& src_fs, const std::string& dst_fs,
                    const nlohmann::json& opts,
                    AsyncCallback<int64_t> callback);
    void move_async(const std::string& src_fs, const std::string& dst_fs,
                    const nlohmann::json& opts,
                    AsyncCallback<int64_t> callback);
    void bisync_async(const std::string& path1, const std::string& path2,
                      const nlohmann::json& opts,
                      AsyncCallback<int64_t> callback);

    void get_stats(AsyncCallback<SyncStats> callback);
    void get_stats(int64_t jobid, AsyncCallback<SyncStats> callback);
    void get_about(const std::string& remote, AsyncCallback<AboutInfo> callback);
    void job_status(int64_t jobid, AsyncCallback<JobStatus> callback);
    void job_stop(int64_t jobid, AsyncCallback<std::monostate> callback);

private:
    std::string m_base_url;
    SoupSession* m_session = nullptr;
    GPid m_daemon_pid = 0;

    void rc_post(const std::string& endpoint,
                 const nlohmann::json& body,
                 AsyncCallback<nlohmann::json> callback);

    void spawn_daemon(AsyncCallback<std::monostate> callback);
};

} // namespace saddle::rclone
