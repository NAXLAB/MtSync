# Changelog

## 0.5.5 — Activity Log Font Consistency
- Activity log columns now use a uniform monospace font at 0.8em across all fields (Time, State, Job ID, Type, Contents)

## 0.5.4 — Job Cancel Fix & Browse Button Icons
- Fixed cancelled jobs not clearing `running` state — daemon and GUI now set `running = false` and `last_status = "stopped"` when a job is stopped
- Delete and New Folder buttons on the Browse tab now show icons to the left of their labels

## 0.5.3 — Job View Refresh Fix & About Tab Update
- Fixed job list not reflecting running state when switching back to the Jobs tab — stop button, progress bar, and status label now correctly restore from persisted `running` flag
- Jobs now track `running` state in `jobs.json`, set by daemon on start and cleared on completion
- About tab now includes a lyric quote below the copyright section

## 0.5.2 — Custom Idle Tray Icon
- Replaced the system systray idle icon (`network-server-symbolic`) with a custom Saddle-branded icon
- Custom icon is bundled as a GLib resource and rendered via Cairo (no external icon dependencies)
- Idle icon is scaled to 22×22 ARGB32 to match the busy animation frame dimensions
- Fixed tray icon not updating when animation stopped by emitting the standard D-Bus `PropertiesChanged` signal alongside `NewIcon`
- Icon now correctly transitions from busy spinner back to idle state

## 0.5.1 — Tab Styling Update
- Main navigation tabs (Browse, Jobs, Remotes, Settings, About) now show a deep blue background when selected
- Selected tab hover state uses a slightly lighter blue for visual feedback

## 0.5.0 — Notification Settings & Tray Menu Title
- Added **Notifications** section to the Settings tab with three independent toggles: On Job Start, On Completion, On Completion with Errors/Warnings
- All three default to off
- Settings tab general section renamed to **Start Up &amp; Shut Down** and moved below Notifications
- Notification toggles are applied at the daemon level via `load_settings()` at the point of dispatch
- System tray right-click menu now shows "Saddle" as a non-clickable title above a separator, followed by Open and Quit

## 0.4.8 — Notification Fix
- Fixed desktop notifications falling back to a console `g_message` instead of displaying via `notify-send`
- `Glib::spawn_sync` now receives the resolved full executable path instead of the bare command name

## 0.4.7 — Activity Log Column View
- Activity log now displays as a structured column view with five columns: Time, State, Job ID, Type, and Contents
- State column is colour-coded: blue for STARTED, green for COMPLETED, orange for SKIPPED/RETRYING
- Log lines are parsed from the existing log file format; unparsed lines fall back to the Contents column

## 0.4.6 — About Tagline Update
- About tab description updated to "Mount network storage in comfort."

## 0.4.5 — VFS Cache Mode for Mount Jobs & Job Start Time
- Mount jobs now include a **Cache Mode** dropdown (off, minimal, writes, full) that maps to rclone's `--vfs-cache-mode` option
- Cache mode is passed to the rclone RC `mount/mount` endpoint via `vfsOpt.CacheMode`
- Jobs now track `last_start` (when a job was invoked) separately from `last_run` (when it completed)

## 0.4.4 — Improved Log Timestamp Format
- Activity log timestamps now use a cleaner `YYYY-MM-DD HH:MM:SS` format instead of ISO 8601
- Log entries are now more readable at a glance (e.g. `[2026-04-07 14:32:01] COMPLETED ...`)

## 0.4.3 — Job Duration in Activity Log
- Completed job log entries now include total run time (e.g. `ran for 2m 15s`)
- Duration is displayed in human-readable format: seconds, minutes+seconds, or hours+minutes+seconds as appropriate

## 0.4.2 — Per-Job Stats in Activity Log
- Fixed activity log showing incorrect (cumulative) transfer stats for sync/copy/move jobs
- Stats are now queried per-job via rclone RC `core/stats` with the `group` parameter, so each job reports only its own bytes transferred, file count, and speed
- Sync jobs that find nothing to transfer now correctly log `0 files, 0 B, 0 B/s`

## 0.4.1 — Remote Capacity Display & Browser Action Buttons
- Remotes tab now shows used/free capacity for each configured remote
- Capacity is fetched asynchronously via the rclone RC `operations/about` endpoint
- Backends that don't report usage gracefully show no capacity text
- Copy, Move, Sync, and Mount buttons on the browser tab now show both icon and text label
- Action buttons styled with a muted green background to match the destructive-action Delete button

## 0.4.0 — Updated Application & Tray Icons
- New application icon replacing the previous one
- Updated AppIndicator/tray icon for improved visibility and consistency

