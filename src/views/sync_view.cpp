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

#include "views/sync_view.hpp"
#include "views/sync_edit_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <format>

namespace saddle {

using json = nlohmann::json;
namespace fs = std::filesystem;

SyncView::SyncView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {

    // Config path
    auto config_dir = fs::path(g_get_user_config_dir()) / "saddle";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "sync_pairs.json").string();

    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    append(m_scroll);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    m_scroll.set_child(*clamp);

    m_prefs_group = adw::preferences_group();
    adw::preferences_group_set_title(m_prefs_group, "Sync Pairs");

    auto* add_btn = Gtk::make_managed<Gtk::Button>();
    add_btn->set_icon_name("list-add-symbolic");
    add_btn->add_css_class("flat");
    add_btn->signal_clicked().connect(sigc::mem_fun(*this, &SyncView::show_add_dialog));
    adw::preferences_group_set_header_suffix(m_prefs_group, add_btn);

    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), m_prefs_group->gobj());

    signal_map().connect([this]() { load_pairs(); rebuild_ui(); });
}

SyncView::~SyncView() {
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
    }
}

void SyncView::load_pairs() {
    m_pairs.clear();
    if (!fs::exists(m_config_path)) return;

    try {
        std::ifstream f(m_config_path);
        auto j = json::parse(f);
        if (j.contains("sync_pairs")) {
            for (auto& p : j["sync_pairs"])
                m_pairs.push_back(p.get<rclone::SyncPair>());
        }
    } catch (...) {
        // Corrupted config, start fresh
    }
}

void SyncView::save_pairs() {
    json j;
    j["sync_pairs"] = json::array();
    for (auto& p : m_pairs)
        j["sync_pairs"].push_back(p);

    fs::create_directories(fs::path(m_config_path).parent_path());
    std::ofstream f(m_config_path);
    f << j.dump(2);
}

void SyncView::rebuild_ui() {
    // Stop all timers and remove old rows
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
        adw_preferences_group_remove(
            ADW_PREFERENCES_GROUP(m_prefs_group->gobj()),
            ui.row->gobj());
    }
    m_ui_rows.clear();

    for (size_t i = 0; i < m_pairs.size(); ++i) {
        auto& pair = m_pairs[i];

        auto* row = adw::action_row();
        auto title = pair.source + " -> " + pair.destination;
        adw::preferences_row_set_title(row, title.c_str());

        std::string subtitle;
        if (!pair.last_run.empty())
            subtitle = "Last: " + pair.last_run;
        if (!pair.last_status.empty())
            subtitle += " (" + pair.last_status + ")";
        if (pair.dry_run)
            subtitle += " [dry-run]";
        adw::action_row_set_subtitle(row, subtitle.c_str());

        SyncPairUI ui;
        ui.row = row;

        // Progress bar (hidden initially)
        ui.progress = std::make_unique<Gtk::ProgressBar>();
        ui.progress->set_visible(false);
        ui.progress->set_valign(Gtk::Align::CENTER);
        ui.progress->set_size_request(120, -1);

        // Status label (hidden initially)
        ui.status_label = std::make_unique<Gtk::Label>();
        ui.status_label->set_visible(false);
        ui.status_label->set_valign(Gtk::Align::CENTER);
        ui.status_label->add_css_class("dim-label");

        // Run button
        ui.run_btn = std::make_unique<Gtk::Button>();
        ui.run_btn->set_icon_name("media-playback-start-symbolic");
        ui.run_btn->set_valign(Gtk::Align::CENTER);
        ui.run_btn->add_css_class("flat");
        ui.run_btn->signal_clicked().connect([this, i]() { on_run_sync(i); });

        // Stop button (hidden initially)
        ui.stop_btn = std::make_unique<Gtk::Button>();
        ui.stop_btn->set_icon_name("media-playback-stop-symbolic");
        ui.stop_btn->set_valign(Gtk::Align::CENTER);
        ui.stop_btn->add_css_class("flat");
        ui.stop_btn->set_visible(false);
        ui.stop_btn->signal_clicked().connect([this, i]() { on_stop_sync(i); });

        // Edit button
        ui.edit_btn = std::make_unique<Gtk::Button>();
        ui.edit_btn->set_icon_name("document-edit-symbolic");
        ui.edit_btn->set_valign(Gtk::Align::CENTER);
        ui.edit_btn->add_css_class("flat");
        ui.edit_btn->signal_clicked().connect([this, i]() { show_edit_dialog(i); });

        // Delete button
        ui.del_btn = std::make_unique<Gtk::Button>();
        ui.del_btn->set_icon_name("user-trash-symbolic");
        ui.del_btn->set_valign(Gtk::Align::CENTER);
        ui.del_btn->add_css_class("flat");
        ui.del_btn->signal_clicked().connect([this, i]() { on_delete_pair(i); });

        adw::action_row_add_suffix(row, ui.status_label.get());
        adw::action_row_add_suffix(row, ui.progress.get());
        adw::action_row_add_suffix(row, ui.run_btn.get());
        adw::action_row_add_suffix(row, ui.stop_btn.get());
        adw::action_row_add_suffix(row, ui.edit_btn.get());
        adw::action_row_add_suffix(row, ui.del_btn.get());

        adw::preferences_group_add(m_prefs_group, row);
        m_ui_rows.push_back(std::move(ui));
    }
}

