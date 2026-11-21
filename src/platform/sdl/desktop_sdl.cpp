// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4): SDL's file dialogs and tray; notifications through each
// OS's own (SDL has none): platform/<os>/notifications.*.

#include <elg/desktop.h>

#include "platform/desktop.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "core/utf8.h"
#include "platform/sdl/native.h"
#include "platform/system_text.h"

#include <elg/error.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>


namespace elg {

using detail::log_warning;

void detail::platform::require_app(const char* what) {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        throw UsageError(std::format("{}: call it while an App runs", what));
    }
}
using detail::platform::require_app;

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path from_utf8(const char* text) {
    const std::string_view view(text);
    return fs::path(std::u8string(view.begin(), view.end()));
}

// The dialog's filters, kept alive (SDL may read them until the dialog closes).
struct DialogRequest {
    std::vector<std::string> names;
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string location;
    std::function<void(std::vector<fs::path>)> done;

    void set_filters(std::vector<FileFilter> source) {
        detail::make_valid_utf8(source);
        for (const FileFilter& filter : source) {
            std::string pattern;
            for (const std::string& extension : filter.extensions) {
                if (!pattern.empty()) {
                    pattern += ';';
                }
                pattern += extension.starts_with('.') ? extension.substr(1) : extension;
            }
            names.push_back(filter.name.empty() ? std::string("Files") : filter.name);
            patterns.push_back(pattern.empty() ? std::string("*") : pattern);
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            filters.push_back({names[i].c_str(), patterns[i].c_str()});
        }
    }
};

// The dialogs open, by the id SDL hands back: SDL may read a request's filters until it calls
// dialog_closed(), on any thread. A run that ends destroys their `done` callbacks first.
std::mutex g_dialogs_mutex;
std::map<std::uintptr_t, std::unique_ptr<DialogRequest>> g_dialogs;
std::uintptr_t g_next_dialog = 1;

// Shows a dialog for `request` with `show`, which gets its id as SDL's userdata.
void show_dialog(std::unique_ptr<DialogRequest> request, const std::function<void(DialogRequest&, void*)>& show) {
    DialogRequest& shown = *request;
    std::uintptr_t id = 0;
    {
        const std::scoped_lock lock(g_dialogs_mutex);
        id = g_next_dialog++;
        g_dialogs.emplace(id, std::move(request));
    }
    show(shown, reinterpret_cast<void*>(id)); // NOLINT(performance-no-int-to-ptr): an id, not a pointer
}

// Called by SDL, maybe on another thread, once the dialog closes: `done` runs on the UI thread,
// unless the run ended (then it's gone already). Nothing may escape it (ELGSPEC-0002.R10).
void SDLCALL dialog_closed(void* userdata, const char* const* files, int) {
    detail::run_foreign_callback([&] {
        std::unique_ptr<DialogRequest> request;
        {
            const std::scoped_lock lock(g_dialogs_mutex);
            if (const auto found = g_dialogs.find(reinterpret_cast<std::uintptr_t>(userdata)); found != g_dialogs.end()) {
                request = std::move(found->second);
                g_dialogs.erase(found);
            }
        }
        if (!request || !request->done) {
            return;
        }
        std::vector<fs::path> chosen;
        if (files) {
            for (const char* const* file = files; *file; ++file) {
                chosen.push_back(from_utf8(*file));
            }
        } else {
            log_warning("file dialog: {}", SDL_GetError());
        }
        detail::post_ui_task([done = std::move(request->done), chosen = std::move(chosen)] { done(chosen); });
    });
}

} // namespace

void choose_files(const OpenFilesOptions& options, std::function<void(std::vector<fs::path>)> done) {
    require_app("choose_files");
    auto request = std::make_unique<DialogRequest>();
    request->set_filters(options.filters);
    request->location = utf8(options.directory);
    request->done = std::move(done);
    show_dialog(std::move(request), [&](DialogRequest& shown, void* id) {
        SDL_ShowOpenFileDialog(dialog_closed, id, SDL_GetKeyboardFocus(), shown.filters.data(),
                               static_cast<int>(shown.filters.size()),
                               shown.location.empty() ? nullptr : shown.location.c_str(), options.multiple);
    });
}

