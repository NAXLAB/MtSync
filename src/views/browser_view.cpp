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
#include "widgets/adw_wrapper.hpp"
#include <format>

namespace saddle {

BrowserView::BrowserView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {
    m_list_store = Gio::ListStore<FileObject>::create();
    setup_ui();

    signal_map().connect([this]() { load_remotes(); });
}

void BrowserView::setup_ui() {
    // Toolbar row: remote selector + path + nav buttons
    auto* toolbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    toolbar->set_margin_top(6);
    toolbar->set_margin_bottom(6);
    toolbar->set_margin_start(12);
    toolbar->set_margin_end(12);

    // Remote dropdown
    m_remote_model = Gtk::StringList::create({});
    m_remote_dropdown.set_model(m_remote_model);
    m_remote_dropdown.set_size_request(200, -1);
    m_remote_dropdown.property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &BrowserView::on_remote_changed));
    toolbar->append(m_remote_dropdown);

    // Path entry
    m_path_entry.set_hexpand(true);
    m_path_entry.set_placeholder_text("Path");
    m_path_entry.signal_activate().connect([this]() {
        navigate(m_path_entry.get_text());
    });
    toolbar->append(m_path_entry);

    // Navigation buttons
    m_back_btn.set_icon_name("go-previous-symbolic");
    m_back_btn.set_tooltip_text("Go Back");
    m_back_btn.add_css_class("flat");
    m_back_btn.signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::go_back));
    toolbar->append(m_back_btn);

    m_up_btn.set_icon_name("go-up-symbolic");
    m_up_btn.set_tooltip_text("Go Up");
    m_up_btn.add_css_class("flat");
    m_up_btn.signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::go_up));
    toolbar->append(m_up_btn);

    m_refresh_btn.set_icon_name("view-refresh-symbolic");
    m_refresh_btn.set_tooltip_text("Refresh");
    m_refresh_btn.add_css_class("flat");
    m_refresh_btn.signal_clicked().connect([this]() { navigate(m_current_path); });
    toolbar->append(m_refresh_btn);

    append(*toolbar);

    // Separator
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    append(*sep);

    // Column view
    m_selection = Gtk::SingleSelection::create(m_list_store);

    m_column_view = Gtk::make_managed<Gtk::ColumnView>(m_selection);
    m_column_view->set_vexpand(true);
    m_column_view->add_css_class("data-table");

    // Name column
    auto name_factory = Gtk::SignalListItemFactory::create();
    name_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0);
        label->set_hexpand(true);
        box->append(*icon);
        box->append(*label);
        item->set_child(*box);
    });
    name_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto* icon = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* label = dynamic_cast<Gtk::Label*>(icon->get_next_sibling());
        if (icon) icon->set_from_icon_name(
            obj->property_is_dir.get_value() ? "folder-symbolic" : "text-x-generic-symbolic");
        if (label) label->set_text(obj->property_name.get_value());
    });
    auto name_col = Gtk::ColumnViewColumn::create("Name", name_factory);
    name_col->set_expand(true);
    m_column_view->append_column(name_col);

    // Size column
    auto size_factory = Gtk::SignalListItemFactory::create();
    size_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(1);
        item->set_child(*label);
    });
    size_factory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        if (obj->property_is_dir.get_value())
            label->set_text("--");
        else
            label->set_text(format_size(obj->property_size.get_value()));
    });
    auto size_col = Gtk::ColumnViewColumn::create("Size", size_factory);
    size_col->set_fixed_width(100);
    m_column_view->append_column(size_col);

    // Modified column
    auto mod_factory = Gtk::SignalListItemFactory::create();
    mod_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0);
        item->set_child(*label);
    });
    mod_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        auto mod = std::string(obj->property_mod_time.get_value());
        // Show just the date part (first 10 chars of ISO timestamp)
        if (mod.size() >= 10) mod = mod.substr(0, 10);
        label->set_text(mod);
    });
    auto mod_col = Gtk::ColumnViewColumn::create("Modified", mod_factory);
    mod_col->set_fixed_width(120);
    m_column_view->append_column(mod_col);

    // Activate (double-click / enter)
    m_column_view->signal_activate().connect(
        sigc::mem_fun(*this, &BrowserView::on_item_activated));

    m_scroll.set_vexpand(true);
    m_scroll.set_child(*m_column_view);
    append(m_scroll);

    // Status bar
    auto* status_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    status_box->set_margin_start(12);
    status_box->set_margin_end(12);
    status_box->set_margin_top(4);
    status_box->set_margin_bottom(4);
    m_spinner.set_spinning(false);
    status_box->append(m_spinner);
    m_status_label.set_xalign(0);
    status_box->append(m_status_label);
    append(*status_box);
}

void BrowserView::load_remotes() {
    m_manager.cli().list_remotes([this](auto result) {
        if (!result.has_value()) return;
        m_remotes = std::move(result.value());

        // Clear and repopulate
        while (m_remote_model->get_n_items() > 0)
            m_remote_model->remove(0);

        for (auto& r : m_remotes)
            m_remote_model->append(r.name);
    });
}

void BrowserView::on_remote_changed() {
    auto idx = m_remote_dropdown.get_selected();
    if (idx == GTK_INVALID_LIST_POSITION || idx >= m_remotes.size()) return;
    m_current_remote = m_remotes[idx].name;
    m_current_path = "";
    m_path_history.clear();
    navigate("");
}

void BrowserView::navigate(const std::string& path) {
    if (m_current_remote.empty()) return;

    m_current_path = path;
    m_path_entry.set_text(path);
    m_spinner.set_spinning(true);
    update_status("Loading...");

    std::string remote_path = m_current_remote + ":" + path;
    m_manager.cli().lsjson(remote_path, [this](auto result) {
        m_spinner.set_spinning(false);

        if (!result.has_value()) {
            update_status("Error: " + result.error());
            return;
        }

        auto& entries = result.value();
        m_list_store->remove_all();

        // Sort: directories first, then by name
        auto sorted = entries;
        std::ranges::sort(sorted, [](const auto& a, const auto& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return a.name < b.name;
        });

        for (auto& entry : sorted)
            m_list_store->append(FileObject::create(entry));

        update_status(std::format("{} items", entries.size()));
    });
}

void BrowserView::on_item_activated(guint position) {
    auto obj = m_list_store->get_item(position);
    if (!obj || !obj->property_is_dir.get_value()) return;

    m_path_history.push_back(m_current_path);
    std::string new_path = m_current_path.empty()
        ? std::string(obj->property_path.get_value())
        : m_current_path + "/" + std::string(obj->property_name.get_value());
    navigate(new_path);
}

void BrowserView::go_back() {
    if (m_path_history.empty()) return;
    auto prev = m_path_history.back();
    m_path_history.pop_back();
    navigate(prev);
}

void BrowserView::go_up() {
    if (m_current_path.empty()) return;
    m_path_history.push_back(m_current_path);
    auto pos = m_current_path.rfind('/');
    navigate(pos == std::string::npos ? "" : m_current_path.substr(0, pos));
}

void BrowserView::update_status(const std::string& text) {
    m_status_label.set_text(text);
}

std::string BrowserView::format_size(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024 * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.1f} GB", bytes / (1024.0 * 1024 * 1024));
}

} // namespace saddle
