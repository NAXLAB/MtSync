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

#include "views/settings_view.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <filesystem>
#include <fstream>
#include <format>

namespace saddle {

namespace fs = std::filesystem;

SettingsView::SettingsView(Settings& settings)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_settings(settings) {
    setup_ui();
}

void SettingsView::save() {
    save_settings(m_settings);
}

void SettingsView::write_autostart(bool enable) {
    auto autostart_dir = fs::path(g_get_user_config_dir()) / "autostart";
    auto desktop_file  = autostart_dir / "saddle-daemon.desktop";

    if (enable) {
        fs::create_directories(autostart_dir);
        std::ofstream f(desktop_file);
        f << "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=Saddle Daemon\n"
             "Exec=saddle --daemon\n"
             "X-GNOME-Autostart-enabled=true\n";
    } else {
        std::error_code ec;
        fs::remove(desktop_file, ec);
    }
}

void SettingsView::setup_ui() {
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

    // ── General ──────────────────────────────────────────────────────────────
    auto* general_group = adw::preferences_group();
    adw::preferences_group_set_title(general_group, "General");
    vbox->append(*general_group);

    m_autostart_row = adw::switch_row();
    adw::preferences_row_set_title(m_autostart_row, "Start daemon on login");
    adw::switch_row_set_active(m_autostart_row, m_settings.start_daemon_on_login);
    adw::preferences_group_add(general_group, m_autostart_row);

    m_minimized_row = adw::switch_row();
    adw::preferences_row_set_title(m_minimized_row, "Start minimized to tray");
    adw::switch_row_set_active(m_minimized_row, m_settings.start_minimized);
    adw::preferences_group_add(general_group, m_minimized_row);

    m_tray_row = adw::switch_row();
    adw::preferences_row_set_title(m_tray_row, "Shutdown daemon when closing application");
    adw::switch_row_set_active(m_tray_row, m_settings.shutdown_daemon_on_close);
    adw::preferences_group_add(general_group, m_tray_row);

    // ── Transfers ─────────────────────────────────────────────────────────────
    auto* transfers_group = adw::preferences_group();
    adw::preferences_group_set_title(transfers_group, "Transfers");
    vbox->append(*transfers_group);

    m_bandwidth_row = adw::entry_row();
    adw::preferences_row_set_title(m_bandwidth_row, "Default bandwidth limit (e.g. 10M)");
    if (!m_settings.default_bandwidth.empty())
        adw::entry_row_set_text(m_bandwidth_row, m_settings.default_bandwidth.c_str());
    adw::preferences_group_add(transfers_group, m_bandwidth_row);

    m_checksums_row = adw::switch_row();
    adw::preferences_row_set_title(m_checksums_row, "Verify checksums");
    adw::switch_row_set_active(m_checksums_row, m_settings.verify_checksums);
    adw::preferences_group_add(transfers_group, m_checksums_row);

    m_transfers_row = adw::entry_row();
    adw::preferences_row_set_title(m_transfers_row, "Parallel transfers");
    adw::entry_row_set_text(m_transfers_row,
        std::format("{}", m_settings.parallel_transfers).c_str());
    adw::preferences_group_add(transfers_group, m_transfers_row);

    m_retries_row = adw::entry_row();
    adw::preferences_row_set_title(m_retries_row, "Retries on failure");
    adw::entry_row_set_text(m_retries_row,
        std::format("{}", m_settings.retries).c_str());
    adw::preferences_group_add(transfers_group, m_retries_row);

    // ── rclone ────────────────────────────────────────────────────────────────
    auto* rclone_group = adw::preferences_group();
    adw::preferences_group_set_title(rclone_group, "rclone");
    vbox->append(*rclone_group);

    m_rclone_path_row = adw::entry_row();
    adw::preferences_row_set_title(m_rclone_path_row, "rclone binary path");
    if (!m_settings.rclone_path.empty())
        adw::entry_row_set_text(m_rclone_path_row, m_settings.rclone_path.c_str());
    adw::preferences_group_add(rclone_group, m_rclone_path_row);

    auto* rclone_hint = Gtk::make_managed<Gtk::Label>(
        "Leave empty to use PATH lookup. Restart required.");
    rclone_hint->add_css_class("dim-label");
    rclone_hint->set_xalign(0.0f);
    rclone_hint->set_margin_start(12);
    rclone_hint->set_margin_top(4);
    vbox->append(*rclone_hint);

    // ── Signal wiring (after all member pointers are stored) ──────────────────

    g_signal_connect(m_autostart_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.start_daemon_on_login =
                adw::switch_row_get_active(self->m_autostart_row);
            write_autostart(self->m_settings.start_daemon_on_login);
            self->save();
        }), this);

    g_signal_connect(m_minimized_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.start_minimized =
                adw::switch_row_get_active(self->m_minimized_row);
            self->save();
        }), this);

    g_signal_connect(m_tray_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.shutdown_daemon_on_close =
                adw::switch_row_get_active(self->m_tray_row);
            self->save();
        }), this);

    g_signal_connect(m_bandwidth_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.default_bandwidth =
                adw::entry_row_get_text(self->m_bandwidth_row);
            self->save();
        }), this);

    g_signal_connect(m_checksums_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.verify_checksums =
                adw::switch_row_get_active(self->m_checksums_row);
            self->save();
        }), this);

    g_signal_connect(m_transfers_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            try {
                int v = std::stoi(adw::entry_row_get_text(self->m_transfers_row));
                if (v > 0) self->m_settings.parallel_transfers = v;
            } catch (...) {}
            self->save();
        }), this);

    g_signal_connect(m_retries_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            try {
                int v = std::stoi(adw::entry_row_get_text(self->m_retries_row));
                if (v >= 0) self->m_settings.retries = v;
            } catch (...) {}
            self->save();
        }), this);

    g_signal_connect(m_rclone_path_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.rclone_path =
                adw::entry_row_get_text(self->m_rclone_path_row);
            self->save();
        }), this);
}

} // namespace saddle
