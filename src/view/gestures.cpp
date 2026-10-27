// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The gestures View::recognize() attaches (elg/gestures.h).

#include "view/gestures.h"

#include <elg/gestures.h>
#include <elg/modifiers/events.h>

#include <chrono>
#include <cmath>
#include <deque>
#include <type_traits>

namespace elg {

namespace detail {

namespace {

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

struct GestureAccess {
    static std::unique_ptr<EventHandler> handler(const DragGesture& gesture);
    static std::unique_ptr<EventHandler> handler(const LongPressGesture& gesture);
    template <class Event>
    static std::unique_ptr<EventHandler> handler(const PhasedGesture<Event>& gesture);
    static std::unique_ptr<EventHandler> handler(const TwoFingerDoubleTapGesture& gesture);
};

namespace {

struct DragCallbacks {
    std::function<void(const DragEvent&)> began;
    std::function<void(const DragEvent&)> changed;
    std::function<void(const DragEvent&)> ended;
    std::function<void()> cancelled;
};

class DragRecognizer final : public GestureRecognizer {
public:
    DragRecognizer(DragCallbacks callbacks, DragOptions options) : callbacks_(std::move(callbacks)), options_(options) {}

    bool observe(const PointerEvent& event, Rect frame) override {
        switch (event.kind) {
        case PointerEvent::Kind::down:
            if (event.button != options_.button) {
                return false;
            }
            tracking_ = true;
            start_ = {event.location.x - frame.x, event.location.y - frame.y};
            start_window_ = event.window_location;
            modifiers_ = event.modifiers;
            samples_.clear();
            sample(event.window_location);
            return options_.minimum_distance <= 0 && begin(event, frame);
        case PointerEvent::Kind::move:
            if (!tracking_) {
                return false;
            }
            sample(event.window_location);
            return distance(event.window_location, start_window_) >= options_.minimum_distance && begin(event, frame);
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel: reset(); return false;
        case PointerEvent::Kind::hover: return false;
        }
        return false;
    }

    void reset() override { tracking_ = false; }

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        if (!active_) {
            return {};
        }
        switch (event.kind) {
        case PointerEvent::Kind::move:
            sample(event.window_location);
            call(callbacks_.changed, event, frame);
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::up:
            sample(event.window_location);
            active_ = tracking_ = false;
            call(callbacks_.ended, event, frame);
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::cancel:
            active_ = tracking_ = false;
            if (callbacks_.cancelled) {
                callbacks_.cancelled();
            }
            return {.redraw = true};
        case PointerEvent::Kind::down:
        case PointerEvent::Kind::hover: return {};
        }
        return {};
    }

private:
    bool begin(const PointerEvent& event, Rect frame) {
        active_ = true;
        call(callbacks_.began, event, frame);
        call(callbacks_.changed, event, frame);
        return true;
    }

    void call(const std::function<void(const DragEvent&)>& action, const PointerEvent& event, Rect frame) const {
        if (action) {
            action(make_event(event, frame));
        }
    }

    DragEvent make_event(const PointerEvent& event, Rect frame) const {
        return {
            .location = {event.location.x - frame.x, event.location.y - frame.y},
            .start_location = start_,
            .translation = {event.window_location.x - start_window_.x, event.window_location.y - start_window_.y},
            .velocity = velocity(),
            .button = options_.button,
            .modifiers = modifiers_,
        };
    }

    // Keeps the last moments of the drag, for its velocity.
    void sample(Point location) {
        constexpr double kWindow = 0.1;
        const double now = now_seconds();
        samples_.push_back({now, location});
        while (samples_.size() > 2 && now - samples_.front().time > kWindow) {
            samples_.pop_front();
        }
    }

    Point velocity() const {
        if (samples_.size() < 2) {
            return {};
        }
        const Sample& first = samples_.front();
        const Sample& last = samples_.back();
        const auto dt = static_cast<float>(last.time - first.time);
        if (dt <= 0) {
            return {};
        }
        return {(last.location.x - first.location.x) / dt, (last.location.y - first.location.y) / dt};
    }

    struct Sample {
        double time;
        Point location;
    };

    DragCallbacks callbacks_;
    DragOptions options_;
    bool tracking_ = false; // a press with our button is down
    bool active_ = false;   // and it became the drag
    Point start_;
    Point start_window_;
    KeyModifiers modifiers_ = KeyModifiers::none;
    std::deque<Sample> samples_;
};

class LongPressRecognizer final : public GestureRecognizer {
public:
    LongPressRecognizer(std::function<void()> action, LongPressOptions options)
        : action_(std::move(action)), options_(std::move(options)) {}

    bool observe(const PointerEvent& event, Rect) override {
        switch (event.kind) {
        case PointerEvent::Kind::down:
            if (event.button != options_.button) {
                return false;
            }
            start_ = event.window_location;
            due_ = now_seconds() + options_.duration;
            set_pressing(true);
            return false;
        case PointerEvent::Kind::move:
            if (pressing_ && distance(event.window_location, start_) > options_.maximum_distance) {
                reset();
            }
            return false;
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel: reset(); return false;
        case PointerEvent::Kind::hover: return false;
        }
        return false;
    }

