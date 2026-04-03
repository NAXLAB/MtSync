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

#include "views/job_view.hpp"
#include "views/job_edit_dialog.hpp"
#include "rclone/cron_utils.hpp"
#include "widgets/adw_wrapper.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <format>

namespace saddle {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const char* type_badge(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return "[SYNC]";
        case rclone::JobType::Copy:  return "[COPY]";
        case rclone::JobType::Move:  return "[MOVE]";
        case rclone::JobType::Mount: return "[MOUNT]";
    }
    return "[?]";
}

} // namespace

JobView::JobView(DaemonProxy* daemon_proxy)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_daemon_proxy(daemon_proxy) {

    auto config_dir = fs::path(g_get_user_config_dir()) / "saddle";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "jobs.json").string();

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
    adw::preferences_group_set_title(m_prefs_group, "Jobs");

    auto* add_btn = Gtk::make_managed<Gtk::Button>();
    add_btn->set_icon_name("list-add-symbolic");
    add_btn->add_css_class("flat");
    add_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobView::show_add_dialog));
    adw::preferences_group_set_header_suffix(m_prefs_group, add_btn);

    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), m_prefs_group->gobj());

    if (m_daemon_proxy) {
        m_daemon_proxy->signal_message().connect(
            sigc::mem_fun(*this, &JobView::on_daemon_message));
        Glib::signal_timeout().connect([this]() {
            m_daemon_proxy->get_jobs([this](auto) {});
            return true;
        }, 10000);
    }

    signal_map().connect([this]() { load_jobs(); rebuild_ui(); });
}

JobView::~JobView() {
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
    }
}

void JobView::load_jobs() {
    m_jobs.clear();

    if (fs::exists(m_config_path)) {
        try {
            std::ifstream f(m_config_path);
            auto j = json::parse(f);
            if (j.contains("jobs")) {
                for (auto& p : j["jobs"])
                    m_jobs.push_back(p.get<rclone::Job>());
            }
        } catch (const std::exception& e) {
            g_warning("Failed to load jobs: %s", e.what());
        }
        return;
    }
}

void JobView::save_jobs() {
    json j;
    j["jobs"] = json::array();
    for (auto& job : m_jobs)
        j["jobs"].push_back(job);

    fs::create_directories(fs::path(m_config_path).parent_path());
    std::ofstream f(m_config_path);
    f << j.dump(2);
}

