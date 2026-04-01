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
#include <format>
#include <unordered_map>

namespace saddle {

// GObject property helpers for use inside C comparison callbacks
static Glib::ustring get_str_prop(GObject* obj, const char* prop) {
    gchar* s = nullptr;
    g_object_get(obj, prop, &s, nullptr);
    Glib::ustring r = s ? s : "";
    g_free(s);
    return r;
}

static gint64 get_int64_prop(GObject* obj, const char* prop) {
    gint64 v = 0;
    g_object_get(obj, prop, &v, nullptr);
    return v;
}

static bool get_bool_prop(GObject* obj, const char* prop) {
    gboolean v = FALSE;
    g_object_get(obj, prop, &v, nullptr);
    return static_cast<bool>(v);
}

static std::string remote_type_to_icon(const std::string& type) {
    if (type == "local") return "drive-harddisk-symbolic";
    return "folder-remote-symbolic";
}

BrowserView::BrowserView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {
    set_vexpand(true);
    set_hexpand(true);

    m_list_store   = Gio::ListStore<FileObject>::create();
    m_remote_store = Gio::ListStore<RemoteObject>::create();

    m_split_view = adw::navigation_split_view_new();
    adw::navigation_split_view_set_min_sidebar_width(m_split_view, 180.0);
    adw::navigation_split_view_set_sidebar_width_fraction(m_split_view, 0.25);

    setup_sidebar();
    setup_content();

    auto* sv = adw::navigation_split_view_widget(m_split_view);
    sv->set_vexpand(true);
    sv->set_hexpand(true);
    append(*sv);

    signal_map().connect([this]() { load_remotes(); });
}

void BrowserView::setup_sidebar() {
    m_remote_selection = Gtk::SingleSelection::create(m_remote_store);
    m_remote_selection->set_autoselect(false);
    m_remote_selection->set_can_unselect(true);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        box->set_margin_start(8);
        box->set_margin_end(8);
        box->set_margin_top(4);
        box->set_margin_bottom(4);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        auto* lbl  = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        lbl->set_hexpand(true);
        box->append(*icon);
        box->append(*lbl);
        item->set_child(*box);
    });
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<RemoteObject>(item->get_item());
        if (!obj) return;
        auto* box  = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto* icon = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* lbl  = dynamic_cast<Gtk::Label*>(icon ? icon->get_next_sibling() : nullptr);
        if (icon) icon->set_from_icon_name(
            remote_type_to_icon(std::string(obj->property_type.get_value())));
        if (lbl)  lbl->set_text(obj->property_name.get_value());
    });

    auto* list_view = Gtk::make_managed<Gtk::ListView>(m_remote_selection, factory);
    list_view->add_css_class("navigation-sidebar");

    m_remote_selection->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &BrowserView::on_remote_selection_changed));

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_child(*list_view);

    auto* sidebar_toolbar = adw::toolbar_view();
    adw::toolbar_view_set_content(sidebar_toolbar, scroll);

    auto* sidebar_page = adw::navigation_page_new(sidebar_toolbar, "Remotes");
    adw::navigation_split_view_set_sidebar(m_split_view, sidebar_page);
}