    std::optional<double> deadline() const override {
        return pressing_ && !active_ ? std::optional(due_) : std::nullopt;
    }

    bool expire() override {
        if (!pressing_ || active_ || now_seconds() < due_) {
            return false;
        }
        active_ = true;
        if (action_) {
            action_();
        }
        return true;
    }

    void reset() override { set_pressing(false); }

    // After it fired, the rest of the press is its own.
    EventResult pointer(const PointerEvent& event, Rect) override {
        if (!active_) {
            return {};
        }
        if (event.kind == PointerEvent::Kind::up || event.kind == PointerEvent::Kind::cancel) {
            active_ = false;
            set_pressing(false);
        }
        return {.consumed = event.kind != PointerEvent::Kind::cancel, .redraw = true};
    }

private:
    void set_pressing(bool pressing) {
        if (pressing_ == pressing) {
            return;
        }
        pressing_ = pressing;
        if (options_.pressing) {
            options_.pressing(pressing);
        }
    }

    std::function<void()> action_;
    LongPressOptions options_;
    Point start_;
    double due_ = 0;
    bool pressing_ = false;
    bool active_ = false;
};

template <class Event>
struct PhasedCallbacks {
    std::function<void(const Event&)> began;
    std::function<void(const Event&)> changed;
    std::function<void(const Event&)> ended;
    std::function<void()> cancelled;

    EventResult deliver(const Event& event) const {
        const auto call = [&](const std::function<void(const Event&)>& action) {
            if (action) {
                action(event);
            }
        };
        switch (event.phase) {
        case GesturePhase::began: call(began); break;
        case GesturePhase::none:
        case GesturePhase::changed: call(changed); break;
        case GesturePhase::ended: call(ended); break;
        case GesturePhase::cancelled:
            if (cancelled) {
                cancelled();
            }
            break;
        }
        return {.consumed = true, .redraw = true};
    }
};

class MagnifyHandler final : public EventHandler {
public:
    explicit MagnifyHandler(PhasedCallbacks<MagnifyEvent> callbacks) : callbacks_(std::move(callbacks)) {}
    EventResult magnify(const MagnifyEvent& event) override { return callbacks_.deliver(event); }

private:
    PhasedCallbacks<MagnifyEvent> callbacks_;
};

class RotateHandler final : public EventHandler {
public:
    explicit RotateHandler(PhasedCallbacks<RotateEvent> callbacks) : callbacks_(std::move(callbacks)) {}
    EventResult rotate(const RotateEvent& event) override { return callbacks_.deliver(event); }

private:
    PhasedCallbacks<RotateEvent> callbacks_;
};

class TwoFingerDoubleTapHandler final : public EventHandler {
public:
    explicit TwoFingerDoubleTapHandler(std::function<void(Point)> action) : action_(std::move(action)) {}

    EventResult two_finger_double_tap(Point location) override {
        if (!action_) {
            return {};
        }
        action_(location);
        return {.consumed = true, .redraw = true};
    }

private:
    std::function<void(Point)> action_;
};

} // namespace

std::unique_ptr<EventHandler> GestureAccess::handler(const DragGesture& gesture) {
    return std::make_unique<DragRecognizer>(
        DragCallbacks{gesture.began_, gesture.changed_, gesture.ended_, gesture.cancelled_}, gesture.options_);
}

std::unique_ptr<EventHandler> GestureAccess::handler(const LongPressGesture& gesture) {
    return std::make_unique<LongPressRecognizer>(gesture.action_, gesture.options_);
}

template <class Event>
std::unique_ptr<EventHandler> GestureAccess::handler(const PhasedGesture<Event>& gesture) {
    PhasedCallbacks<Event> callbacks{gesture.began_, gesture.changed_, gesture.ended_, gesture.cancelled_};
    if constexpr (std::is_same_v<Event, MagnifyEvent>) {
        return std::make_unique<MagnifyHandler>(std::move(callbacks));
    } else {
        return std::make_unique<RotateHandler>(std::move(callbacks));
    }
}

std::unique_ptr<EventHandler> GestureAccess::handler(const TwoFingerDoubleTapGesture& gesture) {
    return std::make_unique<TwoFingerDoubleTapHandler>(gesture.action_);
}

} // namespace detail

std::unique_ptr<EventHandler> DragGestureModifier::make_handler() const {
    return detail::GestureAccess::handler(gesture_);
}

std::unique_ptr<EventHandler> LongPressGestureModifier::make_handler() const {
    return detail::GestureAccess::handler(gesture_);
}

std::unique_ptr<EventHandler> MagnifyGestureModifier::make_handler() const {
    return detail::GestureAccess::handler(gesture_);
}

std::unique_ptr<EventHandler> RotateGestureModifier::make_handler() const {
    return detail::GestureAccess::handler(gesture_);
}

std::unique_ptr<EventHandler> TwoFingerDoubleTapModifier::make_handler() const {
    return detail::GestureAccess::handler(gesture_);
}

} // namespace elg
