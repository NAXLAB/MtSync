# Saddle — C++ GTK4 Frontend to rclone: Implementation Reference

## Context

Saddle is a C++ GTK4 GUI frontend to rclone. It allows users to configure rclone backends via a
graphical interface, manage sync/copy/move/mount jobs with live progress, browse remote file
systems, and control application settings — replacing the need for CLI-based rclone configuration.

**Key system facts:**
- rclone v1.60.1-DEV installed at `/usr/bin/rclone`, config at `~/.config/rclone/rclone.conf`
- 46 backend providers available, each with structured `Options` arrays (Name, Type, Required, IsPassword, Advanced, Examples, etc.)
- g++ 15.2.0, CMake 4.2.3, pkg-config 2.5.1
- gtkmm-4.0 4.20.0, libadwaita-1 1.9.0, libsoup-3.0 3.6.6, nlohmann-json 3.11.3
- **No `libadwaitamm` C++ bindings exist** in Ubuntu 26.04 — libadwaita C API is used with gtkmm, bridged via `Glib::wrap()`

---

## 1. Dependencies

```bash
sudo apt install \
  libgtkmm-4.0-dev \
  libadwaita-1-dev \
  libsoup-3.0-dev \
  nlohmann-json3-dev
```

- **libsoup-3.0** over libcurl: integrates with GLib main loop, async HTTP callbacks fire on GTK thread automatically
- **nlohmann/json**: header-only, modern C++ API, zero build overhead
- **libadwaita C API directly**: no libadwaitamm available; thin `adw_wrapper.hpp` bridges C API → gtkmm widget tree via `Glib::wrap()`

---

## 2. Project Structure

```
Saddle/
├── CMakeLists.txt
├── .gitignore
├── src/
│   ├── main.cpp                        # Entry point, adw_init(), --daemon dispatch
│   ├── application.hpp/cpp             # SaddleApplication (Gtk::Application subclass)
│   ├── window.hpp/cpp                  # SaddleWindow — AdwToolbarView + AdwViewStack + AdwViewSwitcher
│   ├── daemon.hpp/cpp                  # SaddleDaemon — background process, job scheduler
│   ├── daemon_proxy.hpp/cpp            # GUI-side IPC client wrapper
│   ├── settings.hpp                    # Settings struct + load/save (settings.json)
│   ├── notification.hpp/cpp            # Desktop notifications (notify-send / kdialog)
│   ├── tray.hpp/cpp                    # StatusNotifierItem tray icon (D-Bus SNI + dbusmenu)
│   ├── ipc/
│   │   ├── protocol.hpp                # JSON message types, RequestType enum
│   │   ├── server.hpp/cpp              # Unix socket IPC server (daemon side)
│   │   └── client.hpp/cpp              # Unix socket IPC client (GUI side)
│   ├── rclone/
│   │   ├── rclone_types.hpp            # Data structs: RemoteInfo, ProviderInfo, FileEntry, Job, etc.
│   │   ├── rclone_cli.hpp/cpp          # CLI subprocess interface (Gio::Subprocess)
│   │   ├── rclone_rc.hpp/cpp           # RC HTTP API interface (libsoup-3.0)
│   │   ├── rclone_manager.hpp          # Facade owning CLI + RC (header-only)
│   │   └── cron_utils.hpp              # Header-only cron engine: parser, next-occurrence, description
│   ├── views/
│   │   ├── backends_view.hpp/cpp       # Remote list with AdwNavigationView for drill-down
│   │   ├── backend_edit_view.hpp/cpp   # Dynamic form for create/edit remote
│   │   ├── job_view.hpp/cpp            # Job list with progress, run/stop controls
│   │   ├── job_edit_dialog.hpp/cpp     # Dialog to configure a job (type, src, dst, schedule)
│   │   ├── browser_view.hpp/cpp        # Dual-pane file browser (two BrowserPane widgets)
│   │   ├── settings_view.hpp/cpp       # Application and transfer settings
│   │   └── about_view.hpp/cpp          # Version, license, and copyright
│   └── widgets/
│       ├── adw_wrapper.hpp             # Thin C++ helpers over libadwaita C API
│       ├── browser_pane.hpp/cpp        # Single browser pane (ColumnView, breadcrumbs, nav)
│       ├── file_row.hpp/cpp            # FileObject (Glib::Object subclass) for ListView model
```

---

## 3. Architecture

```
Single executable (saddle) with two modes:

1. GUI Mode (default):
   saddle main() → SaddleApplication → SaddleWindow
   └── RcloneManager (CLI + RC interfaces)
       └── DaemonProxy (IPC client → SaddleDaemon)

2. Daemon Mode (--daemon flag):
   saddle --daemon → SaddleDaemon
   ├── RcloneManager (CLI + RC interfaces)
   ├── TrayIcon (StatusNotifierItem via D-Bus)
   ├── IpcServer (Unix socket at ~/.cache/saddle/socket)
   └── JobScheduler (cron-based scheduling)

**Tray animation system**: `SaddleDaemon` tracks active transfers via `m_running_job_count`
(counter, not a scan). Mount jobs are excluded — they're persistent state, not active transfers.
A `m_job_submitting` guard prevents duplicate counter increments from rapid repeated calls
(e.g. scheduler firing multiple times in the same millisecond). `set_attention()` is only
called when count reaches zero to avoid GNOME AppIndicator resetting its property cache mid-run.
```

**IPC Protocol** (`src/ipc/protocol.hpp`):
- Unix socket communication
- JSON messages with type (`RequestType` enum) + payload
- Request/Response pattern with `request_id` correlation

