# Saddle — C++ GTK4 Frontend to rclone: Implementation Plan

## Context

Saddle is a C++ GTK4 GUI frontend to rclone. It allows users to configure rclone backends via a graphical interface, manage sync operations between remotes with live progress, and browse remote file systems — replacing the need for CLI-based rclone configuration.

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
│   ├── main.cpp                    # Entry point, adw_init(), app run
│   ├── application.hpp/cpp         # SaddleApplication (Gtk::Application subclass)
│   ├── window.hpp/cpp              # SaddleWindow — AdwToolbarView + AdwViewStack + AdwViewSwitcher
│   ├── rclone/
│   │   ├── rclone_types.hpp        # Data structs: RemoteInfo, ProviderInfo, FileEntry, SyncStats, etc.
│   │   ├── rclone_cli.hpp/cpp      # CLI subprocess interface (Gio::Subprocess)
│   │   ├── rclone_rc.hpp/cpp       # RC HTTP API interface (libsoup-3.0)
│   │   └── rclone_manager.hpp      # Facade owning CLI + RC (header-only)
│   ├── views/
│   │   ├── backends_view.hpp/cpp   # Remote list with AdwNavigationView for drill-down
│   │   ├── backend_edit_view.hpp/cpp  # Dynamic form for create/edit remote
│   │   ├── sync_view.hpp/cpp       # Sync pair list with progress
│   │   ├── sync_edit_dialog.hpp/cpp   # Dialog to configure a sync pair
│   │   └── browser_view.hpp/cpp    # File browser with ColumnView
│   └── widgets/
│       ├── adw_wrapper.hpp         # Thin C++ helpers over libadwaita C API
│       ├── file_row.hpp/cpp        # FileObject (Glib::Object subclass) for ListView model
```

---

## 3. Architecture

```
SaddleApplication (Gtk::Application + adw_init())
 └── SaddleWindow (Gtk::ApplicationWindow)
      ├── AdwHeaderBar + AdwViewSwitcher (top-level nav: Backends | Sync | Browse)
      └── AdwViewStack
           ├── BackendsView → AdwNavigationView → BackendEditView (push/pop)
           ├── SyncView
           └── BrowserView

RcloneManager (owned by SaddleApplication, passed by reference to views)
 ├── RcloneCli — Gio::Subprocess for one-shot commands (config dump, providers, lsjson, config create/update/delete)
 └── RcloneRc  — libsoup HTTP to rclone rcd daemon for long-running ops (sync with progress, job control)
```

**Navigation**: `AdwViewStack` + `AdwViewSwitcher` for 3 top-level views (GNOME HIG pattern). `AdwNavigationView` only inside BackendsView for list → edit push/pop.

**Async model**: Both `Gio::Subprocess::communicate_utf8_async()` and `soup_session_send_and_read_async()` dispatch on GLib main loop = GTK thread. No manual threading needed. All async callbacks use `std::expected<T, std::string>` (C++23).

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

### RC API (`RcloneRc`) — for sync with progress
- `ensure_daemon()`: spawn `rclone rcd --rc-addr localhost:5572 --rc-no-auth`, verify with `core/version`
- `sync_async()`: POST `/sync/sync` with `{srcFs, dstFs, _async: true}` → returns jobid
- `get_stats()`: POST `/core/stats` → `SyncStats` (bytes, speed, ETA, transfers)
- `job_status()`: POST `/job/status` with `{jobid}` → `JobStatus` (finished, success, error)
- `job_stop()`: POST `/job/stop` with `{jobid}`
- HTTP via libsoup: `soup_session_send_and_read_async()` with JSON body

---

## 5. Screen Details

### Screen 1: Backend Configuration
- **BackendsView**: `AdwPreferencesGroup` with `AdwActionRow` per remote (name, type subtitle, edit/delete buttons). "Add Remote" button pushes BackendEditView.
- **BackendEditView**: Provider dropdown populated from `config providers`. On selection, dynamically generates form fields from provider `Options`:
  - `bool` → `AdwSwitchRow`
  - `exclusive + examples` → `AdwComboRow`
  - `is_password` → `AdwPasswordEntryRow`
  - default → `AdwEntryRow`
  - `advanced=true` fields go in `AdwExpanderRow`
  - `required=true` fields marked with asterisk
- Save calls `rclone config create` or `update` with `--non-interactive`

### Screen 2: Sync Management
- Sync pairs stored in `~/.config/saddle/sync_pairs.json` (app-local, rclone has no sync pair concept)
- List of pairs with source/dest, last run time, status
- "Run Sync" → `ensure_daemon()` → `sync_async()` → poll `core/stats` + `job/status` every 500ms via `Glib::signal_timeout()`
- Progress UI: bar, bytes transferred, speed, ETA, file count, stop button

### Screen 3: File Browser
- Remote dropdown + path entry bar
- `Gtk::ColumnView` with columns: Name (icon + text), Size, Modified
- GTK4 list model pattern: `FileObject` (Glib::Object subclass with `Glib::Property`) → `Gio::ListStore` → `Gtk::SingleSelection`
- Entries sorted in-code (directories first, then by name) before populating the ListStore
- Directory click → re-fetch `lsjson` for new path; history stack for back navigation

---

## 6. Incremental Build Phases

### Phase 1: Skeleton — empty window with Adwaita styling
**Files**: `CMakeLists.txt`, `.gitignore`, `main.cpp`, `application.hpp/cpp`, `window.hpp/cpp`, `adw_wrapper.hpp`
**Result**: Window with AdwHeaderBar + ViewSwitcher + 3 placeholder labels
**Verify**: `cmake -B build && cmake --build build && ./build/saddle`

### Phase 2: CLI layer + Backend list
**Files**: `rclone_types.hpp`, `rclone_cli.hpp/cpp`, `rclone_manager.hpp`, `backends_view.hpp/cpp`
**Result**: Fetches and displays existing remotes with delete capability
**Verify**: App shows "ANTG GG OneDrive" in the Backends tab

### Phase 3: Backend creation/editing
**Files**: `backend_edit_view.hpp/cpp`
**Result**: Dynamic form generation from provider options, create/update remotes
**Verify**: Create a "local" type remote, see it appear, edit it, delete it

### Phase 4: File Browser
**Files**: `browser_view.hpp/cpp`, `file_row.hpp/cpp`
**Result**: Browse remote contents with ColumnView, directory navigation
**Verify**: Select a remote, browse files, navigate into directories

### Phase 5: RC API + Sync
**Files**: `rclone_rc.hpp/cpp`, `sync_view.hpp/cpp`, `sync_edit_dialog.hpp/cpp`
**Result**: Configure sync pairs, run sync with live progress bar
**Verify**: Create local→local sync pair, run it, see progress, completion

### Phase 6: Polish
- `AdwToastOverlay` for error notifications
- `AdwStatusPage` empty state on Backends view when no remotes configured
- Loading spinner and status label on Browser view

---

## 7. CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.25)
project(Saddle VERSION 0.1.0 LANGUAGES CXX)

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

## 8. Verification Plan

1. **Phase 1**: `./build/saddle` opens a window with Adwaita styling and 3-tab switcher
2. **Phase 2**: Backends tab lists the existing "ANTG GG OneDrive" remote
3. **Phase 3**: Can create a new "local" remote via the GUI form, verify with `rclone listremotes`
4. **Phase 4**: Can browse files on OneDrive remote, navigate directories
5. **Phase 5**: Create a local→local sync pair, run it, observe progress bar updating, verify files synced
6. **End-to-end**: Full workflow — add remote → browse its files → set up sync → run sync with progress
