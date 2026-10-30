// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Notifications on Windows (elg/desktop.h): a shell notification icon's balloon, which Windows 10
// and later show as a toast and keep in the notification center.

#include "platform/generic/native.h"

#include "core/log.h"
#include "core/ui_tasks.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>

namespace elg::detail::platform::native {

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

void show_notification(const Notification& notification) {
    Notifier::instance().show(notification);
}

} // namespace elg::detail::platform::native
