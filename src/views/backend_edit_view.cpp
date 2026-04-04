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

#include "views/backend_edit_view.hpp"
#include "widgets/adw_wrapper.hpp"

namespace saddle {

BackendEditView::BackendEditView(rclone::RcloneManager& manager, DoneCallback on_done)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager)
    , m_on_done(std::move(on_done)) {
    setup_ui();
    load_providers();
}

BackendEditView::BackendEditView(rclone::RcloneManager& manager,
                                   const rclone::RemoteInfo& remote,
                                   DoneCallback on_done)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager)
    , m_on_done(std::move(on_done))
    , m_editing(remote) {
    setup_ui();
    load_providers();
}

void BackendEditView::setup_ui() {
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

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // Basic settings group
    auto* basic_group = adw::preferences_group();
    adw::preferences_group_set_title(basic_group, "Remote Settings");
    vbox->append(*basic_group);

    // Name entry
    m_name_row = adw::entry_row();
    adw::preferences_row_set_title(m_name_row, "Name");
    if (m_editing) {
        adw::entry_row_set_text(m_name_row, m_editing->name.c_str());
        gtk_widget_set_sensitive(m_name_row->gobj(), false);
    }
    adw::preferences_group_add(basic_group, m_name_row);

    // Provider combo
    m_provider_combo = adw::combo_row();
    adw::preferences_row_set_title(m_provider_combo, "Provider");
    m_provider_model = gtk_string_list_new(nullptr);
    adw::combo_row_set_string_list_model(m_provider_combo, m_provider_model);
    adw_combo_row_set_enable_search(ADW_COMBO_ROW(m_provider_combo->gobj()), true);
    adw::preferences_group_add(basic_group, m_provider_combo);

    // Connect provider selection change
    g_signal_connect(m_provider_combo->gobj(), "notify::selected",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<BackendEditView*>(data);
            auto idx = adw::combo_row_get_selected(self->m_provider_combo);
            if (idx != GTK_INVALID_LIST_POSITION)
                self->on_provider_selected(static_cast<int>(idx));
        }), this);

    // OAuth group (hidden by default, shown for OAuth providers)
    m_oauth_group = adw::preferences_group();
    adw::preferences_group_set_title(m_oauth_group, "Authorization");
    m_oauth_group->set_visible(false);

    auto* oauth_row = adw::action_row();
    adw::preferences_row_set_title(oauth_row, "OAuth Login");
    adw::action_row_set_subtitle(oauth_row,
        "Click Authorize to sign in via your browser");

    m_authorize_btn.add_css_class("suggested-action");
    m_authorize_btn.set_valign(Gtk::Align::CENTER);
    m_authorize_btn.signal_clicked().connect(
        sigc::mem_fun(*this, &BackendEditView::on_authorize));
    adw::action_row_add_suffix(oauth_row, &m_authorize_btn);

    m_oauth_spinner.set_spinning(false);
    m_oauth_spinner.set_visible(false);
    m_oauth_spinner.set_valign(Gtk::Align::CENTER);
    adw::action_row_add_suffix(oauth_row, &m_oauth_spinner);

    adw::preferences_group_add(m_oauth_group, oauth_row);

    m_oauth_status.set_xalign(0);
    m_oauth_status.add_css_class("dim-label");
    m_oauth_status.set_margin_start(12);
    m_oauth_status.set_visible(false);

    vbox->append(*m_oauth_group);
    vbox->append(m_oauth_status);

    // Form group for dynamic fields (created empty, populated on provider select)
    m_form_group = adw::preferences_group();
    adw::preferences_group_set_title(m_form_group, "Options");
    vbox->append(*m_form_group);

    // Advanced options expander (inside a separate group)
    auto* advanced_group = adw::preferences_group();
    m_advanced_expander = adw::expander_row();
    adw::preferences_row_set_title(m_advanced_expander, "Advanced Options");
    adw::preferences_group_add(advanced_group, m_advanced_expander);
    vbox->append(*advanced_group);

    // Save / Cancel buttons
    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);
    btn_box->set_margin_top(12);

    auto* save_btn = Gtk::make_managed<Gtk::Button>(m_editing ? "Update" : "Save");
    save_btn->add_css_class("suggested-action");
    save_btn->signal_clicked().connect(sigc::mem_fun(*this, &BackendEditView::on_save));
    btn_box->append(*save_btn);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->signal_clicked().connect([this]() {
        if (m_on_done) m_on_done();
    });
    btn_box->append(*cancel_btn);

    vbox->append(*btn_box);
}

