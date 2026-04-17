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

#include "notification.hpp"
#include <glib.h>
#include <glibmm.h>

namespace {

bool try_notify_send(const std::string& title, const std::string& body) {
    auto exe = Glib::find_program_in_path("notify-send");
    if (!exe.empty()) {
        try {
            Glib::spawn_sync(
                {},
                {exe, "-a", "Mt. Sync", "-i", "drive", title, body}
            );
            return true;
        } catch (...) {}
    }

    exe = Glib::find_program_in_path("kdialog");
    if (!exe.empty()) {
        try {
            Glib::spawn_sync(
                {},
                {exe, "--passivepopup", body, "5", "--title", title}
            );
            return true;
        } catch (...) {}
    }

    return false;
}

} // namespace

namespace mtsync {

void send_notification(const std::string& title, const std::string& body) {
    if (!try_notify_send(title, body)) {
        g_message("Notification: %s - %s", title.c_str(), body.c_str());
    }
}

} // namespace mtsync
