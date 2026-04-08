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

#include "views/compare_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <algorithm>
#include <format>
#include <unordered_map>

namespace saddle {

// ── Static CSS (installed once per display) ───────────────────────────────

static void install_compare_css() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_string(
        "label.compare-same      { color: alpha(@window_fg_color, 0.45); }\n"
        "label.compare-missing   { color: #e05555; }\n"
        "label.compare-extra     { color: #4a90d9; }\n"
        "label.compare-different { color: #e6a817; }\n"
        "label.compare-error     { color: #e05555; font-weight: bold; }\n"
        "label.compare-status    { font-family: monospace; font-weight: bold; }\n"
        "label.compare-meta      { font-size: 0.82em; }\n"
        "label.compare-dir-header { font-weight: bold; color: @accent_color; }\n"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// ── Helpers ───────────────────────────────────────────────────────────────

const char* CompareDialog::status_css_class(char status) {
    switch (status) {
        case '=': return "compare-same";
        case '-': return "compare-missing";
        case '+': return "compare-extra";
        case '*': return "compare-different";
        case '!': return "compare-error";
        default:  return "compare-same";
    }
}

std::string CompareDialog::format_size(int64_t bytes) {
    if (bytes < 0)   return "";
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024 * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string CompareDialog::format_date(const std::string& iso) {
    // ISO 8601: "2024-04-08T12:34:56.789Z" → "2024-04-08 12:34"
    if (iso.size() < 16) return iso;
    std::string s = iso.substr(0, 16);
    if (s[10] == 'T') s[10] = ' ';
    return s;
}

// ── Constructor ───────────────────────────────────────────────────────────

CompareDialog::CompareDialog(const std::string& src,
                             const std::string& dst,
                             rclone::RcloneManager& manager)
    : m_src(src), m_dst(dst) {
    set_title("Compare");
    set_default_size(1100, 640);
    set_modal(true);
    set_destroy_with_parent(true);
    install_compare_css();
    m_page_store = Gio::ListStore<CompareRowObject>::create();
    setup_ui();
    start_load(manager);
}

// ── UI setup ─────────────────────────────────────────────────────────────

void CompareDialog::setup_ui() {
    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_child(*root);

    // Path label below the window's built-in title bar
    auto* path_label = Gtk::make_managed<Gtk::Label>(m_src + "  →  " + m_dst);
    path_label->add_css_class("dim-label");
    path_label->set_margin_top(6);
    path_label->set_margin_bottom(6);
    path_label->set_margin_start(12);
    path_label->set_margin_end(12);
    path_label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    root->append(*path_label);

    // Main stack: loading / results / error
    m_stack = Gtk::make_managed<Gtk::Stack>();
    m_stack->set_vexpand(true);
    m_stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);

    // ── "loading" page ───────────────────────────────────────────────────
    auto* loading_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    loading_box->set_halign(Gtk::Align::CENTER);
    loading_box->set_valign(Gtk::Align::CENTER);
    loading_box->set_vexpand(true);
    auto* spinner = adw::spinner();
    spinner->set_size_request(32, 32);
    loading_box->append(*spinner);
    auto* loading_lbl = Gtk::make_managed<Gtk::Label>("Comparing…");
    loading_lbl->add_css_class("dim-label");
    loading_box->append(*loading_lbl);

    auto* hint_lbl = Gtk::make_managed<Gtk::Label>("Large scans can take a long time");
    hint_lbl->add_css_class("dim-label");
    hint_lbl->set_margin_top(4);
    loading_box->append(*hint_lbl);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->set_halign(Gtk::Align::CENTER);
    cancel_btn->set_margin_top(12);
    cancel_btn->signal_clicked().connect([this]() {
        if (m_load_state) {
            m_load_state->cancelled = true;
            for (auto& proc : m_load_state->procs)
                if (proc) proc->force_exit();
        }
        close();
    });
    loading_box->append(*cancel_btn);

    m_stack->add(*loading_box, "loading");

    // ── "results" page ───────────────────────────────────────────────────
    auto* results_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    build_column_view();
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_child(*m_column_view);
    results_box->append(*scroll);

    // Pagination footer
    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    footer->set_margin_top(6);
    footer->set_margin_bottom(6);
    footer->set_margin_start(8);
    footer->set_margin_end(8);

    m_prev_btn = Gtk::make_managed<Gtk::Button>();
    m_prev_btn->set_icon_name("go-previous-symbolic");
    m_prev_btn->add_css_class("flat");
    m_prev_btn->set_sensitive(false);
    m_prev_btn->signal_clicked().connect([this]() { show_page(m_current_page - 1); });

    m_page_label = Gtk::make_managed<Gtk::Label>();
    m_page_label->set_hexpand(true);
    m_page_label->set_xalign(0.5f);
    m_page_label->add_css_class("dim-label");

    m_next_btn = Gtk::make_managed<Gtk::Button>();
    m_next_btn->set_icon_name("go-next-symbolic");
    m_next_btn->add_css_class("flat");
    m_next_btn->set_sensitive(false);
    m_next_btn->signal_clicked().connect([this]() { show_page(m_current_page + 1); });

    footer->append(*m_prev_btn);
    footer->append(*m_page_label);
    footer->append(*m_next_btn);
    results_box->append(*footer);

    m_stack->add(*results_box, "results");

    // ── "error" page ─────────────────────────────────────────────────────
    auto* error_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    error_box->set_halign(Gtk::Align::CENTER);
    error_box->set_valign(Gtk::Align::CENTER);
    error_box->set_vexpand(true);
    m_error_label = Gtk::make_managed<Gtk::Label>();
    m_error_label->add_css_class("error");
    m_error_label->set_wrap(true);
    m_error_label->set_max_width_chars(60);
    error_box->append(*m_error_label);
    m_stack->add(*error_box, "error");

    m_stack->set_visible_child("loading");
    root->append(*m_stack);
}

// ── ColumnView ────────────────────────────────────────────────────────────

void CompareDialog::build_column_view() {
    auto selection = Gtk::SingleSelection::create(m_page_store);
    selection->set_can_unselect(true);
    m_column_view = Gtk::make_managed<Gtk::ColumnView>(selection);
    m_column_view->set_vexpand(true);
    m_column_view->add_css_class("data-table");

    // Helper: make a simple label column (string property, given xalign, optional fixed width)
    auto make_str_col = [](const char* title, const char* prop, float xalign, int fixed_w = 0,
                           bool ellipsize = false) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([xalign, ellipsize](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(xalign);
            if (ellipsize)
                lbl->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            if (xalign == 0.0f)
                lbl->set_hexpand(true);
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            bool is_hdr = !st.empty() && static_cast<char>(st[0]) == '/';
            is_hdr ? lbl->add_css_class("compare-dir-header")
                   : lbl->remove_css_class("compare-dir-header");
            gchar* val = nullptr;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(val ? val : "");
            g_free(val);
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        if (fixed_w > 0)
            col->set_fixed_width(fixed_w);
        else
            col->set_expand(true);
        return col;
    };

    // Helper: size column (gint64 property, rendered as human-readable or "--")
    auto make_size_col = [](const char* title, const char* prop) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(1.0f);
            lbl->add_css_class("compare-meta");
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            if (!st.empty() && static_cast<char>(st[0]) == '/') { lbl->set_text(""); return; }
            gint64 val = 0;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(format_size(val));
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        col->set_fixed_width(90);
        return col;
    };

    // Helper: date column (string property, formatted)
    auto make_date_col = [](const char* title, const char* prop) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(0.0f);
            lbl->add_css_class("compare-meta");
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            if (!st.empty() && static_cast<char>(st[0]) == '/') { lbl->set_text(""); return; }
            gchar* val = nullptr;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(format_date(val ? val : ""));
            g_free(val);
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        col->set_fixed_width(120);
        return col;
    };

    // Status column (centered, colored)
    static const char* k_status_classes[] = {
        "compare-same", "compare-missing", "compare-extra", "compare-different", "compare-error"
    };
    auto status_factory = Gtk::SignalListItemFactory::create();
    status_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.5f);
        lbl->add_css_class("compare-status");
        item->set_child(*lbl);
    });
    status_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
        auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!obj || !lbl) return;
        auto status_ustr = obj->property_status.get_value();
        char s = status_ustr.empty() ? '=' : static_cast<char>(status_ustr[0]);
        for (auto* cls : k_status_classes)
            lbl->remove_css_class(cls);
        if (s == '/') { lbl->set_text(""); return; }
        const char* sym;
        switch (s) {
            case '-': sym = "←"; break;
            case '+': sym = "→"; break;
            case '*': sym = "≠"; break;
            case '!': sym = "!"; break;
            default:  sym = "="; break;
        }
        lbl->set_text(sym);
        lbl->add_css_class(status_css_class(s));
    });
    auto status_col = Gtk::ColumnViewColumn::create("", status_factory);
    status_col->set_fixed_width(28);

    // Add all 7 columns
    m_column_view->append_column(make_str_col("Filename", "src-name", 0.0f, 0, true));
    m_column_view->append_column(make_size_col("Size",    "src-size"));
    m_column_view->append_column(make_date_col("Modified","src-mod"));
    m_column_view->append_column(status_col);
    m_column_view->append_column(make_str_col("Filename", "dst-name", 0.0f, 0, true));
    m_column_view->append_column(make_size_col("Size",    "dst-size"));
    m_column_view->append_column(make_date_col("Modified","dst-mod"));
}

