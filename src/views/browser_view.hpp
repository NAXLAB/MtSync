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
#include "rclone/rclone_types.hpp"
#include "widgets/browser_pane.hpp"
#include <gtkmm.h>
#include <memory>

namespace saddle {

class JobEditDialog;

class BrowserView : public Gtk::Box {
public:
    explicit BrowserView(rclone::RcloneManager& manager);
    ~BrowserView();

    sigc::signal<void(rclone::Job)> signal_job_created;
    sigc::signal<void(rclone::Job)> signal_job_saved;

private:
    rclone::RcloneManager& m_manager;

    BrowserPane* m_left_pane   = nullptr;
    BrowserPane* m_right_pane  = nullptr;
    BrowserPane* m_active_pane = nullptr;

    std::unique_ptr<JobEditDialog> m_job_dialog;

    void set_active_pane(BrowserPane* pane);
    void swap_source_destination();
    void show_job_dialog(rclone::JobType type);
    void on_delete_confirm();
    void on_delete();
};

} // namespace saddle
