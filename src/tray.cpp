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

const gchar SADDLE_SERVICE[]    = "com.saddle.Daemon";
const gchar SADDLE_PATH[]       = "/com/saddle/Daemon";
const gchar SADDLE_IFACE[]      = "com.saddle.Daemon";

const gchar SADDLE_SNI_PATH[]   = "/StatusNotifierItem";
const gchar SNI_IFACE[]         = "org.kde.StatusNotifierItem";
const gchar SNI_WATCHER_SVC[]   = "org.kde.StatusNotifierWatcher";
const gchar SNI_WATCHER_OBJ[]   = "/StatusNotifierWatcher";
const gchar SNI_WATCHER_IFACE[] = "org.kde.StatusNotifierWatcher";

const gchar SNI_ICON[]          = "network-server-symbolic";

const gchar SADDLE_MENU_PATH[]  = "/com/saddle/Daemon/Menu";

std::string g_tooltip = "Saddle";
int g_running_jobs = 0;
bool g_attention = false;
saddle::TrayIcon* g_tray = nullptr;

// ── com.saddle.Daemon handlers ────────────────────────────────────────────────

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

static const GDBusInterfaceVTable saddle_vtable = {
    handle_method_call,
    handle_get_prop,
    handle_set_prop
};

// ── org.kde.StatusNotifierItem handlers ──────────────────────────────────────

static void handle_method_call_sni(GDBusConnection*, const gchar*, const gchar*,
                                   const gchar*, const gchar* method,
                                   GVariant*, GDBusMethodInvocation* inv, gpointer) {
    if (g_strcmp0(method, "Activate") == 0 || g_strcmp0(method, "SecondaryActivate") == 0) {
        if (g_tray) g_tray->signal_show_window().emit();
    }
    g_dbus_method_invocation_return_value(inv, nullptr);
}

static GVariant* handle_get_prop_sni(GDBusConnection*, const gchar*, const gchar*,
                                     const gchar*, const gchar* prop, GError**, gpointer) {
    if (g_strcmp0(prop, "Category") == 0)
        return g_variant_new_string("ApplicationStatus");
    if (g_strcmp0(prop, "Id") == 0)
        return g_variant_new_string("saddle");
    if (g_strcmp0(prop, "Title") == 0)
        return g_variant_new_string("Saddle");
    if (g_strcmp0(prop, "Status") == 0)
        return g_variant_new_string(g_attention ? "NeedsAttention" : "Active");
    if (g_strcmp0(prop, "WindowId") == 0)
        return g_variant_new_uint32(0);
    if (g_strcmp0(prop, "IconName") == 0)
        return g_variant_new_string(SNI_ICON);
    if (g_strcmp0(prop, "OverlayIconName") == 0)
        return g_variant_new_string("");
    if (g_strcmp0(prop, "AttentionIconName") == 0)
        return g_variant_new_string(SNI_ICON);
    if (g_strcmp0(prop, "AttentionMovieName") == 0)
        return g_variant_new_string("");
    if (g_strcmp0(prop, "IconPixmap") == 0 ||
        g_strcmp0(prop, "OverlayIconPixmap") == 0 ||
        g_strcmp0(prop, "AttentionIconPixmap") == 0)
        return g_variant_new("a(iiay)", nullptr);
    if (g_strcmp0(prop, "ItemIsMenu") == 0)
        return g_variant_new_boolean(FALSE);
    if (g_strcmp0(prop, "Menu") == 0)
        return g_variant_new_object_path(SADDLE_MENU_PATH);
    if (g_strcmp0(prop, "ToolTip") == 0) {
        // (sa(iiay)ss): icon_name, icon_data[], title, description
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("(sa(iiay)ss)"));
        g_variant_builder_add(&b, "s", "");
        g_variant_builder_open(&b, G_VARIANT_TYPE("a(iiay)"));
        g_variant_builder_close(&b);
        g_variant_builder_add(&b, "s", "Saddle");
        g_variant_builder_add(&b, "s", g_tooltip.c_str());
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

static gboolean handle_set_prop_sni(GDBusConnection*, const gchar*, const gchar*,
                                    const gchar*, const gchar*, GVariant*, GError**, gpointer) {
    return FALSE;
}

static const GDBusInterfaceVTable sni_vtable = {
    handle_method_call_sni,
    handle_get_prop_sni,
    handle_set_prop_sni
};

// ── com.canonical.dbusmenu helpers ────────────────────────────────────────────

static GVariant* make_menu_item(int id, const char* label) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "label",   g_variant_new_string(label));
    g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    return g_variant_new("(ia{sv}av)", id, &props, &children);
}

