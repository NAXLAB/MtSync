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

#include <glib.h>
#include <gio/gio.h>
#include <sigc++/sigc++.h>
#include <string>

namespace saddle {

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    void set_tooltip(const std::string& text);
    void set_running_jobs(int count);
    void set_attention(bool attention);

    sigc::signal<void()>& signal_show_window() { return m_signal_show_window; }
    sigc::signal<void()>& signal_quit() { return m_signal_quit; }

private:
    guint m_owner_id = 0;

    sigc::signal<void()> m_signal_show_window;
    sigc::signal<void()> m_signal_quit;

public:
    GDBusConnection* m_connection = nullptr;
    guint m_sni_reg_id = 0;
    guint m_menu_reg_id = 0;
};

} // namespace saddle
