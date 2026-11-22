// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AccessKit (design.md §12): each window's tree, as AccessKit nodes, to the screen readers of its
// platform (ELGSPEC-0020.R1): UI Automation on Windows and NSAccessibility on macOS, whose
// subclassing adapters hook the window and call back on the UI thread, and AT-SPI on Linux, whose
// adapter talks to the bus on a thread of its own. The same nodes go to each (ELGSPEC-0020.R2). A build for Mikros has
// no AccessKit, so this file is only the stub there (ELGSPEC-0040.R14 isn't implemented).

#include "platform/accessibility_adapter.h"

#include "core/ui_tasks.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(ELG_HAVE_ACCESSKIT)
#if defined(_WIN32)
#include <windows.h>
#endif
#include <accesskit.h>
#endif

namespace elg::detail {

#if defined(ELG_HAVE_ACCESSKIT)

namespace {

accesskit_role role_of(AccessRole role) {
    switch (role) {
    case AccessRole::none:
    case AccessRole::group: return ACCESSKIT_ROLE_GROUP;
    case AccessRole::window: return ACCESSKIT_ROLE_WINDOW;
    case AccessRole::label: return ACCESSKIT_ROLE_LABEL;
    case AccessRole::heading: return ACCESSKIT_ROLE_HEADING;
    case AccessRole::button: return ACCESSKIT_ROLE_BUTTON;
    case AccessRole::link: return ACCESSKIT_ROLE_LINK;
    case AccessRole::toggle: return ACCESSKIT_ROLE_SWITCH;
    case AccessRole::checkbox: return ACCESSKIT_ROLE_CHECK_BOX;
    case AccessRole::slider: return ACCESSKIT_ROLE_SLIDER;
    case AccessRole::stepper: return ACCESSKIT_ROLE_SPIN_BUTTON;
    case AccessRole::progress: return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
    case AccessRole::text_field: return ACCESSKIT_ROLE_TEXT_INPUT;
    case AccessRole::text_area: return ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT;
    case AccessRole::password_field: return ACCESSKIT_ROLE_PASSWORD_INPUT;
    case AccessRole::image: return ACCESSKIT_ROLE_IMAGE;
    case AccessRole::video: return ACCESSKIT_ROLE_VIDEO;
    case AccessRole::list: return ACCESSKIT_ROLE_LIST;
    case AccessRole::list_item: return ACCESSKIT_ROLE_LIST_ITEM;
    case AccessRole::list_box: return ACCESSKIT_ROLE_LIST_BOX;
    case AccessRole::list_box_option: return ACCESSKIT_ROLE_LIST_BOX_OPTION;
    case AccessRole::scroll_view: return ACCESSKIT_ROLE_SCROLL_VIEW;
    case AccessRole::tab: return ACCESSKIT_ROLE_TAB;
    case AccessRole::menu: return ACCESSKIT_ROLE_MENU;
    case AccessRole::menu_item: return ACCESSKIT_ROLE_MENU_ITEM;
    case AccessRole::popup_button: return ACCESSKIT_ROLE_COMBO_BOX;
    case AccessRole::separator: return ACCESSKIT_ROLE_SPLITTER; // ARIA's separator (Chromium does the same)
    }
    return ACCESSKIT_ROLE_GROUP;
}

// What a screen reader asked for, copied for the UI thread; nothing for an action Elegance doesn't
// take. Frees `request`.
std::optional<AccessRequest> request_of(accesskit_action_request* request) {
    AccessRequest copy;
    copy.target = request->target_node;
    bool taken = true;
    switch (request->action) {
    case ACCESSKIT_ACTION_CLICK: copy.action.kind = AccessibilityAction::Kind::click; break;
    case ACCESSKIT_ACTION_FOCUS: copy.action.kind = AccessibilityAction::Kind::focus; break;
    case ACCESSKIT_ACTION_INCREMENT: copy.action.kind = AccessibilityAction::Kind::increment; break;
    case ACCESSKIT_ACTION_DECREMENT: copy.action.kind = AccessibilityAction::Kind::decrement; break;
    case ACCESSKIT_ACTION_SCROLL_INTO_VIEW: copy.action.kind = AccessibilityAction::Kind::scroll_into_view; break;
    case ACCESSKIT_ACTION_SET_VALUE:
        if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE) {
            copy.action.kind = AccessibilityAction::Kind::set_numeric;
            copy.action.number = request->data.value.numeric_value;
        } else if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_VALUE &&
                   request->data.value.value) {
            copy.action.kind = AccessibilityAction::Kind::set_value;
            copy.action.text = request->data.value.value;
        } else {
            taken = false;
        }
        break;
    default: taken = false; break;
    }
    accesskit_action_request_free(request);
    return taken ? std::optional(std::move(copy)) : std::nullopt;
}

} // namespace