## 0.3.27 — Tray Spinner Fix
- Fixed tray spinner stopping when multiple jobs ran concurrently and one finished before the others
- Replaced `any_job_running()` scan with a `m_running_job_count` counter so the spinner reflects the actual number of active transfers
- Fixed race condition where rapid duplicate job starts (e.g. from the scheduler) incremented the counter multiple times; added a `m_job_submitting` guard to prevent duplicate increments
- Fixed duplicate completion callbacks from overlapping poll cycles double-decrementing the counter
- Fixed `set_attention()` emitting `NewStatus` mid-run, which caused GNOME AppIndicator to reset its property cache and fall back to the static icon
- Suppressed rclone `rcd` daemon output so its notices and warnings no longer clutter the console

## 0.3.26 — Performance Improvements
- Removed unnecessary 10-second job polling in Jobs tab; updates now arrive exclusively via daemon broadcast messages
- Optimised activity log loading to tail only the last 100 lines instead of reading the entire file
- Fixed race condition in daemon poll timer where `m_job_ids[index]` could be cleared between the status check and the async callback
- Added type-safe index parsing throughout daemon message handler to prevent implicit integer conversion

## 0.3.25 — Tray Spinner Fix
- Fixed animated tray spinner not appearing on the second and subsequent jobs
- After a successful job the tray entered `NeedsAttention` state; starting a new job now resets the status to `Active` before beginning animation so the system tray displays the spinner instead of the static attention icon

## 0.3.24 — Animated Tray Icon
- Tray icon now shows a rotating spinner while any job is in progress
- Animation stops and the static icon is restored when all job activity has finished
- Uses Cairo-rendered 8-frame spinner delivered via SNI `IconPixmap`

## 0.3.23 — Job Dialog Auto-resize
- Job add/edit dialog now shrinks correctly when switching to Mount type (which hides several fields)
- Dialog also shrinks when the Advanced Options expander is collapsed

## 0.3.22 — Job Retry Support
- New **Retries on failure** setting in Settings → Transfers (default 0)
- Failed jobs are automatically retried up to the configured number of times before being marked as failed
- Each retry attempt is recorded in the activity log as `RETRYING … attempt k/N`
- Per-job retry count can be overridden in the job add/edit dialog under **Advanced Options**; defaults to the global setting

## 0.3.21 — Concurrent Job Protection
- Scheduled jobs now skip execution if the previous instance of the same job is still running
- Skipped attempts are recorded in the activity log as `SKIPPED … previous instance still running`

## 0.3.20 — Activity Log Newest-First
- Activity log on the Jobs tab now displays entries newest-first, so the latest messages are always visible without scrolling

## 0.3.19 — Job Dialog Defaults & UI Tweaks
- Default window width increased to 1250px
- File Filters field renamed to **File Include Filters** for clarity
- Dry Run is now enabled by default when creating a new job
- Job dialog now includes a **Parallel Transfers** field; defaults to the value set in Settings, but can be overridden per-job
- Bandwidth Limit and Parallel Transfers are now grouped under a collapsed **Advanced Options** expander row
- Fixed: completed jobs were being logged repeatedly — poll timer now stops immediately when a job finishes by resetting the internal job ID to -1

## 0.3.18 — Mount Job Dialog Cleanup (continued)
- Dry Run, Enable Checksum, and Bandwidth Limit options are now hidden when the job type is Mount

## 0.3.17 — Smaller Job Log Font
- Activity log in the Jobs tab now renders at 80% font size for a more compact display

## 0.3.16 — Extended Provider Icons
- Added Simple Icons SVGs for Google Photos, Seafile, Zoho WorkDrive, Filen, OpenStack Swift, Apache HDFS, Citrix ShareFile, DigitalOcean, Wasabi, Cloudflare, and Hetzner
- SMB remotes now use `folder-remote-symbolic` instead of the generic terminal icon
- Provider icons appear in both the Backends tab and the file manager remote dropdown

## 0.3.15 — Provider Icons
- Remote type icons now use Simple Icons SVGs (Google Drive, Dropbox, Backblaze, MEGA, Box, Google Cloud, Proton Drive, Internet Archive) with symbolic fallbacks for all other types
- Light and dark variants of each icon — dark mode shows white icons automatically
- Icons appear both in the Backends tab remote list and in the file manager remote dropdown
- Remote dropdown width increased to 150px to accommodate icon + name

## 0.3.14 — Remote Type Icons on Backends Tab
- Each remote row now shows a symbolic icon representing its type (Google Drive, Dropbox, encrypted, local disk, terminal-based, or generic network server)

## 0.3.13 — Compact Browser Navigation Bar
- Navigation strip (Back, Up, breadcrumbs, Reload) is now more compact: halved spacing, margins, and remote dropdown minimum width
- Breadcrumb buttons use reduced horizontal padding for a tighter path display

