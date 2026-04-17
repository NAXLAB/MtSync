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

#include "views/about_view.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>

namespace mtsync {

AboutView::AboutView(rclone::RcloneManager& manager, DaemonProxy* daemon_proxy)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager)
    , m_daemon_proxy(daemon_proxy) {
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
    adw::status_page_set_title(status, "Mt. Sync");
    adw::status_page_set_description(status, "<b>Mount or sync network storage in comfort</b>");

    GBytes* probe = g_resources_lookup_data("/io/github/mtsync/icons/application.png",
                                             G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (probe) {
        g_bytes_unref(probe);
        auto texture = Gdk::Texture::create_from_resource("/io/github/mtsync/icons/application.png");
        adw_status_page_set_paintable(ADW_STATUS_PAGE(status->gobj()), GDK_PAINTABLE(texture->gobj()));
    } else {
        adw::status_page_set_icon_name(status, "help-about-symbolic");
    }
    vbox->append(*status);

    // ── Info rows ─────────────────────────────────────────────────────────────
    auto* info_group = adw::preferences_group();
    vbox->append(*info_group);

    auto* version_row = adw::action_row();
    adw::preferences_row_set_title(version_row, "Version");
    adw::action_row_set_subtitle(version_row, "0.7.12");
    adw::preferences_group_add(info_group, version_row);

    auto* license_row = adw::action_row();
    adw::preferences_row_set_title(license_row, "License");
    adw::action_row_set_subtitle(license_row, "GNU General Public License v2.0");
    adw::preferences_group_add(info_group, license_row);

    auto* copyright_row = adw::action_row();
    adw::preferences_row_set_title(copyright_row, "Copyright");
    adw::action_row_set_subtitle(copyright_row, "© 2026 Gavin Graham");
    adw::preferences_group_add(info_group, copyright_row);

    // ── rclone group ─────────────────────────────────────────────────────────
    auto* rclone_group = adw::preferences_group();
    adw::preferences_group_set_title(rclone_group, "rclone");
    vbox->append(*rclone_group);

    // Single row: version · socket path · status — all on one line
    auto* rclone_row = adw::preferences_row_new();

    auto* info_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    info_box->set_margin_top(12);
    info_box->set_margin_bottom(12);
    info_box->set_margin_start(12);
    info_box->set_margin_end(12);

    m_rclone_version_label = Gtk::make_managed<Gtk::Label>("Loading...");
    m_rclone_version_label->add_css_class("dim-label");

    auto* sep1 = Gtk::make_managed<Gtk::Label>("·");
    sep1->add_css_class("dim-label");

    std::string socket_path = std::string(g_get_user_cache_dir()) + "/mtsync/socket";
    auto* socket_label = Gtk::make_managed<Gtk::Label>(socket_path);
    socket_label->add_css_class("dim-label");
    socket_label->set_hexpand(true);
    socket_label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);

    auto* sep2 = Gtk::make_managed<Gtk::Label>("·");
    sep2->add_css_class("dim-label");

    m_status_label = Gtk::make_managed<Gtk::Label>();
    m_status_label->add_css_class("dim-label");

    info_box->append(*m_rclone_version_label);
    info_box->append(*sep1);
    info_box->append(*socket_label);
    info_box->append(*sep2);
    info_box->append(*m_status_label);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(rclone_row->gobj()), GTK_WIDGET(info_box->gobj()));
    adw::preferences_group_add(rclone_group, rclone_row);

    // ── Lyric quote ──────────────────────────────────────────────────────────
    auto* quote_label = Gtk::make_managed<Gtk::Label>(
        "I have been stripped of a layer of meaning,\n"
        "I fell through the floor watched you fly through the ceiling\n"
        "And I find, you're finer than porcelain\n"
        "Or bromine on silver gelatine\n"
        "In blue sky, she's carried by seraphim\n"
        "Aptly named valium\n"
        "Into the arms of the night\n"
        "The arms of the night");
    quote_label->set_wrap(true);
    quote_label->set_xalign(0.5);
    quote_label->set_justify(Gtk::Justification::CENTER);
    quote_label->set_css_classes({"dim-label"});
    vbox->append(*quote_label);

    // ── Refresh on map ────────────────────────────────────────────────────────
    signal_map().connect([this]() {
        // Status: refresh every visit
        bool connected = m_daemon_proxy && m_daemon_proxy->is_connected();
        m_status_label->set_text(connected ? "Connected" : "Disconnected");

        // Version: fetch once only
        if (m_rclone_version_label->get_text() == "Loading...") {
            m_manager.cli().get_version([this](auto result) {
                m_rclone_version_label->set_text(
                    result.has_value() ? result.value() : "unavailable");
            });
        }
    });
}

} // namespace mtsync