void choose_save_file(const SaveFileOptions& options, std::function<void(std::optional<fs::path>)> done) {
    require_app("choose_save_file");
    auto request = std::make_unique<DialogRequest>();
    request->set_filters(options.filters);
    request->location = utf8(options.name.empty() ? options.directory
                                                  : options.directory / from_utf8(detail::valid_utf8(options.name).c_str()));
    request->done = [done = std::move(done)](std::vector<fs::path> files) {
        if (done) {
            done(files.empty() ? std::nullopt : std::optional(files.front()));
        }
    };
    show_dialog(std::move(request), [](DialogRequest& shown, void* id) {
        SDL_ShowSaveFileDialog(dialog_closed, id, SDL_GetKeyboardFocus(), shown.filters.data(),
                               static_cast<int>(shown.filters.size()),
                               shown.location.empty() ? nullptr : shown.location.c_str());
    });
}

void choose_folders(const ChooseFolderOptions& options, std::function<void(std::vector<fs::path>)> done) {
    require_app("choose_folders");
    auto request = std::make_unique<DialogRequest>();
    request->location = utf8(options.directory);
    request->done = std::move(done);
    show_dialog(std::move(request), [&](DialogRequest& shown, void* id) {
        SDL_ShowOpenFolderDialog(dialog_closed, id, SDL_GetKeyboardFocus(),
                                 shown.location.empty() ? nullptr : shown.location.c_str(), options.multiple);
    });
}

void detail::platform::close_file_dialogs() {
    // Destroyed here, on the UI thread, without being called: the run is over.
    std::vector<std::function<void(std::vector<fs::path>)>> abandoned;
    {
        const std::scoped_lock lock(g_dialogs_mutex);
        for (const auto& [id, request] : g_dialogs) {
            abandoned.push_back(std::move(request->done));
        }
    }
    abandoned.clear();
    // Each closed dialog calls dialog_closed(), which frees its request. A dialog that doesn't
    // close keeps its request until it does: SDL may still read its filters.
    SDL_CloseFileDialogs();
}

// --- Notifications ---

void notify(const Notification& notification) {
    require_app("notify");
    Notification valid = notification;
    detail::make_valid_utf8(valid);
    detail::platform::native::show_notification(valid);
}

// --- Dock and taskbar ---

void set_badge(const std::optional<std::string>& text) {
    require_app("set_badge");
    detail::platform::native::set_badge(text ? std::optional(detail::valid_utf8(*text)) : std::nullopt);
}

void set_progress(std::optional<double> fraction, ProgressState state) {
    require_app("set_progress");
    if (fraction) {
        fraction = std::isfinite(*fraction) ? std::clamp(*fraction, 0.0, 1.0) : 0.0;
    }
    detail::platform::native::set_progress(fraction, state);
}

void request_attention(bool critical) {
    require_app("request_attention");
    // SDL asks the system: the Dock icon bounces (macOS), the taskbar button flashes elsewhere.
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    for (int i = 0; windows && i < count; ++i) {
        if ((SDL_GetWindowFlags(windows[i]) & (SDL_WINDOW_POPUP_MENU | SDL_WINDOW_TOOLTIP)) == 0) {
            SDL_FlashWindow(windows[i], critical ? SDL_FLASH_UNTIL_FOCUSED : SDL_FLASH_BRIEFLY);
            break;
        }
    }
    SDL_free(static_cast<void*>(windows));
}

// --- Tray icon ---

struct TrayIcon::State {
    TrayIconOptions options;
    SDL_Tray* tray = nullptr;
    SDL_Surface* icon = nullptr;
    detail::platform::TrayIconPicture picture; // the surface's pixels
    // One per menu entry with something to do, at stable addresses (SDL keeps pointers to them).
    struct Entry {
        std::function<void()> action;
        std::function<void(bool)> toggled;
    };
    std::list<Entry> entries;

    // Every State, while it lives (UI thread only), for remove_tray_icons().
    static std::vector<State*>& all() {
        static std::vector<State*> states;
        return states;
    }

    State() { all().push_back(this); }
    State(const State&) = delete;
    State(State&&) = delete;
    State& operator=(const State&) = delete;
    State& operator=(State&&) = delete;
    ~State() {
        take_down();
        std::erase(all(), this);
    }

    // Out of the tray for good: the methods then find no tray, and do nothing.
    void take_down() {
        if (tray) {
            SDL_DestroyTray(tray);
            tray = nullptr;
        }
        if (icon) {
            SDL_DestroySurface(icon);
            icon = nullptr;
        }
        entries.clear();
    }