## 0.3.12 — Mount Job Dialog Cleanup
- File Filters field is now hidden when the job type is Mount, as rclone mount does not use include filters

## 0.3.11 — Job Row Display
- Job title now shows `SourceDir → DestDir` (last path component of each) instead of the raw UUID
- Footer line of each job row now shows the UUID left-justified and the status text right-justified

## 0.3.10 — Job Type Icons
- Job rows in the Jobs tab now show the job type as a symbolic icon instead of a text badge (`[SYNC]`, `[COPY]`, etc.)
- Icons match those used on the Copy/Move/Sync/Mount buttons in the browse tab action bar

## 0.3.9 — Browser Pane Cleanup
- Removed source/destination role labels from browser pane headers
- Removed virtual swap logic (`m_source_on_left`, `update_pane_roles`, `BrowserPane::Role`) — left pane is always source now that swapping is physical
- Removed vertical separator between remote dropdown and navigation buttons

## 0.3.8 — Physical Pane Swap
- Swap button now exchanges the actual remote and path between the two browser panes, rather than just flipping the source/destination role labels
- Navigation history is also swapped, so the Back button continues to work correctly after a swap

## 0.3.7 — Security Hardening
- Fixed: deleting a job now disconnects its poll/schedule timers and removes it from all internal tracking vectors — previously stale timer callbacks could fire after deletion and reference the wrong job
- Fixed: poll timer lambda now exits early if the job index is out of range or the job ID is unset — prevents stale callbacks after a job delete
- Fixed: `rclone authorize` callback now uses a weak lifetime guard — no longer accesses the widget if it was destroyed while OAuth was in progress
- Fixed: malformed rclone authorize output with reversed `-->` / `<--` markers no longer causes a size_t underflow in the token extraction

## 0.3.6 — Breadcrumb Bar Overflow Fix
- File path bar no longer grows the main window when navigating into deep directories
- Breadcrumbs are now clipped to their allocated space and scroll to show the deepest segment

## 0.3.5 — File Filters in Job Dialog
- Job add/edit dialog now shows a **File Filters** field displaying the include patterns for the job
- Patterns are shown space-separated and can be edited directly
- Fixed: editing an existing job no longer silently discards its file filter patterns

## 0.3.4 — Job Activity Log
- Job execution results are now written to `~/.local/state/saddle/saddle.log`
- Each job logs a STARTED line and a COMPLETED line (with file count, bytes transferred, and speed for sync/copy/move jobs)
- Mount jobs log success or failure with error message
- Jobs tab now shows an **Activity Log** panel at the bottom, displaying the log file contents and auto-scrolling to the latest entry

## 0.3.3 — Progress Bar Visibility
- Progress bar in the job list is now hidden when a job is inactive
- Progress bar appears only while a job is running and is hidden again on completion or stop

## 0.3.2 — Job Dialog Improvements & Coloured File Icons
- Scheduling off: shows **Run Now** + **Save** buttons; scheduling on: shows **Schedule** only
- Cron fields section is now hidden entirely when scheduling is disabled (previously greyed out)
- Dialog shrinks to fit when cron section is hidden
- Run Now button styled as destructive-action; Cancel button styled as success
- File browser icons are now coloured by type (folders amber, images blue, video purple, audio green, archives orange, documents/PDFs red, code teal, etc.)

## 0.3.1 — Desktop Integration
- Application icon now appears in taskbar, launcher, and dock when the app is running
- Added `data/com.saddle.Saddle.desktop` for XDG desktop entry
- CMake install rules deploy the icon to `share/icons/hicolor/256x256/apps/` and the `.desktop` file to `share/applications/`
- Window sets `icon-name` to `com.saddle.Saddle` for X11 tray/taskbar resolution
- Added Installing section to README

## 0.3.0 — Application Icon
- Application icon now embedded in the binary via GLib resources and displayed on the About tab
- Added `data/saddle.gresource.xml` and updated CMakeLists.txt to compile resources at build time
- No installation step required — icon is always available regardless of system paths

## 0.2.6 - Mount at startup fix
- Fixed "Mount at Start-up" jobs not mounting on daemon start — auto-mount now runs inside the `ensure_daemon` callback so rclone RC is ready before the mount is attempted

## 0.2.5 - Sync empty directory fix
- Fixed `--create-empty-src-dirs` not being passed correctly to rclone RC (`createEmptySrcDirs` is a direct `sync/sync` parameter, not a `_config` override)

## 0.2.4 - Job Edit Checksum Fix
- Fixed "Enable Checksum" toggle not loading correctly when editing existing jobs

## 0.2.3 - Job dialog Save button and sync fixes
- Added "Save" button to JobEditDialog to save job without running it
- Fixed jobs not being synced to daemon when created from browser view
- Fixed Sync jobs using `--create-empty-src-dirs` flag (was using wrong case)

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