struct AccessibilityAdapter::Impl {
    Callbacks callbacks;
#if defined(_WIN32)
    accesskit_windows_subclassing_adapter* adapter = nullptr;
#elif defined(__APPLE__)
    accesskit_macos_subclassing_adapter* adapter = nullptr;
#else
    accesskit_unix_adapter* adapter = nullptr;
    std::uintptr_t id = 0; // how its handlers, on AccessKit's thread, find it
#endif
    // A screen reader has the tree. On Linux the whole tree is then due: AccessKit asked for it
    // on its thread, and the UI thread sends it.
    bool active = false;
    bool whole_tree_due = false;

    // AccessKit's factories run synchronously on this (the UI) thread.
    // What AccessKit has, to send only what changed after it (rebuilding every node of a large
    // window each frame costs milliseconds).
    std::unordered_map<std::uint64_t, AccessNode> sent;
    std::uint64_t sent_focus = 0;
    double sent_scale = 0;
    // The nodes of the update being built (update()).
    std::vector<const AccessNode*> pending;
    std::uint64_t pending_focus = 0;

    // The whole tree: when a client first asks for it (Windows and macOS, on the UI thread).
    static accesskit_tree_update* build_update(void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        self->active = true;
        return self->whole_tree();
    }

    // The changes since the last update, or the whole tree when it's due.
    static accesskit_tree_update* build_next(void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        if (std::exchange(self->whole_tree_due, false)) {
            return self->whole_tree();
        }
        return self->convert(self->pending, self->pending_focus, false);
    }

    accesskit_tree_update* whole_tree() {
        const AccessTree& tree = callbacks.build();
        std::vector<const AccessNode*> nodes;
        nodes.reserve(tree.nodes.size());
        for (const AccessNode& node : tree.nodes) {
            nodes.push_back(&node);
        }
        remember(tree);
        return convert(nodes, tree.focus, true);
    }

    // Pixels a point: what the tree's bounds are multiplied by.
    [[nodiscard]] double scale() const { return callbacks.scale ? static_cast<double>(callbacks.scale()) : 1.0; }

    void remember(const AccessTree& tree) {
        sent.clear();
        for (const AccessNode& node : tree.nodes) {
            sent.emplace(node.id, node);
        }
        sent_focus = tree.focus;
        sent_scale = scale();
    }

    [[nodiscard]] accesskit_tree_update* convert(const std::vector<const AccessNode*>& nodes, std::uint64_t focus,
                                   bool whole_tree) const {
        const double k = scale();
        accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(nodes.size(), focus);
        if (whole_tree) {
            accesskit_tree_update_set_tree_info(update, accesskit_tree_info_new(AccessTree::kRoot));
        }
        for (const AccessNode* node_source : nodes) {
            const AccessNode& source = *node_source;
            const AccessibilityInfo& info = source.info;
            accesskit_node* node = accesskit_node_new(role_of(info.role));
            const auto px = [k](float points) { return static_cast<double>(points) * k; };
            accesskit_node_set_bounds(node, accesskit_rect{px(source.bounds.x), px(source.bounds.y),
                                                           px(source.bounds.max_x()), px(source.bounds.max_y())});
            if (info.role == AccessRole::label) {
                accesskit_node_set_value(node, info.label.c_str()); // static text is a label's value
            } else if (!info.label.empty()) {
                accesskit_node_set_label(node, info.label.c_str());
            }
            const bool text_field = info.role == AccessRole::text_field || info.role == AccessRole::text_area ||
                                    info.role == AccessRole::password_field;
            if (!info.value.empty() || text_field) { // fields always have a value, even an empty one
                accesskit_node_set_value(node, info.value.c_str());
            }
            if (!info.hint.empty()) {
                accesskit_node_set_description(node, info.hint.c_str());
            }
            if (!info.placeholder.empty()) {
                accesskit_node_set_placeholder(node, info.placeholder.c_str());
            }
            if (info.disabled) {
                accesskit_node_set_disabled(node);
            }
            if (info.toggled) {
                accesskit_node_set_toggled(node, *info.toggled == ToggleState::mixed ? ACCESSKIT_TOGGLED_MIXED
                                                 : *info.toggled == ToggleState::on  ? ACCESSKIT_TOGGLED_TRUE
                                                                                     : ACCESSKIT_TOGGLED_FALSE);
            }
            if (info.expanded) {
                accesskit_node_set_expanded(node, *info.expanded);
            }
            if (info.selected) {
                accesskit_node_set_selected(node, true);
            } else if (info.role == AccessRole::list_box_option) {
                accesskit_node_set_selected(node, false); // selectable, but not selected
            }
            if (info.multiselectable) {
                accesskit_node_set_multiselectable(node);
            }
            if (info.numeric) {
                accesskit_node_set_numeric_value(node, *info.numeric);
            }
            if (info.minimum) {
                accesskit_node_set_min_numeric_value(node, *info.minimum);
            }
            if (info.maximum) {
                accesskit_node_set_max_numeric_value(node, *info.maximum);
            }
            if (info.step) {
                accesskit_node_set_numeric_value_step(node, *info.step);
            }
            if (info.can_click) {
                accesskit_node_add_action(node, ACCESSKIT_ACTION_CLICK);
            }
            if (info.can_focus) {
                accesskit_node_add_action(node, ACCESSKIT_ACTION_FOCUS);
            }
            if (info.can_adjust) {
                accesskit_node_add_action(node, ACCESSKIT_ACTION_INCREMENT);
                accesskit_node_add_action(node, ACCESSKIT_ACTION_DECREMENT);
            }
            if (info.can_set_value) {
                accesskit_node_add_action(node, ACCESSKIT_ACTION_SET_VALUE);
            }
            if (source.id != AccessTree::kRoot) {
                accesskit_node_add_action(node, ACCESSKIT_ACTION_SCROLL_INTO_VIEW);
            }
            for (const std::uint64_t child : source.children) {
                accesskit_node_push_child(node, child);
            }
            accesskit_tree_update_push_node(update, source.id, node);
        }
        return update;
    }