**Navigation**: `AdwViewStack` + `AdwViewSwitcher` for 5 top-level views (GNOME HIG pattern).
`AdwNavigationView` used only inside `BackendsView` for list → edit push/pop.

**Async model**: Both `Gio::Subprocess::communicate_utf8_async()` and
`soup_session_send_and_read_async()` dispatch on GLib main loop = GTK thread. No manual
threading needed. All async callbacks use `std::expected<T, std::string>` (C++23).

---

## 4. rclone Interaction Layer

### CLI (`RcloneCli`) — for config management
- `run_command()`: spawns `Gio::Subprocess` with `STDOUT_PIPE | STDERR_PIPE`, calls `communicate_utf8_async()`
- `list_remotes()` → `rclone config dump` → parse JSON → `vector<RemoteInfo>`
- `get_providers()` → `rclone config providers` → parse JSON → `vector<ProviderInfo>`
- `config_create()` → `rclone config create <name> <type> k=v... --non-interactive`
- `config_update()` → `rclone config update <name> k=v... --non-interactive`
- `config_delete()` → `rclone config delete <name>`
- `lsjson()` → `rclone lsjson <remote:path>` → `vector<FileEntry>`

### RC API (`RcloneRc`) — for jobs with progress
- `ensure_daemon()`: spawn `rclone rcd --rc-addr localhost:5571 --rc-no-auth` with stdout/stderr redirected to `/dev/null` so rclone's log output doesn't clutter the console, verify with `core/version`
- `sync_async()` / `copy_async()` / `move_async()`: POST `/sync/{sync,copy,move}` with `_async: true` → jobid
- `mount_async()`: POST `mount/mount`; `unmount()`: POST `mount/unmount`
- `get_stats()`: POST `/core/stats` → `SyncStats` (bytes, speed, ETA, transfers)
- `job_status()`: POST `/job/status` with `{jobid}` → `JobStatus` (finished, success, error)
- `job_stop()`: POST `/job/stop` with `{jobid}`

---

## 5. Screen Details

### Tab 1: Browse
- **BrowserView**: Two independent `BrowserPane` widgets in a `Gtk::Paned` split
- Each pane: remote dropdown, breadcrumb nav, back history, `Gtk::ColumnView` (Name/Size/Modified), hidden-files toggle, status bar (file/folder counts + total size)
- Active pane tracked with accent stripe; multi-selection supported
- Copy/Move/Sync/Mount action bar buttons; Copy and Move pass selected files as `--include` filters
- Swap button physically exchanges the remote and path between panes (navigation history swapped too)
- New Folder popover; Delete via `AdwAlertDialog` confirmation

### Tab 2: Jobs
- **JobView**: List of persisted jobs (`~/.config/saddle/jobs.json`). Jobs are loaded from the daemon on initial map; subsequent updates arrive via IPC broadcast messages (no polling).
- Each job row shows: type icon (symbolic), `SourceDir → DestDir` display name derived from the last path component of source and destination, source/destination full paths, progress bar (visible while running), footer with UUID left and last status right
- Activity log panel at the bottom of the tab (auto-scrolling, persisted to `~/.local/state/saddle/saddle.log`)
- Run/Stop per job; live progress (bytes, speed, ETA) via polling `core/stats` + `job/status`
- **JobEditDialog**: source, destination, job type, file filter patterns (space-separated `--include` patterns), cron schedule (five fields + human-readable summary), "Mount at Start-up" option for mount jobs; bi-directional sync toggle for Sync jobs

### Tab 3: Remotes
- **BackendsView**: `AdwPreferencesGroup` with `AdwActionRow` per remote; "Add Remote" pushes BackendEditView via `AdwNavigationView`
- **BackendEditView**: Provider dropdown → dynamic form from provider `Options`:
  - `bool` → `AdwSwitchRow`
  - `exclusive + examples` → `AdwComboRow`
  - `is_password` → `AdwPasswordEntryRow`
  - default → `AdwEntryRow`
  - `advanced=true` fields in `AdwExpanderRow`
  - `required=true` fields marked with asterisk
- Save calls `rclone config create` or `update --non-interactive`

### Tab 4: Settings
- **SettingsView**: `AdwClamp` (max 600px) + scrollable `AdwPreferencesGroup` sections
- **General**: Start daemon on login (writes `~/.config/autostart/saddle-daemon.desktop`), Start minimized to tray, Shutdown daemon when closing
- **Transfers**: Default bandwidth limit, Verify checksums, Parallel transfers count
- **rclone**: Binary path override (empty = PATH lookup; restart required)
- All settings persist immediately to `~/.config/saddle/settings.json`

### Tab 5: About
- **AboutView**: `AdwStatusPage` (icon, app name, description) + `AdwPreferencesGroup` rows for Version, License, Copyright

---

## 6. CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.25)
project(Saddle VERSION 0.3.27 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0)
find_package(nlohmann_json 3.2 REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(saddle ${SOURCES})
target_include_directories(saddle PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(saddle PRIVATE
    PkgConfig::GTKMM PkgConfig::ADWAITA PkgConfig::SOUP nlohmann_json::nlohmann_json)
install(TARGETS saddle DESTINATION bin)
```

C++23 for `std::expected`, `std::format`, ranges. `CONFIGURE_DEPENDS` auto-detects new source files.

---

## 7. Configuration Files

| Path | Purpose |
|------|---------|
| `~/.config/rclone/rclone.conf` | rclone backend configuration |
| `~/.config/saddle/jobs.json` | Saddle job definitions |
| `~/.config/saddle/settings.json` | Saddle application settings |
| `~/.config/autostart/saddle-daemon.desktop` | Autostart entry (written when enabled in Settings) |
| `~/.cache/saddle/socket` | IPC socket (daemon ↔ GUI) |
