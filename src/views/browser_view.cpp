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

#include "views/browser_view.hpp"
#include "views/job_edit_dialog.hpp"

namespace saddle {

BrowserView::~BrowserView() = default;

BrowserView::BrowserView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {
    set_vexpand(true);
    set_hexpand(true);

    // CSS for active-pane indicator (top accent stripe)
    auto css = Gtk::CssProvider::create();
    css->load_from_string(
        ".browser-pane-active { box-shadow: inset 0 3px 0 @accent_color; }");
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Two panes separated by a draggable handle
    auto* paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    paned->set_vexpand(true);
    paned->set_wide_handle(true);

    m_left_pane  = Gtk::make_managed<BrowserPane>(manager);
    m_right_pane = Gtk::make_managed<BrowserPane>(manager);

    paned->set_start_child(*m_left_pane);
    paned->set_end_child(*m_right_pane);
    paned->set_shrink_start_child(false);
    paned->set_shrink_end_child(false);

    append(*paned);

    // ── Transfer action bar ──────────────────────────────────────────────
    auto* action_bar = Gtk::make_managed<Gtk::ActionBar>();

    // Copy buttons grouped
    auto* copy_group = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    copy_group->add_css_class("linked");

    auto* copy_back_btn = Gtk::make_managed<Gtk::Button>("← Copy");
    copy_back_btn->set_tooltip_text("Copy selection from other pane to active pane");
    copy_back_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Copy);
    });

    auto* copy_fwd_btn = Gtk::make_managed<Gtk::Button>("Copy →");
    copy_fwd_btn->set_tooltip_text("Copy selection from active pane to other pane");
    copy_fwd_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Copy);
    });

    copy_group->append(*copy_back_btn);
    copy_group->append(*copy_fwd_btn);

    // Move buttons grouped
    auto* move_group = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    move_group->add_css_class("linked");

    auto* move_back_btn = Gtk::make_managed<Gtk::Button>("← Move");
    move_back_btn->set_tooltip_text("Move selection from other pane to active pane");
    move_back_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Move);
    });

    auto* move_fwd_btn = Gtk::make_managed<Gtk::Button>("Move →");
    move_fwd_btn->set_tooltip_text("Move selection from active pane to other pane");
    move_fwd_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Move);
    });

    move_group->append(*move_back_btn);
    move_group->append(*move_fwd_btn);

    // Delete button
    auto* delete_btn = Gtk::make_managed<Gtk::Button>("Delete");
    delete_btn->add_css_class("destructive-action");
    delete_btn->set_tooltip_text("Delete selection in active pane");
    delete_btn->signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::on_delete_confirm));

    // New Folder — MenuButton with a Popover containing a text entry
    auto* mkdir_btn    = Gtk::make_managed<Gtk::MenuButton>();
    auto* mkdir_popover = Gtk::make_managed<Gtk::Popover>();
    auto* pop_box      = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    auto* folder_entry = Gtk::make_managed<Gtk::Entry>();
    auto* create_btn   = Gtk::make_managed<Gtk::Button>("Create");

    mkdir_btn->set_label("New Folder");
    mkdir_btn->set_tooltip_text("Create a new folder in the active pane");

    pop_box->set_margin(12);
    folder_entry->set_placeholder_text("Folder name");
    folder_entry->set_width_chars(22);
    create_btn->add_css_class("suggested-action");
    pop_box->append(*folder_entry);
    pop_box->append(*create_btn);
    mkdir_popover->set_child(*pop_box);
    mkdir_btn->set_popover(*mkdir_popover);

    create_btn->signal_clicked().connect([this, folder_entry, mkdir_popover]() {
        auto name = std::string(folder_entry->get_text());
        if (name.empty() || !m_active_pane) return;
        folder_entry->set_text("");
        mkdir_popover->popdown();
        auto dest = m_active_pane->get_current_rclone_path();
        if (dest.empty()) return;
        m_manager.cli().mkdir(dest + "/" + name, [this](auto) {
            if (m_active_pane) m_active_pane->refresh();
        });
    });

    // Also trigger create on Enter key in the entry
    folder_entry->signal_activate().connect([create_btn]() {
        create_btn->activate();
    });

    action_bar->pack_start(*copy_group);
    action_bar->pack_start(*move_group);
    action_bar->pack_end(*mkdir_btn);
    action_bar->pack_end(*delete_btn);

    append(*action_bar);

    // Active-pane tracking — left pane starts active
    m_active_pane = m_left_pane;
    m_left_pane->set_active(true);

    m_left_pane->signal_focused.connect([this]() { set_active_pane(m_left_pane); });
    m_right_pane->signal_focused.connect([this]() { set_active_pane(m_right_pane); });
}

void BrowserView::set_active_pane(BrowserPane* pane) {
    if (m_active_pane == pane) return;
    if (m_active_pane) m_active_pane->set_active(false);
    m_active_pane = pane;
    if (m_active_pane) m_active_pane->set_active(true);
}

BrowserPane* BrowserView::other_pane(BrowserPane* pane) const {
    return (pane == m_left_pane) ? m_right_pane : m_left_pane;
}

void BrowserView::show_job_dialog(rclone::JobType type) {
    if (!m_active_pane) return;
    auto src = m_active_pane->get_current_rclone_path();
    auto dst = other_pane(m_active_pane)->get_current_rclone_path();

    m_job_dialog = std::make_unique<JobEditDialog>(m_manager, type, src, dst,
        [this](rclone::Job job) { signal_job_created.emit(job); });
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root()))
        m_job_dialog->set_transient_for(*win);
    m_job_dialog->present();
}

void BrowserView::on_delete_confirm() {
    if (!m_active_pane) return;
    if (m_active_pane->get_selected_files().empty()) return;

    auto* dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        "Delete Files?",
        "The selected files will be permanently deleted."));
    adw_alert_dialog_add_response(dlg, "cancel", "Cancel");
    adw_alert_dialog_add_response(dlg, "delete", "Delete");
    adw_alert_dialog_set_response_appearance(dlg, "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");

    g_signal_connect(dlg, "response",
        G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer data) {
            if (std::string_view(response) == "delete")
                static_cast<BrowserView*>(data)->on_delete();
        }), this);

    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(get_root()->gobj()));
}

void BrowserView::on_delete() {
    if (!m_active_pane) return;
    auto files = m_active_pane->get_selected_files();
    if (files.empty()) return;
    auto dir_path = m_active_pane->get_current_rclone_path();
    if (dir_path.empty()) return;

    std::vector<std::string> includes;
    includes.reserve(files.size());
    for (auto& f : files) {
        auto name = std::string(f->property_name.get_value());
        includes.push_back(f->property_is_dir.get_value() ? name + "/**" : name);
    }

    m_manager.cli().delete_files(dir_path, includes, [this](auto) {
        if (m_active_pane) m_active_pane->refresh();
    });
}

} // namespace saddle