void BrowserView::setup_content() {
    auto* content_toolbar = adw::toolbar_view();

    // Plain toolbar box — no AdwHeaderBar, so no window controls
    auto* nav_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    nav_bar->add_css_class("toolbar");
    nav_bar->set_margin_start(6);
    nav_bar->set_margin_end(6);
    nav_bar->set_margin_top(4);
    nav_bar->set_margin_bottom(4);

    m_back_btn.set_icon_name("go-previous-symbolic");
    m_back_btn.set_tooltip_text("Go Back");
    m_back_btn.add_css_class("flat");
    m_back_btn.signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::go_back));
    nav_bar->append(m_back_btn);

    m_up_btn.set_icon_name("go-up-symbolic");
    m_up_btn.set_tooltip_text("Go Up");
    m_up_btn.add_css_class("flat");
    m_up_btn.signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::go_up));
    nav_bar->append(m_up_btn);

    m_breadcrumb_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    m_breadcrumb_box->set_hexpand(true);
    nav_bar->append(*m_breadcrumb_box);

    m_refresh_btn.set_icon_name("view-refresh-symbolic");
    m_refresh_btn.set_tooltip_text("Refresh");
    m_refresh_btn.add_css_class("flat");
    m_refresh_btn.signal_clicked().connect([this]() { navigate(m_current_path); });
    nav_bar->append(m_refresh_btn);

    adw::toolbar_view_add_top_bar(content_toolbar, nav_bar);

    m_content_stack = Gtk::make_managed<Gtk::Stack>();
    m_content_stack->set_vexpand(true);
    m_content_stack->set_hexpand(true);
    m_content_stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);

    // "no-remote" state
    m_no_remote_status = adw::status_page();
    adw::status_page_set_icon_name(m_no_remote_status, "network-server-symbolic");
    adw::status_page_set_title(m_no_remote_status, "No Remote Selected");
    adw::status_page_set_description(m_no_remote_status, "Choose a remote from the sidebar.");
    m_content_stack->add(*m_no_remote_status, "no-remote");

    // "loading" state
    auto* loading_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    loading_box->set_halign(Gtk::Align::CENTER);
    loading_box->set_valign(Gtk::Align::CENTER);
    loading_box->set_vexpand(true);
    auto* spinner_widget = adw::spinner();
    spinner_widget->set_size_request(32, 32);
    loading_box->append(*spinner_widget);
    m_content_stack->add(*loading_box, "loading");

    // "empty" state
    m_empty_status = adw::status_page();
    adw::status_page_set_icon_name(m_empty_status, "folder-open-symbolic");
    adw::status_page_set_title(m_empty_status, "Empty Folder");
    adw::status_page_set_description(m_empty_status, "This directory contains no files.");
    m_content_stack->add(*m_empty_status, "empty");

    // "files" state
    build_column_view();
    auto* file_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    file_scroll->set_vexpand(true);
    file_scroll->set_child(*m_column_view);
    m_content_stack->add(*file_scroll, "files");

    adw::toolbar_view_set_content(content_toolbar, m_content_stack);

    auto* content_page = adw::navigation_page_new(content_toolbar, "Browse");
    adw::navigation_split_view_set_content(m_split_view, content_page);

    show_content_state("no-remote");
}

