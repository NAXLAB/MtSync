# Changelog

## 0.2.2 - Removed redundant scheduler from GUI
- Removed duplicate job scheduling from JobView since daemon handles all scheduling
- Removed sched_timer member and schedule_job() method from job_view.hpp/cpp
- Sync jobs now run with `--create-empty-src-dirs` by default

## 0.2.1 - Code clean-up of dead code as operations have shifted from GUI to Daemon
- Removed copy_files / move_files from rclone_cli.hpp, rclone_cli.cpp
- Removed set_running_jobs / g_running_jobs from tray.hpp, tray.cpp
- Removed ErrorCallback typedef from daemon_proxy.hpp
- Removed m_args member + <vector> include from notification.hpp
- Removed m_old_config_path + migration from job_view.hpp, job_view.cpp
- Removed redundant inner bounds check from daemon.cpp:283
- Removed spurious 500ms connectivity ping from rclone_rc.cpp

## 0.2.0 — Real-time Job Progress & Mount Improvements

- Real-time progress updates in the Jobs tab showing transfer stats (files transferred, speed, ETA)
- Mount jobs now properly show stop button when active
- "Ignore Checksum" enabled by default; "Enable Checksum" option to disable
- Mount status persists via `active` field and is restored when switching to Jobs tab
- Jobs tab periodically refreshes job list from daemon to keep UI in sync

## 0.1.7 — Daemon RC Startup

- Daemon now starts `rclone rcd` immediately upon startup
- Jobs no longer need to ensure the daemon on-demand; it's always running
- GUI only configures the daemon and performs no backend work

## 0.1.6 — Bi-directional Sync

- **Bi-directional sync** toggle added to the job dialog when type is **Sync**
- When enabled, the job uses the rclone RC `bisync/bisync` endpoint instead of `sync/sync`
- Persisted as `bisync` field in `jobs.json`

## 0.1.5 — About Tab

- New **About** tab to the right of Settings
- Shows app name, description, version, license, and copyright

## 0.1.4 — Settings Tab

- New **Settings** tab to the right of Backends
- **General** group: Start daemon on login (writes/removes `~/.config/autostart/saddle-daemon.desktop`), Start minimized to tray, Shutdown daemon when closing application
- **Transfers** group: Default bandwidth limit, Verify checksums, Parallel transfers count
- **rclone** group: Override rclone binary path (leave empty for PATH lookup; restart required)
- Settings persist to `~/.config/saddle/settings.json`
- "Close to tray" takes effect immediately without restart

## 0.1.3 — Mount Job Type

- **Mount** button added to the browser action bar (after Sync)
- Mount is a first-class job type: persists in `jobs.json`, supports cron scheduling
- "Mount at Start-up" option in the job dialog — daemon automatically mounts on startup when enabled
- Mount jobs use the rclone RC `mount/mount` endpoint; stop button calls `mount/unmount`
- Job list shows `[MOUNT]` type badge
- Running a mount job shows "Mounted" status; stopping shows "Unmounted"

## 0.1.2 — File Browser Status Bar

- Footer bar in each browser pane now shows file/folder counts and total file size on the left (e.g. "12 files, 3 folders, Total: 45.2 MB")
- Counts reflect the current hidden-files filter state
- Status clears while navigating and on empty directories

## 0.1.1 — Show Hidden Files

- Each browser pane now has a "Show hidden files" checkbox in the bottom-right corner, off by default
- Files and directories with names starting with `.` are hidden unless the checkbox is ticked
- Each pane's setting is independent

## 0.1.0 — System Tray Icon & Bug Fixes

- Fixed: tray **Open** no longer silently drops the message when the GUI is closed — daemon now spawns a fresh GUI process if no clients are connected

## 0.0.11 — System Tray Icon

- System tray icon implemented via StatusNotifierItem (SNI/`org.kde.StatusNotifierItem`) protocol — no additional dependencies, pure GIO D-Bus
- Icon uses `network-server-symbolic` (GNOME monochrome network drive icon)
- Right-click context menu via `com.canonical.dbusmenu` with **Open** and **Quit** items
- Open broadcasts `show_window` over the IPC socket; connected GUI clients call `present()` on the window (if no GUI is running, one is spawned)
- Quit stops the daemon and rclone RC daemon
- SNI registration deferred to `on_name_acquired` to ensure the bus name is owned before the watcher contacts us
- `set_attention()` now emits `NewStatus` signal to toggle `NeedsAttention` state

## 0.0.10 — Code Quality & IPC Fixes

- DaemonProxy methods now properly correlate request/response via `request_id` instead of returning stub data
- Daemon forwards `request_id` from incoming requests to outgoing responses
- DaemonProxy uses `ipc::RequestType` enum instead of raw strings
- Removed dead code (`allocate_request_id`, unused `m_pending_callbacks` population)
- Fixed scheduling timer overflow: `MAX_DELAY_MS` was 576 days (`24*24*3600*1000`), corrected to 24 hours
- Window allocation uses `Gtk::make_managed` instead of raw `new`
- Hardcoded `/usr/bin/rclone` replaced with PATH lookup via `Glib::find_program_in_path`
- Silent `catch (...) {}` blocks replaced with `catch (const std::exception&)` + `g_warning()`
- IPC socket directory is now created if missing before client connection
- Added missing `<nlohmann/json.hpp>` include in `daemon_proxy.cpp`

