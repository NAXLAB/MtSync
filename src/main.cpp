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

#include "daemon.hpp"
#include "application.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>

static std::sig_atomic_t g_running = true;

static void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    bool daemon_mode = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--daemon" || arg == "-d") {
            daemon_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: saddle [OPTIONS]\n"
                      << "  --daemon, -d    Run as background daemon\n"
                      << "  --help, -h      Show this help\n";
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (daemon_mode) {
        saddle::SaddleDaemon daemon;
        daemon.run();
        return 0;
    }

    // Run as GUI application
    auto app = saddle::SaddleApplication::create();
    return app->run(argc, argv);
}
