// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// How elements describe themselves to the inspector (design.md §16).

#include "view/element.h"
#include "view/view_node.h"

#include <format>
#include <string_view>
#include <typeinfo>

namespace elg::detail {

namespace {

// "class elg::detail::TextElement" -> "Text"; "struct FlowLayout" -> "FlowLayout".
std::string type_label(const std::type_info& type, std::string_view drop_suffix) {
    std::string_view name = type.name();
    for (const std::string_view prefix : {"class ", "struct "}) {
        if (name.starts_with(prefix)) {
            name.remove_prefix(prefix.size());
        }
    }
    if (const auto colon = name.rfind("::"); colon != std::string_view::npos) {
        name.remove_prefix(colon + 2);
    }
    // GCC/Clang mangled names: leading digits give the length.
    while (!name.empty() && name.front() >= '0' && name.front() <= '9') {
        name.remove_prefix(1);
    }
    if (name.size() > drop_suffix.size() && name.ends_with(drop_suffix)) {
        name.remove_suffix(drop_suffix.size());
    }
    return std::string(name);
}

std::string format_float(float v) {
    if (v == std::floor(v) && std::abs(v) < 1e6f) {
        return std::format("{}", static_cast<int>(v));
    }
    return std::format("{:.1f}", v);
}

std::string format_color(const Color& c) {
    const auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    if (c.a >= 1) {
        return std::format("#{:02X}{:02X}{:02X}", byte(c.r), byte(c.g), byte(c.b));
    }
    return std::format("#{:02X}{:02X}{:02X} @ {:.0f}%", byte(c.r), byte(c.g), byte(c.b), c.a * 100);
}

std::string_view shape_name(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::rectangle: return "Rectangle";
    case ShapeKind::rounded_rectangle: return "RoundedRectangle";
    case ShapeKind::capsule: return "Capsule";
    case ShapeKind::circle: return "Circle";
    case ShapeKind::path: return "Path";
    }
    return "Shape";
}

std::string style_summary(const ShapeStyle& style) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Color>) {
                return format_color(value);
            } else if constexpr (std::is_same_v<T, LinearGradient>) {
                return std::format("linear gradient, {} stops", value.stops.size());
            } else if constexpr (std::is_same_v<T, RadialGradient>) {
                return std::format("radial gradient, {} stops", value.stops.size());
            } else if constexpr (std::is_same_v<T, EllipticalGradient>) {
                return std::format("elliptical gradient, {} stops", value.stops.size());
            } else if constexpr (std::is_same_v<T, AngularGradient>) {
                return std::format("angular gradient, {} stops", value.stops.size());
            } else {
                return std::format("material, blur {}", value.blur);
            }
        },
        style.value());
}

} // namespace

void Element::inspect(Inspection& out) const {
    out.name = type_label(typeid(*this), "Element");
}

void TextElement::inspect(Inspection& out) const {
    out.name = "Text";
    constexpr std::size_t kMax = 48;
    out.detail = content_.size() > kMax ? content_.substr(0, kMax) + "…" : content_;
    out.properties.emplace_back("font size", format_float(size_));
    out.properties.emplace_back("color", format_color(color_.current()));
    if (line_limit_ > 0) {
        out.properties.emplace_back("line limit", std::to_string(line_limit_));
    }
    out.properties.emplace_back("lines", std::to_string(arranged_.lines.size()));
}

void ShapeElement::inspect(Inspection& out) const {
    out.name = std::string(shape_name(spec_.kind));
    if (has_fill_) {
        out.properties.emplace_back("fill", style_summary(fill_.current()));
    }
    if (spec_.stroke) {
        out.properties.emplace_back("stroke", std::format("{} pt, {}", format_float(spec_.stroke->width),
                                                          format_color(stroke_color_.current())));
        if (!spec_.stroke->style.dash.empty()) {
            out.properties.emplace_back("dashed", "yes (GPU path)");
        }
    }
    if (spec_.kind == ShapeKind::rounded_rectangle) {
        out.detail = std::format("radius {}", format_float(spec_.radii.top_left));
    }
}

void LayoutElement::inspect(Inspection& out) const {
    const auto* layout = layout_.get();
    out.name = type_label(typeid(*layout), "Layout");
    out.detail = std::format("{} children", laid_out_.size());
}

void LayoutModifierElement::inspect(Inspection& out) const {
    const auto* modifier = modifier_.get();
    out.name = type_label(typeid(*modifier), "Modifier");
}

void EffectElement::inspect(Inspection& out) const {
    out.name = "Effects";
    std::string summary;
    const auto add = [&](std::string_view part) {
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += part;
    };
    if (effects_.clip_shape) {
        const auto& node = static_cast<const ShapeNode&>(*ViewAccess::node(*effects_.clip_shape));
        add(std::format("clip_shape({})", shape_name(node.spec().kind)));
        if (node.spec().kind == ShapeKind::path) {
            out.properties.emplace_back("cost", "offscreen layer + path mask");
        }
    }
    const float opacity = opacity_.current();
    if (opacity < 1) {
        add(std::format("opacity {:.2f}", opacity));
    }
    if (effects_.shadow) {
        add(std::format("shadow {}", format_float(effects_.shadow->radius)));
    }
    if (offset_x_.current() != 0 || offset_y_.current() != 0) {
        add(std::format("offset {}, {}", format_float(offset_x_.current()), format_float(offset_y_.current())));
    }
    if (z_index_) {
        add(std::format("z {}", format_float(*z_index_)));
    }
    out.detail = summary;
}

void DecorationElement::inspect(Inspection& out) const {
    out.name = modifier_->placement() == DecorationModifier::Placement::in_front ? "Overlay" : "Background";
}

} // namespace elg::detail