void BackendEditView::load_providers() {
    m_manager.cli().get_providers([this](auto result) {
        if (!result.has_value()) return;
        m_providers = std::move(result.value());

        for (auto& p : m_providers) {
            auto label = p.description.empty() ? p.name : p.name + " - " + p.description;
            gtk_string_list_append(m_provider_model, label.c_str());
        }

        // If editing, select the matching provider
        if (m_editing) {
            for (size_t i = 0; i < m_providers.size(); ++i) {
                if (m_providers[i].name == m_editing->type ||
                    m_providers[i].prefix == m_editing->type) {
                    adw_combo_row_set_selected(
                        ADW_COMBO_ROW(m_provider_combo->gobj()),
                        static_cast<guint>(i));
                    break;
                }
            }
        }
    });
}

void BackendEditView::on_provider_selected(int index) {
    if (index < 0 || index >= static_cast<int>(m_providers.size())) return;
    m_selected_provider = index;
    m_oauth_token.clear();

    // Show/hide OAuth group based on provider
    bool oauth = m_providers[index].needs_oauth();
    m_oauth_group->set_visible(oauth);
    m_oauth_status.set_visible(false);
    m_authorize_btn.set_sensitive(true);
    m_authorize_btn.set_label("Authorize");
    m_oauth_spinner.set_visible(false);
    m_oauth_spinner.set_spinning(false);

    build_form(m_providers[index]);
}

void BackendEditView::build_form(const rclone::ProviderInfo& provider) {
    // Clear existing dynamic fields
    for (auto& f : m_fields) {
        if (f.type == "advanced_bool" || f.type == "advanced_string" ||
            f.type == "advanced_password" || f.type == "advanced_combo") {
            // In expander — can't easily remove individually
        } else {
            adw_preferences_group_remove(
                ADW_PREFERENCES_GROUP(m_form_group->gobj()),
                f.widget->gobj());
        }
    }
    m_fields.clear();

    for (auto& opt : provider.options) {
        // Skip the token field — we handle it via OAuth authorize
        if (opt.name == "token") continue;

        Gtk::Widget* widget = nullptr;
        std::string type;

        if (opt.type == "bool") {
            widget = adw::switch_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            if (opt.default_value == "true")
                adw::switch_row_set_active(widget, true);
            type = "bool";
        } else if (opt.is_password) {
            widget = adw::password_entry_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            type = "password";
        } else if (opt.exclusive && !opt.examples.empty()) {
            widget = adw::combo_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            auto* model = gtk_string_list_new(nullptr);
            for (auto& ex : opt.examples) {
                auto label = ex.help.empty() ? ex.value : ex.value + " - " + ex.help;
                gtk_string_list_append(model, label.c_str());
            }
            adw::combo_row_set_string_list_model(widget, model);
            type = "combo";
        } else {
            widget = adw::entry_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            if (!opt.default_value.empty())
                adw::entry_row_set_text(widget, opt.default_value.c_str());
            type = "string";
        }

        // Pre-fill if editing
        if (m_editing && m_editing->params.contains(opt.name)) {
            auto val = m_editing->params[opt.name].get<std::string>();
            if (type == "bool") {
                adw::switch_row_set_active(widget, val == "true");
            } else if (type == "string" || type == "password") {
                adw::entry_row_set_text(widget, val.c_str());
            }
        }

        // Place in appropriate container
        if (opt.advanced) {
            adw::expander_row_add_row(m_advanced_expander, widget);
            type = "advanced_" + type;
        } else {
            adw::preferences_group_add(m_form_group, widget);
        }

        m_fields.push_back({opt.name, widget, type});
    }
}

