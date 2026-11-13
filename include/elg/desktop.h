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

/// A choice in a dialog's file type list: {"Images", {"png", "jpg", "jpeg"}}. No extensions: all
/// files.
struct FileFilter {
    std::string name;                    ///< What the list shows; empty: "Files".
    std::vector<std::string> extensions; ///< Without the dot ("png"); a leading dot is dropped.
};

/// Options for choose_files().
struct OpenFilesOptions {
    std::vector<FileFilter> filters; ///< The file type list; empty: all files.
    std::filesystem::path directory; ///< where it opens (empty: the system's choice)
    bool multiple = false;           ///< Whether several files can be chosen.
};

/// Options for choose_save_file().
struct SaveFileOptions {
    std::vector<FileFilter> filters; ///< The file type list; empty: all files.
    std::filesystem::path directory; ///< Where it opens (empty: the system's choice).
    std::string name;                ///< the suggested file name
};

/// Options for choose_folders().
struct ChooseFolderOptions {
    std::filesystem::path directory; ///< Where it opens (empty: the system's choice).
    bool multiple = false;           ///< Whether several folders can be chosen.
};

/// The system's open dialog, over the active window. Returns at once; the dialog's answer comes
/// later.
///
/// @param options The file types, starting folder, and whether several files can be chosen.
/// @param done Gets the chosen files, or none if cancelled (or the dialog failed). Runs on the UI
///   thread; may be empty.
/// @throws UsageError When no App is running.
ELG_API void choose_files(const OpenFilesOptions& options, std::function<void(std::vector<std::filesystem::path>)> done);
/// The system's save dialog. The file isn't created; the dialog asked about replacing an existing
/// one. Returns at once; the dialog's answer comes later.
///
/// @param options The file types, starting folder, and suggested name.
/// @param done Gets the chosen path, or nullopt if cancelled. Runs on the UI thread; may be empty.
/// @throws UsageError When no App is running.
ELG_API void choose_save_file(const SaveFileOptions& options, std::function<void(std::optional<std::filesystem::path>)> done);
/// The system's folder dialog, over the active window. Returns at once; the dialog's answer comes
/// later.
///
/// @param options The starting folder, and whether several folders can be chosen.
/// @param done Gets the chosen folders, or none if cancelled (or the dialog failed). Runs on the
///   UI thread; may be empty.
/// @throws UsageError When no App is running.
ELG_API void choose_folders(const ChooseFolderOptions& options, std::function<void(std::vector<std::filesystem::path>)> done);

// --- Notifications ---

/// A system notification, for notify().
struct Notification {
    std::string title;              ///< The heading.
    std::string body;               ///< The message under it.
    std::function<void()> on_click; ///< when the notification is clicked
};

/// Shows a system notification (Windows: a toast, also kept in the notification center).
///
/// @param notification What it says, and what clicking it does.
/// @throws UsageError When no App is running.
ELG_API void notify(const Notification& notification);

// --- Dock and taskbar ---

/// What a progress bar on the app's icon says besides how far along it is.
enum class ProgressState {
    normal,
    paused,       ///< Windows: yellow; elsewhere drawn as normal
    error,        ///< Windows: red; macOS: a red bar
    indeterminate ///< busy, with no fraction: Windows animates it; macOS and Linux show it full
};

/// A short label on the app's icon, like an unread count. macOS: the Dock shows the text.
/// Windows: a small badge over the taskbar button (fits about two characters). Linux: launchers
/// that support it (the Unity LauncherEntry API: GNOME's Dash to Dock, KDE) show only a number,
/// so other text is ignored.
///
/// @param text The label; nullopt removes it.
/// @throws UsageError When no App is running.
ELG_API void set_badge(const std::optional<std::string>& text);
/// A progress bar on the app's icon. Windows shows it on every window's taskbar button; macOS on
/// the Dock icon; Linux where the launcher supports it.
///
/// @param fraction How far along, 0...1 (clamped; not finite counts as 0); nullopt removes it.
/// @param state What else the bar says.
/// @throws UsageError When no App is running.
ELG_API void set_progress(std::optional<double> fraction, ProgressState state = ProgressState::normal);
/// Asks for the person's attention while the app is in the background: the Dock icon bounces
/// (once, or until the app is active when `critical`), or the taskbar button flashes. Nothing
/// happens while the app is in front.
///
/// @param critical Keep asking until the app is active, rather than once.
/// @throws UsageError When no App is running.
ELG_API void request_attention(bool critical = false);

// --- Tray icon ---

/// One entry of a tray icon's menu. Its callbacks run on the UI thread.
struct TrayMenuItem {
    std::string title;                 ///< empty: a separator
    std::function<void()> action;      ///< when chosen
    std::optional<bool> checked;       ///< a check box, initially checked or not
    std::function<void(bool)> toggled; ///< check boxes: when chosen, with the new state
    bool enabled = true;               ///< false: shown dimmed, and can't be chosen.
    std::vector<TrayMenuItem> submenu; ///< Not empty: the entry opens this menu instead.
};

/// Options for TrayIcon.
struct TrayIconOptions {
    /// a file or data image; empty (or unreadable, with a warning in the log): a plain blue dot
    ImageSource icon;
    std::string tooltip;            ///< Shown when the pointer rests on the icon.
    std::vector<TrayMenuItem> menu; ///< The menu the icon opens.
};

/// An icon in the system tray (Windows: the notification area) with a menu. Shown while any copy
/// lives. When the system has no tray, there's a warning in the log and nothing shows.
class ELG_API TrayIcon {
public:
    /// Shows the icon.
    ///
    /// @param options The icon, tooltip and menu.
    /// @throws UsageError When no App is running.
    explicit TrayIcon(TrayIconOptions options);

    /// @param tooltip The new tooltip.
    void set_tooltip(const std::string& tooltip) const;
    /// Replaces the menu.
    ///
    /// @param menu The new menu.
    void set_menu(std::vector<TrayMenuItem> menu) const;

    /// Internal: the icon's state, shared by its copies.
    struct State;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<State> state_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_DESKTOP_H
