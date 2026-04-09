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

#include "rclone_rc.hpp"
#include <glibmm.h>
#include <format>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

namespace saddle::rclone {

using json = nlohmann::json;

// Timeout (ms) for stop_daemon before escalating from SIGTERM to SIGKILL
static constexpr int STOP_TIMEOUT_MS = 5000;

RcloneRc::RcloneRc(std::string addr, int port)
    : m_base_url(std::format("http://{}:{}", addr, port)) {
    m_session = soup_session_new();
}

RcloneRc::~RcloneRc() {
    stop_daemon();
    if (m_session) {
        g_object_unref(m_session);
        m_session = nullptr;
    }
}

void RcloneRc::rc_post(const std::string& endpoint,
                        const json& body,
                        AsyncCallback<json> callback) {
    auto url = m_base_url + "/" + endpoint;
    auto body_str = body.dump();

    auto* msg = soup_message_new("POST", url.c_str());
    if (!msg) {
        callback(std::unexpected("Failed to create HTTP message for " + url));
        return;
    }

    auto* bytes = g_bytes_new(body_str.c_str(), body_str.size());
    soup_message_set_request_body_from_bytes(msg, "application/json", bytes);
    g_bytes_unref(bytes);

    // Store callback in a shared_ptr so the C closure can own it
    auto cb_ptr = std::make_shared<AsyncCallback<json>>(std::move(callback));

    soup_session_send_and_read_async(m_session, msg, G_PRIORITY_DEFAULT, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* cb = static_cast<std::shared_ptr<AsyncCallback<json>>*>(user_data);
            GError* error = nullptr;
            auto* bytes = soup_session_send_and_read_finish(
                SOUP_SESSION(source), result, &error);

            if (error) {
                (**cb)(std::unexpected(std::string("HTTP error: ") + error->message));
                g_error_free(error);
            } else {
                gsize size = 0;
                auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
                try {
                    auto j = json::parse(std::string_view(data, size));
                    (**cb)(std::move(j));
                } catch (const json::exception& e) {
                    (**cb)(std::unexpected(std::string("JSON parse error: ") + e.what()));
                }
                g_bytes_unref(bytes);
            }
            delete cb;
        },
        new std::shared_ptr<AsyncCallback<json>>(cb_ptr));

    g_object_unref(msg);
}

void RcloneRc::ensure_daemon(AsyncCallback<std::monostate> callback) {
    if (m_daemon_pid > 0) {
        // Daemon is alive (child watch will fire if it dies)
        callback(std::monostate{});
        return;
    }

    // m_daemon_pid == 0: probe first — a previous session's rclone rcd may still
    // be listening (e.g. app crashed, or stop_daemon() raced with restart).
    // Adopt it rather than spawning a new one that would fail to bind the port.
    rc_post("core/version", json::object(), [this, callback = std::move(callback)](auto result) mutable {
        if (result.has_value()) {
            // An rclone rcd is already running — adopt it by setting up a child
            // watch so we get notified when it dies.  We don't know the real PID,
            // so we can't use g_child_watch_add; instead we'll detect death via
            // HTTP ping failure on the next ensure_daemon call.  Mark with -1 to
            // distinguish from a PID we spawned ourselves.
            m_daemon_pid = -1;
            callback(std::monostate{});
            return;
        }
        // Nothing listening — spawn a fresh one
        spawn_daemon(std::move(callback));
    });
}

