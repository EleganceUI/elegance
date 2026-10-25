// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4): SDL's file dialogs and tray; notifications through the
// Windows shell (SDL has none).

#include <elg/desktop.h>

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/image_decode.h"

#include <elg/error.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <format>
#include <fstream>
#include <list>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

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

#if defined(_WIN32)

namespace {

constexpr UINT kNotifyMessage = WM_APP + 0x4E;

// One shell notification icon, added while a notification shows. UI thread.
class Notifier {
public:
    static Notifier& instance() {
        static Notifier notifier;
        return notifier;
    }

    void show(const Notification& notification) {
        if (!window_) {
            create_window();
        }
        on_click_ = notification.on_click;
        NOTIFYICONDATAW data = base();
        data.uFlags |= NIF_INFO;
        copy(data.szInfoTitle, notification.title);
        copy(data.szInfo, notification.body);
        data.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
        data.hBalloonIcon = icon();
        if (!added_) {
            added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
            NOTIFYICONDATAW version = base();
            version.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &version);
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
        if (!added_) {
            log_warning("notify: the shell refused the notification");
        }
    }

    ~Notifier() {
        remove();
        if (window_) {
            DestroyWindow(window_);
        }
    }

private:
    template <std::size_t N>
    static void copy(wchar_t (&out)[N], const std::string& text) {
        std::wstring wide(text.size(), L'\0');
        const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                          static_cast<int>(wide.size()));
        wide.resize(static_cast<std::size_t>(std::max(n, 0)));
        wcsncpy_s(out, wide.c_str(), _TRUNCATE);
    }

    // The app's own icon (its exe's), else the system's.
    static HICON icon() {
        static const HICON handle = [] {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            HICON large = nullptr;
            ExtractIconExW(path, 0, &large, nullptr, 1);
            return large ? large : LoadIconW(nullptr, MAKEINTRESOURCEW(32512)) /* IDI_APPLICATION */;
        }();
        return handle;
    }

    NOTIFYICONDATAW base() const {
        NOTIFYICONDATAW data{sizeof(NOTIFYICONDATAW)};
        data.hWnd = window_;
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        data.uCallbackMessage = kNotifyMessage;
        data.hIcon = icon();
        wchar_t title[128] = L"";
        GetModuleFileNameW(nullptr, title, 128);
        const wchar_t* name = wcsrchr(title, L'\\');
        wcsncpy_s(data.szTip, name ? name + 1 : title, _TRUNCATE);
        return data;
    }

    void remove() {
        if (added_) {
            NOTIFYICONDATAW data = base();
            Shell_NotifyIconW(NIM_DELETE, &data);
            added_ = false;
        }
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == kNotifyMessage) {
            Notifier& self = instance();
            switch (LOWORD(lparam)) {
            case NIN_BALLOONUSERCLICK:
                if (auto click = self.on_click_) {
                    detail::post_ui_task(std::move(click));
                }
                self.remove();
                break;
            case NIN_BALLOONTIMEOUT:
            case NIN_BALLOONHIDE: self.remove(); break;
            default: break;
            }
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void create_window() {
        WNDCLASSW type{};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EleganceNotifier";
        RegisterClassW(&type);
        // A message-only window: it receives the icon's messages and is never shown.
        window_ = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    }

    HWND window_ = nullptr;
    bool added_ = false;
    std::function<void()> on_click_;
};

} // namespace

void notify(Notification notification) {
    require_app("notify");
    Notifier::instance().show(notification);
}

#else

void notify(Notification notification) {
    require_app("notify");
    log_info("notification (not shown: no backend on this platform yet): {}: {}", notification.title, notification.body);
}

#endif

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
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    const float d = std::hypot(x - 15.5f, y - 15.5f);
                    const auto alpha = static_cast<std::uint8_t>(std::clamp(13.f - d, 0.f, 1.f) * 255);
                    std::uint8_t* p = &dot->rgba[(y * 32 + x) * 4];
                    p[0] = 0x8A, p[1] = 0xAD, p[2] = 0xF4, p[3] = alpha;
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
