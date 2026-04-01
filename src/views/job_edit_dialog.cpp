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

#include "views/job_edit_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <random>

namespace saddle {

namespace {

constexpr guint job_type_to_index(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync: return 0;
        case rclone::JobType::Copy: return 1;
        case rclone::JobType::Move: return 2;
    }
    return 0;
}

constexpr rclone::JobType index_to_job_type(guint i) {
    switch (i) {
        case 0:  return rclone::JobType::Sync;
        case 1:  return rclone::JobType::Copy;
        case 2:  return rclone::JobType::Move;
        default: return rclone::JobType::Sync;
    }
}

} // namespace

JobEditDialog::JobEditDialog(rclone::RcloneManager& manager, DoneCallback on_done)
    : m_manager(manager), m_on_done(std::move(on_done)) {
    setup_ui(rclone::JobType::Sync, "", "");
}

JobEditDialog::JobEditDialog(rclone::RcloneManager& manager, const rclone::Job& job,
                               DoneCallback on_done)
    : m_manager(manager), m_on_done(std::move(on_done)), m_editing(job) {
    setup_ui(job.type, job.source, job.destination);
}

JobEditDialog::JobEditDialog(rclone::RcloneManager& manager, rclone::JobType type,
                               const std::string& src, const std::string& dst,
                               DoneCallback on_done)
    : m_manager(manager), m_on_done(std::move(on_done)) {
    setup_ui(type, src, dst);
}

void JobEditDialog::setup_ui(rclone::JobType initial_type,
                               const std::string& initial_src,
                               const std::string& initial_dst) {
    set_title(m_editing ? "Edit Job" : "New Job");
    set_default_size(460, -1);
    set_modal(true);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 520);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);

    auto* toolbar = adw::toolbar_view();
    auto* header  = adw::header_bar();
    adw::toolbar_view_add_top_bar(toolbar, header);
    adw::toolbar_view_set_content(toolbar, clamp);
    set_child(*toolbar);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    auto* group = adw::preferences_group();
    adw::preferences_group_set_title(group, "Job Configuration");
    vbox->append(*group);

    // Type combo row (Sync / Copy / Move)
    auto* type_list = gtk_string_list_new(nullptr);
    gtk_string_list_append(type_list, "Sync");
    gtk_string_list_append(type_list, "Copy");
    gtk_string_list_append(type_list, "Move");
    m_type_combo = adw::combo_row();
    adw::preferences_row_set_title(m_type_combo, "Type");
    adw::combo_row_set_string_list_model(m_type_combo, type_list);
    g_object_unref(type_list);
    adw_combo_row_set_selected(ADW_COMBO_ROW(m_type_combo->gobj()),
        job_type_to_index(initial_type));
    adw::preferences_group_add(group, m_type_combo);

    // Source
    m_source_entry = adw::entry_row();
    adw::preferences_row_set_title(m_source_entry, "Source (remote:path)");
    if (!initial_src.empty())
        adw::entry_row_set_text(m_source_entry, initial_src.c_str());
    else if (m_editing)
        adw::entry_row_set_text(m_source_entry, m_editing->source.c_str());
    adw::preferences_group_add(group, m_source_entry);

    // Destination
    m_dest_entry = adw::entry_row();
    adw::preferences_row_set_title(m_dest_entry, "Destination (remote:path)");
    if (!initial_dst.empty())
        adw::entry_row_set_text(m_dest_entry, initial_dst.c_str());
    else if (m_editing)
        adw::entry_row_set_text(m_dest_entry, m_editing->destination.c_str());
    adw::preferences_group_add(group, m_dest_entry);

    // Dry Run
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

    // Schedule (ISO 8601)
    m_schedule_entry = adw::entry_row();
    adw::preferences_row_set_title(m_schedule_entry,
        "Schedule (ISO 8601, e.g. 2026-04-05T14:00:00 — leave empty to run now)");
    if (m_editing && !m_editing->scheduled_at.empty())
        adw::entry_row_set_text(m_schedule_entry, m_editing->scheduled_at.c_str());
    adw::preferences_group_add(group, m_schedule_entry);

    // Action button — label changes based on whether schedule is filled
    bool has_schedule = m_editing && !m_editing->scheduled_at.empty();
    m_action_btn = Gtk::make_managed<Gtk::Button>(has_schedule ? "Schedule" : "Run Now");
    m_action_btn->add_css_class("suggested-action");
    m_action_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobEditDialog::on_commit));

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->signal_clicked().connect([this]() { close(); });

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);
    btn_box->append(*m_action_btn);
    btn_box->append(*cancel_btn);
    vbox->append(*btn_box);

    // Update button label when schedule entry content changes
    g_signal_connect(m_schedule_entry->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            const char* text = adw::entry_row_get_text(self->m_schedule_entry);
            self->m_action_btn->set_label((text && *text) ? "Schedule" : "Run Now");
        }), this);
}

void JobEditDialog::on_commit() {
    rclone::Job job;
    job.id           = m_editing ? m_editing->id : generate_uuid();
    job.type         = index_to_job_type(adw::combo_row_get_selected(m_type_combo));
    job.source       = adw::entry_row_get_text(m_source_entry);
    job.destination  = adw::entry_row_get_text(m_dest_entry);
    job.dry_run      = adw::switch_row_get_active(m_dry_run_switch);
    job.bandwidth    = adw::entry_row_get_text(m_bandwidth_entry);
    job.scheduled_at = adw::entry_row_get_text(m_schedule_entry);
    job.last_run     = m_editing ? m_editing->last_run    : "";
    job.last_status  = m_editing ? m_editing->last_status : "";

    if (job.source.empty() || job.destination.empty()) return;

    if (m_on_done) m_on_done(std::move(job));
    close();
}

std::string JobEditDialog::generate_uuid() {
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
