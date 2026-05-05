<p align="center">
  <img src="https://raw.githubusercontent.com/gavindi/Mt. Sync/main/assets/icons/application/Mt. Sync%20App%20Icon%201.svg" alt="Mt. Sync Logo" width="128">
</p>

# Mt. Sync

Mount or sync network storage in comfort

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/P5P21M7MBS)

A C++ GTK4/libadwaita frontend to [rclone](https://rclone.org/). Configure backends, browse remote
file systems, and manage sync/copy/move/mount jobs with live progress — all from a native GNOME
desktop application backed by a persistent daemon.

## Features

- **Backend configuration** — Create, edit, and delete rclone remotes via dynamically generated
  forms derived from rclone's own provider option metadata; each remote row shows a type-appropriate
  provider icon (Google Drive, Dropbox, Backblaze B2, MEGA, Box, Google Cloud Storage, Proton Drive,
  and more) with automatic light/dark variants; unknown providers fall back to symbolic icons;
  each remote also displays its used/free capacity (total, used, free) queried asynchronously via
  the rclone RC `operations/about` endpoint
- **Dual-pane file browser** — Two independent browser panes with column view, breadcrumb
  navigation, back history, MIME-type icons, sortable columns, and a status bar showing file/folder
  counts and total size; hidden files toggled per-pane; compact navigation bar with provider icon
  in the remote dropdown; swap button physically exchanges remote and path between panes; **Compare**
  button runs `rclone check` between the two panes and displays results in a paginated 7-column
  dialog grouped by subdirectory — filename, size, and modified date shown only on the side where
  the file exists; status glyphs `→` (source only) `←` (dest only) `≠` (differs) `=` (identical);
  all seven columns support interactive sorting and files stay grouped with their directory header
  when any column sort is active; five filter toggles (`←` `→` `=` `≠` `!`) in the action bar
  show rows by status (all on by default; deactivate to hide that category) — directory headers
  with no visible children are suppressed automatically
- **Jobs system** — Define Sync, Copy, Move, and Mount jobs; run them on demand or on a cron
  schedule; real-time progress with transfer stats (files, speed, ETA); jobs persist across GUI
  restarts; each job row shows a type icon, a `SourceDir → DestDir` display name, and a footer
  with the job UUID and last status; the add/edit dialog is split into three tabs — **Job**
  (type, source, destination, filters, dry run, bi-directional sync, checksum, mount options),
  **Schedule** (enable toggle; preset dropdown — Every minute, Hourly, Daily, Weekly, Monthly,
  Custom; text entries for Minute, Hour, and Day of Month each with a clear-to-`*` button;
  Day of Week checkbox row Sun–Sat; Month checkbox grid Jan–Dec; live preview panel showing the
  raw cron expression, a human-readable description, a calendar with run days marked for the
  current month, and a scrollable list of the next 15 upcoming execution times), and **Advanced**
  (bandwidth limit, parallel transfers, retries on failure, extra rclone flags appended to the RC
  call at run time — supports `--flag value`, `--flag=value`, and boolean `--flag` forms); Sync jobs support bi-directional sync
  mode (rclone bisync), copy empty directories, and file include-pattern filters; Mount jobs show
  active state and can be stopped/unmounted; checksum verification disabled by default; Save button
  to store job without running; scheduled jobs skip execution if the previous instance is still
  running; failed jobs are automatically retried up to a configurable count with exponential backoff
  (2 s, 4 s, 8 s, … capped at 60 s) before being marked as failed; activity log panel shows the last 100 entries in a structured column view (Time, State,
  Job ID, Type, Contents) with colour-coded state labels, newest first
- **Background daemon** — `mtsync --daemon` keeps jobs running when the GUI is closed; GUI
  reconnects automatically on next launch; daemon starts rclone RC on startup; periodic mount
  liveness checks detect and mark stale FUSE mounts; HTTP request timeouts and poll-rate
  guards prevent resource exhaustion during network outages
- **System tray icon** — StatusNotifierItem tray icon with Open/Quit menu; Open re-launches the
  GUI if it is not running; animated spinner shown while any job is active; custom Mt. Sync-branded
  idle icon rendered via Cairo and bundled as a GLib resource
- **Desktop notifications** — Optional notifications via `notify-send` or `kdialog`; independently
  configurable for job start, successful completion, and completion with errors/warnings
- **Settings** — Startup & shutdown behaviour (autostart, tray), notification toggles, transfer
  defaults (bandwidth limit, checksums, parallel transfers, retries on failure), rclone binary path
  override, global rclone flags applied to every job at execution time (lowest priority — per-job
  extra flags and explicit job settings take precedence); persisted to
  `~/.config/mtsync/settings.json`

## Dependencies

Requires a C++23 compiler, CMake 3.25+, and the following libraries. rclone is bundled
automatically at build time (downloaded from the official rclone release page and verified by
SHA256); a system rclone installation is not required for packaged builds. Source builds fall back
to `rclone` on `PATH`.

```bash
sudo apt install \
  libgtkmm-4.0-dev \
  libadwaita-1-dev \
  libsoup-3.0-dev \
  nlohmann-json3-dev
```

## Building

Create the build directory, then configure and build:

```bash
mkdir build
cmake \
  -DCMAKE_BUILD_TYPE:STRING=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
  -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++ \
  --no-warn-unused-cli \
  -S . -B build -G "Unix Makefiles"
cmake --build build
```

`CMAKE_EXPORT_COMPILE_COMMANDS` writes a `compile_commands.json` into the build directory, which
IDEs and tools such as clangd use for accurate code intelligence. Use `Debug` for development;
substitute `Release` for an optimised binary.

## Installing

Pre-built packages are produced by CI for each push to the `build` branch. Package filenames follow
the `name_version_distro_arch.ext` convention (e.g. `mtsync_0.8.1_ubuntu24.04_x86_64.deb`).

### DEB (Ubuntu)

```bash
sudo apt install ./mtsync_*_ubuntu*_x86_64.deb
```

Packages are built for Ubuntu 24.04, 25.10, and 26.04. Install the one that matches your release.
rclone is bundled — no separate rclone installation needed.

### RPM (Fedora)

```bash
sudo dnf install ./mtsync_*_fedora*_x86_64.rpm
```

rclone is bundled — no separate rclone installation needed.

### AppImage

```bash
chmod +x mtsync_*_x86_64.AppImage
./mtsync_*_x86_64.AppImage
```

No installation required — the AppImage is self-contained and runs directly. rclone is bundled.

### From source

```bash
cmake --install build --prefix ~/.local
gtk-update-icon-cache ~/.local/share/icons/hicolor
update-desktop-database ~/.local/share/applications
```

Installing places the binary in `~/.local/bin`, the bundled rclone in `~/.local/lib/mtsync/rclone`,
the application icon (256×256 PNG and scalable SVG) with the hicolor theme, and the `.desktop` file
so the icon appears in the launcher and taskbar. For source builds, `rclone` must be available on
`PATH` (the bundled binary is only used once installed).

### Flatpak

Build and install the Flatpak bundle from the packaging manifest:

```bash
sudo apt install flatpak flatpak-builder
flatpak install --user flathub org.gnome.Platform//48 org.gnome.Sdk//48
flatpak-builder --user --install --force-clean build-flatpak packaging/com.mtsync.MtSync.yml
```

Launch with `flatpak run com.mtsync.MtSync`. rclone is bundled inside the Flatpak — no host
installation needed. The manifest grants `--filesystem=home`, so enabling **Start daemon on
login** in Settings writes the autostart entry directly to the host's `~/.config/autostart/`
with the correct `flatpak run --command=mtsync com.mtsync.MtSync --daemon` exec line.

### Snap

Build and sideload the `.snap`:

```bash
sudo apt install snapcraft
snapcraft pack
sudo snap install --dangerous mtsync_*.snap
```

The Snap uses `strict` confinement. To enable **Start daemon on login**, connect the privileged
`personal-files` plug once (sideloaded snaps do not auto-connect it):

```bash
sudo snap connect mtsync:dot-config-autostart :personal-files
```

Without that connection the autostart toggle fails silently, because the `home` plug cannot write
to hidden directories such as `~/.config/autostart/`. rclone is bundled inside the snap. The
**Local** remote in the Browse tab is resolved via `$SNAP_REAL_HOME` so your real home directory
is listed rather than the empty snap-private data directory.

## Running

```bash
./build/mtsync          # launch GUI (starts daemon automatically)
./build/mtsync --daemon # run as background daemon only
```

Packaged builds (DEB, RPM, AppImage, Flatpak, Snap) include rclone — no separate installation
needed. Source builds require `rclone` on `PATH`.

## Architecture

```
MtSyncApplication (Gtk::Application)
 └── MtSyncWindow (Gtk::ApplicationWindow)
      ├── AdwHeaderBar + AdwViewSwitcher
      └── AdwViewStack
           ├── BrowserView   — dual-pane file browser (two BrowserPane widgets)
           ├── JobView       — job list with progress, run/stop controls, activity log
           ├── BackendsView  — remote list with provider icons and drill-down edit form
           ├── SettingsView  — application and transfer settings
           └── AboutView     — version, license, and copyright

MtSyncDaemon (background process, mtsync --daemon)
 ├── IpcServer             — Unix socket at ~/.cache/mtsync/socket
 ├── TrayIcon              — StatusNotifierItem + dbusmenu via D-Bus
 └── RcloneManager
      ├── RcloneCli         — Gio::Subprocess for one-shot commands
      └── RcloneRc          — libsoup HTTP to rclone RC daemon
```

**GUI ↔ Daemon communication:** The GUI connects to the daemon via a Unix socket IPC. The GUI
starts the daemon automatically if it is not already running. Jobs continue executing in the daemon
when the GUI window is closed.

**D-Bus services:** `com.mtsync.Daemon` (daemon) exposes `ShowWindow` and `Quit` methods.

libadwaita is used via its C API bridged to gtkmm with `Glib::wrap()`, as no C++ bindings
(libadwaitamm) are available.

All async I/O dispatches on the GLib main loop — no manual threading.

## Configuration

| Path | Purpose |
|------|---------|
| `~/.config/rclone/rclone.conf` | rclone backend configuration |
| `~/.config/mtsync/jobs.json` | Mt. Sync job definitions |
| `~/.config/mtsync/settings.json` | Mt. Sync application settings |
| `~/.cache/mtsync/socket` | IPC socket (daemon ↔ GUI) |

## License

GNU General Public License v2.0. See [LICENSE](LICENSE) for details.
