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
#include <glibmm.h>

namespace saddle {

// GObject wrapper around RemoteInfo for use with Gio::ListStore in the sidebar
class RemoteObject : public Glib::Object {
public:
    static Glib::RefPtr<RemoteObject> create(const rclone::RemoteInfo& info);

    Glib::Property<Glib::ustring> property_name{*this, "name"};
    Glib::Property<Glib::ustring> property_type{*this, "type"};

protected:
    RemoteObject();
};

// GObject wrapper around FileEntry for use with Gio::ListStore
class FileObject : public Glib::Object {
public:
    static Glib::RefPtr<FileObject> create(const rclone::FileEntry& entry);

    Glib::Property<Glib::ustring> property_name{*this, "name"};
    Glib::Property<Glib::ustring> property_path{*this, "path"};
    Glib::Property<gint64> property_size{*this, "size"};
    Glib::Property<Glib::ustring> property_mod_time{*this, "mod-time"};
    Glib::Property<Glib::ustring> property_mime_type{*this, "mime-type"};
    Glib::Property<bool> property_is_dir{*this, "is-dir"};

protected:
    FileObject();
};

} // namespace saddle
