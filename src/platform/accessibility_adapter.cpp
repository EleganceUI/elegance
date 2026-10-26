// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AccessKit (design.md §12): our tree, as AccessKit nodes, to UI Automation (and later AT-SPI and
// NSAccessibility). On Windows the subclassing adapter hooks the window's WM_GETOBJECT.

#include "platform/accessibility_adapter.h"

#include "core/log.h"
#include "core/ui_tasks.h"

#include <atomic>
#include <unordered_map>
#include <vector>

#if defined(ELG_HAVE_ACCESSKIT)
#if defined(_WIN32)
#include <windows.h>
#endif
#include <accesskit.h>
#endif

namespace elg::detail {

#if defined(ELG_HAVE_ACCESSKIT) && defined(_WIN32)

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
    case AccessRole::scroll_view: return ACCESSKIT_ROLE_SCROLL_VIEW;
    case AccessRole::tab: return ACCESSKIT_ROLE_TAB;
    case AccessRole::menu: return ACCESSKIT_ROLE_MENU;
    case AccessRole::menu_item: return ACCESSKIT_ROLE_MENU_ITEM;
    case AccessRole::popup_button: return ACCESSKIT_ROLE_COMBO_BOX;
    }
    return ACCESSKIT_ROLE_GROUP;
}

} // namespace

struct AccessibilityAdapter::Impl {
    Callbacks callbacks;
    accesskit_windows_subclassing_adapter* adapter = nullptr;
    std::atomic<bool> active{false};

    // AccessKit's factories run synchronously on this (the UI) thread.
    // What AccessKit has, to send only what changed after it (rebuilding every node of a large
    // window each frame costs milliseconds).
    std::unordered_map<std::uint64_t, AccessNode> sent;
    std::uint64_t sent_focus = 0;
    double sent_scale = 0;
    // The nodes of the update being built (update()).
    std::vector<const AccessNode*> pending;
    std::uint64_t pending_focus = 0;

    // The whole tree: when a client first asks for it.
    static accesskit_tree_update* build_update(void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        self->active = true;
        const AccessTree& tree = self->callbacks.build();
        std::vector<const AccessNode*> nodes;
        nodes.reserve(tree.nodes.size());
        for (const AccessNode& node : tree.nodes) {
            nodes.push_back(&node);
        }
        self->remember(tree);
        return self->convert(nodes, tree.focus, true);
    }

    // The changes since the last update.
    static accesskit_tree_update* build_changes(void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        return self->convert(self->pending, self->pending_focus, false);
    }

    void remember(const AccessTree& tree) {
        sent.clear();
        for (const AccessNode& node : tree.nodes) {
            sent.emplace(node.id, node);
        }
        sent_focus = tree.focus;
        sent_scale = callbacks.scale ? callbacks.scale() : 1.0;
    }

    accesskit_tree_update* convert(const std::vector<const AccessNode*>& nodes, std::uint64_t focus,
                                   bool whole_tree) const {
        const double k = callbacks.scale ? callbacks.scale() : 1.0;
        accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(nodes.size(), focus);
        if (whole_tree) {
            accesskit_tree_update_set_tree_info(update, accesskit_tree_info_new(AccessTree::kRoot));
        }
        for (const AccessNode* node_source : nodes) {
            const AccessNode& source = *node_source;
            const AccessibilityInfo& info = source.info;
            accesskit_node* node = accesskit_node_new(role_of(info.role));
            accesskit_node_set_bounds(node, accesskit_rect{source.bounds.x * k, source.bounds.y * k,
                                                           source.bounds.max_x() * k, source.bounds.max_y() * k});
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

    // On a screen reader's thread: copied, then handled on the UI thread.
    static void on_action(accesskit_action_request* request, void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        AccessRequest copy;
        copy.target = request->target_node;
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
                accesskit_action_request_free(request);
                return;
            }
            break;
        default: accesskit_action_request_free(request); return;
        }
        accesskit_action_request_free(request);
        // The adapter (and so `self`) lives on the UI thread; the task checks it's still there by
        // going through the window's callbacks, which the App replaces when the window goes.
        auto handler = self->callbacks.request;
        post_ui_task([handler, copy] {
            if (handler) {
                handler(copy);
            }
        });
    }
};

AccessibilityAdapter::AccessibilityAdapter(void* native_window, Callbacks callbacks) : impl_(std::make_unique<Impl>()) {
    impl_->callbacks = std::move(callbacks);
    impl_->adapter = accesskit_windows_subclassing_adapter_new(static_cast<HWND>(native_window), Impl::build_update,
                                                               impl_.get(), Impl::on_action, impl_.get());
}

AccessibilityAdapter::~AccessibilityAdapter() {
    if (impl_->adapter) {
        accesskit_windows_subclassing_adapter_free(impl_->adapter);
    }
}

void AccessibilityAdapter::update() {
    // Nothing until a client asks for the tree (build_update); then only what changed.
    if (!impl_->adapter || !impl_->active) {
        return;
    }
    Impl& impl = *impl_;
    const AccessTree& tree = impl.callbacks.build();
    const double scale = impl.callbacks.scale ? impl.callbacks.scale() : 1.0;
    impl.pending.clear();
    for (const AccessNode& node : tree.nodes) {
        const auto it = impl.sent.find(node.id);
        if (scale != impl.sent_scale || it == impl.sent.end() || !(it->second == node)) {
            impl.pending.push_back(&node);
        }
    }
    if (impl.pending.empty() && tree.focus == impl.sent_focus) {
        return;
    }
    impl.pending_focus = tree.focus;
    if (accesskit_windows_queued_events* events =
            accesskit_windows_subclassing_adapter_update_if_active(impl.adapter, Impl::build_changes, impl_.get())) {
        accesskit_windows_queued_events_raise(events);
    }
    impl.pending.clear();
    impl.remember(tree);
}

bool AccessibilityAdapter::active() const noexcept {
    return impl_->active;
}

#else

struct AccessibilityAdapter::Impl {};

AccessibilityAdapter::AccessibilityAdapter(void*, Callbacks) {}
AccessibilityAdapter::~AccessibilityAdapter() = default;
void AccessibilityAdapter::update() {}
bool AccessibilityAdapter::active() const noexcept {
    return false;
}

#endif

} // namespace elg::detail
