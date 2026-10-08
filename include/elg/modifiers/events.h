// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_EVENTS_H
#define ELG_MODIFIERS_EVENTS_H

// Built-in event modifiers. View::on_tap_gesture() and on_hover() are sugar over these.

#include <elg/events.h>
#include <elg/export.h>
#include <elg/modifier.h>

#include <functional>
#include <memory>
#include <utility>

namespace elg {

// Runs `action` when the view is pressed and released inside it. The innermost tap gesture
// under the pointer wins; outer ones don't fire.
class ELG_API TapGestureModifier final : public EventModifier {
public:
    TapGestureModifier(int count, std::function<void()> action) : count_(count), action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    int count_;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void()> action_;
    ELG_PRIVATE_MEMBERS_END
};

// Calls `action(true)` when the pointer enters the view and `action(false)` when it leaves.
class ELG_API HoverModifier final : public EventModifier {
public:
    explicit HoverModifier(std::function<void(bool)> action) : action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(bool)> action_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_MODIFIERS_EVENTS_H
