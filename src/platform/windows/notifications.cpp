// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Notifications on Windows (elg/desktop.h): WinRT toasts, each tagged and with click handlers of
// its own (ELGSPEC-0018.R2), shown under the app's AppUserModelID (app_id.h).

#include "platform/sdl/native.h"
#include "platform/windows/app_id.h"

#include "core/log.h"
#include "core/ui_tasks.h"

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace elg::detail::platform::native {

namespace {

using ABI::Windows::Data::Xml::Dom::IXmlDocument;
using ABI::Windows::Data::Xml::Dom::IXmlDocumentIO;
using ABI::Windows::UI::Notifications::IToastDismissedEventArgs;
using ABI::Windows::UI::Notifications::IToastFailedEventArgs;
using ABI::Windows::UI::Notifications::IToastNotification;
using ABI::Windows::UI::Notifications::IToastNotification2;
using ABI::Windows::UI::Notifications::IToastNotificationFactory;
using ABI::Windows::UI::Notifications::IToastNotificationManagerStatics;
using ABI::Windows::UI::Notifications::IToastNotifier;
using ABI::Windows::UI::Notifications::ToastDismissalReason;
using Microsoft::WRL::ComPtr;

using ActivatedHandler = ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Notifications::ToastNotification*,
                                                                      IInspectable*>;
using DismissedHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::UI::Notifications::ToastNotification*, ABI::Windows::UI::Notifications::ToastDismissedEventArgs*>;
using FailedHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::UI::Notifications::ToastNotification*, ABI::Windows::UI::Notifications::ToastFailedEventArgs*>;

std::uint32_t code(HRESULT result) {
    return static_cast<std::uint32_t>(result);
}

std::wstring widen(const std::string& text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(static_cast<std::size_t>(n > 0 ? n : 0));
    return out;
}

// An HSTRING holding a copy of `text` (null, the empty string, if Windows can't make one).
class WinString {
public:
    explicit WinString(std::wstring_view text) {
        if (FAILED(WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &string_))) {
            string_ = nullptr;
        }
    }
    ~WinString() { WindowsDeleteString(string_); }
    WinString(const WinString&) = delete;
    WinString& operator=(const WinString&) = delete;

    HSTRING get() const { return string_; }

private:
    HSTRING string_ = nullptr;
};

// A WinRT event handler (Interface, an ITypedEventHandler with a toast as its sender) that calls
// `function` with the event's arguments, on whichever thread raises it: agile. Nothing may escape
// it (ELGSPEC-0002.R10).
template <class Interface, class Args>
class EventHandler final : public Interface {
public:
    static ComPtr<Interface> make(std::function<void(Args*)> function) {
        ComPtr<Interface> handler;
        handler.Attach(new EventHandler(std::move(function))); // its one reference
        return handler;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface) || iid == __uuidof(IAgileObject)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0) {
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IToastNotification*, Args* args) override {
        run_foreign_callback([&] { function_(args); });
        return S_OK;
    }

private:
    explicit EventHandler(std::function<void(Args*)> function) : function_(std::move(function)) {}
    virtual ~EventHandler() = default;

    std::atomic<ULONG> references_{1};
    std::function<void(Args*)> function_;
};

// `text` as XML character data. Control characters, which XML 1.0 can't hold, are left out.
std::wstring xml_text(const std::string& text) {
    std::wstring out;
    for (const wchar_t c : widen(text)) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default:
            if (c >= L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
                out += c;
            }
            break;
        }
    }
    return out;
}

// The toasts shown in this run, each with its click handler, by a token of their own. UI thread;
// the toasts' events, on other threads, only post their token to it.
class Toasts {
public:
    static Toasts& instance() {
        static Toasts toasts;
        return toasts;
    }

    void show(const Notification& notification) {
        if (!notifier()) {
            log_info("notification (not shown): {}: {}", notification.title, notification.body);
            return;
        }
        const std::uint64_t token = ++last_token_;
        Shown shown;
        shown.on_click = notification.on_click;
        const std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\"><text>" +
                                 xml_text(notification.title) + L"</text><text>" + xml_text(notification.body) +
                                 L"</text></binding></visual></toast>";
        const std::wstring tag = L"elegance-" + std::to_wstring(token);

        ComPtr<IInspectable> created;
        ComPtr<IXmlDocument> document;
        ComPtr<IXmlDocumentIO> loader;
        ComPtr<IToastNotificationFactory> factory;
        ComPtr<IToastNotification2> tagged;
        HRESULT result = RoActivateInstance(WinString(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).get(), &created);
        if (SUCCEEDED(result)) {
            result = created.As(&document);
        }
        if (SUCCEEDED(result)) {
            result = document.As(&loader);
        }
        if (SUCCEEDED(result)) {
            result = loader->LoadXml(WinString(xml).get());
        }
        if (SUCCEEDED(result)) {
            result = RoGetActivationFactory(WinString(RuntimeClass_Windows_UI_Notifications_ToastNotification).get(),
                                            IID_PPV_ARGS(&factory));
        }
        if (SUCCEEDED(result)) {
            result = factory->CreateToastNotification(document.Get(), &shown.toast);
        }
        if (SUCCEEDED(result)) {
            result = shown.toast.As(&tagged);
        }
        if (SUCCEEDED(result)) {
            result = tagged->put_Tag(WinString(tag).get());
        }
        if (SUCCEEDED(result)) {
            result = listen(token, shown);
        }
        if (SUCCEEDED(result)) {
            result = notifier_->Show(shown.toast.Get());
        }
        if (FAILED(result)) {
            stop_listening(shown);
            log_warning("notify: Windows didn't take the notification (HRESULT {:#010x})", code(result));
            return;
        }
        shown_.emplace(token, std::move(shown));
    }