void RcloneRc::spawn_daemon(AsyncCallback<std::monostate> callback) {
    auto rclone_path = Glib::find_program_in_path("rclone");
    if (rclone_path.empty()) {
        callback(std::unexpected("rclone not found in PATH"));
        return;
    }
    gchar* argv[] = {
        const_cast<gchar*>(rclone_path.c_str()),
        const_cast<gchar*>("rcd"),
        const_cast<gchar*>("--rc-no-auth"),
        const_cast<gchar*>("--rc-addr"),
        const_cast<gchar*>("localhost:5571"),
        nullptr
    };

    // Redirect rclone's stdout/stderr to /dev/null so it doesn't pollute the console
    gint dev_null = open("/dev/null", O_RDWR);

    GSpawnChildSetupFunc setup_func = dev_null >= 0 ? +[](gpointer data) {
        int fd = GPOINTER_TO_INT(data);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    } : nullptr;

    GError* error = nullptr;
    gboolean ok = g_spawn_async(nullptr, argv, nullptr,
        G_SPAWN_DO_NOT_REAP_CHILD,
        setup_func,
        dev_null >= 0 ? GINT_TO_POINTER(dev_null) : nullptr,
        &m_daemon_pid, &error);

    if (dev_null >= 0) close(dev_null);

    if (!ok) {
        m_daemon_pid = 0;
        std::string err = error ? error->message : "unknown error";
        if (error) g_error_free(error);
        callback(std::unexpected("Failed to spawn rclone rcd: " + err));
        return;
    }

    // Register a child watch so we get async notification when the daemon dies.
    // The watch callback reaps the zombie and zeros m_daemon_pid.
    // We pass m_daemon_pid by value through a heap-allocated struct since the
    // lambda may outlive the current scope.
    struct WatchPack {
        RcloneRc* self;
        GPid pid;
    };
    auto* pack = new WatchPack{this, m_daemon_pid};
    g_child_watch_add(pack->pid, [](GPid pid, gint status, gpointer user_data) {
        auto* p = static_cast<WatchPack*>(user_data);
        // Reap the zombie (waitpid already succeeded internally for g_child_watch_add,
        // but call g_spawn_close_pid to release GLib resources).
        g_spawn_close_pid(pid);
        p->self->m_daemon_pid = 0;
        delete p;
        // Log that the daemon died — any pending ensure_daemon or job submission
        // will detect this via the zeroed PID / HTTP failure.
        g_warning("rclone rcd (pid %d) exited with status %d", (int)pid, status);
    }, pack);

    // Give rclone rcd time to start, then verify it's up
    auto cb_ptr = std::make_shared<AsyncCallback<std::monostate>>(std::move(callback));
    auto* verify_pack = new std::pair<RcloneRc*, std::shared_ptr<AsyncCallback<std::monostate>>>(this, cb_ptr);
    g_timeout_add(1500, [](gpointer data) -> gboolean {
        auto* pack = static_cast<std::pair<RcloneRc*, std::shared_ptr<AsyncCallback<std::monostate>>>*>(data);
        pack->first->rc_post("core/version", json::object(),
            [self = pack->first, cb = pack->second](auto result) {
                if (result.has_value())
                    (*cb)(std::monostate{});
                else {
                    // Startup verification failed — kill the daemon and zero PID
                    // so that subsequent ensure_daemon calls retry cleanly.
                    g_warning("rclone rcd failed startup verification, terminating");
                    self->stop_daemon();
                    (*cb)(std::unexpected("rclone rcd failed to start"));
                }
            });
        delete pack;
        return G_SOURCE_REMOVE;
    }, verify_pack);
}

void RcloneRc::stop_daemon() {
    if (m_daemon_pid > 0) {
        GPid pid = m_daemon_pid;
        m_daemon_pid = 0;  // Prevent re-entry; child watch may still fire

        // Send SIGTERM first
        kill(pid, SIGTERM);

        // Poll with WNOHANG for up to STOP_TIMEOUT_MS, then escalate to SIGKILL
        const int poll_interval_ms = 50;
        const int max_attempts = STOP_TIMEOUT_MS / poll_interval_ms;
        for (int i = 0; i < max_attempts; ++i) {
            if (waitpid(pid, nullptr, WNOHANG) != 0) {
                // Process exited
                g_spawn_close_pid(pid);
                return;
            }
            g_usleep(poll_interval_ms * 1000);
        }

        // Still alive after timeout — escalate
        g_warning("rclone rcd (pid %d) did not exit after %dms, sending SIGKILL",
                   (int)pid, STOP_TIMEOUT_MS);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        g_spawn_close_pid(pid);
    } else if (m_daemon_pid == -1) {
        // Adopted daemon (unknown PID) — try to stop via HTTP then best-effort kill
        // Find any rclone rcd process on our port and kill it
        rc_post("core/quit", json::object(), [](auto) {
            // Best effort — ignore result
        });
        m_daemon_pid = 0;
    }
    // m_daemon_pid == 0: nothing to do
}

void RcloneRc::mount_async(const std::string& src, const std::string& mountpoint,
                            AsyncCallback<std::monostate> callback) {
    mount_async(src, mountpoint, "", std::move(callback));
}

void RcloneRc::mount_async(const std::string& src, const std::string& mountpoint,
                            const std::string& vfs_cache_mode,
                            AsyncCallback<std::monostate> callback) {
    json body = {{"fs", src}, {"mountPoint", mountpoint}};
    if (!vfs_cache_mode.empty()) {
        // rclone RC mount/mount accepts vfsOpt.CacheMode as integer:
        // 0=off, 1=minimal, 2=writes, 3=full
        int cache_mode = 0;
        if (vfs_cache_mode == "minimal") cache_mode = 1;
        else if (vfs_cache_mode == "writes") cache_mode = 2;
        else if (vfs_cache_mode == "full") cache_mode = 3;
        body["vfsOpt"]["CacheMode"] = cache_mode;
    }
    rc_post("mount/mount", body,
        [callback = std::move(callback)](auto result) {
            if (result.has_value()) callback(std::monostate{});
            else                    callback(std::unexpected(result.error()));
        });
}

