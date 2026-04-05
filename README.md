# Saddle

A C++ GTK4/libadwaita frontend to [rclone](https://rclone.org/). Configure backends, browse remote
file systems, and manage sync/copy/move/mount jobs with live progress — all from a native GNOME
desktop application backed by a persistent daemon.

## Features

- **Backend configuration** — Create, edit, and delete rclone remotes via dynamically generated
  forms derived from rclone's own provider option metadata; each remote row shows a type-appropriate
  provider icon (Google Drive, Dropbox, Backblaze B2, MEGA, Box, Google Cloud Storage, Proton Drive,
  and more) with automatic light/dark variants; unknown providers fall back to symbolic icons
- **Dual-pane file browser** — Two independent browser panes with column view, breadcrumb
  navigation, back history, MIME-type icons, sortable columns, and a status bar showing file/folder
  counts and total size; hidden files toggled per-pane; compact navigation bar with provider icon
  in the remote dropdown; swap button physically exchanges remote and path between panes
- **Jobs system** — Define Sync, Copy, Move, and Mount jobs; run them on demand or on a cron
  schedule; real-time progress with transfer stats (files, speed, ETA); jobs persist across GUI
  restarts; each job row shows a type icon, a `SourceDir → DestDir` display name, and a footer
  with the job UUID and last status; Sync jobs support bi-directional sync mode (rclone bisync),
  copy empty directories, and file include-pattern filters; Mount jobs show active state, can be
  stopped/unmounted, and expose only mount-relevant options (irrelevant fields such as Dry Run,
  Enable Checksum, Bandwidth Limit, and File Filters are hidden); checksum verification disabled
  by default; Save button to store job without running; scheduled jobs skip execution if the previous
  instance is still running; failed jobs are automatically retried up to a configurable count before
  being marked as failed; activity log panel displays newest entries first
- **Background daemon** — `saddle --daemon` keeps jobs running when the GUI is closed; GUI
  reconnects automatically on next launch; daemon starts rclone RC on startup
- **System tray icon** — StatusNotifierItem tray icon with Open/Quit menu; Open re-launches the
  GUI if it is not running
- **Desktop notifications** — Notified on job completion via `notify-send` or `kdialog`
- **Settings** — General app settings (autostart, tray behaviour), transfer defaults (bandwidth
  limit, checksums, parallel transfers, retries on failure), rclone binary path override; persisted
  to `~/.config/saddle/settings.json`

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

## Installing

```bash
cmake --install build --prefix ~/.local
gtk-update-icon-cache ~/.local/share/icons/hicolor
update-desktop-database ~/.local/share/applications
```

Installing places the binary in `~/.local/bin`, registers the application icon with the hicolor
theme, and installs the `.desktop` file so the icon appears in the launcher and taskbar.

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
           ├── JobView       — job list with progress, run/stop controls, activity log
           ├── BackendsView  — remote list with provider icons and drill-down edit form
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
