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
#include <gtkmm.h>
#include <functional>
#include <optional>
#include <string>

namespace saddle {

class SyncEditDialog : public Gtk::Window {
public:
    using DoneCallback = std::function<void(rclone::SyncPair)>;

    SyncEditDialog(rclone::RcloneManager& manager, DoneCallback on_done);
    // Edit existing pair
    SyncEditDialog(rclone::RcloneManager& manager, const rclone::SyncPair& pair,
                   DoneCallback on_done);

private:
    rclone::RcloneManager& m_manager;
    DoneCallback m_on_done;
    std::optional<rclone::SyncPair> m_editing;

    Gtk::Widget* m_source_entry = nullptr;
    Gtk::Widget* m_dest_entry = nullptr;
    Gtk::Widget* m_dry_run_switch = nullptr;
    Gtk::Widget* m_bandwidth_entry = nullptr;

    Glib::RefPtr<Gtk::StringList> m_source_model;
    Glib::RefPtr<Gtk::StringList> m_dest_model;

    void setup_ui();
    void load_remotes();
    void on_save();
    static std::string generate_uuid();
};

} // namespace saddle
