// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/system.h"

#include "core/log.h"

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

// These bring in <windows.h> (the build defines WIN32_LEAN_AND_MEAN and NOMINMAX).
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::gpu {

namespace {

// Dawn's Direct3D 12 backend compiles shaders with the DirectX shader compiler, which it loads
// from beside the module Dawn is in (webgpu_dawn.dll, or the app itself when it links Dawn
// statically) or from System32. If it isn't there, creating the adapter fails instead of falling
// back to the older FXC, so ask for FXC up front.
bool dxc_available() {
    std::vector<std::filesystem::path> folders;
    HMODULE dawn = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&wgpuCreateInstance), &dawn)) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(dawn, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            folders.push_back(std::filesystem::path(path).parent_path());
        }
    }
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        folders.emplace_back(system);
    }
    std::error_code error;
    return std::ranges::any_of(folders, [&](const std::filesystem::path& folder) {
        return std::filesystem::exists(folder / L"dxcompiler.dll", error) &&
               std::filesystem::exists(folder / L"dxil.dll", error);
    });
}

} // namespace

wgpu::Instance create_instance(const wgpu::InstanceDescriptor& descriptor) {
    // Dawn looks for d3dcompiler_47.dll (Direct3D 11) and vulkan-1.dll next to itself, then by
    // bare name. That second try uses LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, which needs an absolute
    // path, so it always fails (ERROR_INVALID_PARAMETER) and the system's copies are never found.
    // Name System32 as a search path instead (Dawn appends the file name).
    char system[MAX_PATH];
    const UINT length = GetSystemDirectoryA(system, MAX_PATH);
    std::string system_dir = length > 0 && length < MAX_PATH ? std::string(system, length) + '\\' : std::string();
    std::string_view search_paths[] = {system_dir};
    dawn::native::DawnInstanceDescriptor dawn_descriptor;
    dawn_descriptor.additionalRuntimeSearchPaths = std::span(search_paths, system_dir.empty() ? 0 : 1);
    wgpu::InstanceDescriptor chained = descriptor;
    chained.nextInChain = &dawn_descriptor;
    return wgpu::CreateInstance(&chained);
}

std::span<const char* const> disabled_adapter_toggles() {
    static const char* const no_dxc[] = {"use_dxc"};
    if (dxc_available()) {
        return {};
    }
    log_info("dxcompiler.dll and dxil.dll aren't beside the app: Direct3D 12 compiles shaders with FXC (slower)");
    return no_dxc;
}

// WebGPU doesn't report the adapter's LUID; DXGI does, for the adapter with the same IDs.
std::optional<std::array<std::uint8_t, 8>> adapter_luid(std::uint32_t vendor, std::uint32_t device) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == vendor && desc.DeviceId == device) {
            std::array<std::uint8_t, 8> luid{};
            static_assert(sizeof(desc.AdapterLuid) == luid.size());
            std::memcpy(luid.data(), &desc.AdapterLuid, luid.size());
            return luid;
        }
    }
    return std::nullopt;
}

} // namespace elg::detail::gpu
