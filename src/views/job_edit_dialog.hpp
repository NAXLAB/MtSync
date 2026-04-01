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
#include <gtkmm.h>
#include <functional>
#include <optional>
#include <string>

namespace saddle {

class JobEditDialog : public Gtk::Window {
public:
    using DoneCallback = std::function<void(rclone::Job)>;

    // Create new job (type=Sync, empty fields)
    JobEditDialog(rclone::RcloneManager& manager, DoneCallback on_done);
    // Edit existing job
    JobEditDialog(rclone::RcloneManager& manager, const rclone::Job& job,
                  DoneCallback on_done);
    // Pre-fill from browser (type, src, dst already known)
    JobEditDialog(rclone::RcloneManager& manager, rclone::JobType type,
                  const std::string& src, const std::string& dst,
                  DoneCallback on_done);

private:
    rclone::RcloneManager& m_manager;
    DoneCallback           m_on_done;
    std::optional<rclone::Job> m_editing;

    Gtk::Widget* m_type_combo      = nullptr;
    Gtk::Widget* m_source_entry    = nullptr;
    Gtk::Widget* m_dest_entry      = nullptr;
    Gtk::Widget* m_dry_run_switch  = nullptr;
    Gtk::Widget* m_bandwidth_entry = nullptr;
    Gtk::Widget* m_schedule_entry  = nullptr;
    Gtk::Button* m_action_btn      = nullptr;

    void setup_ui(rclone::JobType initial_type,
                  const std::string& initial_src,
                  const std::string& initial_dst);
    void on_commit();
    static std::string generate_uuid();
};

} // namespace saddle
