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

#include "rclone/rclone_types.hpp"
#include <gtkmm.h>
#include <functional>
#include <optional>
#include <string>

namespace saddle {

class JobEditDialog : public Gtk::Window {
public:
    using DoneCallback = std::function<void(rclone::Job)>;

    JobEditDialog(DoneCallback on_done);
    JobEditDialog(const rclone::Job& job, DoneCallback on_done);
    JobEditDialog(rclone::JobType type,
                  const std::string& src, const std::string& dst,
                  const std::vector<std::string>& includes,
                  DoneCallback on_done);
    void set_save_callback(DoneCallback cb);

private:
    DoneCallback           m_on_done;
    DoneCallback           m_on_save;
    std::optional<rclone::Job> m_editing;
    std::vector<std::string>  m_includes;

    Gtk::Widget* m_type_combo         = nullptr;
    Gtk::Widget* m_source_entry       = nullptr;
    Gtk::Widget* m_dest_entry         = nullptr;
    Gtk::Widget* m_dry_run_switch        = nullptr;
    Gtk::Widget* m_bisync_switch         = nullptr;
    Gtk::Widget* m_enable_checksum_switch = nullptr;
    Gtk::Widget* m_bandwidth_entry       = nullptr;
    Gtk::Widget* m_mount_startup_switch = nullptr;
    Gtk::Widget* m_schedule_switch    = nullptr;
    Gtk::Widget* m_cron_fields_group  = nullptr;
    Gtk::Widget* m_cron_minute_entry  = nullptr;
    Gtk::Widget* m_cron_hour_entry    = nullptr;
    Gtk::Widget* m_cron_day_entry     = nullptr;
    Gtk::Widget* m_cron_month_entry   = nullptr;
    Gtk::Widget* m_cron_weekday_entry = nullptr;
    Gtk::Label*  m_schedule_summary   = nullptr;
    Gtk::Button* m_action_btn         = nullptr;
    Gtk::Button* m_save_btn          = nullptr;

    void setup_ui(rclone::JobType initial_type,
                  const std::string& initial_src,
                  const std::string& initial_dst);
    void update_summary();
    void on_commit();
    void on_save();
    static std::string generate_uuid();
};

} // namespace saddle