void JobView::rebuild_ui() {
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
        adw_preferences_group_remove(
            ADW_PREFERENCES_GROUP(m_prefs_group->gobj()),
            ui.row->gobj());
    }
    m_ui_rows.clear();

    for (size_t i = 0; i < m_jobs.size(); ++i) {
        auto& job = m_jobs[i];

        auto* row   = adw::action_row();
        auto  title = job.source + " → " + job.destination;
        adw::preferences_row_set_title(row, title.c_str());

        std::string subtitle = type_badge(job.type);
        if (job.dry_run) subtitle += " [dry-run]";
        if (job.schedule_enabled)
            subtitle += " | " + cron::describe(job);
        if (!job.last_run.empty())
            subtitle += " | Last: " + job.last_run;
        if (!job.last_status.empty())
            subtitle += " (" + job.last_status + ")";
        adw::action_row_set_subtitle(row, subtitle.c_str());

        JobUI ui;
        ui.row = row;

        ui.progress = std::make_unique<Gtk::ProgressBar>();
        ui.progress->set_visible(false);
        ui.progress->set_valign(Gtk::Align::CENTER);
        ui.progress->set_size_request(120, -1);

        ui.status_label = std::make_unique<Gtk::Label>();
        ui.status_label->set_visible(false);
        ui.status_label->set_valign(Gtk::Align::CENTER);
        ui.status_label->add_css_class("dim-label");

        ui.run_btn = std::make_unique<Gtk::Button>();
        ui.run_btn->set_icon_name("media-playback-start-symbolic");
        ui.run_btn->set_valign(Gtk::Align::CENTER);
        ui.run_btn->add_css_class("flat");
        ui.run_btn->signal_clicked().connect([this, i]() { on_run_job(i); });

        ui.stop_btn = std::make_unique<Gtk::Button>();
        ui.stop_btn->set_icon_name("media-playback-stop-symbolic");
        ui.stop_btn->set_valign(Gtk::Align::CENTER);
        ui.stop_btn->add_css_class("flat");
        ui.stop_btn->set_visible(false);
        ui.stop_btn->signal_clicked().connect([this, i]() { on_stop_job(i); });

        // For mount jobs, reflect active state in UI
        if (job.type == rclone::JobType::Mount) {
            if (job.active) {
                ui.run_btn->set_visible(false);
                ui.stop_btn->set_visible(true);
                ui.status_label->set_text("Mounted");
                ui.status_label->set_visible(true);
            } else {
                ui.run_btn->set_visible(true);
                ui.stop_btn->set_visible(false);
            }
        }

        ui.edit_btn = std::make_unique<Gtk::Button>();
        ui.edit_btn->set_icon_name("document-edit-symbolic");
        ui.edit_btn->set_valign(Gtk::Align::CENTER);
        ui.edit_btn->add_css_class("flat");
        ui.edit_btn->signal_clicked().connect([this, i]() { show_edit_dialog(i); });

        ui.del_btn = std::make_unique<Gtk::Button>();
        ui.del_btn->set_icon_name("user-trash-symbolic");
        ui.del_btn->set_valign(Gtk::Align::CENTER);
        ui.del_btn->add_css_class("flat");
        ui.del_btn->signal_clicked().connect([this, i]() { on_delete_job(i); });

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

void JobView::show_add_dialog() {
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::unique_ptr<JobEditDialog>(new JobEditDialog(
        [this](rclone::Job job) { add_job(std::move(job)); }));
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void JobView::show_edit_dialog(size_t index) {
    if (index >= m_jobs.size()) return;
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::unique_ptr<JobEditDialog>(new JobEditDialog(m_jobs[index],
        [this, index](rclone::Job job) {
            if (index < m_jobs.size()) {
                m_jobs[index] = std::move(job);
                save_jobs();
                rebuild_ui();
                if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
                    m_daemon_proxy->update_job(index, m_jobs[index], [](auto) {});
                }
            }
        }));
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void JobView::add_job(rclone::Job job) {
    m_jobs.push_back(job);
    save_jobs();
    rebuild_ui();

    size_t index = m_jobs.size() - 1;
    if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
        m_daemon_proxy->add_job(job, [this, index](auto result) {
            if (!result.has_value()) {
                g_warning("Failed to add job to daemon: %s", result.error().c_str());
            }
        });
    }

    if (!job.schedule_enabled)
        on_run_job(index);
}

void JobView::on_run_job(size_t index) {
    if (index >= m_jobs.size() || index >= m_ui_rows.size()) return;
    auto& ui = m_ui_rows[index];

    ui.run_btn->set_visible(false);
    ui.stop_btn->set_visible(true);
    ui.progress->set_visible(true);
    ui.progress->set_fraction(0.0);
    ui.status_label->set_visible(true);
    ui.status_label->set_text("Starting...");

    // Capture job data by value so the async callback is safe if m_jobs is modified
    auto src      = m_jobs[index].source;
    auto dst      = m_jobs[index].destination;
    auto dry_run  = m_jobs[index].dry_run;
    auto bisync   = m_jobs[index].bisync;
    auto bw       = m_jobs[index].bandwidth;
    auto type     = m_jobs[index].type;
    auto includes = m_jobs[index].includes;

    if (!m_daemon_proxy || !m_daemon_proxy->is_connected()) {
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text("Error: not connected to daemon");
            m_ui_rows[index].run_btn->set_visible(true);
            m_ui_rows[index].stop_btn->set_visible(false);
        }
        return;
    }

    m_daemon_proxy->run_job(index, [this, index](auto result) {
        if (!result.has_value()) {
            if (index < m_ui_rows.size()) {
                m_ui_rows[index].status_label->set_text("Error: " + result.error());
                m_ui_rows[index].run_btn->set_visible(true);
                m_ui_rows[index].stop_btn->set_visible(false);
            }
            return;
        }
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text("Running...");
        }
    });
}

void JobView::on_stop_job(size_t index) {
    if (index >= m_ui_rows.size()) return;

    if (!m_daemon_proxy || !m_daemon_proxy->is_connected()) {
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text("Error: not connected to daemon");
        }
        return;
    }

    m_daemon_proxy->stop_job(index, [this, index](auto result) {
        if (index >= m_ui_rows.size()) return;
        m_ui_rows[index].poll_timer.disconnect();
        if (!result.has_value()) {
            m_ui_rows[index].status_label->set_text("Error: " + result.error());
        } else {
            m_ui_rows[index].status_label->set_text("Stopped");
        }
        m_ui_rows[index].run_btn->set_visible(true);
        m_ui_rows[index].stop_btn->set_visible(false);
        m_ui_rows[index].progress->set_visible(false);
    });
}

