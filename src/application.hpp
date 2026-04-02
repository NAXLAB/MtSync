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
#include "daemon_proxy.hpp"
#include <gtkmm.h>
#include <memory>

namespace saddle {

class SaddleWindow;

class SaddleApplication : public Gtk::Application {
public:
    static Glib::RefPtr<SaddleApplication> create();

    rclone::RcloneManager& rclone_manager() { return m_rclone_manager; }
    DaemonProxy& daemon_proxy() { return *m_daemon_proxy; }
    bool is_daemon_connected() const { return m_daemon_proxy && m_daemon_proxy->is_connected(); }

protected:
    SaddleApplication();
    void on_activate() override;

private:
    void ensure_daemon_running();

    SaddleWindow* m_window = nullptr;
    rclone::RcloneManager m_rclone_manager;
    std::unique_ptr<DaemonProxy> m_daemon_proxy;
};

} // namespace saddle
