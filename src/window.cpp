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

#include "window.hpp"
#include "widgets/adw_wrapper.hpp"

namespace saddle {

SaddleWindow::SaddleWindow(rclone::RcloneManager& manager, DaemonProxy* daemon_proxy)
    : m_backends_view(manager)
    , m_job_view(manager, daemon_proxy)
    , m_browser_view(manager) {
    set_title("Saddle");
    set_default_size(900, 900);

    m_view_stack = adw::view_stack_new();

    auto* page1 = adw::view_stack_add_titled(
        m_view_stack, &m_browser_view, "browser", "Browse");
    adw_view_stack_page_set_icon_name(page1, "folder-symbolic");

    auto* page2 = adw::view_stack_add_titled(
        m_view_stack, &m_job_view, "jobs", "Jobs");
    adw_view_stack_page_set_icon_name(page2, "emblem-synchronizing-symbolic");

    auto* page3 = adw::view_stack_add_titled(
        m_view_stack, &m_backends_view, "backends", "Backends");
    adw_view_stack_page_set_icon_name(page3, "preferences-system-symbolic");

    auto* switcher = adw::view_switcher(m_view_stack);
    auto* header = adw::header_bar();
    adw::header_bar_set_show_start_title_buttons(header, false);
    adw::header_bar_set_show_end_title_buttons(header, false);
    adw::header_bar_set_title_widget(header, switcher);

    // Toast overlay wraps the view stack
    m_toast_overlay = adw::toast_overlay();
    adw::toast_overlay_set_child(m_toast_overlay, adw::view_stack_widget(m_view_stack));

    auto* toolbar = adw::toolbar_view();
    adw::toolbar_view_add_top_bar(toolbar, header);
    adw::toolbar_view_set_content(toolbar, m_toast_overlay);

    set_child(*toolbar);

    m_browser_view.signal_job_created.connect(
        sigc::mem_fun(m_job_view, &JobView::add_job));
}

void SaddleWindow::show_toast(const char* message) {
    adw::toast_overlay_add_toast(m_toast_overlay, message);
}

} // namespace saddle