// ── Async loading ─────────────────────────────────────────────────────────

void CompareDialog::start_load(rclone::RcloneManager& manager) {
    m_load_state = std::make_shared<LoadState>();
    auto state = m_load_state;

    auto try_finish = [this, state]() {
        if (state->cancelled) return;
        state->done_count++;
        if (state->done_count < 3) return;
        if (!state->error.empty()) {
            m_error_label->set_text("Error: " + state->error);
            m_stack->set_visible_child("error");
            return;
        }
        merge_results(state->src_entries, state->dst_entries, state->check_entries);
        show_page(0);
    };

    state->procs.push_back(
        manager.cli().lsjson_r(m_src, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->src_entries = std::move(*result);
            else if (state->error.empty()) state->error = result.error();
            try_finish();
        }));

    state->procs.push_back(
        manager.cli().lsjson_r(m_dst, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->dst_entries = std::move(*result);
            else if (state->error.empty()) state->error = result.error();
            try_finish();
        }));

    // check() never returns an error value — non-zero exit means diffs, not failure
    state->procs.push_back(
        manager.cli().check(m_src, m_dst, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->check_entries = std::move(*result);
            try_finish();
        }));
}

// ── Merge results ─────────────────────────────────────────────────────────

void CompareDialog::merge_results(const std::vector<rclone::FileEntry>& src_files,
                                   const std::vector<rclone::FileEntry>& dst_files,
                                   const std::vector<rclone::CheckEntry>& checks) {
    // Build path-keyed maps (files only, skip dirs)
    std::unordered_map<std::string, const rclone::FileEntry*> src_map, dst_map;
    for (auto& e : src_files) if (!e.is_dir) src_map[e.path] = &e;
    for (auto& e : dst_files) if (!e.is_dir) dst_map[e.path] = &e;

    auto basename = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return pos == std::string::npos ? p : p.substr(pos + 1);
    };
    auto dirpart = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return pos == std::string::npos ? "" : p.substr(0, pos);
    };

    // Sort by directory first, then by path — raw path sort interleaves root-level
    // files (dir="") with subdirectory entries, causing duplicate "/" headers.
    std::vector<rclone::CheckEntry> sorted = checks;
    std::sort(sorted.begin(), sorted.end(),
              [&dirpart](const auto& a, const auto& b) {
                  auto da = dirpart(a.path), db = dirpart(b.path);
                  if (da != db) return da < db;
                  return a.path < b.path;
              });

    m_all_rows.clear();
    m_all_rows.reserve(sorted.size() + 32);

    std::string last_dir = "\x01"; // sentinel — never matches a real path
    for (auto& ce : sorted) {
        const rclone::FileEntry* src_fe = nullptr;
        const rclone::FileEntry* dst_fe = nullptr;
        if (auto it = src_map.find(ce.path); it != src_map.end()) src_fe = it->second;
        if (auto it = dst_map.find(ce.path); it != dst_map.end()) dst_fe = it->second;

        // Insert a directory header row when the directory changes
        std::string dir = dirpart(ce.path);
        if (dir != last_dir) {
            std::string header = dir.empty() ? "/" : dir + "/";
            m_all_rows.push_back(CompareRowObject::create('/', header, -1, "", "", -1, ""));
            last_dir = dir;
        }

        std::string bn = basename(ce.path);

        // Show name only on the side where the file exists
        // rclone: '+' = in source only, '-' = in dest only
        std::string src_name = (ce.status != '-') ? bn : "";
        std::string dst_name = (ce.status != '+') ? bn : "";
        int64_t  src_size = src_fe ? src_fe->size    : -1;
        int64_t  dst_size = dst_fe ? dst_fe->size    : -1;
        std::string src_mod = src_fe ? src_fe->mod_time : "";
        std::string dst_mod = dst_fe ? dst_fe->mod_time : "";

        m_all_rows.push_back(CompareRowObject::create(
            ce.status,
            src_name, src_size, src_mod,
            dst_name, dst_size, dst_mod));
    }

    m_total_pages = m_all_rows.empty()
        ? 1
        : static_cast<int>((m_all_rows.size() + PAGE_SIZE - 1) / PAGE_SIZE);
}

// ── Pagination ────────────────────────────────────────────────────────────

void CompareDialog::show_page(int page) {
    m_current_page = page;
    m_page_store->remove_all();
    int start = page * PAGE_SIZE;
    int end   = std::min(start + PAGE_SIZE, static_cast<int>(m_all_rows.size()));
    for (int i = start; i < end; ++i)
        m_page_store->append(m_all_rows[i]);
    update_pagination_controls();
    m_stack->set_visible_child("results");
}

void CompareDialog::update_pagination_controls() {
    int total = static_cast<int>(m_all_rows.size());
    m_page_label->set_text(std::format(
        "Page {} of {}  ({} files)", m_current_page + 1, m_total_pages, total));
    m_prev_btn->set_sensitive(m_current_page > 0);
    m_next_btn->set_sensitive(m_current_page < m_total_pages - 1);
}

} // namespace saddle
