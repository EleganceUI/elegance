// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_EVENTS_H
#define ELG_MODIFIERS_EVENTS_H

// Built-in event modifiers. View::on_click(), on_pointer_enter/leave(), on_hover_change(),
// on_pointer(), on_scroll(), on_key(), on_text_input() and recognize() are sugar over these.

#include <elg/events.h>
#include <elg/export.h>
#include <elg/gestures.h>
#include <elg/modifier.h>
#include <elg/prop.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace elg {

/// Calls `action` when the view is pressed and released inside it with the options' button and
/// click count. The innermost click handler under the pointer for that button wins; outer ones
/// don't fire.
class ELG_API ClickModifier final : public EventModifier {
public:
    /// Calls `action` on clicks that match `options`.
    ///
    /// @param action Called with the click, located in the view's own coordinates. May be empty.
    /// @param options The button and click count.
    ClickModifier(std::function<void(const ClickEvent&)> action, ClickOptions options)
        : action_(std::move(action)), options_(options) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls the action on matching clicks.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(const ClickEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
    ClickOptions options_;
};

/// Calls `enter` as soon as the pointer comes over the view (its hit area) and `leave` as soon as it
/// goes, with no delay.
class ELG_API PointerBoundaryModifier final : public EventModifier {
public:
    /// Calls `enter` and `leave` as the pointer comes and goes.
    ///
    /// @param enter Called when the pointer comes over the view. May be empty.
    /// @param leave Called when it leaves. May be empty.
    PointerBoundaryModifier(std::function<void()> enter, std::function<void()> leave)
        : enter_(std::move(enter)), leave_(std::move(leave)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls `enter` and `leave`, ignoring the hover delay.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void()> enter_;
    std::function<void()> leave_;
    ELG_PRIVATE_MEMBERS_END
};

/// Hover intent: calls `action(true)` when the pointer has rested on the view for the hover delay
/// and `action(false)` when it leaves (after the exit delay), both from the environment's
/// HoverDelay (.hover_delay()).
class ELG_API HoverModifier final : public EventModifier {
public:
    /// Calls `action` with hover changes.
    ///
    /// @param action Called with whether the view is hovered. May be empty.
    explicit HoverModifier(std::function<void(bool)> action) : action_(std::move(action)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls the action after the environment's hover delay.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<void(bool)> action_;
    ELG_PRIVATE_MEMBERS_END
};

// The gestures View::recognize() attaches (elg/gestures.h).

/// Recognizes a drag over the view (View::recognize(DragGesture, GestureOptions)).
class ELG_API DragGestureModifier final : public EventModifier {
public:
    /// Recognizes `gesture`.
    ///
    /// @param gesture The drag and its handlers.
    /// @param options Its priority over gestures inside the view, or whether it runs alongside.
    explicit DragGestureModifier(DragGesture gesture, GestureOptions options = {})
        : gesture_(std::move(gesture)), options_(options) {}
    /// Called once per mounted view.
    ///
    /// @return A recognizer for the drag.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    DragGesture gesture_;
    ELG_PRIVATE_MEMBERS_END
    GestureOptions options_;
};

/// Recognizes a long press on the view (View::recognize(LongPressGesture, GestureOptions)).
class ELG_API LongPressGestureModifier final : public EventModifier {
public:
    /// Recognizes `gesture`.
    ///
    /// @param gesture The long press and its handler.
    /// @param options Its priority over gestures inside the view, or whether it runs alongside.
    explicit LongPressGestureModifier(LongPressGesture gesture, GestureOptions options = {})
        : gesture_(std::move(gesture)), options_(options) {}
    /// Called once per mounted view.
    ///
    /// @return A recognizer for the long press.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    LongPressGesture gesture_;
    ELG_PRIVATE_MEMBERS_END
    GestureOptions options_;
};

/// Recognizes a trackpad pinch over the view (View::recognize(MagnifyGesture)).
class ELG_API MagnifyGestureModifier final : public EventModifier {
public:
    /// Recognizes `gesture`.
    ///
    /// @param gesture The pinch and its handlers.
    explicit MagnifyGestureModifier(MagnifyGesture gesture) : gesture_(std::move(gesture)) {}
    /// Called once per mounted view.
    ///
    /// @return A recognizer for the pinch.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    MagnifyGesture gesture_;
    ELG_PRIVATE_MEMBERS_END
};

/// Recognizes a trackpad two-finger rotation over the view (View::recognize(RotateGesture)).
class ELG_API RotateGestureModifier final : public EventModifier {
public:
    /// Recognizes `gesture`.
    ///
    /// @param gesture The rotation and its handlers.
    explicit RotateGestureModifier(RotateGesture gesture) : gesture_(std::move(gesture)) {}
    /// Called once per mounted view.
    ///
    /// @return A recognizer for the rotation.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    RotateGesture gesture_;
    ELG_PRIVATE_MEMBERS_END
};

/// Recognizes a trackpad two-finger double tap over the view
/// (View::recognize(TwoFingerDoubleTapGesture)).
class ELG_API TwoFingerDoubleTapModifier final : public EventModifier {
public:
    /// Recognizes `gesture`.
    ///
    /// @param gesture The double tap and its handler.
    explicit TwoFingerDoubleTapModifier(TwoFingerDoubleTapGesture gesture) : gesture_(std::move(gesture)) {}
    /// Called once per mounted view.
    ///
    /// @return A recognizer for the double tap.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    TwoFingerDoubleTapGesture gesture_;
    ELG_PRIVATE_MEMBERS_END
};

/// The pointer's shape over the view (View::pointer_style()).
class ELG_API PointerStyleModifier final : public EventModifier {
public:
    /// Shows `style` over the view.
    ///
    /// @param style The pointer's shape. Reactive: read when the pointer moves or the views change.
    explicit PointerStyleModifier(Prop<PointerStyle> style) : style_(std::move(style)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that gives the pointer's shape.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<PointerStyle> style_;
    ELG_PRIVATE_MEMBERS_END
};

/// Lets the view take keyboard focus (Tab order, clicks), so key handlers on it receive keys.
class ELG_API FocusableModifier final : public EventModifier {
public:
    /// Makes the view focusable, or keeps focus out of it (View::focusable()).
    ///
    /// @param focusable true: the view takes focus. false: neither it nor anything inside does.
    explicit FocusableModifier(bool focusable = true) noexcept : focusable_(focusable) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that takes focus if focusable.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    bool focusable_;
};

/// Calls `action` for key downs and ups while the view (or something inside it) has focus.
/// `action` returns true if it handled the key; unhandled keys bubble outwards.
class ELG_API KeyEventModifier final : public EventModifier {
public:
    /// Calls `action` with key events.
    ///
    /// @param action Called with each key event; returns whether it handled it. May be empty.
    explicit KeyEventModifier(std::function<bool(const KeyEvent&)> action) : action_(std::move(action)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls the action with key events.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<bool(const KeyEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

/// Calls `action` with the view's pointer events, located in the view's own coordinates.
/// Returning true consumes the event; consuming a `down` captures the pointer until the `up`.
class ELG_API PointerModifier final : public EventModifier {
public:
    /// Calls `action` with pointer events.
    ///
    /// @param action Called with each event; returns whether it consumed it. May be empty.
    explicit PointerModifier(std::function<bool(const PointerEvent&)> action) : action_(std::move(action)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls the action with pointer events.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<bool(const PointerEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

/// Calls `action` with wheel and trackpad scrolling over the view, located in its own coordinates.
/// Returning true consumes it.
class ELG_API ScrollModifier final : public EventModifier {
public:
    /// Calls `action` with scrolling.
    ///
    /// @param action Called with each scroll event; returns whether it consumed it. May be empty.
    explicit ScrollModifier(std::function<bool(const ScrollEvent&)> action) : action_(std::move(action)) {}
    /// Called once per mounted view.
    ///
    /// @return A handler that calls the action with scroll events.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<bool(const ScrollEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

/// Makes the view focusable and calls `action` with the text typed (and IME compositions) while it
/// has focus. Returning true consumes it.
class ELG_API TextInputModifier final : public EventModifier {
public:
    /// Calls `action` with text input.
    ///
    /// @param action Called with each text input event; returns whether it consumed it. May be
    ///   empty.
    explicit TextInputModifier(std::function<bool(const TextInputEvent&)> action) : action_(std::move(action)) {}
    /// Called once per mounted view.
    ///
    /// @return A focusable handler that calls the action with text input.
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::function<bool(const TextInputEvent&)> action_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_MODIFIERS_EVENTS_H
