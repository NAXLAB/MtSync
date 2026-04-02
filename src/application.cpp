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

#include "application.hpp"
#include "window.hpp"
#include "widgets/adw_wrapper.hpp"
#include <glib.h>
#include <glibmm.h>

namespace saddle {

SaddleApplication::SaddleApplication()
    : Gtk::Application("com.saddle.Saddle") {
    adw::init();
}

Glib::RefPtr<SaddleApplication> SaddleApplication::create() {
    return Glib::make_refptr_for_instance(new SaddleApplication());
}

void SaddleApplication::ensure_daemon_running() {
    m_daemon_proxy = std::make_unique<DaemonProxy>();

    if (!m_daemon_proxy->connect()) {
        g_message("Daemon not running, starting it...");
        
        auto exe_path = Glib::find_program_in_path("saddle");
        if (exe_path.empty()) {
            exe_path = "/proc/self/exe";
        }

        try {
            Glib::spawn_async(
                {},
                {exe_path, "--daemon"}
            );
            g_message("Started daemon process");
        } catch (const Glib::Error& e) {
            g_warning("Failed to spawn daemon: %s", e.what());
        }

        for (int i = 0; i < 10; ++i) {
            g_usleep(100000);
            if (m_daemon_proxy->connect()) {
                g_message("Connected to daemon");
                return;
            }
        }

        g_warning("Could not connect to daemon after spawning");
        m_daemon_proxy.reset();
    } else {
        g_message("Connected to existing daemon");
    }
}

void SaddleApplication::on_activate() {
    if (!m_window) {
        ensure_daemon_running();

        m_window = Gtk::make_managed<SaddleWindow>(m_rclone_manager, m_daemon_proxy.get());
        add_window(*m_window);
    }
    m_window->present();
}

} // namespace saddle
