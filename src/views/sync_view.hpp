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
#include <adwaita.h>
#include <gtkmm.h>
#include <memory>
#include <vector>

namespace saddle {

class SyncEditDialog;

class SyncView : public Gtk::Box {
public:
    explicit SyncView(rclone::RcloneManager& manager);
    ~SyncView() override;

private:
    rclone::RcloneManager& m_manager;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Widget* m_prefs_group = nullptr;

    // Sync pair state
    std::vector<rclone::SyncPair> m_pairs;
    std::string m_config_path;

    // Per-pair UI state for running syncs
    struct SyncPairUI {
        Gtk::Widget* row = nullptr;
        std::unique_ptr<Gtk::Button> run_btn;
        std::unique_ptr<Gtk::Button> stop_btn;
        std::unique_ptr<Gtk::Button> edit_btn;
        std::unique_ptr<Gtk::Button> del_btn;
        std::unique_ptr<Gtk::ProgressBar> progress;
        std::unique_ptr<Gtk::Label> status_label;
        int64_t jobid = -1;
        sigc::connection poll_timer;
    };
    std::vector<SyncPairUI> m_ui_rows;

    std::unique_ptr<SyncEditDialog> m_edit_dialog;

    void load_pairs();
    void save_pairs();
    void rebuild_ui();
    void show_add_dialog();
    void show_edit_dialog(size_t index);
    void on_run_sync(size_t index);
    void on_stop_sync(size_t index);
    void on_delete_pair(size_t index);
    void poll_progress(size_t index);
    std::string format_speed(double bytes_per_sec);
};

} // namespace saddle
