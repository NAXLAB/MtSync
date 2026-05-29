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

#include "rclone/rclone_manager.hpp"
#include <adwaita.h>
#include <gtkmm.h>
#include <memory>
#include <vector>

namespace mtsync {

class BackendEditView;

class BackendsView : public Gtk::Box {
public:
    explicit BackendsView(rclone::RcloneManager& manager);
    ~BackendsView() override;

    void refresh();

private:
    rclone::RcloneManager& m_manager;

    // Navigation
    Gtk::Widget* m_nav_view = nullptr;
    AdwNavigationPage* m_list_page = nullptr;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Widget* m_prefs_group = nullptr;

    Gtk::Spinner m_spinner;
    Gtk::Widget* m_empty_status = nullptr;

    struct RemoteRow {
        Gtk::Widget* row = nullptr;
        Gtk::Box* capacity_box = nullptr;
        Gtk::ProgressBar* capacity_bar = nullptr;
        Gtk::Label* capacity_label = nullptr;
        std::unique_ptr<Gtk::Button> edit_btn;
        std::unique_ptr<Gtk::Button> del_btn;
    };
    std::vector<RemoteRow> m_rows;

    // Currently shown edit view (kept alive while pushed)
    std::unique_ptr<BackendEditView> m_edit_view;

    gulong m_dark_signal_id = 0;

    // Invalidated each time populate() runs; async get_about callbacks hold a weak_ptr
    std::shared_ptr<bool> m_populate_token;

    void populate(const std::vector<rclone::RemoteInfo>& remotes);
    void on_delete_remote(const std::string& name);
    void show_add_remote();
    void show_edit_remote(const rclone::RemoteInfo& remote);
    void on_edit_done();
};

} // namespace mtsync