static GVariant* make_menu_layout() {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    g_variant_builder_add(&children, "v", make_menu_item(1, "Open"));
    g_variant_builder_add(&children, "v", make_menu_item(2, "Quit"));
    return g_variant_new("(ia{sv}av)", 0, &props, &children);
}

// ── com.canonical.dbusmenu handlers ──────────────────────────────────────────

static void handle_method_call_menu(GDBusConnection*, const gchar*, const gchar*,
                                    const gchar*, const gchar* method,
                                    GVariant* params, GDBusMethodInvocation* inv, gpointer) {
    if (g_strcmp0(method, "GetLayout") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(u@(ia{sv}av))", 1u, make_menu_layout()));

    } else if (g_strcmp0(method, "GetGroupProperties") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&b)));

    } else if (g_strcmp0(method, "GetProperty") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(v)", g_variant_new_string("")));

    } else if (g_strcmp0(method, "Event") == 0) {
        gint32 id = 0;
        const gchar* event_id = nullptr;
        GVariant* data_v = nullptr;
        guint32 ts = 0;
        g_variant_get(params, "(is@vu)", &id, &event_id, &data_v, &ts);
        if (data_v) g_variant_unref(data_v);
        if (g_strcmp0(event_id, "clicked") == 0 && g_tray) {
            if (id == 1) g_tray->signal_show_window().emit();
            else if (id == 2) g_tray->signal_quit().emit();
        }
        g_dbus_method_invocation_return_value(inv, nullptr);

    } else if (g_strcmp0(method, "EventGroup") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai)", g_variant_builder_end(&b)));

    } else if (g_strcmp0(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));

    } else if (g_strcmp0(method, "AboutToShowGroup") == 0) {
        GVariantBuilder bu, bi;
        g_variant_builder_init(&bu, G_VARIANT_TYPE("ab"));
        g_variant_builder_init(&bi, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(@ab@ai)", g_variant_builder_end(&bu), g_variant_builder_end(&bi)));

    } else {
        g_dbus_method_invocation_return_value(inv, nullptr);
    }
}

static GVariant* handle_get_prop_menu(GDBusConnection*, const gchar*, const gchar*,
                                      const gchar*, const gchar* prop, GError**, gpointer) {
    if (g_strcmp0(prop, "Version") == 0)       return g_variant_new_uint32(3);
    if (g_strcmp0(prop, "TextDirection") == 0) return g_variant_new_string("ltr");
    if (g_strcmp0(prop, "Status") == 0)        return g_variant_new_string("normal");
    if (g_strcmp0(prop, "IconThemePath") == 0) return g_variant_new_strv(nullptr, 0);
    return nullptr;
}

static gboolean handle_set_prop_menu(GDBusConnection*, const gchar*, const gchar*,
                                     const gchar*, const gchar*, GVariant*, GError**, gpointer) {
    return FALSE;
}

static const GDBusInterfaceVTable menu_vtable = {
    handle_method_call_menu,
    handle_get_prop_menu,
    handle_set_prop_menu
};

// ── Bus callbacks ─────────────────────────────────────────────────────────────