void SyncView::show_add_dialog() {
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::make_unique<SyncEditDialog>(m_manager,
        [this](rclone::SyncPair pair) {
            m_pairs.push_back(std::move(pair));
            save_pairs();
            rebuild_ui();
        });
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void SyncView::show_edit_dialog(size_t index) {
    if (index >= m_pairs.size()) return;
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::make_unique<SyncEditDialog>(m_manager, m_pairs[index],
        [this, index](rclone::SyncPair pair) {
            m_pairs[index] = std::move(pair);
            save_pairs();
            rebuild_ui();
        });
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void SyncView::on_run_sync(size_t index) {
    if (index >= m_pairs.size() || index >= m_ui_rows.size()) return;
    auto& pair = m_pairs[index];
    auto& ui = m_ui_rows[index];

    // Show progress UI
    ui.run_btn->set_visible(false);
    ui.stop_btn->set_visible(true);
    ui.progress->set_visible(true);
    ui.progress->set_fraction(0.0);
    ui.status_label->set_visible(true);
    ui.status_label->set_text("Starting...");

    // Ensure daemon is running, then start sync
    m_manager.rc().ensure_daemon([this, index, &pair](auto result) {
        if (!result.has_value()) {
            if (index < m_ui_rows.size()) {
                m_ui_rows[index].status_label->set_text("Error: " + result.error());
                m_ui_rows[index].run_btn->set_visible(true);
                m_ui_rows[index].stop_btn->set_visible(false);
            }
            return;
        }

        nlohmann::json opts;
        if (pair.dry_run) opts["_config"] = {{"DryRun", true}};
        if (!pair.bandwidth.empty()) opts["_config"]["BwLimit"] = pair.bandwidth;

        m_manager.rc().sync_async(pair.source, pair.destination, opts,
            [this, index](auto result) {
                if (index >= m_ui_rows.size()) return;
                if (!result.has_value()) {
                    m_ui_rows[index].status_label->set_text("Error: " + result.error());
                    m_ui_rows[index].run_btn->set_visible(true);
                    m_ui_rows[index].stop_btn->set_visible(false);
                    return;
                }
                m_ui_rows[index].jobid = result.value();
                m_ui_rows[index].status_label->set_text("Syncing...");
                poll_progress(index);
            });
    });
}

void SyncView::on_stop_sync(size_t index) {
    if (index >= m_ui_rows.size()) return;
    auto& ui = m_ui_rows[index];
    if (ui.jobid < 0) return;

    m_manager.rc().job_stop(ui.jobid, [this, index](auto) {
        if (index >= m_ui_rows.size()) return;
        m_ui_rows[index].poll_timer.disconnect();
        m_ui_rows[index].status_label->set_text("Stopped");
        m_ui_rows[index].run_btn->set_visible(true);
        m_ui_rows[index].stop_btn->set_visible(false);
        m_ui_rows[index].progress->set_visible(false);
    });
}

void SyncView::on_delete_pair(size_t index) {
    if (index >= m_pairs.size()) return;
    if (index < m_ui_rows.size())
        m_ui_rows[index].poll_timer.disconnect();
    m_pairs.erase(m_pairs.begin() + static_cast<ptrdiff_t>(index));
    save_pairs();
    rebuild_ui();
}

void SyncView::poll_progress(size_t index) {
    if (index >= m_ui_rows.size()) return;
    auto& ui = m_ui_rows[index];

    ui.poll_timer = Glib::signal_timeout().connect([this, index]() -> bool {
        if (index >= m_ui_rows.size()) return false;
        auto& ui = m_ui_rows[index];

        // Poll stats
        m_manager.rc().get_stats([this, index](auto result) {
            if (index >= m_ui_rows.size()) return;
            if (!result.has_value()) return;
            auto& stats = result.value();
            auto& ui = m_ui_rows[index];

            double frac = (stats.total_bytes > 0)
                ? static_cast<double>(stats.bytes) / stats.total_bytes : 0.0;
            ui.progress->set_fraction(frac);

            auto text = std::format("{}/{} files | {}",
                stats.transfers, stats.total_transfers,
                format_speed(stats.speed));
            if (stats.eta) {
                int secs = static_cast<int>(*stats.eta);
                text += std::format(" | ETA {}:{:02d}", secs / 60, secs % 60);
            }
            ui.status_label->set_text(text);
        });

        // Poll job status
        m_manager.rc().job_status(ui.jobid, [this, index](auto result) {
            if (index >= m_ui_rows.size()) return;
            if (!result.has_value()) return;
            auto& status = result.value();
            if (status.finished) {
                auto& ui = m_ui_rows[index];
                ui.poll_timer.disconnect();
                ui.run_btn->set_visible(true);
                ui.stop_btn->set_visible(false);
                ui.progress->set_visible(false);

                auto now = Glib::DateTime::create_now_local().format_iso8601();

                if (status.success) {
                    ui.status_label->set_text("Complete");
                    if (index < m_pairs.size()) {
                        m_pairs[index].last_status = "success";
                        m_pairs[index].last_run = now;
                    }
                } else {
                    ui.status_label->set_text("Failed: " + status.error);
                    if (index < m_pairs.size()) {
                        m_pairs[index].last_status = "error";
                        m_pairs[index].last_run = now;
                    }
                }
                save_pairs();
            }
        });

        return true; // keep polling
    }, 500);
}

std::string SyncView::format_speed(double bytes_per_sec) {
    if (bytes_per_sec < 1024) return std::format("{:.0f} B/s", bytes_per_sec);
    if (bytes_per_sec < 1024 * 1024) return std::format("{:.1f} KB/s", bytes_per_sec / 1024);
    if (bytes_per_sec < 1024 * 1024 * 1024)
        return std::format("{:.1f} MB/s", bytes_per_sec / (1024 * 1024));
    return std::format("{:.1f} GB/s", bytes_per_sec / (1024.0 * 1024 * 1024));
}

} // namespace saddle