void BrowserView::build_column_view() {
    m_sort_model     = Gtk::SortListModel::create(m_list_store, {});
    m_file_selection = Gtk::SingleSelection::create(m_sort_model);

    m_column_view = Gtk::make_managed<Gtk::ColumnView>(m_file_selection);
    m_column_view->set_vexpand(true);
    m_column_view->add_css_class("data-table");

    // --- Name column ---
    auto name_factory = Gtk::SignalListItemFactory::create();
    name_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_pixel_size(16);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        box->append(*icon);
        box->append(*label);
        item->set_child(*box);
    });
    name_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* box   = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto* icon  = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* label = dynamic_cast<Gtk::Label*>(icon ? icon->get_next_sibling() : nullptr);
        if (icon)  icon->set_from_icon_name(
            mime_to_icon(std::string(obj->property_mime_type.get_value()),
                         obj->property_is_dir.get_value()));
        if (label) label->set_text(obj->property_name.get_value());
    });
    auto name_col = Gtk::ColumnViewColumn::create("Name", name_factory);
    name_col->set_expand(true);
    name_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        auto na = get_str_prop(a, "name").lowercase();
        auto nb = get_str_prop(b, "name").lowercase();
        return na < nb ? -1 : (na > nb ? 1 : 0);
    }));
    m_column_view->append_column(name_col);

    // --- Size column ---
    auto size_factory = Gtk::SignalListItemFactory::create();
    size_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(1.0f);
        item->set_child(*label);
    });
    size_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        label->set_text(obj->property_is_dir.get_value()
            ? "--" : format_size(obj->property_size.get_value()));
    });
    auto size_col = Gtk::ColumnViewColumn::create("Size", size_factory);
    size_col->set_fixed_width(100);
    size_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        auto sa = get_int64_prop(a, "size");
        auto sb = get_int64_prop(b, "size");
        return sa < sb ? -1 : (sa > sb ? 1 : 0);
    }));
    m_column_view->append_column(size_col);

    // --- Modified column ---
    auto mod_factory = Gtk::SignalListItemFactory::create();
    mod_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0.0f);
        item->set_child(*label);
    });
    mod_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        auto mod = std::string(obj->property_mod_time.get_value());
        if (mod.size() >= 10) mod = mod.substr(0, 10);
        label->set_text(mod);
    });
    auto mod_col = Gtk::ColumnViewColumn::create("Modified", mod_factory);
    mod_col->set_fixed_width(120);
    mod_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        return get_str_prop(a, "mod-time").compare(get_str_prop(b, "mod-time"));
    }));
    m_column_view->append_column(mod_col);

    m_column_view->signal_activate().connect(
        sigc::mem_fun(*this, &BrowserView::on_item_activated));

    // Dirs-first primary sort + user column sort via MultiSorter
    auto dirs_first = adw::make_sorter([](GObject* a, GObject* b) -> int {
        bool da = get_bool_prop(a, "is-dir");
        bool db = get_bool_prop(b, "is-dir");
        if (da == db) return 0;
        return da ? -1 : 1;
    });

    auto multi = Gtk::MultiSorter::create();
    multi->append(dirs_first);
    multi->append(m_column_view->get_sorter());
    m_sort_model->set_sorter(multi);
}

void BrowserView::load_remotes() {
    m_manager.cli().list_remotes([this](auto result) {
        if (!result.has_value()) return;
        m_remotes = std::move(result.value());
        m_remote_store->remove_all();
        for (auto& r : m_remotes)
            m_remote_store->append(RemoteObject::create(r));
    });
}

void BrowserView::on_remote_selection_changed() {
    auto idx = m_remote_selection->get_selected();
    if (idx == GTK_INVALID_LIST_POSITION) {
        m_current_remote.clear();
        show_content_state("no-remote");
        return;
    }
    auto obj = std::dynamic_pointer_cast<RemoteObject>(m_remote_store->get_item(idx));
    if (!obj) return;
    m_current_remote = obj->property_name.get_value();
    m_current_path   = "";
    m_path_history.clear();
    adw::navigation_split_view_set_show_content(m_split_view, true);
    navigate("");
}

void BrowserView::navigate(const std::string& path) {
    if (m_current_remote.empty()) return;
    m_current_path = path;
    uint64_t gen   = ++m_load_generation;

    show_content_state("loading");
    rebuild_breadcrumbs();

    m_manager.cli().lsjson(m_current_remote + ":" + path, [this, gen](auto result) {
        if (gen != m_load_generation) return;
        if (!result.has_value()) {
            show_content_state("empty");
            return;
        }
        m_list_store->remove_all();
        for (auto& e : result.value())
            m_list_store->append(FileObject::create(e));
        show_content_state(result.value().empty() ? "empty" : "files");
    });
}

