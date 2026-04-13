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

#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <glib.h>

namespace mtsync {

struct Settings {
    // General
    bool        start_daemon_on_login = false;
    bool        start_minimized       = false;
    bool        shutdown_daemon_on_close = false;
    // Transfers
    std::string default_bandwidth     = "";
    bool        verify_checksums      = false;
    int         parallel_transfers    = 4;
    int         retries               = 0;
    // rclone
    std::string rclone_path           = "";
    // Notifications
    bool        notify_on_start       = false;
    bool        notify_on_completion  = false;
    bool        notify_on_errors      = true;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings,
    start_daemon_on_login, start_minimized, shutdown_daemon_on_close,
    default_bandwidth, verify_checksums, parallel_transfers, retries,
    rclone_path,
    notify_on_start, notify_on_completion, notify_on_errors)

inline std::filesystem::path settings_file_path() {
    return std::filesystem::path(g_get_user_config_dir()) / "mtsync" / "settings.json";
}

inline Settings load_settings() {
    auto path = settings_file_path();
    if (!std::filesystem::exists(path)) return {};
    try {
        std::ifstream f(path);
        return nlohmann::json::parse(f).get<Settings>();
    } catch (...) { return {}; }
}

inline void save_settings(const Settings& s) {
    auto path = settings_file_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f << nlohmann::json(s).dump(2);
}

} // namespace mtsync
