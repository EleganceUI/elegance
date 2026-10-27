// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_EVENTS_H
#define ELG_MODIFIERS_EVENTS_H

// Built-in event modifiers. View::on_click(), on_pointer_enter/leave() and on_hover_change() are
// sugar over these.

#include <elg/events.h>
#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/prop.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace elg {

// Calls `action` when the view is pressed and released inside it with the options' button and
// click count. The innermost click handler under the pointer for that button wins; outer ones
// don't fire.
class ELG_API ClickModifier final : public EventModifier {
public:
    ClickModifier(std::function<void(const ClickEvent&)> action, ClickOptions options)
        : action_(std::move(action)), options_(options) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(const ClickEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
    ClickOptions options_;
};

// Calls `enter` as soon as the pointer comes over the view (its hit area) and `leave` as soon as it
// goes, with no delay.
class ELG_API PointerBoundaryModifier final : public EventModifier {
public:
    PointerBoundaryModifier(std::function<void()> enter, std::function<void()> leave)
        : enter_(std::move(enter)), leave_(std::move(leave)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void()> enter_;
    std::function<void()> leave_;
    ELG_PRIVATE_MEMBERS_END
};

// Hover intent: calls `action(true)` when the pointer has rested on the view for the hover delay
// and `action(false)` when it leaves (after the exit delay). `delay` overrides the environment's.
class ELG_API HoverModifier final : public EventModifier {
public:
    explicit HoverModifier(std::function<void(bool)> action, std::optional<HoverDelay> delay = std::nullopt)
        : action_(std::move(action)), delay_(delay) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(bool)> action_;
    std::optional<HoverDelay> delay_;
    ELG_PRIVATE_MEMBERS_END
};

// Calls `action` as the user pinches over the view (began ... ended). The innermost one under the
// pointer takes the gesture.
class ELG_API MagnifyGestureModifier final : public EventModifier {
public:
    explicit MagnifyGestureModifier(std::function<void(const MagnifyEvent&)> action) : action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(const MagnifyEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

// Calls `action` as the user turns two fingers over the view (began ... ended).
class ELG_API RotateGestureModifier final : public EventModifier {
public:
    explicit RotateGestureModifier(std::function<void(const RotateEvent&)> action) : action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(const RotateEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

// Calls `action` with the location on a two-finger double tap over the view (macOS's smart zoom).
class ELG_API SmartMagnifyModifier final : public EventModifier {
public:
    explicit SmartMagnifyModifier(std::function<void(Point)> action) : action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(Point)> action_;
    ELG_PRIVATE_MEMBERS_END
};

// The pointer's shape over the view (View::pointer_style()).
class ELG_API PointerStyleModifier final : public EventModifier {
public:
    explicit PointerStyleModifier(Prop<PointerStyle> style) : style_(std::move(style)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<PointerStyle> style_;
    ELG_PRIVATE_MEMBERS_END
};

// Lets the view take keyboard focus (Tab order, clicks), so key handlers on it receive keys.
class ELG_API FocusableModifier final : public EventModifier {
public:
    explicit FocusableModifier(bool focusable = true) noexcept : focusable_(focusable) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    bool focusable_;
};

// Calls `action` for key presses while the view (or something inside it) has focus. `action`
// returns true if it handled the key; unhandled keys bubble outwards.
class ELG_API KeyPressModifier final : public EventModifier {
public:
    explicit KeyPressModifier(std::function<bool(const KeyEvent&)> action) : action_(std::move(action)) {}
    std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<bool(const KeyEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_MODIFIERS_EVENTS_H