    // A screen reader's request (Windows and macOS: on the UI thread), handled on the UI thread
    // through a task: the window's callbacks find the window, or nothing once it's gone.
    static void on_action(accesskit_action_request* request, void* userdata) {
        run_foreign_callback([&] {
            const auto* self = static_cast<const Impl*>(userdata);
            if (std::optional<AccessRequest> copy = request_of(request)) {
                post_ui_task([handler = self->callbacks.request, copy = std::move(*copy)] {
                    if (handler) {
                        handler(copy);
                    }
                });
            }
        });
    }

    // Sends what changed since the last update, or the whole tree when it's due. UI thread.
    void update() {
        if (!adapter || !active) {
            return;
        }
        const AccessTree* tree = nullptr;
        if (!whole_tree_due) {
            tree = &callbacks.build();
            const double now_scale = scale();
            pending.clear();
            for (const AccessNode& node : tree->nodes) {
                const auto it = sent.find(node.id);
                if (now_scale != sent_scale || it == sent.end() || !(it->second == node)) {
                    pending.push_back(&node);
                }
            }
            if (pending.empty() && tree->focus == sent_focus) {
                return;
            }
            pending_focus = tree->focus;
        }
#if defined(_WIN32)
        if (accesskit_windows_queued_events* events =
                accesskit_windows_subclassing_adapter_update_if_active(adapter, build_next, this)) {
            accesskit_windows_queued_events_raise(events);
        }
#elif defined(__APPLE__)
        if (accesskit_macos_queued_events* events =
                accesskit_macos_subclassing_adapter_update_if_active(adapter, build_next, this)) {
            accesskit_macos_queued_events_raise(events);
        }
#else
        accesskit_unix_adapter_update_if_active(adapter, build_next, this);
#endif
        pending.clear();
        if (tree) {
            remember(*tree);
        }
    }
};

#if !defined(_WIN32) && !defined(__APPLE__)

