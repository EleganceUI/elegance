// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/animation.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace elg {

namespace {

// How close a spring must be to its target to count as settled.
constexpr double kSpringTolerance = 0.0005;

// A CSS-style cubic Bézier easing curve through (0,0), (x1,y1), (x2,y2), (1,1).
double cubic_bezier(double x1, double y1, double x2, double y2, double x) {
    const auto sample = [](double a1, double a2, double t) {
        const double u = 1 - t;
        return 3 * u * u * t * a1 + 3 * u * t * t * a2 + t * t * t;
    };
    // Find t where the curve's x equals `x` (x(t) is monotonic for valid easing curves).
    double low = 0;
    double high = 1;
    double t = x;
    for (int i = 0; i < 24; ++i) {
        const double current = sample(x1, x2, t);
        if (std::abs(current - x) < 1e-6) {
            break;
        }
        (current < x ? low : high) = t;
        t = (low + high) / 2;
    }
    return sample(y1, y2, t);
}

} // namespace

Animation Animation::linear(double duration) {
    return {Curve::linear, std::max(duration, 0.0), 0};
}

Animation Animation::ease_in(double duration) {
    return {Curve::ease_in, std::max(duration, 0.0), 0};
}

Animation Animation::ease_out(double duration) {
    return {Curve::ease_out, std::max(duration, 0.0), 0};
}

Animation Animation::ease_in_out(double duration) {
    return {Curve::ease_in_out, std::max(duration, 0.0), 0};
}

Animation Animation::spring(double response, double damping_fraction) {
    return {Curve::spring, std::max(response, 0.01), std::max(damping_fraction, 0.01)};
}

Animation Animation::delay(double seconds) const {
    Animation copy = *this;
    copy.delay_ += std::max(seconds, 0.0);
    return copy;
}

double Animation::progress(double seconds) const noexcept {
    const double t = seconds - delay_;
    if (t <= 0) {
        return 0;
    }
    if (curve_ != Curve::spring) {
        if (duration_ <= 0 || t >= duration_) {
            return 1;
        }
        const double x = t / duration_;
        switch (curve_) {
        case Curve::linear: return x;
        case Curve::ease_in: return cubic_bezier(0.42, 0, 1, 1, x);
        case Curve::ease_out: return cubic_bezier(0, 0, 0.58, 1, x);
        case Curve::ease_in_out: return cubic_bezier(0.42, 0, 0.58, 1, x);
        case Curve::spring: break;
        }
        return x;
    }

    // A damped harmonic oscillator released from 0 toward 1 at rest.
    const double omega = 2 * std::numbers::pi / duration_;
    const double zeta = damping_;
    if (zeta < 1 - 1e-6) {
        const double damped = omega * std::sqrt(1 - zeta * zeta);
        const double envelope = std::exp(-zeta * omega * t);
        return 1 - envelope * (std::cos(damped * t) + (zeta * omega / damped) * std::sin(damped * t));
    }
    if (zeta <= 1 + 1e-6) {
        return 1 - std::exp(-omega * t) * (1 + omega * t);
    }
    const double root = std::sqrt(zeta * zeta - 1);
    const double r1 = -omega * (zeta - root);
    const double r2 = -omega * (zeta + root);
    return 1 - (r2 * std::exp(r1 * t) - r1 * std::exp(r2 * t)) / (r2 - r1);
}

bool Animation::finished(double seconds) const noexcept {
    const double t = seconds - delay_;
    if (curve_ != Curve::spring) {
        return t >= duration_;
    }
    // Settled once the slowest-decaying term falls below the tolerance.
    const double omega = 2 * std::numbers::pi / duration_;
    const double zeta = damping_;
    const double decay = zeta <= 1 ? zeta * omega : omega * (zeta - std::sqrt(zeta * zeta - 1));
    const double settle = std::log(1 / kSpringTolerance) / decay + (zeta >= 1 - 1e-6 ? 1 / omega : 0);
    return t >= settle;
}

// --- Transition ---

Transition Transition::opacity() {
    Transition transition;
    transition.insertion_.opacity = 0;
    transition.removal_.opacity = 0;
    return transition;
}

Transition Transition::identity() {
    return Transition();
}

Transition Transition::scale(float from) {
    Transition transition;
    transition.insertion_.scale = from;
    transition.removal_.scale = from;
    return transition;
}

Transition Transition::offset(float x, float y) {
    Transition transition;
    transition.insertion_.x = transition.removal_.x = x;
    transition.insertion_.y = transition.removal_.y = y;
    return transition;
}

Transition Transition::move(Edges edge) {
    Transition transition;
    Effect effect;
    if (contains(edge, Edges::leading)) {
        effect.edge_x -= 1;
    }
    if (contains(edge, Edges::trailing)) {
        effect.edge_x += 1;
    }
    if (contains(edge, Edges::top)) {
        effect.edge_y -= 1;
    }
    if (contains(edge, Edges::bottom)) {
        effect.edge_y += 1;
    }
    transition.insertion_ = effect;
    transition.removal_ = effect;
    return transition;
}

Transition Transition::slide() {
    return asymmetric(move(Edges::leading), move(Edges::trailing));
}

Transition Transition::asymmetric(const Transition& insertion, const Transition& removal) {
    Transition transition;
    transition.insertion_ = insertion.insertion_;
    transition.removal_ = removal.removal_;
    return transition;
}

Transition::Effect Transition::merge(const Effect& a, const Effect& b) noexcept {
    return {a.opacity * b.opacity, a.scale * b.scale, a.x + b.x, a.y + b.y, a.edge_x + b.edge_x, a.edge_y + b.edge_y};
}

Transition Transition::combined(const Transition& other) const {
    Transition transition;
    transition.insertion_ = merge(insertion_, other.insertion_);
    transition.removal_ = merge(removal_, other.removal_);
    return transition;
}

Transition::Pose Transition::pose(float progress, bool removal, Size size) const noexcept {
    const Effect& e = removal ? removal_ : insertion_;
    const float p = progress;
    const float away = 1 - p;
    return {
        .opacity = e.opacity + (1 - e.opacity) * p,
        .scale = e.scale + (1 - e.scale) * p,
        .x = away * (e.x + e.edge_x * size.width),
        .y = away * (e.y + e.edge_y * size.height),
    };
}

} // namespace elg
