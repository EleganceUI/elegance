// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/windows/app_id.h"

#include "core/log.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace elg::detail::platform::windows {

namespace {

using Microsoft::WRL::ComPtr;

// PKEY_AppUserModel_ID (propkey.h), which is only defined in a file that includes initguid.h.
constexpr PROPERTYKEY kAppUserModelId{{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

std::uint32_t code(HRESULT result) {
    return static_cast<std::uint32_t>(result);
}

// A path for the log, in UTF-8 (path::string() throws for characters outside the code page).
std::string utf8(const std::filesystem::path& path) {
    const std::wstring& wide = path.native();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                      nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::filesystem::path executable() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// The AppUserModelID the app set for its process, or empty.
std::wstring own_app_id() {
    PWSTR id = nullptr;
    std::wstring result;
    if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&id)) && id) {
        result = id;
    }
    CoTaskMemFree(id);
    return result;
}

// An AppUserModelID made from the executable's name: its ASCII letters and digits.
std::wstring app_id_from(const std::filesystem::path& exe) {
    std::wstring name;
    for (const wchar_t c : exe.stem().wstring()) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            name += c;
        }
    }
    return L"Elegance.App." + (name.empty() ? std::wstring(L"Unnamed") : name.substr(0, 64));
}

// Makes sure the person's Start menu has a shortcut to `exe` carrying `id` (app_user_model_id()).
void install_shortcut(const std::filesystem::path& exe, const std::wstring& id) {
    PWSTR programs = nullptr;
    const HRESULT found = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programs);
    const std::filesystem::path folder = SUCCEEDED(found) && programs ? programs : L"";
    CoTaskMemFree(programs);
    if (folder.empty()) {
        log_warning("notify: no Start menu folder for the app's shortcut (HRESULT {:#010x})", code(found));
        return;
    }
    const std::filesystem::path link = folder / (exe.stem().wstring() + L".lnk");

    ComPtr<IShellLinkW> shortcut;
    ComPtr<IPersistFile> file;
    ComPtr<IPropertyStore> properties;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shortcut));
    if (SUCCEEDED(result)) {
        result = shortcut.As(&file);
    }
    if (SUCCEEDED(result)) {
        result = shortcut.As(&properties);
    }
    if (FAILED(result)) {
        log_warning("notify: can't make the app's Start menu shortcut (HRESULT {:#010x})", code(result));
        return;
    }

    std::error_code ignored;
    if (std::filesystem::exists(link, ignored) && SUCCEEDED(file->Load(link.c_str(), STGM_READ))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        const bool same_id = SUCCEEDED(properties->GetValue(kAppUserModelId, &value)) && value.vt == VT_LPWSTR &&
                             value.pwszVal && id == value.pwszVal;
        PropVariantClear(&value);
        wchar_t target[MAX_PATH] = L"";
        const bool ours = SUCCEEDED(shortcut->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)) &&
                          CompareStringOrdinal(target, -1, exe.c_str(), -1, TRUE) == CSTR_EQUAL;
        if (same_id && ours) {
            return;
        }
        if (!ours) {
            log_warning("notify: the Start menu shortcut {} is another program's; Windows may not show this app's "
                        "notifications",
                        utf8(link));
            return;
        }
    }

    PROPVARIANT value;
    result = InitPropVariantFromString(id.c_str(), &value);
    if (SUCCEEDED(result)) {
        result = shortcut->SetPath(exe.c_str());
        if (SUCCEEDED(result)) {
            result = shortcut->SetArguments(L"");
        }
        if (SUCCEEDED(result)) {
            result = shortcut->SetWorkingDirectory(exe.parent_path().c_str());
        }
        if (SUCCEEDED(result)) {
            result = properties->SetValue(kAppUserModelId, value);
        }
        if (SUCCEEDED(result)) {
            result = properties->Commit();
        }
        if (SUCCEEDED(result)) {
            result = file->Save(link.c_str(), TRUE);
        }
        PropVariantClear(&value);
    }
    if (FAILED(result)) {
        log_warning("notify: can't save the app's Start menu shortcut {} (HRESULT {:#010x})", utf8(link),
                    code(result));
    }
}

} // namespace

std::wstring app_user_model_id() {
    if (std::wstring id = own_app_id(); !id.empty()) {
        return id;
    }
    const std::filesystem::path exe = executable();
    std::wstring id = app_id_from(exe);
    if (!exe.empty()) {
        install_shortcut(exe, id); // without it Windows may drop the toasts: they're still tried
    }
    return id;
}

} // namespace elg::detail::platform::windows
