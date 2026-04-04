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

#pragma once

#include "rclone/rclone_manager.hpp"
#include <adwaita.h>
#include <gtkmm.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace saddle {

class BackendEditView : public Gtk::Box {
public:
    using DoneCallback = std::function<void()>;

    // For creating a new remote
    BackendEditView(rclone::RcloneManager& manager, DoneCallback on_done);
    // For editing an existing remote
    BackendEditView(rclone::RcloneManager& manager, const rclone::RemoteInfo& remote,
                    DoneCallback on_done);

private:
    rclone::RcloneManager& m_manager;
    DoneCallback m_on_done;
    std::optional<rclone::RemoteInfo> m_editing;

    std::vector<rclone::ProviderInfo> m_providers;
    int m_selected_provider = -1;

    // Widgets
    Gtk::ScrolledWindow m_scroll;
    Gtk::Widget* m_form_group = nullptr;
    Gtk::Widget* m_advanced_expander = nullptr;
    Gtk::Widget* m_name_row = nullptr;
    Gtk::Widget* m_provider_combo = nullptr;
    GtkStringList* m_provider_model = nullptr;

    // OAuth widgets
    Gtk::Widget* m_oauth_group = nullptr;
    Gtk::Button m_authorize_btn{"Authorize"};
    Gtk::Label m_oauth_status;
    Gtk::Spinner m_oauth_spinner;
    std::string m_oauth_token;                           // captured token JSON
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // authorize callback guard

    // Dynamic form fields
    struct FormField {
        std::string option_name;
        Gtk::Widget* widget = nullptr;
        std::string type; // "string", "bool", "password", "combo"
    };
    std::vector<FormField> m_fields;

    void setup_ui();
    void load_providers();
    void on_provider_selected(int index);
    void build_form(const rclone::ProviderInfo& provider);
    void on_authorize();
    void on_save();
    std::vector<std::pair<std::string, std::string>> collect_params();
};

} // namespace saddle
