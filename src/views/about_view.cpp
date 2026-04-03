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

#include "views/about_view.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <gdk/gdk.h>

namespace saddle {

AboutView::AboutView()
    : Gtk::Box(Gtk::Orientation::VERTICAL) {
    setup_ui();
}

void AboutView::setup_ui() {
    set_vexpand(true);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    append(*scroll);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    scroll->set_child(*clamp);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // ── Status page ───────────────────────────────────────────────────────────
    auto* status = adw::status_page();
    adw::status_page_set_title(status, "Saddle");
    adw::status_page_set_description(status, "GTK4 frontend to rclone");

    std::vector<std::string> icon_paths = {
        Glib::build_filename(Glib::get_user_data_dir(), "saddle", "icons", "application.svg"),
        Glib::build_filename(Glib::get_home_dir(), ".local", "share", "saddle", "icons", "application.svg"),
    };

    bool icon_loaded = false;
    for (const auto& icon_path : icon_paths) {
        if (Glib::file_test(icon_path, Glib::FileTest::EXISTS)) {
            try {
                auto pixbuf = Gdk::Pixbuf::create_from_file(icon_path, 128, 128);
                auto image = Gtk::make_managed<Gtk::Image>(pixbuf);
                adw::status_page_set_child(status, image);
                icon_loaded = true;
                break;
            } catch (...) {}
        }
    }
    if (!icon_loaded) {
        adw::status_page_set_icon_name(status, "help-about-symbolic");
    }
    vbox->append(*status);

    // ── Info rows ─────────────────────────────────────────────────────────────
    auto* info_group = adw::preferences_group();
    vbox->append(*info_group);

    auto* version_row = adw::action_row();
    adw::preferences_row_set_title(version_row, "Version");
    adw::action_row_set_subtitle(version_row, "0.2.0");
    adw::preferences_group_add(info_group, version_row);

    auto* license_row = adw::action_row();
    adw::preferences_row_set_title(license_row, "License");
    adw::action_row_set_subtitle(license_row, "GNU General Public License v2.0");
    adw::preferences_group_add(info_group, license_row);

    auto* copyright_row = adw::action_row();
    adw::preferences_row_set_title(copyright_row, "Copyright");
    adw::action_row_set_subtitle(copyright_row, "© 2026 Gavin Graham");
    adw::preferences_group_add(info_group, copyright_row);
}

} // namespace saddle
