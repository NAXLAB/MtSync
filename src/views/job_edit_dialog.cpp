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

#include "views/job_edit_dialog.hpp"
#include "rclone/cron_utils.hpp"
#include "settings.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <format>
#include <random>
#include <sstream>

namespace mtsync {

namespace {

constexpr guint job_type_to_index(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return 0;
        case rclone::JobType::Copy:  return 1;
        case rclone::JobType::Move:  return 2;
        case rclone::JobType::Mount: return 3;
    }
    return 0;
}

constexpr rclone::JobType index_to_job_type(guint i) {
    switch (i) {
        case 0:  return rclone::JobType::Sync;
        case 1:  return rclone::JobType::Copy;
        case 2:  return rclone::JobType::Move;
        case 3:  return rclone::JobType::Mount;
        default: return rclone::JobType::Sync;
    }
}

} // namespace

JobEditDialog::JobEditDialog(DoneCallback on_done)
    : m_on_done(std::move(on_done)) {
    setup_ui(rclone::JobType::Sync, "", "");
}

JobEditDialog::JobEditDialog(const rclone::Job& job, DoneCallback on_done)
    : m_on_done(std::move(on_done)), m_editing(job), m_includes(job.includes) {
    setup_ui(job.type, job.source, job.destination);
}

JobEditDialog::JobEditDialog(rclone::JobType type,
                               const std::string& src, const std::string& dst,
                               const std::vector<std::string>& includes,
                               DoneCallback on_done)
    : m_on_done(std::move(on_done)), m_includes(includes) {
    setup_ui(type, src, dst);
}

