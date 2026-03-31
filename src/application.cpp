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

namespace saddle {

SaddleApplication::SaddleApplication()
    : Gtk::Application("com.saddle.Saddle") {
    adw::init();
}

Glib::RefPtr<SaddleApplication> SaddleApplication::create() {
    return Glib::make_refptr_for_instance(new SaddleApplication());
}

void SaddleApplication::on_activate() {
    if (!m_window) {
        m_window = new SaddleWindow(m_rclone_manager);
        add_window(*m_window);
    }
    m_window->present();
}

} // namespace saddle
