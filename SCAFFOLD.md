# Mt. Sync — C++ GTK4 Frontend to rclone: Implementation Reference

## Context

Mt. Sync is a C++ GTK4 GUI frontend to rclone. It allows users to configure rclone backends via a
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
Mt. Sync/
├── CMakeLists.txt
├── .gitignore
├── src/
│   ├── main.cpp                        # Entry point, adw_init(), --daemon dispatch
│   ├── application.hpp/cpp             # MtSyncApplication (Gtk::Application subclass)
│   ├── window.hpp/cpp                  # MtSyncWindow — AdwToolbarView + AdwViewStack + AdwViewSwitcher
│   ├── daemon.hpp/cpp                  # MtSyncDaemon — background process, job scheduler
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
│   │   ├── rclone_path.hpp             # Header-only find_rclone_binary(): bundled path → PATH fallback
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
Single executable (mtsync) with two modes:

1. GUI Mode (default):
   mtsync main() → MtSyncApplication → MtSyncWindow
   └── RcloneManager (CLI + RC interfaces)
       └── DaemonProxy (IPC client → MtSyncDaemon)

2. Daemon Mode (--daemon flag):
   mtsync --daemon → MtSyncDaemon
   ├── RcloneManager (CLI + RC interfaces)
   ├── TrayIcon (StatusNotifierItem via D-Bus)
   ├── IpcServer (Unix socket at ~/.cache/mtsync/socket)
   └── JobScheduler (cron-based scheduling)

**Tray animation system**: `MtSyncDaemon` tracks active transfers via `m_running_job_count`
(counter, not a scan). Mount jobs are excluded — they're persistent state, not active transfers.
A `m_job_submitting` guard prevents duplicate counter increments from rapid repeated calls
(e.g. scheduler firing multiple times in the same millisecond). `set_attention()` is only
called when count reaches zero to avoid GNOME AppIndicator resetting its property cache mid-run.

**Idle tray icon**: The system systray idle icon (`network-server-symbolic`) is replaced with
a custom Mt. Sync-branded icon loaded from the GLib resource `/io/github/mtsync/icons/idle.png`.
The PNG is loaded via `cairo_image_surface_create_from_png_stream()` (no gdk-pixbuf dependency),
scaled to 22×22 to match animation frame dimensions, and converted to ARGB32 pixel data.
When animation stops, `stop_animation()` emits both the standard D-Bus `PropertiesChanged`
signal (with the new `IconPixmap` value) and the SNI `NewIcon` signal to force tray
implementations to refresh. This solved the issue where some trays cached the last animation
frame and did not update on `NewIcon` alone.
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
The `SoupSession` has a 15-second timeout to prevent unbounded request accumulation on
network hangs.

**Fault tolerance**: Job retries use exponential backoff (2 s, 4 s, … capped at 60 s) via
`m_retry_timers`. The 500 ms job-status poll skips a tick when the previous HTTP request is
still in-flight (`m_poll_in_flight` guard). A 60-second `m_mount_health_timer` polls
`list_mounts` via rclone RC and marks stale FUSE mounts inactive. All parallel vectors
(`m_poll_timers`, `m_sched_timers`, `m_retry_timers`, `m_job_ids`, `m_job_submitting`,
`m_poll_in_flight`, `m_retry_counts`, `m_last_stats`) are erased in lockstep on job deletion.

---

## 4. rclone Interaction Layer

### Binary discovery (`rclone_path.hpp`)
`find_rclone_binary()` resolves the rclone executable in priority order:
1. **Bundled path**: reads `/proc/self/exe`, walks two `parent_path()` calls to derive the install
   prefix, appends `lib/mtsync/rclone`. Present in installed DEB/RPM/AppImage packages.
2. **PATH fallback**: returns `"rclone"` — caller passes to `Glib::find_program_in_path()`.
   Used by dev builds (uninstalled), Flatpak, and Snap (all put rclone on PATH themselves).

The compile-time macro `MTSYNC_BUNDLED_RCLONE_RELPATH` (set by CMake when
`MTSYNC_BUNDLE_RCLONE=ON`) guards the bundled-path branch; without it the function is a
no-op returning `"rclone"`. Both `RcloneCli` and `RcloneRc::spawn_daemon` call
`find_rclone_binary()` before their existing `Glib::find_program_in_path()` logic.

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
- **JobView**: List of persisted jobs (`~/.config/mtsync/jobs.json`). Jobs are loaded from the daemon on initial map; subsequent updates arrive via IPC broadcast messages (no polling).
- Each job row shows: type icon (symbolic), `SourceDir → DestDir` display name derived from the last path component of source and destination, source/destination full paths, progress bar (visible while running), footer with UUID left and last status right
- Activity log panel at the bottom of the tab (auto-scrolling, persisted to `~/.local/state/mtsync/mtsync.log`)
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
- **General**: Start daemon on login (writes `~/.config/autostart/mtsync-daemon.desktop`), Start minimized to tray, Shutdown daemon when closing
- **Transfers**: Default bandwidth limit, Verify checksums, Parallel transfers count
- **rclone**: Binary path override (empty = PATH lookup; restart required), `global_rclone_flags` (space-separated CLI flags injected into every job's RC `_config` at execution time; per-job settings take precedence)
- All settings persist immediately to `~/.config/mtsync/settings.json`

### Tab 5: About
- **AboutView**: `AdwStatusPage` (icon, app name, description) + `AdwPreferencesGroup` rows for Version, License, Copyright

---

## 6. CMake Configuration

```cmake
# … standard project/find_package boilerplate …

add_executable(mtsync ${SOURCES})
target_link_libraries(mtsync PRIVATE …)

# ── Bundled rclone ──────────────────────────────────────────────────────────
option(MTSYNC_BUNDLE_RCLONE "Download and bundle rclone (disable for distro packaging)" ON)
# When ON: downloads rclone zip at configure time (idempotent — skipped if already cached),
# installs binary to ${CMAKE_INSTALL_PREFIX}/lib/mtsync/rclone, and defines
# MTSYNC_BUNDLED_RCLONE_RELPATH="lib/mtsync/rclone" for find_rclone_binary().
# When OFF: restores rclone as a DEB/RPM runtime dependency (for distro packaging).
# Flatpak and Snap pass -DMTSYNC_BUNDLE_RCLONE=OFF and bundle rclone via their own mechanisms.

install(TARGETS mtsync DESTINATION bin)
install(PROGRAMS ${RCLONE_BINARY} DESTINATION lib/mtsync)  # only when MTSYNC_BUNDLE_RCLONE=ON
```

C++23 for `std::expected`, `std::format`, ranges. `CONFIGURE_DEPENDS` auto-detects new source files.

---

## 7. Configuration Files

| Path | Purpose |
|------|---------|
| `~/.config/rclone/rclone.conf` | rclone backend configuration |
| `~/.config/mtsync/jobs.json` | Mt. Sync job definitions |
| `~/.config/mtsync/settings.json` | Mt. Sync application settings |
| `~/.config/autostart/mtsync-daemon.desktop` | Autostart entry (written when enabled in Settings) |
| `~/.cache/mtsync/socket` | IPC socket (daemon ↔ GUI) |