void RcloneRc::unmount_async(const std::string& mountpoint,
                              AsyncCallback<std::monostate> callback) {
    rc_post("mount/unmount", {{"mountPoint", mountpoint}},
        [callback = std::move(callback)](auto result) {
            if (result.has_value()) callback(std::monostate{});
            else                    callback(std::unexpected(result.error()));
        });
}

void RcloneRc::sync_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/sync", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::copy_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/copy", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::move_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/move", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::bisync_async(const std::string& path1, const std::string& path2,
                              const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"path1", path1},
        {"path2", path2},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("bisync/bisync", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::get_stats(AsyncCallback<SyncStats> callback) {
    rc_post("core/stats", json::object(), [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        SyncStats stats;
        stats.bytes = j.value("bytes", int64_t{0});
        stats.total_bytes = j.value("totalBytes", int64_t{0});
        stats.transfers = j.value("transfers", 0);
        stats.total_transfers = j.value("totalTransfers", 0);
        stats.checks = j.value("checks", 0);
        stats.total_checks = j.value("totalChecks", 0);
        stats.errors = j.value("errors", 0);
        stats.speed = j.value("speed", 0.0);
        stats.elapsed_time = j.value("elapsedTime", 0.0);
        if (j.contains("eta") && !j["eta"].is_null())
            stats.eta = j["eta"].template get<double>();
        stats.fatal_error = j.value("fatalError", false);
        callback(std::move(stats));
    });
}

void RcloneRc::get_stats(int64_t jobid, AsyncCallback<SyncStats> callback) {
    rc_post("core/stats", {{"group", "job/" + std::to_string(jobid)}},
        [callback = std::move(callback)](auto result) {
            if (!result.has_value()) {
                callback(std::unexpected(result.error()));
                return;
            }
            auto& j = result.value();
            SyncStats stats;
            stats.bytes = j.value("bytes", int64_t{0});
            stats.total_bytes = j.value("totalBytes", int64_t{0});
            stats.transfers = j.value("transfers", 0);
            stats.total_transfers = j.value("totalTransfers", 0);
            stats.checks = j.value("checks", 0);
            stats.total_checks = j.value("totalChecks", 0);
            stats.errors = j.value("errors", 0);
            stats.speed = j.value("speed", 0.0);
            stats.elapsed_time = j.value("elapsedTime", 0.0);
            if (j.contains("eta") && !j["eta"].is_null())
                stats.eta = j["eta"].template get<double>();
            stats.fatal_error = j.value("fatalError", false);
            callback(std::move(stats));
        });
}

void RcloneRc::get_about(const std::string& remote, AsyncCallback<AboutInfo> callback) {
    rc_post("operations/about", {{"fs", remote}}, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        AboutInfo info;
        if (j.contains("total") && !j["total"].is_null())
            info.total = j["total"].template get<int64_t>();
        if (j.contains("used") && !j["used"].is_null())
            info.used = j["used"].template get<int64_t>();
        if (j.contains("free") && !j["free"].is_null())
            info.free = j["free"].template get<int64_t>();
        if (j.contains("trashed") && !j["trashed"].is_null())
            info.trashed = j["trashed"].template get<int64_t>();
        if (j.contains("other") && !j["other"].is_null())
            info.other = j["other"].template get<int64_t>();
        if (j.contains("objects") && !j["objects"].is_null())
            info.objects = j["objects"].template get<int64_t>();
        callback(std::move(info));
    });
}

void RcloneRc::job_status(int64_t jobid, AsyncCallback<JobStatus> callback) {
    rc_post("job/status", {{"jobid", jobid}},
        [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        // rclone wraps RC-level errors as {"error":"...","status":N,"path":"..."}
        // Return as unexpected so callers can distinguish "job not found" from "still running"
        if (j.contains("status") && j.contains("error")) {
            callback(std::unexpected(j["error"].template get<std::string>()));
            return;
        }
        JobStatus status;
        status.id = j.value("id", int64_t{0});
        status.finished = j.value("finished", false);
        status.success = j.value("success", false);
        status.error = j.value("error", std::string{});
        status.duration = j.value("duration", 0.0);
        callback(std::move(status));
    });
}

void RcloneRc::job_stop(int64_t jobid, AsyncCallback<std::monostate> callback) {
    rc_post("job/stop", {{"jobid", jobid}},
        [callback = std::move(callback)](auto result) {
        if (result.has_value())
            callback(std::monostate{});
        else
            callback(std::unexpected(result.error()));
    });
}

} // namespace saddle::rclone
