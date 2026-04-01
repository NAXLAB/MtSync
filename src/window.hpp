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
#include "views/backends_view.hpp"
#include "views/browser_view.hpp"
#include "views/job_view.hpp"
#include <adwaita.h>
#include <gtkmm.h>

namespace saddle {

class SaddleWindow : public Gtk::ApplicationWindow {
public:
    explicit SaddleWindow(rclone::RcloneManager& manager);

    void show_toast(const char* message);

private:
    AdwViewStack* m_view_stack = nullptr;
    Gtk::Widget* m_toast_overlay = nullptr;

    BackendsView m_backends_view;
    JobView m_job_view;
    BrowserView m_browser_view;
};

} // namespace saddle