void JobEditDialog::setup_ui(rclone::JobType initial_type,
                               const std::string& initial_src,
                               const std::string& initial_dst) {
    set_title(m_editing ? "Edit Job" : "New Job");
    set_default_size(460, -1);
    set_modal(true);
    set_destroy_with_parent(true);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 520);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    set_child(*clamp);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // ── Job Configuration group ──────────────────────────────────────────
    auto* group = adw::preferences_group();
    adw::preferences_group_set_title(group, "Job Configuration");
    vbox->append(*group);

    // Type combo row (Sync / Copy / Move)
    auto* type_list = gtk_string_list_new(nullptr);
    gtk_string_list_append(type_list, "Sync");
    gtk_string_list_append(type_list, "Copy");
    gtk_string_list_append(type_list, "Move");
    gtk_string_list_append(type_list, "Mount");
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

    // File filters
    m_includes_entry = adw::entry_row();
    adw::preferences_row_set_title(m_includes_entry, "File Include Filters (space-separated patterns)");
    gtk_text_set_placeholder_text(
        GTK_TEXT(gtk_editable_get_delegate(GTK_EDITABLE(m_includes_entry->gobj()))),
        "All files");
    if (!m_includes.empty()) {
        std::string joined;
        for (size_t i = 0; i < m_includes.size(); ++i) {
            if (i > 0) joined += ' ';
            joined += m_includes[i];
        }
        adw::entry_row_set_text(m_includes_entry, joined.c_str());
    }
    m_includes_entry->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_includes_entry);

    // Dry Run
    m_dry_run_switch = adw::switch_row();
    adw::preferences_row_set_title(m_dry_run_switch, "Dry Run");
    adw::switch_row_set_active(m_dry_run_switch, m_editing ? m_editing->dry_run : true);
    m_dry_run_switch->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_dry_run_switch);

    // Bi-directional sync (only visible when type is Sync)
    m_bisync_switch = adw::switch_row();
    adw::preferences_row_set_title(m_bisync_switch, "Bi-directional sync");
    m_bisync_switch->set_visible(initial_type == rclone::JobType::Sync);
    if (m_editing) adw::switch_row_set_active(m_bisync_switch, m_editing->bisync);
    adw::preferences_group_add(group, m_bisync_switch);

    // Enable Checksum (default ignores checksum)
    m_enable_checksum_switch = adw::switch_row();
    adw::preferences_row_set_title(m_enable_checksum_switch, "Enable Checksum");
    if (m_editing) adw::switch_row_set_active(m_enable_checksum_switch, !m_editing->ignore_checksum);
    m_enable_checksum_switch->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_enable_checksum_switch);

    // Advanced Options expander (Bandwidth + Parallel Transfers)
    m_advanced_row = adw::expander_row();
    adw::preferences_row_set_title(m_advanced_row, "Advanced Options");
    m_advanced_row->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_advanced_row);

    m_bandwidth_entry = adw::entry_row();
    adw::preferences_row_set_title(m_bandwidth_entry, "Bandwidth Limit (e.g. 10M)");
    if (m_editing && !m_editing->bandwidth.empty())
        adw::entry_row_set_text(m_bandwidth_entry, m_editing->bandwidth.c_str());
    adw::expander_row_add_row(m_advanced_row, m_bandwidth_entry);

    {
        auto settings = load_settings();
        int pt_val = (m_editing && m_editing->parallel_transfers > 0)
                     ? m_editing->parallel_transfers
                     : settings.parallel_transfers;
        m_parallel_transfers_entry = adw::entry_row();
        adw::preferences_row_set_title(m_parallel_transfers_entry, "Parallel Transfers");
        adw::entry_row_set_text(m_parallel_transfers_entry,
            std::format("{}", pt_val).c_str());
        adw::expander_row_add_row(m_advanced_row, m_parallel_transfers_entry);

        int r_val = (m_editing && m_editing->retries >= 0)
                    ? m_editing->retries
                    : settings.retries;
        m_retries_entry = adw::entry_row();
        adw::preferences_row_set_title(m_retries_entry, "Retries on Failure");
        adw::entry_row_set_text(m_retries_entry, std::format("{}", r_val).c_str());
        adw::expander_row_add_row(m_advanced_row, m_retries_entry);
    }

    // Mount at Start-up (only visible when type is Mount)
    m_mount_startup_switch = adw::switch_row();
    adw::preferences_row_set_title(m_mount_startup_switch, "Mount at Start-up");
    m_mount_startup_switch->set_visible(initial_type == rclone::JobType::Mount);
    if (m_editing) adw::switch_row_set_active(m_mount_startup_switch, m_editing->mount_at_startup);
    adw::preferences_group_add(group, m_mount_startup_switch);

    // VFS Cache Mode (only visible when type is Mount)
    auto* cache_list = gtk_string_list_new(nullptr);
    gtk_string_list_append(cache_list, "off");
    gtk_string_list_append(cache_list, "minimal");
    gtk_string_list_append(cache_list, "writes");
    gtk_string_list_append(cache_list, "full");
    m_cache_mode_row = adw::combo_row();
    adw::preferences_row_set_title(m_cache_mode_row, "Cache Mode");
    adw::combo_row_set_string_list_model(m_cache_mode_row, cache_list);
    g_object_unref(cache_list);
    m_cache_mode_row->set_visible(initial_type == rclone::JobType::Mount);
    if (m_editing && !m_editing->vfs_cache_mode.empty()) {
        const char* modes[] = {"off", "minimal", "writes", "full"};
        for (int i = 0; i < 4; i++) {
            if (m_editing->vfs_cache_mode == modes[i]) {
                adw_combo_row_set_selected(ADW_COMBO_ROW(m_cache_mode_row->gobj()), i);
                break;
            }
        }
    }
    adw::preferences_group_add(group, m_cache_mode_row);

    // Enable Schedule switch (in same group, after bandwidth)
    m_schedule_switch = adw::switch_row();
    adw::preferences_row_set_title(m_schedule_switch, "Enable Schedule");
    bool sched_on = m_editing && m_editing->schedule_enabled;
    adw::switch_row_set_active(m_schedule_switch, sched_on);
    adw::preferences_group_add(group, m_schedule_switch);

    // ── Cron fields group ─────────────────────────────────────────────────
    m_cron_fields_group = adw::preferences_group();
    adw::preferences_group_set_title(m_cron_fields_group, "Schedule (cron)");
    m_cron_fields_group->set_visible(sched_on);
    vbox->append(*m_cron_fields_group);

    auto make_cron_row = [&](const char* title, const char* placeholder,
                              const std::string& initial_val) -> Gtk::Widget* {
        auto* row = adw::entry_row();
        adw::preferences_row_set_title(row, title);
        adw::entry_row_set_text(row, initial_val.c_str());
        // Store placeholder via GObject property name on the editable
        gtk_text_set_placeholder_text(
            GTK_TEXT(gtk_editable_get_delegate(GTK_EDITABLE(row->gobj()))),
            placeholder);
        adw::preferences_group_add(m_cron_fields_group, row);
        return row;
    };

    auto cron_val = [&](const std::string& field) -> const std::string& {
        return m_editing ? field : field; // default already "*" from Job defaults
    };
    (void)cron_val;

    m_cron_minute_entry  = make_cron_row("Minute",  "0-59 or *",
        m_editing ? m_editing->cron_minute  : "*");
    m_cron_hour_entry    = make_cron_row("Hour",    "0-23 or *",
        m_editing ? m_editing->cron_hour    : "*");
    m_cron_day_entry     = make_cron_row("Day",     "1-31 or *",
        m_editing ? m_editing->cron_day     : "*");
    m_cron_month_entry   = make_cron_row("Month",   "1-12 or *",
        m_editing ? m_editing->cron_month   : "*");
    m_cron_weekday_entry = make_cron_row("Weekday", "0-6 (0=Sun) or *",
        m_editing ? m_editing->cron_weekday : "*");

    // Summary label below cron group
    m_schedule_summary = Gtk::make_managed<Gtk::Label>();
    m_schedule_summary->add_css_class("dim-label");
    m_schedule_summary->set_margin_top(4);
    m_schedule_summary->set_margin_start(12);
    m_schedule_summary->set_xalign(0.0f);
    m_schedule_summary->set_visible(sched_on);
    vbox->append(*m_schedule_summary);

    // ── Buttons ───────────────────────────────────────────────────────────
    m_action_btn = Gtk::make_managed<Gtk::Button>(sched_on ? "Schedule" : "Run Now");
    m_action_btn->add_css_class("destructive-action");
    m_action_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobEditDialog::on_commit));

    m_save_btn = Gtk::make_managed<Gtk::Button>("Save");
    m_save_btn->add_css_class("suggested-action");
    m_save_btn->set_visible(!sched_on);
    m_save_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobEditDialog::on_save));

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->add_css_class("success");
    cancel_btn->signal_clicked().connect([this]() { close(); });

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);
    btn_box->set_margin_top(6);
    btn_box->append(*m_action_btn);
    btn_box->append(*m_save_btn);
    btn_box->append(*cancel_btn);
    vbox->append(*btn_box);

    // ── Reactive wiring ───────────────────────────────────────────────────

    // Show/hide type-specific options when type changes
    g_signal_connect(m_type_combo->gobj(), "notify::selected",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            guint sel = adw::combo_row_get_selected(self->m_type_combo);
            self->m_bisync_switch->set_visible(sel == 0);             // Sync only
            self->m_mount_startup_switch->set_visible(sel == 3);      // Mount only
            self->m_cache_mode_row->set_visible(sel == 3);            // Mount only
            self->m_includes_entry->set_visible(sel != 3);            // Not for Mount
            self->m_dry_run_switch->set_visible(sel != 3);            // Not for Mount
            self->m_enable_checksum_switch->set_visible(sel != 3);    // Not for Mount
            self->m_advanced_row->set_visible(sel != 3);              // Not for Mount
            self->set_default_size(460, 1);
        }), this);

    // Toggle cron group visibility + button state when switch changes
    g_signal_connect(m_schedule_switch->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            bool on = adw::switch_row_get_active(self->m_schedule_switch);
            self->m_cron_fields_group->set_visible(on);
            self->m_schedule_summary->set_visible(on);
            self->m_action_btn->set_label(on ? "Schedule" : "Run Now");
            self->m_save_btn->set_visible(!on);
            if (!on) self->set_default_size(460, 1);
            self->update_summary();
        }), this);

    // Shrink window when Advanced Options expander is collapsed
    g_signal_connect(m_advanced_row->gobj(), "notify::expanded",
        G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer data) {
            gboolean expanded = false;
            g_object_get(obj, "expanded", &expanded, nullptr);
            if (!expanded) static_cast<JobEditDialog*>(data)->set_default_size(460, 1);
        }), this);

    // Update summary when any cron field changes
    auto changed_cb = G_CALLBACK(+[](GtkEditable*, gpointer data) {
        static_cast<JobEditDialog*>(data)->update_summary();
    });
    g_signal_connect(m_cron_minute_entry->gobj(),  "changed", changed_cb, this);
    g_signal_connect(m_cron_hour_entry->gobj(),    "changed", changed_cb, this);
    g_signal_connect(m_cron_day_entry->gobj(),     "changed", changed_cb, this);
    g_signal_connect(m_cron_month_entry->gobj(),   "changed", changed_cb, this);
    g_signal_connect(m_cron_weekday_entry->gobj(), "changed", changed_cb, this);

    // Initial summary
    update_summary();
}