void BrowserView::rebuild_breadcrumbs() {
    while (auto* child = m_breadcrumb_box->get_first_child())
        m_breadcrumb_box->remove(*child);

    auto* root_btn = Gtk::make_managed<Gtk::Button>(
        m_current_remote.empty() ? Glib::ustring("Remote")
                                 : Glib::ustring(m_current_remote));
    root_btn->add_css_class("flat");
    root_btn->signal_clicked().connect([this]() {
        m_path_history.push_back(m_current_path);
        navigate("");
    });
    m_breadcrumb_box->append(*root_btn);

    if (m_current_path.empty()) return;

    // Split path into segments
    std::vector<std::string> segments;
    std::string seg;
    for (char c : m_current_path) {
        if (c == '/') {
            if (!seg.empty()) { segments.push_back(seg); seg.clear(); }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) segments.push_back(seg);

    std::string accumulated;
    for (size_t i = 0; i < segments.size(); ++i) {
        auto* sep = Gtk::make_managed<Gtk::Label>("›");
        sep->add_css_class("dim-label");
        m_breadcrumb_box->append(*sep);

        if (!accumulated.empty()) accumulated += '/';
        accumulated += segments[i];

        auto* btn = Gtk::make_managed<Gtk::Button>(Glib::ustring(segments[i]));
        btn->add_css_class("flat");
        if (i == segments.size() - 1) {
            btn->set_sensitive(false);
        } else {
            std::string target = accumulated;
            btn->signal_clicked().connect([this, target]() {
                m_path_history.push_back(m_current_path);
                navigate(target);
            });
        }
        m_breadcrumb_box->append(*btn);
    }
}

void BrowserView::on_item_activated(guint position) {
    auto obj = std::dynamic_pointer_cast<FileObject>(m_sort_model->get_object(position));
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

void BrowserView::show_content_state(const std::string& name) {
    m_content_stack->set_visible_child(name);
    bool active = (name == "files" || name == "empty");
    m_back_btn.set_sensitive(!m_path_history.empty());
    m_up_btn.set_sensitive(!m_current_path.empty() && active);
    m_refresh_btn.set_sensitive(!m_current_remote.empty() && name != "no-remote");
}

// static
std::string BrowserView::format_size(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.1f} GB", bytes / (1024.0 * 1024 * 1024));
}

// static
std::string BrowserView::mime_to_icon(const std::string& mime, bool is_dir) {
    if (is_dir) return "folder-symbolic";
    if (mime.empty()) return "text-x-generic-symbolic";

    static const std::unordered_map<std::string, std::string> exact = {
        {"application/pdf",               "x-office-document-symbolic"},
        {"application/zip",               "package-x-generic-symbolic"},
        {"application/x-tar",             "package-x-generic-symbolic"},
        {"application/gzip",              "package-x-generic-symbolic"},
        {"application/x-bzip2",           "package-x-generic-symbolic"},
        {"application/x-xz",              "package-x-generic-symbolic"},
        {"application/x-7z-compressed",   "package-x-generic-symbolic"},
        {"application/x-rar-compressed",  "package-x-generic-symbolic"},
        {"application/vnd.ms-excel",      "x-office-spreadsheet-symbolic"},
        {"application/msword",            "x-office-document-symbolic"},
        {"application/vnd.ms-powerpoint", "x-office-presentation-symbolic"},
        {"application/x-executable",      "application-x-executable-symbolic"},
        {"application/json",              "text-x-script-symbolic"},
        {"application/xml",               "text-xml-symbolic"},
        {"image/svg+xml",                 "image-x-generic-symbolic"},
    };
    if (auto it = exact.find(mime); it != exact.end()) return it->second;

    auto slash = mime.find('/');
    std::string cat = (slash != std::string::npos) ? mime.substr(0, slash) : mime;
    std::string sub = (slash != std::string::npos) ? mime.substr(slash + 1) : "";

    if (cat == "image") return "image-x-generic-symbolic";
    if (cat == "video") return "video-x-generic-symbolic";
    if (cat == "audio") return "audio-x-generic-symbolic";
    if (cat == "font")  return "font-x-generic-symbolic";
    if (cat == "text") {
        if (sub == "csv") return "x-office-spreadsheet-symbolic";
        if (sub == "html" || sub == "xml") return "text-xml-symbolic";
        if (sub.find("script") != std::string::npos || sub == "javascript" ||
            sub == "x-python" || sub == "x-go" || sub == "x-rust" ||
            sub == "x-csrc" || sub == "x-c++src" || sub == "x-java")
            return "text-x-script-symbolic";
        return "text-x-generic-symbolic";
    }
    return "text-x-generic-symbolic";
}

} // namespace saddle