void JobView::on_delete_job(size_t index) {
    if (index >= m_jobs.size()) return;
    if (index < m_ui_rows.size()) {
        m_ui_rows[index].poll_timer.disconnect();
    }

    if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
        m_daemon_proxy->delete_job(index, [this](auto) {});
    } else {
        m_jobs.erase(m_jobs.begin() + static_cast<ptrdiff_t>(index));
        save_jobs();
        rebuild_ui();
    }
}

std::string JobView::format_speed(double bytes_per_sec) {
    if (bytes_per_sec < 1024)
        return std::format("{:.0f} B/s", bytes_per_sec);
    if (bytes_per_sec < 1024 * 1024)
        return std::format("{:.1f} KB/s", bytes_per_sec / 1024);
    if (bytes_per_sec < 1024 * 1024 * 1024)
        return std::format("{:.1f} MB/s", bytes_per_sec / (1024 * 1024));
    return std::format("{:.1f} GB/s", bytes_per_sec / (1024.0 * 1024 * 1024));
}

void JobView::on_daemon_message(const nlohmann::json& msg) {
    auto type = msg.value("type", "");
    auto payload = msg.value("payload", json{});

    if (type == "jobs_list") {
        m_jobs.clear();
        if (payload.contains("jobs")) {
            for (auto& j : payload["jobs"]) {
                m_jobs.push_back(j.get<rclone::Job>());
            }
        }
        rebuild_ui();
    } else if (type == "job_added") {
        load_jobs();
        rebuild_ui();
    } else if (type == "job_updated") {
        load_jobs();
        rebuild_ui();
    } else if (type == "job_deleted") {
        load_jobs();
        rebuild_ui();
    } else if (type == "job_started") {
        auto index = payload.value("index", 0);
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].run_btn->set_visible(false);
            m_ui_rows[index].stop_btn->set_visible(true);
            m_ui_rows[index].progress->set_visible(true);
            m_ui_rows[index].status_label->set_visible(true);
            m_ui_rows[index].status_label->set_text("Starting...");
        }
    } else if (type == "job_progress") {
        auto index = payload.value("index", 0);
        if (index >= m_ui_rows.size()) return;
        auto& ui = m_ui_rows[index];

        auto bytes = payload.value("bytes", int64_t{0});
        auto total_bytes = payload.value("total_bytes", int64_t{1});
        auto transfers = payload.value("transfers", 0);
        auto total_transfers = payload.value("total_transfers", 0);
        auto speed = payload.value("speed", 0.0);

        double frac = (total_bytes > 0) ? static_cast<double>(bytes) / total_bytes : 0.0;
        ui.progress->set_fraction(frac);

        std::string text = std::format("{}/{} files | {}", 
            transfers, total_transfers, format_speed(speed));
        if (payload.contains("eta")) {
            auto eta = payload["eta"].get<double>();
            int secs = static_cast<int>(eta);
            text += std::format(" | ETA {}:{:02d}", secs / 60, secs % 60);
        }
        ui.status_label->set_text(text);
    } else if (type == "job_completed") {
        auto index = payload.value("index", 0);
        if (index < m_ui_rows.size()) {
            auto& ui = m_ui_rows[index];
            ui.poll_timer.disconnect();
            ui.progress->set_visible(false);

            auto now = Glib::DateTime::create_now_local().format_iso8601();
            auto success = payload.value("success", false);

            bool is_mount = (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount);

            if (success) {
                ui.status_label->set_text(is_mount ? "Mounted" : "Complete");
                if (index < m_jobs.size()) {
                    m_jobs[index].last_status = "success";
                    m_jobs[index].last_run = now;
                }
                if (is_mount) {
                    ui.run_btn->set_visible(false);
                    ui.stop_btn->set_visible(true);
                } else {
                    ui.run_btn->set_visible(true);
                    ui.stop_btn->set_visible(false);
                }
            } else {
                ui.status_label->set_text(is_mount ? "Mount failed" : "Failed");
                if (index < m_jobs.size()) {
                    m_jobs[index].last_status = "error";
                    m_jobs[index].last_run = now;
                }
                ui.run_btn->set_visible(true);
                ui.stop_btn->set_visible(false);
            }
            save_jobs();
        }
    } else if (type == "job_stopped") {
        auto index = payload.value("index", 0);
        if (index < m_ui_rows.size()) {
            auto& ui = m_ui_rows[index];
            ui.poll_timer.disconnect();
            ui.progress->set_visible(false);

            bool is_mount = (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount);
            ui.status_label->set_text(is_mount ? "Unmounted" : "Stopped");
            ui.run_btn->set_visible(true);
            ui.stop_btn->set_visible(false);
        }
    }
}

} // namespace saddle