void JobEditDialog::update_summary() {
    if (!m_schedule_summary) return;
    if (!adw::switch_row_get_active(m_schedule_switch)) {
        m_schedule_summary->set_text("");
        return;
    }
    // Build a temporary Job with the current field values to generate description
    rclone::Job tmp;
    tmp.cron_minute  = adw::entry_row_get_text(m_cron_minute_entry);
    tmp.cron_hour    = adw::entry_row_get_text(m_cron_hour_entry);
    tmp.cron_day     = adw::entry_row_get_text(m_cron_day_entry);
    tmp.cron_month   = adw::entry_row_get_text(m_cron_month_entry);
    tmp.cron_weekday = adw::entry_row_get_text(m_cron_weekday_entry);
    m_schedule_summary->set_text("↻  " + cron::describe(tmp));
}

void JobEditDialog::on_commit() {
    rclone::Job job;
    job.id               = m_editing ? m_editing->id : generate_uuid();
    job.type             = index_to_job_type(adw::combo_row_get_selected(m_type_combo));
    job.source           = adw::entry_row_get_text(m_source_entry);
    job.destination      = adw::entry_row_get_text(m_dest_entry);
    job.dry_run          = adw::switch_row_get_active(m_dry_run_switch);
    job.bisync           = m_bisync_switch->get_visible()
                        && adw::switch_row_get_active(m_bisync_switch);
    job.ignore_checksum  = !adw::switch_row_get_active(m_enable_checksum_switch);
    job.bandwidth        = adw::entry_row_get_text(m_bandwidth_entry);
    try { job.parallel_transfers = std::stoi(adw::entry_row_get_text(m_parallel_transfers_entry)); }
    catch (...) { job.parallel_transfers = -1; }
    try { job.retries = std::stoi(adw::entry_row_get_text(m_retries_entry)); }
    catch (...) { job.retries = -1; }
    job.schedule_enabled = adw::switch_row_get_active(m_schedule_switch);
    job.cron_minute      = adw::entry_row_get_text(m_cron_minute_entry);
    job.cron_hour        = adw::entry_row_get_text(m_cron_hour_entry);
    job.cron_day         = adw::entry_row_get_text(m_cron_day_entry);
    job.cron_month       = adw::entry_row_get_text(m_cron_month_entry);
    job.cron_weekday     = adw::entry_row_get_text(m_cron_weekday_entry);
    job.mount_at_startup = m_mount_startup_switch->get_visible()
                        && adw::switch_row_get_active(m_mount_startup_switch);
    if (m_cache_mode_row->get_visible()) {
        guint idx = adw::combo_row_get_selected(m_cache_mode_row);
        const char* modes[] = {"off", "minimal", "writes", "full"};
        job.vfs_cache_mode = idx < 4 ? modes[idx] : "off";
    } else {
        job.vfs_cache_mode = "";
    }
    job.last_start       = m_editing ? m_editing->last_start  : "";
    job.last_run         = m_editing ? m_editing->last_run    : "";
    job.last_status      = m_editing ? m_editing->last_status : "";
    {
        std::istringstream ss(adw::entry_row_get_text(m_includes_entry));
        std::string token;
        while (ss >> token) job.includes.push_back(token);
    }

    if (job.source.empty() || job.destination.empty()) return;

    if (m_on_done) m_on_done(std::move(job));
    close();
}

