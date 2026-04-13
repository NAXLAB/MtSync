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

#include "settings.hpp"
#include <gtkmm.h>

namespace mtsync {

class SettingsView : public Gtk::Box {
public:
    explicit SettingsView(Settings& settings);

private:
    Settings&    m_settings;

    // General
    Gtk::Widget* m_autostart_row   = nullptr;
    Gtk::Widget* m_minimized_row   = nullptr;
    Gtk::Widget* m_tray_row        = nullptr;
    // Transfers
    Gtk::Widget* m_bandwidth_row   = nullptr;
    Gtk::Widget* m_checksums_row   = nullptr;
    Gtk::Widget* m_transfers_row   = nullptr;
    Gtk::Widget* m_retries_row     = nullptr;
    // rclone
    Gtk::Widget* m_rclone_path_row       = nullptr;
    // Notifications
    Gtk::Widget* m_notify_start_row      = nullptr;
    Gtk::Widget* m_notify_completion_row = nullptr;
    Gtk::Widget* m_notify_errors_row     = nullptr;

    void setup_ui();
    void save();
    static void write_autostart(bool enable);
};

} // namespace mtsync