## 0.0.9 — GUI ↔ Daemon Integration

- GUI automatically starts daemon if not running
- JobView connects to daemon via IPC for real-time updates
- Jobs continue running in daemon when GUI is closed
- Daemon broadcasts job status changes to all connected GUI clients
- Fixed IPC server/client communication (messages now properly sent/received)
- Desktop notifications on job completion (when notify-send or kdialog is available)
- D-Bus service (com.saddle.Saddle) for external communication
- Daemon exposes ShowWindow and Quit methods via D-Bus

## 0.0.8 — Background Daemon Mode

- `saddle --daemon` runs a background daemon process
- Daemon manages job scheduling and rclone RC daemon lifecycle
- Jobs continue running even when GUI is closed
- IPC server (Unix socket at `~/.cache/saddle/socket`) for GUI ↔ Daemon communication
- Note: System tray icon is stubbed (GTK4 lacks native tray support; full tray integration planned)

## 0.0.7 — Selected Files in Copy/Move/Sync

- Sync button added to the browser action bar alongside Copy and Move
- Copy, Move, and Sync buttons now display icons
- Copy and Move dialogs now include selected files as `--include` filter patterns
- If no files are selected, the entire current directory is used (existing behavior)

## 0.0.6 — Simplified Transfer Actions

- Single Copy and Move buttons in dual-pane browser (instead of four directional buttons)
- Swap button to reverse source/destination between panes
- Role labels displayed in each pane header ("← Source" / "Destination →")

## 0.0.5 — Cron Scheduling

- Recurring cron-style scheduling in `JobEditDialog`: enable switch + five individual fields (Minute, Hour, Day, Month, Weekday) with live human-readable summary
- Action button reads "Run Now" when scheduling is disabled, "Schedule" when enabled
- Jobs re-arm automatically after each run for the next cron occurrence
- `cron_utils.hpp`: header-only cron engine — field parser, next-occurrence calculator, human-readable description

## 0.0.4 — Dual-Pane File Manager & Jobs System

The file browser has been redesigned as a dual-pane manager (rcloneview-style), and the Sync tab has been generalised into a Jobs system supporting Sync, Copy, and Move operations with optional scheduling.

- Two independent browser panes in a resizable split view (`Gtk::Paned`)
- Each pane has its own remote dropdown, breadcrumb navigation, and file list
- Multi-selection support in both panes
- Active pane tracked automatically (accent stripe indicator)
- New Folder popover with Enter-key support
- New rclone CLI operations: `copy`, `move`, `delete` (with `--include` file filters), `mkdir`
- Sync tab renamed to Jobs; supports three job types: Sync, Copy, Move
- `Job` data model replaces `SyncPair`; stored in `~/.config/saddle/jobs.json`
- Automatic migration from `sync_pairs.json` on first launch (existing sync pairs become [SYNC] jobs)
- Browser Copy/Move buttons open a job-configuration dialog instead of running immediately
- `JobEditDialog` pre-fills source/destination from the active and other pane
- Job rows show type badge [SYNC]/[COPY]/[MOVE] and last run status
- Delete button shows an AdwAlertDialog confirmation before deleting files
- `RcloneRc` gains `copy_async` and `move_async` methods (rclone RC `sync/copy`, `sync/move` endpoints)

## 0.0.3 — Local Filesystem Browser

- Local filesystem always available as the first sidebar entry, starting at the user's home directory
- Breadcrumbs, Up, and Back navigation all handle absolute paths and filesystem root correctly

## 0.0.2 — File Browser Overhaul

The file browser has been redesigned from a flat single-pane layout to a GNOME Files-style interface with a collapsible sidebar, breadcrumb navigation, sortable columns, MIME-type-aware icons, and proper empty/loading states.

- Redesigned file browser with sidebar + content pane layout (`AdwNavigationSplitView`)
- Sidebar lists remotes with type-appropriate icons
- Breadcrumb navigation bar with clickable path segments
- Sortable columns (Name, Size, Modified) with directories always sorted above files
- MIME-type icons for files (images, video, audio, archives, documents, scripts)
- Empty folder and loading spinner states
- Stale-load guard prevents out-of-order async results from corrupting the file list
- Fixed duplicate window controls when running under a server-side decorated window manager

## 0.0.1

Initial release.

- GTK4/libadwaita window with three-tab navigation (Backends, Sync, Browse)
- rclone CLI integration for backend configuration (create, edit, delete remotes)
- Dynamic form generation from rclone provider options
- File browser with ColumnView and directory navigation via `rclone lsjson`
- Sync management with rclone RC daemon, live progress (bytes, speed, ETA)
- Sync pair persistence in `~/.config/saddle/sync_pairs.json`