void JobEditDialog::set_save_callback(DoneCallback cb) {
    m_on_save = std::move(cb);
}

void JobEditDialog::on_save() {
    rclone::Job job;
    job.id               = m_editing ? m_editing->id : generate_uuid();
    job.type             = index_to_job_type(adw::combo_row_get_selected(m_type_combo));
    job.source           = adw::entry_row_get_text(m_source_entry);
    job.destination      = adw::entry_row_get_text(m_dest_entry);
    job.dry_run          = adw::switch_row_get_active(m_dry_run_switch);
    job.bisync           = m_bisync_switch->get_visible()
                        && adw::switch_row_get_active(m_bisync_switch);
    job.ignore_checksum  = !adw::switch_row_get_active(m_enable_checksum_switch);
    job.bandwidth        = adw::entry_row_get_text(m_bandwidth_entry);
    try { job.parallel_transfers = std::stoi(adw::entry_row_get_text(m_parallel_transfers_entry)); }
    catch (...) { job.parallel_transfers = -1; }
    try { job.retries = std::stoi(adw::entry_row_get_text(m_retries_entry)); }
    catch (...) { job.retries = -1; }
    job.schedule_enabled = adw::switch_row_get_active(m_schedule_switch);
    job.cron_minute      = adw::entry_row_get_text(m_cron_minute_entry);
    job.cron_hour        = adw::entry_row_get_text(m_cron_hour_entry);
    job.cron_day         = adw::entry_row_get_text(m_cron_day_entry);
    job.cron_month       = adw::entry_row_get_text(m_cron_month_entry);
    job.cron_weekday     = adw::entry_row_get_text(m_cron_weekday_entry);
    job.mount_at_startup = m_mount_startup_switch->get_visible()
                        && adw::switch_row_get_active(m_mount_startup_switch);
    if (m_cache_mode_row->get_visible()) {
        guint idx = adw::combo_row_get_selected(m_cache_mode_row);
        const char* modes[] = {"off", "minimal", "writes", "full"};
        job.vfs_cache_mode = idx < 4 ? modes[idx] : "off";
    } else {
        job.vfs_cache_mode = "";
    }
    job.last_start       = m_editing ? m_editing->last_start  : "";
    job.last_run         = m_editing ? m_editing->last_run    : "";
    job.last_status      = m_editing ? m_editing->last_status : "";
    {
        std::istringstream ss(adw::entry_row_get_text(m_includes_entry));
        std::string token;
        while (ss >> token) job.includes.push_back(token);
    }

    if (job.source.empty() || job.destination.empty()) return;

    if (m_on_save) m_on_save(std::move(job));
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

} // namespace mtsync
