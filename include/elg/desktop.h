// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_DESKTOP_H
#define ELG_DESKTOP_H

// Desktop integration (design.md §13.4): the system's file dialogs, notifications, and tray
// icon. Dropping files and text onto views is View::on_drop (elg/view.h).
//
//   choose_files({.filters = {{"Images", {"png", "jpg"}}}}, [=](std::vector<std::filesystem::path> files) {
//       if (!files.empty()) { photo = files.front(); }
//   });
//   notify({.title = "Export finished", .body = "Saved 12 photos", .on_click = [=] { show_folder(); }});
//   const TrayIcon tray({.tooltip = "Sync", .menu = {{.title = "Pause", .action = pause}, {.title = "Quit", .action = quit}}});
//
// Callbacks run on the UI thread. Call these on the UI thread of a running App.

#include <elg/export.h>
#include <elg/image.h>
#include <elg/reactive.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg {

// --- File dialogs ---

// A choice in a dialog's file type list: {"Images", {"png", "jpg", "jpeg"}}. No extensions: all
// files.
struct FileFilter {
    std::string name;
    std::vector<std::string> extensions;
};

struct OpenFilesOptions {
    std::vector<FileFilter> filters;
    std::filesystem::path directory; // where it opens (empty: the system's choice)
    bool multiple = false;
};

struct SaveFileOptions {
    std::vector<FileFilter> filters;
    std::filesystem::path directory;
    std::string name; // the suggested file name
};

struct ChooseFolderOptions {
    std::filesystem::path directory;
    bool multiple = false;
};

// The system's open dialog, over the active window. `done` gets the chosen files, or none if
// cancelled.
ELG_API void choose_files(OpenFilesOptions options, std::function<void(std::vector<std::filesystem::path>)> done);
// The system's save dialog. `done` gets the chosen path, or nullopt if cancelled. The file isn't
// created; the dialog asked about replacing an existing one.
ELG_API void choose_save_file(SaveFileOptions options, std::function<void(std::optional<std::filesystem::path>)> done);
ELG_API void choose_folders(ChooseFolderOptions options, std::function<void(std::vector<std::filesystem::path>)> done);

// --- Notifications ---

struct Notification {
    std::string title;
    std::string body;
    std::function<void()> on_click; // when the notification is clicked
};

// Shows a system notification (Windows: a toast, also kept in the notification center).
ELG_API void notify(Notification notification);

// --- Dock and taskbar ---

// What a progress bar on the app's icon says besides how far along it is.
enum class ProgressState {
    normal,
    paused,       // Windows: yellow; elsewhere drawn as normal
    error,        // Windows: red; macOS: a red bar
    indeterminate // busy, with no fraction: Windows animates it; macOS and Linux show it full
};

// A short label on the app's icon, like an unread count; nullopt removes it. macOS: the Dock
// shows the text. Windows: a small badge over the taskbar button (fits about two characters).
// Linux: launchers that support it (the Unity LauncherEntry API: GNOME's Dash to Dock, KDE)
// show only a number, so other text is ignored.
ELG_API void set_badge(std::optional<std::string> text);
// A progress bar on the app's icon, 0...1; nullopt removes it. Windows shows it on every window's
// taskbar button; macOS on the Dock icon; Linux where the launcher supports it.
ELG_API void set_progress(std::optional<double> fraction, ProgressState state = ProgressState::normal);
// Asks for the person's attention while the app is in the background: the Dock icon bounces
// (once, or until the app is active when `critical`), or the taskbar button flashes. Nothing
// happens while the app is in front.
ELG_API void request_attention(bool critical = false);

// --- Tray icon ---

struct TrayMenuItem {
    std::string title;                 // empty: a separator
    std::function<void()> action;      // when chosen
    std::optional<bool> checked;       // a check box, initially checked or not
    std::function<void(bool)> toggled; // check boxes: when chosen, with the new state
    bool enabled = true;
    std::vector<TrayMenuItem> submenu;
};

struct TrayIconOptions {
    ImageSource icon; // a file or data image; empty: a plain dot in the theme's accent color
    std::string tooltip;
    std::vector<TrayMenuItem> menu;
};

// An icon in the system tray (Windows: the notification area) with a menu. Shown while any copy
// lives.
class ELG_API TrayIcon {
public:
    explicit TrayIcon(TrayIconOptions options);

    void set_tooltip(const std::string& tooltip) const;
    // Replaces the menu.
    void set_menu(std::vector<TrayMenuItem> menu) const;

    struct State;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<State> state_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_DESKTOP_H
