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

#include "views/sync_edit_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <random>

namespace saddle {

SyncEditDialog::SyncEditDialog(rclone::RcloneManager& manager, DoneCallback on_done)
    : m_manager(manager), m_on_done(std::move(on_done)) {
    setup_ui();
    load_remotes();
}

SyncEditDialog::SyncEditDialog(rclone::RcloneManager& manager,
                                 const rclone::SyncPair& pair,
                                 DoneCallback on_done)
    : m_manager(manager), m_on_done(std::move(on_done)), m_editing(pair) {
    setup_ui();
    load_remotes();
}

void SyncEditDialog::setup_ui() {
    set_title(m_editing ? "Edit Sync Pair" : "New Sync Pair");
    set_default_size(450, -1);
    set_modal(true);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 500);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);

    auto* toolbar = adw::toolbar_view();
    auto* header = adw::header_bar();
    adw::toolbar_view_add_top_bar(toolbar, header);
    adw::toolbar_view_set_content(toolbar, clamp);
    set_child(*toolbar);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    auto* group = adw::preferences_group();
    adw::preferences_group_set_title(group, "Sync Configuration");
    vbox->append(*group);

    // Source
    m_source_entry = adw::entry_row();
    adw::preferences_row_set_title(m_source_entry, "Source (remote:path)");
    if (m_editing) adw::entry_row_set_text(m_source_entry, m_editing->source.c_str());
    adw::preferences_group_add(group, m_source_entry);

    // Destination
    m_dest_entry = adw::entry_row();
    adw::preferences_row_set_title(m_dest_entry, "Destination (remote:path)");
    if (m_editing) adw::entry_row_set_text(m_dest_entry, m_editing->destination.c_str());
    adw::preferences_group_add(group, m_dest_entry);

    // Dry run
    m_dry_run_switch = adw::switch_row();
    adw::preferences_row_set_title(m_dry_run_switch, "Dry Run");
    if (m_editing) adw::switch_row_set_active(m_dry_run_switch, m_editing->dry_run);
    adw::preferences_group_add(group, m_dry_run_switch);

    // Bandwidth limit
    m_bandwidth_entry = adw::entry_row();
    adw::preferences_row_set_title(m_bandwidth_entry, "Bandwidth Limit (e.g. 10M)");
    if (m_editing && !m_editing->bandwidth.empty())
        adw::entry_row_set_text(m_bandwidth_entry, m_editing->bandwidth.c_str());
    adw::preferences_group_add(group, m_bandwidth_entry);

    // Buttons
    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);

    auto* save_btn = Gtk::make_managed<Gtk::Button>("Save");
    save_btn->add_css_class("suggested-action");
    save_btn->signal_clicked().connect(sigc::mem_fun(*this, &SyncEditDialog::on_save));
    btn_box->append(*save_btn);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->signal_clicked().connect([this]() { close(); });
    btn_box->append(*cancel_btn);

    vbox->append(*btn_box);
}

void SyncEditDialog::load_remotes() {
    // Remotes are used as hints; the user types the full remote:path
}

void SyncEditDialog::on_save() {
    rclone::SyncPair pair;
    pair.id = m_editing ? m_editing->id : generate_uuid();
    pair.source = adw::entry_row_get_text(m_source_entry);
    pair.destination = adw::entry_row_get_text(m_dest_entry);
    pair.dry_run = adw::switch_row_get_active(m_dry_run_switch);
    pair.bandwidth = adw::entry_row_get_text(m_bandwidth_entry);
    pair.last_run = m_editing ? m_editing->last_run : "";
    pair.last_status = m_editing ? m_editing->last_status : "";

    if (pair.source.empty() || pair.destination.empty()) return;

    if (m_on_done) m_on_done(std::move(pair));
    close();
}

std::string SyncEditDialog::generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;

    auto r = [&]() { return dis(gen); };
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        r(), r() & 0xFFFF, (r() & 0x0FFF) | 0x4000,
        (r() & 0x3FFF) | 0x8000, r() & 0xFFFF, r());
    return buf;
}

} // namespace saddle
