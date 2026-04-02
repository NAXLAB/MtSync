/*
 * Saddle — GTK4 frontend to rclone
 * Copyright (C) 2026 Saddle contributors
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

#include "tray.hpp"

namespace {

const gchar SADDLE_SERVICE[] = "com.saddle.Daemon";
const gchar SADDLE_PATH[] = "/com/saddle/Daemon";
const gchar SADDLE_IFACE[] = "com.saddle.Daemon";

std::string g_tooltip = "Saddle";
int g_running_jobs = 0;
bool g_attention = false;
saddle::TrayIcon* g_tray = nullptr;

static void handle_method_call(GDBusConnection*, const gchar*, const gchar*,
                              const gchar*, const gchar* method,
                              GVariant*, GDBusMethodInvocation* inv, gpointer) {
    if (g_strcmp0(method, "ShowWindow") == 0) {
        if (g_tray) g_tray->signal_show_window().emit();
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (g_strcmp0(method, "Quit") == 0) {
        if (g_tray) g_tray->signal_quit().emit();
        g_dbus_method_invocation_return_value(inv, nullptr);
    }
}

static GVariant* handle_get_prop(GDBusConnection*, const gchar*, const gchar*,
                                  const gchar*, const gchar* prop, GError**, gpointer) {
    if (g_strcmp0(prop, "Tooltip") == 0)
        return g_variant_new_string(g_tooltip.c_str());
    return nullptr;
}

static gboolean handle_set_prop(GDBusConnection*, const gchar*, const gchar*,
                                const gchar*, const gchar*, GVariant*, GError**, gpointer) {
    return FALSE;
}

static const GDBusInterfaceVTable vtable = {
    handle_method_call,
    handle_get_prop,
    handle_set_prop
};

static GDBusInterfaceInfo* create_interface_info() {
    const gchar* xml = 
        "<node>"
        "  <interface name='com.saddle.Daemon'>"
        "    <method name='ShowWindow'/>"
        "    <method name='Quit'/>"
        "    <property name='Tooltip' type='s' access='read'/>"
        "  </interface>"
        "</node>";
    auto* node = g_dbus_node_info_new_for_xml(xml, nullptr);
    if (!node) return nullptr;
    auto* iface = node->interfaces[0];
    if (iface) g_dbus_interface_info_ref(iface);
    g_dbus_node_info_unref(node);
    return iface;
}

void on_bus_acquired(GDBusConnection* conn, const gchar*, gpointer data) {
    auto* self = static_cast<saddle::TrayIcon*>(data);
    self->m_connection = static_cast<GDBusConnection*>(g_object_ref(conn));
    
    GError* error = nullptr;
    auto* node_info = g_dbus_node_info_new_for_xml(
        "<node>"
        "  <interface name='com.saddle.Daemon'>"
        "    <method name='ShowWindow'/>"
        "    <method name='Quit'/>"
        "  </interface>"
        "</node>", &error);
    
    if (error) {
        g_warning("Tray: failed to parse introspection: %s", error->message);
        g_error_free(error);
        return;
    }
    
    guint id = g_dbus_connection_register_object(
        conn, SADDLE_PATH, node_info->interfaces[0], &vtable, self, nullptr, &error);
    
    g_dbus_node_info_unref(node_info);
    
    if (error) {
        g_warning("Tray: failed to register: %s", error->message);
        g_error_free(error);
    } else {
        g_message("Tray: D-Bus service registered at %s", SADDLE_PATH);
    }
}

void on_name_acquired(GDBusConnection*, const gchar* name, gpointer) {
    g_debug("Tray: acquired name %s", name);
}

void on_name_lost(GDBusConnection*, const gchar* name, gpointer) {
    g_warning("Tray: lost name %s - another instance may be running", name);
}

} // namespace

namespace saddle {

TrayIcon::TrayIcon() {
    g_tray = this;
    
    m_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        SADDLE_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        this,
        nullptr
    );
}

TrayIcon::~TrayIcon() {
    if (m_owner_id > 0) {
        g_bus_unown_name(m_owner_id);
    }
    if (m_connection) {
        g_object_unref(m_connection);
    }
    g_tray = nullptr;
}

void TrayIcon::set_tooltip(const std::string& text) {
    g_tooltip = text;
}

void TrayIcon::set_running_jobs(int count) {
    g_running_jobs = count;
}

void TrayIcon::set_attention(bool attention) {
    g_attention = attention;
}

} // namespace saddle
