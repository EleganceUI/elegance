// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_GENERIC_ACCESSIBILITY_ADAPTER_H
#define ELG_PLATFORM_GENERIC_ACCESSIBILITY_ADAPTER_H

#include "view/accessibility.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace elg::detail {

// A request from assistive technology, for the UI thread.
struct AccessRequest {
    std::uint64_t target = 0;
    AccessibilityAction action;
};

// Connects one window to the platform's accessibility API (design.md §12) through AccessKit.
// It stays dormant, costing nothing, until assistive technology asks for the window's tree; from
// then on update() sends the tree after each frame. Without AccessKit in the build it does nothing.
class AccessibilityAdapter {
public:
    struct Callbacks {
        // Builds the tree now (UI thread), valid until the next build. The scale converts window
        // points to pixels.
        std::function<const AccessTree&()> build;
        std::function<float()> scale;
        // A request arrived; called on the UI thread (through post_ui_task).
        std::function<void(const AccessRequest&)> request;
    };

    // `native_window`: the HWND on Windows. UI thread.
    AccessibilityAdapter(void* native_window, Callbacks callbacks);
    ~AccessibilityAdapter();

    AccessibilityAdapter(const AccessibilityAdapter&) = delete;
    AccessibilityAdapter& operator=(const AccessibilityAdapter&) = delete;

    // Sends the current tree if assistive technology is listening. UI thread.
    void update();
    bool active() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace elg::detail

#endif // ELG_PLATFORM_GENERIC_ACCESSIBILITY_ADAPTER_H
