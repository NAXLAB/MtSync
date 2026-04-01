# Changelog

## 0.0.2 — File Browser Overhaul

The file browser has been redesigned from a flat single-pane layout to a two-pane GNOME Files-style interface, with a collapsible remote sidebar, breadcrumb navigation, sortable columns, MIME-type-aware icons, and proper empty/loading states.

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
