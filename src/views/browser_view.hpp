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
#include "widgets/file_row.hpp"
#include <gtkmm.h>
#include <string>
#include <vector>

namespace saddle {

class BrowserView : public Gtk::Box {
public:
    explicit BrowserView(rclone::RcloneManager& manager);

private:
    rclone::RcloneManager& m_manager;

    // Current state
    std::string m_current_remote;
    std::string m_current_path;
    std::vector<std::string> m_path_history;

    // Widgets
    Gtk::DropDown m_remote_dropdown;
    Gtk::Entry m_path_entry;
    Gtk::Button m_back_btn{"Go Back"};
    Gtk::Button m_up_btn{"Go Up"};
    Gtk::Button m_refresh_btn;
    Gtk::ScrolledWindow m_scroll;
    Gtk::ColumnView* m_column_view = nullptr;
    Gtk::Label m_status_label;
    Gtk::Spinner m_spinner;

    // Data model
    Glib::RefPtr<Gio::ListStore<FileObject>> m_list_store;
    Glib::RefPtr<Gtk::SingleSelection> m_selection;
    Glib::RefPtr<Gtk::StringList> m_remote_model;

    // Remote list cache
    std::vector<rclone::RemoteInfo> m_remotes;

    void setup_ui();
    void load_remotes();
    void navigate(const std::string& path);
    void on_remote_changed();
    void on_item_activated(guint position);
    void go_back();
    void go_up();
    void update_status(const std::string& text);
    std::string format_size(int64_t bytes);
};

} // namespace saddle
