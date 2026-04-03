# Saddle

A C++ GTK4/libadwaita frontend to [rclone](https://rclone.org/). Configure backends, browse remote
file systems, and manage sync/copy/move jobs with live progress — all from a native GNOME desktop
application backed by a persistent daemon.

## Features

- **Backend configuration** — Create, edit, and delete rclone remotes via dynamically generated
  forms derived from rclone's own provider option metadata
- **Dual-pane file browser** — Two independent browser panes with column view, breadcrumb
  navigation, back history, MIME-type icons, sortable columns, and a status bar showing file/folder
  counts and total size; hidden files toggled per-pane
- **Jobs system** — Define Sync, Copy, Move, and Mount jobs; run them on demand or on a cron
  schedule; real-time progress with transfer stats (files, speed, ETA); jobs persist across GUI
  restarts; Sync jobs support bi-directional sync mode (rclone bisync); Mount jobs show active
  state and can be stopped/unmounted; checksum verification disabled by default
- **Background daemon** — `saddle --daemon` keeps jobs running when the GUI is closed; GUI
  reconnects automatically on next launch; daemon starts rclone RC on startup
- **System tray icon** — StatusNotifierItem tray icon with Open/Quit menu; Open re-launches the
  GUI if it is not running
- **Desktop notifications** — Notified on job completion via `notify-send` or `kdialog`
- **Settings** — General app settings (autostart, tray behaviour), transfer defaults (bandwidth
  limit, checksums, parallel transfers), rclone binary path override; persisted to
  `~/.config/saddle/settings.json`

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
./build/saddle          # launch GUI (starts daemon automatically)
./build/saddle --daemon # run as background daemon only
```

Saddle expects `rclone` to be available on `PATH`.

## Architecture

```
SaddleApplication (Gtk::Application)
 └── SaddleWindow (Gtk::ApplicationWindow)
      ├── AdwHeaderBar + AdwViewSwitcher
      └── AdwViewStack
           ├── BrowserView   — dual-pane file browser (two BrowserPane widgets)
           ├── JobView       — job list with progress, run/stop controls
           ├── BackendsView  — remote list with drill-down edit form
           ├── SettingsView  — application and transfer settings
           └── AboutView     — version, license, and copyright

SaddleDaemon (background process, saddle --daemon)
 ├── IpcServer             — Unix socket at ~/.cache/saddle/socket
 ├── TrayIcon              — StatusNotifierItem + dbusmenu via D-Bus
 └── RcloneManager
      ├── RcloneCli         — Gio::Subprocess for one-shot commands
      └── RcloneRc          — libsoup HTTP to rclone RC daemon
```

**GUI ↔ Daemon communication:** The GUI connects to the daemon via a Unix socket IPC. The GUI
starts the daemon automatically if it is not already running. Jobs continue executing in the daemon
when the GUI window is closed.

**D-Bus services:** `com.saddle.Daemon` (daemon) exposes `ShowWindow` and `Quit` methods.

libadwaita is used via its C API bridged to gtkmm with `Glib::wrap()`, as no C++ bindings
(libadwaitamm) are available.

All async I/O dispatches on the GLib main loop — no manual threading.

## Configuration

| Path | Purpose |
|------|---------|
| `~/.config/rclone/rclone.conf` | rclone backend configuration |
| `~/.config/saddle/jobs.json` | Saddle job definitions |
| `~/.config/saddle/settings.json` | Saddle application settings |
| `~/.cache/saddle/socket` | IPC socket (daemon ↔ GUI) |

## License

GNU General Public License v2.0. See [LICENSE](LICENSE) for details.