void on_bus_acquired(GDBusConnection* conn, const gchar*, gpointer data) {
    auto* self = static_cast<saddle::TrayIcon*>(data);
    self->m_connection = static_cast<GDBusConnection*>(g_object_ref(conn));

    GError* error = nullptr;

    // Register com.saddle.Daemon IPC object
    auto* daemon_node = g_dbus_node_info_new_for_xml(
        "<node>"
        "  <interface name='com.saddle.Daemon'>"
        "    <method name='ShowWindow'/>"
        "    <method name='Quit'/>"
        "    <property name='Tooltip' type='s' access='read'/>"
        "  </interface>"
        "</node>", &error);

    if (error) {
        g_warning("Tray: failed to parse daemon introspection: %s", error->message);
        g_error_free(error);
        error = nullptr;
    } else {
        g_dbus_connection_register_object(
            conn, SADDLE_PATH, daemon_node->interfaces[0],
            &saddle_vtable, self, nullptr, &error);
        g_dbus_node_info_unref(daemon_node);
        if (error) {
            g_warning("Tray: failed to register daemon object: %s", error->message);
            g_error_free(error);
            error = nullptr;
        } else {
            g_message("Tray: D-Bus service registered at %s", SADDLE_PATH);
        }
    }

    // Register org.kde.StatusNotifierItem object
    auto* sni_node = g_dbus_node_info_new_for_xml(
        "<node>"
        "  <interface name='org.kde.StatusNotifierItem'>"
        "    <property name='Category'           type='s'            access='read'/>"
        "    <property name='Id'                 type='s'            access='read'/>"
        "    <property name='Title'              type='s'            access='read'/>"
        "    <property name='Status'             type='s'            access='read'/>"
        "    <property name='WindowId'           type='u'            access='read'/>"
        "    <property name='IconName'           type='s'            access='read'/>"
        "    <property name='OverlayIconName'    type='s'            access='read'/>"
        "    <property name='AttentionIconName'  type='s'            access='read'/>"
        "    <property name='AttentionMovieName' type='s'            access='read'/>"
        "    <property name='ToolTip'            type='(sa(iiay)ss)' access='read'/>"
        "    <property name='IconPixmap'          type='a(iiay)'      access='read'/>"
        "    <property name='OverlayIconPixmap'  type='a(iiay)'      access='read'/>"
        "    <property name='AttentionIconPixmap' type='a(iiay)'     access='read'/>"
        "    <property name='ItemIsMenu'         type='b'            access='read'/>"
        "    <property name='Menu'               type='o'            access='read'/>"
        "    <method name='Activate'>"
        "      <arg type='i' name='x' direction='in'/>"
        "      <arg type='i' name='y' direction='in'/>"
        "    </method>"
        "    <method name='SecondaryActivate'>"
        "      <arg type='i' name='x' direction='in'/>"
        "      <arg type='i' name='y' direction='in'/>"
        "    </method>"
        "    <method name='ContextMenu'>"
        "      <arg type='i' name='x' direction='in'/>"
        "      <arg type='i' name='y' direction='in'/>"
        "    </method>"
        "    <method name='Scroll'>"
        "      <arg type='i' name='delta'       direction='in'/>"
        "      <arg type='s' name='orientation' direction='in'/>"
        "    </method>"
        "    <signal name='NewTitle'/>"
        "    <signal name='NewIcon'/>"
        "    <signal name='NewAttentionIcon'/>"
        "    <signal name='NewOverlayIcon'/>"
        "    <signal name='NewToolTip'/>"
        "    <signal name='NewStatus'>"
        "      <arg type='s' name='status'/>"
        "    </signal>"
        "  </interface>"
        "</node>", &error);

    if (error) {
        g_warning("Tray: failed to parse SNI introspection: %s", error->message);
        g_error_free(error);
        error = nullptr;
    } else {
        guint sni_id = g_dbus_connection_register_object(
            conn, SADDLE_SNI_PATH, sni_node->interfaces[0],
            &sni_vtable, self, nullptr, &error);
        g_dbus_node_info_unref(sni_node);

        if (error) {
            g_warning("Tray: failed to register SNI object: %s", error->message);
            g_error_free(error);
        } else {
            self->m_sni_reg_id = sni_id;
            g_message("Tray: SNI object registered at %s", SADDLE_SNI_PATH);
        }
    }

    // Register com.canonical.dbusmenu object
    auto* menu_node = g_dbus_node_info_new_for_xml(
        "<node>"
        "  <interface name='com.canonical.dbusmenu'>"
        "    <property name='Version'       type='u'  access='read'/>"
        "    <property name='TextDirection' type='s'  access='read'/>"
        "    <property name='Status'        type='s'  access='read'/>"
        "    <property name='IconThemePath' type='as' access='read'/>"
        "    <method name='GetLayout'>"
        "      <arg type='i'          name='parentId'       direction='in'/>"
        "      <arg type='i'          name='recursionDepth' direction='in'/>"
        "      <arg type='as'         name='propertyNames'  direction='in'/>"
        "      <arg type='u'          name='revision'       direction='out'/>"
        "      <arg type='(ia{sv}av)' name='layout'         direction='out'/>"
        "    </method>"
        "    <method name='GetGroupProperties'>"
        "      <arg type='ai'       name='ids'           direction='in'/>"
        "      <arg type='as'       name='propertyNames' direction='in'/>"
        "      <arg type='a(ia{sv})' name='properties'   direction='out'/>"
        "    </method>"
        "    <method name='GetProperty'>"
        "      <arg type='i' name='id'    direction='in'/>"
        "      <arg type='s' name='name'  direction='in'/>"
        "      <arg type='v' name='value' direction='out'/>"
        "    </method>"
        "    <method name='Event'>"
        "      <arg type='i' name='id'        direction='in'/>"
        "      <arg type='s' name='eventId'   direction='in'/>"
        "      <arg type='v' name='data'      direction='in'/>"
        "      <arg type='u' name='timestamp' direction='in'/>"
        "    </method>"
        "    <method name='EventGroup'>"
        "      <arg type='a(isvu)' name='events'   direction='in'/>"
        "      <arg type='ai'      name='idErrors' direction='out'/>"
        "    </method>"
        "    <method name='AboutToShow'>"
        "      <arg type='i' name='id'         direction='in'/>"
        "      <arg type='b' name='needUpdate' direction='out'/>"
        "    </method>"
        "    <method name='AboutToShowGroup'>"
        "      <arg type='ai' name='ids'           direction='in'/>"
        "      <arg type='ab' name='updatesNeeded' direction='out'/>"
        "      <arg type='ai' name='idErrors'      direction='out'/>"
        "    </method>"
        "    <signal name='ItemsPropertiesUpdated'>"
        "      <arg type='a(ia{sv})' name='updatedProps'/>"
        "      <arg type='a(ias)'    name='removedProps'/>"
        "    </signal>"
        "    <signal name='LayoutUpdated'>"
        "      <arg type='u' name='revision'/>"
        "      <arg type='i' name='parent'/>"
        "    </signal>"
        "    <signal name='ItemActivationRequested'>"
        "      <arg type='i' name='id'/>"
        "      <arg type='u' name='timestamp'/>"
        "    </signal>"
        "  </interface>"
        "</node>", &error);

    if (error) {
        g_warning("Tray: failed to parse dbusmenu introspection: %s", error->message);
        g_error_free(error);
        error = nullptr;
    } else {
        guint menu_id = g_dbus_connection_register_object(
            conn, SADDLE_MENU_PATH, menu_node->interfaces[0],
            &menu_vtable, self, nullptr, &error);
        g_dbus_node_info_unref(menu_node);

        if (error) {
            g_warning("Tray: failed to register dbusmenu object: %s", error->message);
            g_error_free(error);
        } else {
            self->m_menu_reg_id = menu_id;
            g_message("Tray: dbusmenu registered at %s", SADDLE_MENU_PATH);
        }
    }
}

