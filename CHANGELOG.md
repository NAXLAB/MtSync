# Changelog

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
