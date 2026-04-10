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

#include "daemon_proxy.hpp"
#include "rclone/rclone_manager.hpp"
#include <gtkmm.h>

namespace saddle {

class AboutView : public Gtk::Box {
public:
    explicit AboutView(rclone::RcloneManager& manager, DaemonProxy* daemon_proxy);

private:
    rclone::RcloneManager& m_manager;
    DaemonProxy*           m_daemon_proxy          = nullptr;
    Gtk::Label*            m_rclone_version_label  = nullptr;
    Gtk::Label*            m_status_label          = nullptr;

    void setup_ui();
};

} // namespace saddle
