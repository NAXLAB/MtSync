# Saddle

A C++ GTK4/libadwaita frontend to [rclone](https://rclone.org/). Configure backends, browse remote file systems, and manage sync operations with live progress — all from a native GNOME desktop application.

## Features

- **Backend configuration** — Create, edit, and delete rclone remotes via dynamically generated forms (derived from rclone's own provider option metadata)
- **File browser** — Browse remote file systems with a column view, directory navigation, and back history
- **Sync management** — Define source/destination sync pairs, run them with a live progress bar (bytes, speed, ETA), and stop jobs in-flight

## Dependencies

Requires rclone, a C++23 compiler, CMake 3.25+, and the following libraries:

```bash
sudo apt install \
  libgtkmm-4.0-dev \
  libadwaita-1-dev \
  libsoup-3.0-dev \
  nlohmann-json3-dev
```

## Building

```bash
cmake -B build
cmake --build build
```

## Running

```bash
./build/saddle
```

Saddle expects `rclone` to be available on `PATH`. Sync operations launch an rclone RC daemon automatically.

## Architecture

```
SaddleApplication (Gtk::Application)
 └── SaddleWindow (Gtk::ApplicationWindow)
      ├── AdwHeaderBar + AdwViewSwitcher
      └── AdwViewStack
           ├── BackendsView   — remote list with drill-down edit form
           ├── SyncView       — sync pair list with progress
           └── BrowserView    — column-based file browser
```

**RcloneManager** provides two interfaces to rclone:
- **RcloneCli** — `Gio::Subprocess` for one-shot commands (config dump, providers, lsjson, config create/update/delete)
- **RcloneRc** — libsoup HTTP to the rclone RC daemon for long-running sync operations with progress polling

All async I/O dispatches on the GLib main loop — no manual threading.

libadwaita is used via its C API bridged to gtkmm with `Glib::wrap()`, as no C++ bindings (libadwaitamm) are available.

## Configuration

- rclone config: `~/.config/rclone/rclone.conf`
- Sync pairs: `~/.config/saddle/sync_pairs.json`

## License

This project is licensed under the GNU General Public License v2.0. See [LICENSE](LICENSE) for details.
