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

#include "views/backends_view.hpp"
#include "views/backend_edit_view.hpp"
#include "widgets/adw_wrapper.hpp"

namespace saddle {

BackendsView::~BackendsView() = default;

BackendsView::BackendsView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {

    set_vexpand(true);
    set_hexpand(true);

    // Navigation view for list <-> edit push/pop
    m_nav_view = adw::navigation_view();

    // Build the list page content
    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    m_scroll.set_child(*clamp);

    m_prefs_group = adw::preferences_group();
    adw::preferences_group_set_title(m_prefs_group, "Configured Remotes");

    // Add button in the header suffix
    auto* add_btn = Gtk::make_managed<Gtk::Button>();
    add_btn->set_icon_name("list-add-symbolic");
    add_btn->add_css_class("flat");
    add_btn->signal_clicked().connect(sigc::mem_fun(*this, &BackendsView::show_add_remote));
    adw::preferences_group_set_header_suffix(m_prefs_group, add_btn);

    // Empty state
    m_empty_status = adw::status_page();
    adw::status_page_set_icon_name(m_empty_status, "preferences-system-symbolic");
    adw::status_page_set_title(m_empty_status, "No Remotes Configured");
    adw::status_page_set_description(m_empty_status,
        "Click the + button above to add your first remote.");
    m_empty_status->set_visible(false);

    auto* content_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    content_box->append(*m_prefs_group);
    content_box->append(*m_empty_status);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(content_box->gobj()));

    // Wrap the scroll in a navigation page
    m_list_page = adw::navigation_page_new(&m_scroll, "Backends");
    adw::navigation_view_push_page(m_nav_view, m_list_page);

    append(*m_nav_view);

    signal_map().connect([this]() { refresh(); });
}

void BackendsView::refresh() {
    m_manager.cli().list_remotes([this](auto result) {
        if (result.has_value()) {
            populate(result.value());
        }
    });
}

void BackendsView::populate(const std::vector<rclone::RemoteInfo>& remotes) {
    for (auto& r : m_rows) {
        adw_preferences_group_remove(
            ADW_PREFERENCES_GROUP(m_prefs_group->gobj()),
            r.row->gobj());
    }
    m_rows.clear();

    m_empty_status->set_visible(remotes.empty());

    for (auto& remote : remotes) {
        auto* row = adw::action_row();
        adw::preferences_row_set_title(row, remote.name.c_str());
        adw::action_row_set_subtitle(row, remote.type.c_str());

        RemoteRow rr;
        rr.row = row;

        // Edit button
        rr.edit_btn = std::make_unique<Gtk::Button>();
        rr.edit_btn->set_icon_name("document-edit-symbolic");
        rr.edit_btn->set_valign(Gtk::Align::CENTER);
        rr.edit_btn->add_css_class("flat");

        auto remote_copy = remote;
        rr.edit_btn->signal_clicked().connect([this, remote_copy]() {
            show_edit_remote(remote_copy);
        });

        // Delete button
        rr.del_btn = std::make_unique<Gtk::Button>();
        rr.del_btn->set_icon_name("user-trash-symbolic");
        rr.del_btn->set_valign(Gtk::Align::CENTER);
        rr.del_btn->add_css_class("flat");

        std::string name = remote.name;
        rr.del_btn->signal_clicked().connect([this, name]() {
            on_delete_remote(name);
        });

        adw::action_row_add_suffix(row, rr.edit_btn.get());
        adw::action_row_add_suffix(row, rr.del_btn.get());
        adw::preferences_group_add(m_prefs_group, row);

        m_rows.push_back(std::move(rr));
    }
}

void BackendsView::on_delete_remote(const std::string& name) {
    m_manager.cli().config_delete(name, [this](auto result) {
        if (result.has_value()) refresh();
    });
}

void BackendsView::show_add_remote() {
    m_edit_view = std::make_unique<BackendEditView>(m_manager,
        [this]() { on_edit_done(); });

    auto* page = adw::navigation_page_new(m_edit_view.get(), "New Remote");
    adw::navigation_view_push_page(m_nav_view, page);
}

void BackendsView::show_edit_remote(const rclone::RemoteInfo& remote) {
    m_edit_view = std::make_unique<BackendEditView>(m_manager, remote,
        [this]() { on_edit_done(); });

    auto* page = adw::navigation_page_new(m_edit_view.get(), "Edit Remote");
    adw::navigation_view_push_page(m_nav_view, page);
}

void BackendsView::on_edit_done() {
    adw::navigation_view_pop(m_nav_view);
    refresh();
}

} // namespace saddle