    static void SDLCALL chosen(void* userdata, SDL_TrayEntry* entry) {
        const auto* target = static_cast<const Entry*>(userdata);
        if (target->toggled) {
            const bool checked = SDL_GetTrayEntryChecked(entry);
            detail::post_ui_task([toggled = target->toggled, checked] { toggled(checked); });
        }
        if (target->action) {
            detail::post_ui_task(target->action);
        }
    }

    void build(SDL_TrayMenu* menu, const std::vector<TrayMenuItem>& items) {
        for (const TrayMenuItem& item : items) {
            if (item.title.empty()) {
                SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON); // a separator
                continue;
            }
            SDL_TrayEntryFlags flags = item.submenu.empty() ? SDL_TRAYENTRY_BUTTON : SDL_TRAYENTRY_SUBMENU;
            if (item.checked) {
                flags = SDL_TRAYENTRY_CHECKBOX | (*item.checked ? SDL_TRAYENTRY_CHECKED : 0u);
            }
            if (!item.enabled) {
                flags |= SDL_TRAYENTRY_DISABLED;
            }
            SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, item.title.c_str(), flags);
            if (!entry) {
                continue;
            }
            if (!item.submenu.empty()) {
                build(SDL_CreateTraySubmenu(entry), item.submenu);
                continue;
            }
            Entry& target = entries.emplace_back(Entry{item.action, item.toggled});
            SDL_SetTrayEntryCallback(entry, chosen, &target);
        }
    }

    void rebuild_menu() {
        SDL_TrayMenu* menu = SDL_GetTrayMenu(tray);
        if (!menu) {
            menu = SDL_CreateTrayMenu(tray);
        }
        int count = 0;
        const SDL_TrayEntry** existing = SDL_GetTrayEntries(menu, &count);
        for (int i = count - 1; i >= 0; --i) {
            SDL_RemoveTrayEntry(const_cast<SDL_TrayEntry*>(existing[i]));
        }
        entries.clear();
        build(menu, options.menu);
    }

    void create(std::optional<detail::platform::TrayIconPicture> given) {
        // The icon: the given picture, or a dot in a calm blue.
        if (given) {
            picture = std::move(*given);
        } else {
            auto dot = std::make_shared<std::vector<std::uint8_t>>(std::size_t{32} * 32 * 4);
            for (std::size_t y = 0; y < 32; ++y) {
                for (std::size_t x = 0; x < 32; ++x) {
                    const float d = std::hypot(static_cast<float>(x) - 15.5f, static_cast<float>(y) - 15.5f);
                    const auto alpha = static_cast<std::uint8_t>(std::clamp(13.f - d, 0.f, 1.f) * 255);
                    std::uint8_t* p = &(*dot)[(y * 32 + x) * 4];
                    p[0] = 0x8A;
                    p[1] = 0xAD;
                    p[2] = 0xF4;
                    p[3] = alpha;
                }
            }
            picture = {32, 32, std::move(dot)};
        }
        icon = SDL_CreateSurfaceFrom(static_cast<int>(picture.width), static_cast<int>(picture.height),
                                     SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(picture.rgba->data()),
                                     static_cast<int>(picture.width * 4));
        tray = SDL_CreateTray(icon, options.tooltip.c_str());
        if (!tray) {
            log_warning("TrayIcon: {}", SDL_GetError());
            return;
        }
        rebuild_menu();
    }
};

namespace detail::platform {

void remove_tray_icons() {
    for (TrayIcon::State* state : TrayIcon::State::all()) {
        state->take_down();
    }
}

std::shared_ptr<TrayIcon::State> create_tray_icon(TrayIconOptions options, std::optional<TrayIconPicture> picture) {
    native::require_tray_icons();
    auto state = std::make_shared<TrayIcon::State>();
    make_valid_utf8(options);
    state->options = std::move(options);
    state->create(std::move(picture));
    return state;
}

} // namespace detail::platform

void TrayIcon::set_tooltip(const std::string& tooltip) const {
    state_->options.tooltip = detail::valid_utf8(tooltip);
    if (state_->tray) {
        SDL_SetTrayTooltip(state_->tray, state_->options.tooltip.c_str());
    }
}

void TrayIcon::set_menu(std::vector<TrayMenuItem> menu) const {
    detail::make_valid_utf8(menu);
    state_->options.menu = std::move(menu);
    if (state_->tray) {
        state_->rebuild_menu();
    }
}

} // namespace elg
