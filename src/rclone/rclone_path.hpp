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

#include <filesystem>
#include <string>

namespace mtsync::rclone {

// Returns absolute path to bundled rclone, or "rclone" to fall through to PATH.
// Derives install prefix from /proc/self/exe: /usr/bin/mtsync → /usr/lib/mtsync/rclone.
// Works for DEB, RPM, and AppImage. Falls back to PATH for dev builds, Flatpak, and Snap.
inline std::string find_rclone_binary() {
#ifdef MTSYNC_BUNDLED_RCLONE_RELPATH
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto bundled = exe.parent_path().parent_path() / MTSYNC_BUNDLED_RCLONE_RELPATH;
        if (std::filesystem::exists(bundled, ec) && !ec)
            return bundled.string();
    }
#endif
    return "rclone";
}

} // namespace mtsync::rclone