void on_name_acquired(GDBusConnection* conn, const gchar* name, gpointer) {
    g_debug("Tray: acquired name %s", name);

    // Name is now owned — safe to register with the StatusNotifierWatcher
    g_dbus_connection_call(conn,
        SNI_WATCHER_SVC, SNI_WATCHER_OBJ, SNI_WATCHER_IFACE,
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", SADDLE_SERVICE),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        +[](GObject* src, GAsyncResult* res, gpointer) {
            GError* err = nullptr;
            GVariant* r = g_dbus_connection_call_finish(
                reinterpret_cast<GDBusConnection*>(src), res, &err);
            if (err) {
                g_warning("Tray: RegisterStatusNotifierItem failed: %s", err->message);
                g_warning("Tray: Is the AppIndicator/KStatusNotifierItem extension enabled?");
                g_error_free(err);
            } else {
                g_message("Tray: registered with StatusNotifierWatcher");
            }
            if (r) g_variant_unref(r);
        },
        nullptr);
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
    if (m_menu_reg_id > 0 && m_connection)
        g_dbus_connection_unregister_object(m_connection, m_menu_reg_id);
    if (m_sni_reg_id > 0 && m_connection)
        g_dbus_connection_unregister_object(m_connection, m_sni_reg_id);
    if (m_owner_id > 0)
        g_bus_unown_name(m_owner_id);
    if (m_connection)
        g_object_unref(m_connection);
    g_tray = nullptr;
}

void TrayIcon::set_tooltip(const std::string& text) {
    g_tooltip = text;
    if (!m_connection) return;
    GError* err = nullptr;
    g_dbus_connection_emit_signal(m_connection, nullptr,
        SADDLE_SNI_PATH, SNI_IFACE, "NewToolTip", nullptr, &err);
    if (err) { g_warning("Tray: NewToolTip: %s", err->message); g_error_free(err); }
}

void TrayIcon::set_running_jobs(int count) {
    g_running_jobs = count;
}

void TrayIcon::set_attention(bool attention) {
    g_attention = attention;
    if (!m_connection) return;
    GError* err = nullptr;
    g_dbus_connection_emit_signal(m_connection, nullptr,
        SADDLE_SNI_PATH, SNI_IFACE, "NewStatus",
        g_variant_new("(s)", attention ? "NeedsAttention" : "Active"), &err);
    if (err) { g_warning("Tray: NewStatus: %s", err->message); g_error_free(err); }
}

} // namespace saddle
