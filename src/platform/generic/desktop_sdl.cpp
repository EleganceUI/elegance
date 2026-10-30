// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4): SDL's file dialogs and tray; notifications through each
// OS's own (SDL has none): platform/<os>/notifications.*.

#include <elg/desktop.h>

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "platform/generic/native.h"

#include <elg/error.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <list>
#include <mutex>


namespace elg {

using detail::log_info;
using detail::log_warning;

namespace {

namespace fs = std::filesystem;

void require_app(const char* what) {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        throw UsageError(std::format("{}: call it while an App runs", what));
    }
}

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

    void set_filters(const std::vector<FileFilter>& source) {
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

// Called by SDL, maybe on another thread, once the dialog closes.
void SDLCALL dialog_closed(void* userdata, const char* const* files, int) {
    auto* request = static_cast<DialogRequest*>(userdata);
    std::vector<fs::path> chosen;
    if (files) {
        for (const char* const* file = files; *file; ++file) {
            chosen.push_back(from_utf8(*file));
        }
    } else {
        log_warning("file dialog: {}", SDL_GetError());
    }
    detail::post_ui_task([request, chosen = std::move(chosen)] {
        std::unique_ptr<DialogRequest> owned(request);
        if (owned->done) {
            owned->done(chosen);
        }
    });
}

} // namespace

void choose_files(OpenFilesOptions options, std::function<void(std::vector<fs::path>)> done) {
    require_app("choose_files");
    auto request = std::make_unique<DialogRequest>();
    request->set_filters(options.filters);
    request->location = utf8(options.directory);
    request->done = std::move(done);
    DialogRequest* raw = request.release();
    SDL_ShowOpenFileDialog(dialog_closed, raw, SDL_GetKeyboardFocus(), raw->filters.data(),
                           static_cast<int>(raw->filters.size()), raw->location.empty() ? nullptr : raw->location.c_str(),
                           options.multiple);
}

void choose_save_file(SaveFileOptions options, std::function<void(std::optional<fs::path>)> done) {
    require_app("choose_save_file");
    auto request = std::make_unique<DialogRequest>();
    request->set_filters(options.filters);
    request->location = utf8(options.name.empty() ? options.directory : options.directory / from_utf8(options.name.c_str()));
    request->done = [done = std::move(done)](std::vector<fs::path> files) {
        if (done) {
            done(files.empty() ? std::nullopt : std::optional(files.front()));
        }
    };
    DialogRequest* raw = request.release();
    SDL_ShowSaveFileDialog(dialog_closed, raw, SDL_GetKeyboardFocus(), raw->filters.data(),
                           static_cast<int>(raw->filters.size()), raw->location.empty() ? nullptr : raw->location.c_str());
}

void choose_folders(ChooseFolderOptions options, std::function<void(std::vector<fs::path>)> done) {
    require_app("choose_folders");
    auto request = std::make_unique<DialogRequest>();
    request->location = utf8(options.directory);
    request->done = std::move(done);
    DialogRequest* raw = request.release();
    SDL_ShowOpenFolderDialog(dialog_closed, raw, SDL_GetKeyboardFocus(),
                             raw->location.empty() ? nullptr : raw->location.c_str(), options.multiple);
}

// --- Notifications ---

void notify(Notification notification) {
    require_app("notify");
    detail::platform::native::show_notification(notification);
}

// --- Dock and taskbar ---

void set_badge(std::optional<std::string> text) {
    require_app("set_badge");
    detail::platform::native::set_badge(text);
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
    SDL_free(windows);
}

// --- Tray icon ---

struct TrayIcon::State {
    TrayIconOptions options;
    SDL_Tray* tray = nullptr;
    SDL_Surface* icon = nullptr;
    std::shared_ptr<const detail::DecodedImage> pixels;
    // One per menu entry with something to do, at stable addresses (SDL keeps pointers to them).
    struct Entry {
        std::function<void()> action;
        std::function<void(bool)> toggled;
    };
    std::list<Entry> entries;

    ~State() {
        if (tray) {
            SDL_DestroyTray(tray);
        }
        if (icon) {
            SDL_DestroySurface(icon);
        }
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

    void create() {
        // The icon: the given image, or a dot in a calm blue.
        try {
            std::vector<std::uint8_t> bytes;
            if (!options.icon.file.empty()) {
                std::ifstream in(options.icon.file, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(in), {});
            } else if (!options.icon.data.empty()) {
                bytes.assign(options.icon.data.begin(), options.icon.data.end());
            }
            if (!bytes.empty()) {
                pixels = detail::decode_image(bytes, 256);
            }
        } catch (const std::exception& failure) {
            log_warning("TrayIcon: {}", failure.what());
        }
        if (!pixels) {
            auto dot = std::make_shared<detail::DecodedImage>();
            dot->width = dot->height = 32;
            dot->rgba.resize(32 * 32 * 4);
            for (std::size_t y = 0; y < 32; ++y) {
                for (std::size_t x = 0; x < 32; ++x) {
                    const float d = std::hypot(static_cast<float>(x) - 15.5f, static_cast<float>(y) - 15.5f);
                    const auto alpha = static_cast<std::uint8_t>(std::clamp(13.f - d, 0.f, 1.f) * 255);
                    std::uint8_t* p = &dot->rgba[(y * 32 + x) * 4];
                    p[0] = 0x8A;
                    p[1] = 0xAD;
                    p[2] = 0xF4;
                    p[3] = alpha;
                }
            }
            pixels = dot;
        }
        icon = SDL_CreateSurfaceFrom(static_cast<int>(pixels->width), static_cast<int>(pixels->height),
                                     SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(pixels->rgba.data()),
                                     static_cast<int>(pixels->width * 4));
        tray = SDL_CreateTray(icon, options.tooltip.c_str());
        if (!tray) {
            log_warning("TrayIcon: {}", SDL_GetError());
            return;
        }
        rebuild_menu();
    }
};

TrayIcon::TrayIcon(TrayIconOptions options) : state_(std::make_shared<State>()) {
    require_app("TrayIcon");
    state_->options = std::move(options);
    state_->create();
}

void TrayIcon::set_tooltip(const std::string& tooltip) const {
    state_->options.tooltip = tooltip;
    if (state_->tray) {
        SDL_SetTrayTooltip(state_->tray, tooltip.c_str());
    }
}

void TrayIcon::set_menu(std::vector<TrayMenuItem> menu) const {
    state_->options.menu = std::move(menu);
    if (state_->tray) {
        state_->rebuild_menu();
    }
}

} // namespace elg
