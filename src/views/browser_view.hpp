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
#include "widgets/adw_wrapper.hpp"
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

    // Layout
    AdwNavigationSplitView* m_split_view = nullptr;

    // Sidebar
    Glib::RefPtr<Gio::ListStore<RemoteObject>> m_remote_store;
    Glib::RefPtr<Gtk::SingleSelection> m_remote_selection;

    // Content header
    Gtk::Box* m_breadcrumb_box = nullptr;
    Gtk::Button m_back_btn;
    Gtk::Button m_up_btn;
    Gtk::Button m_refresh_btn;

    // Content stack + state pages
    Gtk::Stack* m_content_stack = nullptr;
    Gtk::Widget* m_no_remote_status = nullptr;
    Gtk::Widget* m_empty_status = nullptr;

    // File list model
    Glib::RefPtr<Gio::ListStore<FileObject>> m_list_store;
    Glib::RefPtr<Gtk::SortListModel> m_sort_model;
    Glib::RefPtr<Gtk::SingleSelection> m_file_selection;
    Gtk::ColumnView* m_column_view = nullptr;

    // Navigation state
    std::string m_current_remote;
    std::string m_current_path;
    bool m_is_local = false;
    std::vector<std::string> m_path_history;
    std::vector<rclone::RemoteInfo> m_remotes;
    uint64_t m_load_generation = 0;

    void setup_sidebar();
    void setup_content();
    void build_column_view();
    void load_remotes();
    void on_remote_selection_changed();
    void navigate(const std::string& path);
    void rebuild_breadcrumbs();
    void on_item_activated(guint position);
    void go_back();
    void go_up();
    void show_content_state(const std::string& name);

    static std::string mime_to_icon(const std::string& mime, bool is_dir);
    static std::string format_size(int64_t bytes);
};

} // namespace saddle