namespace {

// The adapters, by the id their AT-SPI handlers get: those run on AccessKit's thread, and may come
// as an adapter goes, so they find it here (under the mutex), or nothing. UI thread otherwise.
std::mutex g_unix_mutex;
std::unordered_map<std::uintptr_t, AccessibilityAdapter::Impl*> g_unix_adapters;
std::uintptr_t g_unix_next = 1;

AccessibilityAdapter::Impl* unix_adapter(std::uintptr_t id) {
    const std::scoped_lock lock(g_unix_mutex);
    const auto it = g_unix_adapters.find(id);
    return it != g_unix_adapters.end() ? it->second : nullptr;
}

std::uintptr_t unix_id(void* userdata) {
    return reinterpret_cast<std::uintptr_t>(userdata); // NOLINT(performance-no-int-to-ptr): an id, not a pointer
}

// A screen reader listens: the whole tree goes to it from the UI thread.
accesskit_tree_update* unix_activated(void* userdata) {
    run_foreign_callback([id = unix_id(userdata)] {
        post_ui_task([id] {
            if (AccessibilityAdapter::Impl* impl = unix_adapter(id)) {
                impl->active = true;
                impl->whole_tree_due = true;
                impl->update();
            }
        });
    });
    return nullptr; // none yet: AccessKit waits for update_if_active()
}

void unix_deactivated(void* userdata) {
    run_foreign_callback([id = unix_id(userdata)] {
        post_ui_task([id] {
            if (AccessibilityAdapter::Impl* impl = unix_adapter(id)) {
                impl->active = false;
                impl->sent.clear();
            }
        });
    });
}

void unix_action(accesskit_action_request* request, void* userdata) {
    run_foreign_callback([&] {
        std::optional<AccessRequest> copy = request_of(request);
        if (!copy) {
            return;
        }
        std::function<void(const AccessRequest&)> handler;
        {
            const std::scoped_lock lock(g_unix_mutex);
            if (const auto it = g_unix_adapters.find(unix_id(userdata)); it != g_unix_adapters.end()) {
                handler = it->second->callbacks.request;
            }
        }
        if (handler) {
            post_ui_task([handler = std::move(handler), copy = std::move(*copy)] { handler(copy); });
        }
    });
}

} // namespace

#endif

AccessibilityAdapter::AccessibilityAdapter(void* native_window, const Callbacks& callbacks)
    : impl_(std::make_unique<Impl>()) {
    impl_->callbacks = callbacks;
#if defined(_WIN32)
    if (!native_window) {
        return;
    }
    impl_->adapter = accesskit_windows_subclassing_adapter_new(static_cast<HWND>(native_window), Impl::build_update,
                                                               impl_.get(), Impl::on_action, impl_.get());
#elif defined(__APPLE__)
    if (!native_window) {
        return;
    }
    // SDL keeps the keyboard focus on its window, not on the view AccessKit subclasses.
    static std::once_flag forwarder;
    std::call_once(forwarder, [] { accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window"); });
    impl_->adapter = accesskit_macos_subclassing_adapter_for_window(native_window, Impl::build_update, impl_.get(),
                                                                    Impl::on_action, impl_.get());
#else
    (void)native_window;
    {
        const std::scoped_lock lock(g_unix_mutex);
        impl_->id = g_unix_next++;
        g_unix_adapters.emplace(impl_->id, impl_.get());
    }
    // Connects to the AT-SPI bus on AccessKit's thread: without one, nothing here waits for it
    // (ELGSPEC-0020.R4).
    void* const userdata = reinterpret_cast<void*>(impl_->id); // NOLINT(performance-no-int-to-ptr): an id
    impl_->adapter = accesskit_unix_adapter_new(unix_activated, userdata, unix_action, userdata, unix_deactivated,
                                                userdata);
#endif
}

AccessibilityAdapter::~AccessibilityAdapter() {
    if (!impl_->adapter) {
        return;
    }
#if defined(_WIN32)
    accesskit_windows_subclassing_adapter_free(impl_->adapter);
#elif defined(__APPLE__)
    accesskit_macos_subclassing_adapter_free(impl_->adapter);
#else
    {
        const std::scoped_lock lock(g_unix_mutex);
        g_unix_adapters.erase(impl_->id);
    }
    accesskit_unix_adapter_free(impl_->adapter);
#endif
}

void AccessibilityAdapter::update() {
    impl_->update();
}

void AccessibilityAdapter::window_focused(bool focused) {
    if (!impl_->adapter) {
        return;
    }
#if defined(__APPLE__)
    if (accesskit_macos_queued_events* events =
            accesskit_macos_subclassing_adapter_update_view_focus_state(impl_->adapter, focused)) {
        accesskit_macos_queued_events_raise(events);
    }
#elif !defined(_WIN32)
    accesskit_unix_adapter_update_window_focus_state(impl_->adapter, focused);
#else
    (void)focused; // the subclassing adapter follows the window's focus itself
#endif
}

#else

struct AccessibilityAdapter::Impl {};

AccessibilityAdapter::AccessibilityAdapter(void*, const Callbacks&) {}
AccessibilityAdapter::~AccessibilityAdapter() = default;
void AccessibilityAdapter::update() {}
void AccessibilityAdapter::window_focused(bool) {}

#endif

} // namespace elg::detail