    // The run ended: no toast calls anything after this, and the next run starts over.
    void forget() {
        for (auto& entry : shown_) {
            stop_listening(entry.second);
        }
        shown_.clear();
        notifier_.Reset();
        unavailable_ = false;
    }

private:
    struct Shown {
        ComPtr<IToastNotification> toast;
        EventRegistrationToken activated{};
        EventRegistrationToken dismissed{};
        EventRegistrationToken failed{};
        std::function<void()> on_click;
    };

    // The toast notifier for the app's id, made the first time; null if Windows has none for it.
    IToastNotifier* notifier() {
        if (notifier_ || unavailable_) {
            return notifier_.Get();
        }
        unavailable_ = true; // until it's made
        const std::wstring id = windows::app_user_model_id();
        ComPtr<IToastNotificationManagerStatics> manager;
        HRESULT result = RoGetActivationFactory(
            WinString(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).get(), IID_PPV_ARGS(&manager));
        if (SUCCEEDED(result)) {
            result = manager->CreateToastNotifierWithId(WinString(id).get(), &notifier_);
        }
        if (FAILED(result)) {
            log_warning("notify: Windows has no toast notifications for this app (HRESULT {:#010x})", code(result));
            notifier_.Reset();
            return nullptr;
        }
        unavailable_ = false;
        return notifier_.Get();
    }

    // Hears `shown`'s toast: a click runs its handler; once Windows takes it away (dismissed, or
    // failed to show), its handler goes. One that times out stays in the notification center,
    // where it can still be clicked. The events come on other threads, which post to this one.
    static HRESULT listen(std::uint64_t token, Shown& shown) {
        HRESULT result = shown.toast->add_Activated(
            EventHandler<ActivatedHandler, IInspectable>::make([token](IInspectable*) {
                post_ui_task([token] { instance().ended(token, true); });
            }).Get(),
            &shown.activated);
        if (SUCCEEDED(result)) {
            result = shown.toast->add_Dismissed(
                EventHandler<DismissedHandler, IToastDismissedEventArgs>::make([token](IToastDismissedEventArgs* args) {
                    ToastDismissalReason reason = ABI::Windows::UI::Notifications::ToastDismissalReason_TimedOut;
                    if (args && SUCCEEDED(args->get_Reason(&reason)) &&
                        reason != ABI::Windows::UI::Notifications::ToastDismissalReason_TimedOut) {
                        post_ui_task([token] { instance().ended(token, false); });
                    }
                }).Get(),
                &shown.dismissed);
        }
        if (SUCCEEDED(result)) {
            result = shown.toast->add_Failed(
                EventHandler<FailedHandler, IToastFailedEventArgs>::make([token](IToastFailedEventArgs* args) {
                    HRESULT error = E_FAIL;
                    if (args && FAILED(args->get_ErrorCode(&error))) {
                        error = E_FAIL;
                    }
                    post_ui_task([token, error] {
                        log_warning("notify: Windows didn't show the notification (HRESULT {:#010x})", code(error));
                        instance().ended(token, false);
                    });
                }).Get(),
                &shown.failed);
        }
        return result;
    }

    // Takes `shown`'s handlers off its toast. Windows may refuse once the toast has gone; then
    // they're never called anyway.
    static void stop_listening(Shown& shown) {
        if (!shown.toast) {
            return;
        }
        if (shown.activated.value != 0 && FAILED(shown.toast->remove_Activated(shown.activated))) {
            log_info("notify: a toast's click handler stayed registered");
        }
        if (shown.dismissed.value != 0 && FAILED(shown.toast->remove_Dismissed(shown.dismissed))) {
            log_info("notify: a toast's dismissal handler stayed registered");
        }
        if (shown.failed.value != 0 && FAILED(shown.toast->remove_Failed(shown.failed))) {
            log_info("notify: a toast's failure handler stayed registered");
        }
        shown.activated = {};
        shown.dismissed = {};
        shown.failed = {};
    }

    // `token`'s toast was clicked, or went away. Once: later events for it find nothing.
    void ended(std::uint64_t token, bool clicked) {
        const auto found = shown_.find(token);
        if (found == shown_.end()) {
            return; // ended already, or forgotten as its run ended
        }
        Shown shown = std::move(found->second);
        shown_.erase(found);
        stop_listening(shown);
        if (clicked && shown.on_click) {
            shown.on_click();
        }
    }

    ComPtr<IToastNotifier> notifier_;
    bool unavailable_ = false; // making the notifier failed in this run
    std::map<std::uint64_t, Shown> shown_;
    std::uint64_t last_token_ = 0;
};

} // namespace

void show_notification(const Notification& notification) {
    Toasts::instance().show(notification);
}

void forget_notification_handlers() {
    Toasts::instance().forget();
}

} // namespace elg::detail::platform::native
