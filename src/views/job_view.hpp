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
#include "rclone/rclone_types.hpp"
#include <adwaita.h>
#include <gtkmm.h>
#include <memory>
#include <vector>

namespace saddle {

class JobEditDialog;

class JobView : public Gtk::Box {
public:
    explicit JobView(DaemonProxy* daemon_proxy);
    ~JobView() override;

    void add_job(rclone::Job job);

private:
    DaemonProxy* m_daemon_proxy = nullptr;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Widget*        m_prefs_group    = nullptr;

    std::vector<rclone::Job> m_jobs;
    std::string              m_config_path;

    struct JobUI {
        Gtk::Widget*                      row          = nullptr;
        std::unique_ptr<Gtk::Button>      run_btn;
        std::unique_ptr<Gtk::Button>      stop_btn;
        std::unique_ptr<Gtk::Button>      edit_btn;
        std::unique_ptr<Gtk::Button>      del_btn;
        std::unique_ptr<Gtk::ProgressBar> progress;
        std::unique_ptr<Gtk::Label>       status_label;
        int64_t                           jobid        = -1;
        sigc::connection                  poll_timer;
    };
    std::vector<JobUI> m_ui_rows;

    std::unique_ptr<JobEditDialog> m_edit_dialog;

    void load_jobs();
    void save_jobs();
    void rebuild_ui();
    void show_add_dialog();
    void show_edit_dialog(size_t index);
    void on_run_job(size_t index);
    void on_stop_job(size_t index);
    void on_delete_job(size_t index);
    std::string format_speed(double bytes_per_sec);
    void on_daemon_message(const nlohmann::json& msg);
};

} // namespace saddle