void BackendEditView::on_authorize() {
    if (m_selected_provider < 0 ||
        m_selected_provider >= static_cast<int>(m_providers.size())) return;

    auto& provider = m_providers[m_selected_provider];

    // Get client_id and client_secret from form fields if the user filled them
    std::string client_id, client_secret;
    for (auto& f : m_fields) {
        if (f.option_name == "client_id") {
            auto base = f.type;
            if (base.starts_with("advanced_")) base = base.substr(9);
            if (base == "string") client_id = adw::entry_row_get_text(f.widget);
        } else if (f.option_name == "client_secret") {
            auto base = f.type;
            if (base.starts_with("advanced_")) base = base.substr(9);
            if (base == "string" || base == "password")
                client_secret = adw::entry_row_get_text(f.widget);
        }
    }

    // Update UI to show authorizing state
    m_authorize_btn.set_sensitive(false);
    m_authorize_btn.set_label("Waiting...");
    m_oauth_spinner.set_visible(true);
    m_oauth_spinner.set_spinning(true);
    m_oauth_status.set_visible(true);
    m_oauth_status.set_text("Browser opened — complete sign-in to continue...");

    std::string backend = provider.prefix.empty() ? provider.name : provider.prefix;

    m_manager.cli().authorize(backend, client_id, client_secret,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (weak_alive.expired()) return;
            m_oauth_spinner.set_spinning(false);
            m_oauth_spinner.set_visible(false);
            m_authorize_btn.set_sensitive(true);

            if (result.has_value()) {
                m_oauth_token = std::move(result.value());
                m_authorize_btn.set_label("Authorized");
                m_authorize_btn.remove_css_class("suggested-action");
                m_authorize_btn.add_css_class("success");
                m_oauth_status.set_text("Authorization successful.");
            } else {
                m_authorize_btn.set_label("Retry");
                m_oauth_status.set_text("Authorization failed: " + result.error());
            }
        });
}

void BackendEditView::on_save() {
    std::string name = adw::entry_row_get_text(m_name_row);
    if (name.empty()) return;

    auto params = collect_params();
    auto idx = adw::combo_row_get_selected(m_provider_combo);
    if (idx == GTK_INVALID_LIST_POSITION || idx >= m_providers.size()) return;

    // Include OAuth token if we have one
    if (!m_oauth_token.empty())
        params.emplace_back("token", m_oauth_token);

    std::string provider_type = m_providers[idx].prefix.empty()
        ? m_providers[idx].name : m_providers[idx].prefix;

    auto callback = [this](auto result) {
        if (result.has_value() && m_on_done) m_on_done();
    };

    if (m_editing) {
        m_manager.cli().config_update(name, params, std::move(callback));
    } else {
        m_manager.cli().config_create(name, provider_type, params, std::move(callback));
    }
}

std::vector<std::pair<std::string, std::string>> BackendEditView::collect_params() {
    std::vector<std::pair<std::string, std::string>> params;
    for (auto& f : m_fields) {
        std::string base_type = f.type;
        if (base_type.starts_with("advanced_"))
            base_type = base_type.substr(9);

        std::string val;
        if (base_type == "bool") {
            val = adw::switch_row_get_active(f.widget) ? "true" : "false";
        } else if (base_type == "string" || base_type == "password") {
            val = adw::entry_row_get_text(f.widget);
        } else if (base_type == "combo") {
            auto sel = adw::combo_row_get_selected(f.widget);
            for (auto& provider : m_providers) {
                for (auto& opt : provider.options) {
                    if (opt.name == f.option_name && sel < opt.examples.size()) {
                        val = opt.examples[sel].value;
                        break;
                    }
                }
                if (!val.empty()) break;
            }
        }

        if (!val.empty())
            params.emplace_back(f.option_name, val);
    }
    return params;
}

} // namespace saddle
