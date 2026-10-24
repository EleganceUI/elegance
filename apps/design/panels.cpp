// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "panels.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace design {

using namespace elg;

namespace {

// --- Look ---

class ToolButtonStyle final : public ButtonStyle {
public:
    explicit ToolButtonStyle(std::function<bool()> selected = {}) : selected_(std::move(selected)) {}
    View make_body(const ButtonConfiguration& button) const override {
        const std::function<bool()> selected = selected_;
        return button.label
            .foreground_style([=](const EnvironmentValues& env) {
                return selected && selected() ? Color::white : env.color(ThemeColor::primary);
            })
            .background(
                [=](const EnvironmentValues& env) {
                    if (selected && selected()) {
                        return ShapeStyle(env.color(ThemeColor::accent));
                    }
                    const Color base = env.color(ThemeColor::primary);
                    if (button.is_pressed()) {
                        return ShapeStyle(base.opacity(0.16f));
                    }
                    return ShapeStyle(button.is_hovered() ? base.opacity(0.08f) : Color::clear);
                },
                RoundedRectangle(6));
    }

private:
    std::function<bool()> selected_;
};

View IconButton(std::string icon, std::string help, std::function<void()> action, std::function<bool()> selected = {}) {
    return Button(Image(std::move(icon)).font(Font::system(16)).frame(30, 30), std::move(action))
        .button_style(ToolButtonStyle(std::move(selected)))
        .help(help)
        .accessibility_label(help);
}

View SectionTitle(std::string title) {
    return Text(std::move(title)).font(Font::system(12, Weight::semibold));
}

View Section(std::string title, View content) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10}, SectionTitle(std::move(title)), std::move(content))
        .padding(EdgeInsets::symmetric(14, 12))
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .overlay(Rectangle().fill(ThemeColor::outline).frame({.max_width = infinity, .min_height = 1, .max_height = 1}),
                 Alignment::bottom);
}

std::string format_number(float value) {
    if (std::abs(value - std::round(value)) < 0.005f) {
        return std::format("{:.0f}", value);
    }
    return std::format("{:.2f}", value);
}

std::optional<float> parse_number(const std::string& text) {
    float value = 0;
    const char* begin = text.data();
    while (begin != text.data() + text.size() && *begin == ' ') {
        ++begin;
    }
    const auto [end, error] = std::from_chars(begin, text.data() + text.size(), value);
    if (error != std::errc() || end == begin) {
        return std::nullopt;
    }
    return value;
}

// A number that's applied on Enter or when the field loses focus (not while typing, so "1." isn't
// rewritten to "1" under the caret).
View NumberField(std::string label, std::function<float()> get, std::function<void(float)> set) {
    const State<std::string> text;
    const State<bool> focused(false);
    const auto dirty = std::make_shared<bool>(false);
    effect([=] {
        const float value = get(); // tracked: shows changes made elsewhere
        if (!focused.peek()) {
            text = format_number(value);
        }
    });
    const auto commit = [=] {
        if (!*dirty) {
            return;
        }
        *dirty = false;
        if (const std::optional<float> value = parse_number(text.peek())) {
            set(*value);
        }
        text = format_number(untrack(get));
    };
    effect([=] {
        if (!focused()) {
            untrack(commit);
        }
    });
    const Binding<std::string> binding([=] { return text(); },
                                       [=](std::string value) {
                                           *dirty = true;
                                           text = std::move(value);
                                       });
    return HStack({.spacing = 6}, Text(std::move(label))
                      .font(Font::system(11))
                      .foreground_style(ThemeColor::tertiary)
                      .fixed_size()
                      .frame({.min_width = 14, .alignment = Alignment::leading}),
                  TextField("", binding, {.on_submit = commit}).text_field_style(PlainTextFieldStyle{}).font(Font::system(12)).focused(focused.binding()))
        .padding(EdgeInsets::symmetric(8, 5))
        .frame({.max_width = infinity})
        .background(ThemeColor::surface_raised, RoundedRectangle(5));
}

// --- Color picker ---

struct Hsv {
    float h = 0; // 0...1
    float s = 0;
    float v = 0;
};

Color from_hsv(Hsv c) {
    const float h = std::fmod(c.h, 1.f) * 6;
    const int i = static_cast<int>(h);
    const float f = h - static_cast<float>(i);
    const float p = c.v * (1 - c.s);
    const float q = c.v * (1 - c.s * f);
    const float t = c.v * (1 - c.s * (1 - f));
    switch (i % 6) {
    case 0: return {c.v, t, p, 1};
    case 1: return {q, c.v, p, 1};
    case 2: return {p, c.v, t, 1};
    case 3: return {p, q, c.v, 1};
    case 4: return {t, p, c.v, 1};
    default: return {c.v, p, q, 1};
    }
}

Hsv to_hsv(Color c) {
    const float max = std::max({c.r, c.g, c.b});
    const float min = std::min({c.r, c.g, c.b});
    const float d = max - min;
    Hsv out{0, max > 0 ? d / max : 0, max};
    if (d > 0) {
        if (max == c.r) {
            out.h = std::fmod((c.g - c.b) / d, 6.f) / 6;
        } else if (max == c.g) {
            out.h = ((c.b - c.r) / d + 2) / 6;
        } else {
            out.h = ((c.r - c.g) / d + 4) / 6;
        }
        if (out.h < 0) {
            out.h += 1;
        }
    }
    return out;
}

// Dragging inside a view reports where, as fractions of its size (0...1 each way).
class FractionDrag final : public EventHandler {
public:
    FractionDrag(std::function<void()> began, std::function<void(Point)> changed)
        : began_(std::move(began)), changed_(std::move(changed)) {}
    EventResult pointer(const PointerEvent& event, Rect frame) override {
        const auto report = [&] {
            changed_({std::clamp((event.location.x - frame.x) / std::max(frame.width, 1.f), 0.f, 1.f),
                      std::clamp((event.location.y - frame.y) / std::max(frame.height, 1.f), 0.f, 1.f)});
        };
        switch (event.kind) {
        case PointerEvent::Kind::down:
            if (event.button != MouseButton::left) {
                return {};
            }
            dragging_ = true;
            began_();
            report();
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::move:
            if (dragging_) {
                report();
            }
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel: dragging_ = false; return {.consumed = true};
        case PointerEvent::Kind::hover: set_cursor(Cursor::crosshair); return {.consumed = true};
        }
        return {};
    }
    EventResult hover(bool inside) override {
        if (!inside) {
            set_cursor(Cursor::arrow);
        }
        return {};
    }

private:
    std::function<void()> began_;
    std::function<void(Point)> changed_;
    bool dragging_ = false;
};

class FractionDragModifier final : public EventModifier {
public:
    FractionDragModifier(std::function<void()> began, std::function<void(Point)> changed)
        : began_(std::move(began)), changed_(std::move(changed)) {}
    std::unique_ptr<EventHandler> make_handler() const override { return std::make_unique<FractionDrag>(began_, changed_); }

private:
    std::function<void()> began_;
    std::function<void(Point)> changed_;
};

View ColorPicker(const Document& doc, std::function<Color()> get, std::function<void(Color)> set) {
    const State<Hsv> hsv(to_hsv(untrack(get)));
    const auto apply = [=](Hsv next) {
        hsv = next;
        set(from_hsv(next));
    };
    const auto begin = [=] { doc.checkpoint(); };
    const View square =
        Canvas([hsv](GraphicsContext& g) {
            const Size size = g.size();
            const Hsv c = hsv();
            Path area;
            area.add_rounded_rect({0, 0, size.width, size.height}, 6);
            g.fill_path(area, LinearGradient({Color::white, from_hsv({c.h, 1, 1})}, UnitPoint::leading, UnitPoint::trailing));
            g.fill_path(area, LinearGradient({Color::black.opacity(0.f), Color::black}, UnitPoint::top, UnitPoint::bottom));
            const Point knob{c.s * size.width, (1 - c.v) * size.height};
            Path ring;
            ring.add_circle(knob, 7);
            g.fill_path(ring, from_hsv(c));
            g.stroke_path(ring, Color::white, {.width = 2.5f});
        })
            .frame({.max_width = infinity, .min_height = 160, .max_height = 160})
            .modifier(FractionDragModifier(begin, [=](Point f) {
                const Hsv c = hsv.peek();
                apply({c.h, f.x, 1 - f.y});
            }));
    const View hue =
        Canvas([hsv](GraphicsContext& g) {
            const Size size = g.size();
            Path bar;
            bar.add_rounded_rect({0, 0, size.width, size.height}, size.height / 2);
            g.fill_path(bar, LinearGradient({Color::hex(0xFF0000), Color::hex(0xFFFF00), Color::hex(0x00FF00), Color::hex(0x00FFFF),
                                             Color::hex(0x0000FF), Color::hex(0xFF00FF), Color::hex(0xFF0000)},
                                            UnitPoint::leading, UnitPoint::trailing));
            Path ring;
            ring.add_circle({hsv().h * size.width, size.height / 2}, size.height / 2 + 1);
            g.fill_path(ring, from_hsv({hsv().h, 1, 1}));
            g.stroke_path(ring, Color::white, {.width = 2.5f});
        })
            .frame({.max_width = infinity, .min_height = 12, .max_height = 12})
            .modifier(FractionDragModifier(begin, [=](Point f) {
                const Hsv c = hsv.peek();
                apply({std::min(f.x, 0.999f), c.s, c.v});
            }));
    const std::vector<std::uint32_t> presets{0x000000, 0xFFFFFF, 0xD9D9D9, 0xF24822, 0xFFA629, 0xFFCD29,
                                             0x14AE5C, 0x0D99FF, 0x9747FF, 0xFF24BD, 0x6C5CE7, 0x2D3436};
    return VStack({.spacing = 12}, square, hue,
                  HStack({.spacing = 6}, ForEach(presets, [=](std::uint32_t hex) {
                             return Button(RoundedRectangle(4)
                                               .fill(Color::hex(hex))
                                               .frame(18, 18)
                                               .overlay(RoundedRectangle(4).stroke_border(Color::black.opacity(0.15f))),
                                           [=] {
                                               doc.checkpoint();
                                               apply(to_hsv(Color::hex(hex)));
                                           })
                                 .button_style(PlainButtonStyle{});
                         })))
        .padding(14)
        .frame({.width = 260});
}

// A color swatch (opening the picker) and its hex code.
View ColorRow(const Document& doc, std::function<Color()> get, std::function<void(Color)> set) {
    const State<bool> picking(false);
    const State<std::string> hex;
    const State<bool> focused(false);
    effect([=] {
        const Color c = get();
        if (!focused.peek()) {
            hex = to_hex(c);
        }
    });
    const auto commit = [=] {
        if (const std::optional<Color> c = from_hex(hex.peek()); c && to_hex(*c) != to_hex(untrack(get))) {
            doc.checkpoint();
            set(*c);
        }
        hex = to_hex(untrack(get));
    };
    effect([=] {
        if (!focused()) {
            untrack(commit);
        }
    });
    return HStack({.spacing = 8},
                  Button(RoundedRectangle(4)
                             .fill([=] { return ShapeStyle(get()); })
                             .frame(20, 20)
                             .overlay(RoundedRectangle(4).stroke_border([](const EnvironmentValues& env) {
                                 return env.color(ThemeColor::outline);
                             })),
                         [=] { picking = !picking.peek(); })
                      .button_style(PlainButtonStyle{})
                      .popover(picking.binding(), [=] { return ColorPicker(doc, get, set); }, {.padded = false})
                      .help("Pick a color"),
                  TextField("", hex.binding(), {.on_submit = commit})
                      .text_field_style(PlainTextFieldStyle{})
                      .font(Font::system(12))
                      .focused(focused.binding()))
        .padding(EdgeInsets::symmetric(6, 4))
        .frame({.max_width = infinity})
        .background(ThemeColor::surface_raised, RoundedRectangle(5));
}

std::string kind_icon(const Node& n) {
    switch (n.kind) {
    case Kind::frame: return n.auto_layout ? (n.horizontal ? "columns" : "rows") : "hash";
    case Kind::group: return "bounding-box";
    case Kind::rectangle: return "square";
    case Kind::ellipse: return "circle";
    case Kind::line: return "line-segment";
    case Kind::text: return "text-t";
    }
    return "square";
}

// A row of the layers tree.
struct LayerEntry {
    Node node;
    int depth = 0;
    bool has_children = false;
    friend bool operator==(const LayerEntry&, const LayerEntry&) = default;
};

} // namespace

// --- Toolbar ---

View ToolPalette(const Document& doc) {
    struct ToolInfo {
        Tool tool;
        std::string icon;
        std::string help;
    };
    const std::vector<ToolInfo> tools{{Tool::select, "cursor", "Move (V)"},         {Tool::hand, "hand", "Hand (H, or hold Space)"},
                                      {Tool::frame, "hash", "Frame (F)"},           {Tool::rectangle, "square", "Rectangle (R)"},
                                      {Tool::ellipse, "circle", "Ellipse (O)"},     {Tool::line, "line-segment", "Line (L)"},
                                      {Tool::text, "text-t", "Text (T)"}};
    std::vector<int> indices(tools.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }
    const auto button = [=](int i) {
        const ToolInfo& info = tools[static_cast<std::size_t>(i)];
        const Tool tool = info.tool;
        View b = Button(Image(info.icon).font(Font::system(18)).frame(36, 36), [=] { doc.tool = tool; })
                     .button_style(ToolButtonStyle([=] { return doc.tool() == tool; }))
                     .help(info.help)
                     .accessibility_label(info.help);
        // A divider after the navigation tools, as in Figma.
        if (tool == Tool::hand) {
            return View(HStack({.spacing = 4}, b,
                               Rectangle().fill(ThemeColor::outline).frame(1, 22).padding(EdgeInsets::symmetric(4, 0))));
        }
        return b;
    };
    return HStack({.spacing = 4}, ForEach(indices, button))
        .padding(6)
        .background(ThemeColor::surface, RoundedRectangle(12))
        .overlay(RoundedRectangle(12).stroke_border([](const EnvironmentValues& env) { return env.color(ThemeColor::outline); }))
        .shadow({.color = Color::black.opacity(0.35f), .radius = 16, .y = 4});
}

View Toolbar(const Document& doc, State<Size> viewport) {
    const View zoom_menu = MenuButton(Text([=] { return std::format("{:.0f}%", doc.zoom() * 100); }).font(Font::system(12)).frame({.width = 44}),
                                      [=] {
                                          return Menu(MenuItem("Zoom in", [=] { doc.zoom_to(doc.zoom.peek() * 1.5f, viewport.peek()); }),
                                                      MenuItem("Zoom out", [=] { doc.zoom_to(doc.zoom.peek() / 1.5f, viewport.peek()); }),
                                                      MenuItem("Zoom to 100%", [=] { doc.zoom_to(1, viewport.peek()); }, {.shortcut = "Ctrl+0"}),
                                                      MenuItem("Zoom to fit", [=] { doc.zoom_to_fit(viewport.peek()); }, {.shortcut = "Shift+1"}),
                                                      Divider(),
                                                      MenuItem("Pixel grid", [=] { doc.show_grid = !doc.show_grid.peek(); },
                                                               {.checked = Prop<bool>([=] { return doc.show_grid(); })}),
                                                      MenuItem("Snap to layers", [=] { doc.snapping = !doc.snapping.peek(); },
                                                               {.checked = Prop<bool>([=] { return doc.snapping(); })}));
                                      })
                                .button_style(ToolButtonStyle{});
    return HStack({.spacing = 4},
                  Image("pen-nib.fill").font(Font::system(18)).foreground_style(ThemeColor::accent).padding(EdgeInsets::symmetric(8, 0)),
                  Text("Drafts  /").font(Font::system(13)).foreground_style(ThemeColor::secondary),
                  TextField("Untitled", doc.name.binding())
                      .text_field_style(PlainTextFieldStyle{})
                      .font(Font::system(13, Weight::semibold))
                      .frame({.width = 180}),
                  Spacer(),
                  IconButton("arrow-counter-clockwise", "Undo (Ctrl+Z)", [=] { doc.undo(); }),
                  IconButton("arrow-clockwise", "Redo (Ctrl+Shift+Z)", [=] { doc.redo(); }),
                  zoom_menu,
                  Button("Share", [] {}).padding(EdgeInsets{.leading = 6}))
        .padding(EdgeInsets::symmetric(8, 6))
        .frame({.max_width = infinity, .min_height = 46, .max_height = 46})
        .background(ThemeColor::surface)
        .overlay(Rectangle().fill(ThemeColor::outline).frame({.max_width = infinity, .min_height = 1, .max_height = 1}),
                 Alignment::bottom);
}

// --- Layers ---

View LayerRow(const Document& doc, const LayerEntry& entry, State<std::set<int>> collapsed) {
    const Node& n = entry.node;
    const int id = n.id;
    const bool visible = n.visible;
    const bool locked = n.locked;
    const State<bool> hovered(false);
    const State<bool> renaming(false);
    const State<std::string> name(n.name);
    const auto finish_rename = [=] {
        if (!name.peek().empty() && name.peek() != n.name) {
            doc.checkpoint();
            doc.update(id, [&](Node& node) { node.name = name.peek(); });
        }
        renaming = false;
    };
    const View label = If([=] { return renaming(); },
                          TextField("", name.binding(), {.on_submit = finish_rename})
                              .text_field_style(PlainTextFieldStyle{})
                              .font(Font::system(12))
                              .focused(Binding<bool>([=] { return renaming(); }, [=](bool on) {
                                  if (!on) {
                                      finish_rename();
                                  }
                              })),
                          Text(n.name).font(Font::system(12)).line_limit(1).opacity(visible ? 1.f : 0.45f));
    const auto toggle = [=](std::string on_icon, std::string off_icon, bool value, std::function<void(Node&)> flip, std::string help) {
        return Button(Image(value ? on_icon : off_icon).font(Font::system(13)).frame(22, 22),
                      [=] {
                          doc.checkpoint();
                          doc.update(id, flip);
                      })
            .button_style(PlainButtonStyle{})
            .foreground_style(ThemeColor::secondary)
            .opacity([=] { return hovered() || !value ? 1.f : 0.f; })
            .help(std::move(help));
    };
    const View disclosure =
        entry.has_children
            ? View(Button(Image([&] { return collapsed.peek().contains(id) ? "caret-right" : "caret-down"; }())
                              .font(Font::system(10))
                              .frame(14, 22),
                          [=] {
                              collapsed.update([&](std::set<int>& ids) {
                                  if (!ids.erase(id)) {
                                      ids.insert(id);
                                  }
                              });
                          })
                       .button_style(PlainButtonStyle{})
                       .foreground_style(ThemeColor::secondary))
            : View(Rectangle().fill(Color::clear).frame(14, 22));
    return HStack({.spacing = 6}, disclosure,
                  Image(kind_icon(n)).font(Font::system(13)).foreground_style(ThemeColor::secondary).frame({.width = 16}),
                  label, Spacer(),
                  toggle("lock-simple-open", "lock-simple", !locked, [](Node& node) { node.locked = !node.locked; }, "Lock"),
                  toggle("eye", "eye-slash", visible, [](Node& node) { node.visible = !node.visible; }, "Show or hide"))
        .padding(EdgeInsets{.leading = 4 + 14 * static_cast<float>(entry.depth), .trailing = 6})
        .frame({.max_width = infinity, .min_height = 30, .max_height = 30})
        .background([=](const EnvironmentValues& env) {
            if (doc.is_selected(id)) {
                return ShapeStyle(env.color(ThemeColor::accent).opacity(0.22f));
            }
            return ShapeStyle(hovered() || doc.hovered() == id ? env.color(ThemeColor::primary).opacity(0.06f) : Color::clear);
        }, RoundedRectangle(5))
        .content_shape(Rectangle())
        .on_hover([=](bool inside) { hovered = inside; }, HoverDelay{0, 0})
        .on_tap_gesture([=] { renaming = true; }, 2)
        .on_tap_gesture([=] { doc.selection = std::vector<int>{id}; });
}

View LayersPanel(const Document& doc) {
    const State<std::set<int>> collapsed;
    // The tree, front first at each level; children under their parent unless it's collapsed.
    const auto rows = [=] {
        const std::vector<Node>& all = doc.nodes();
        const Children children = children_of(all);
        std::map<int, const Node*> by_id;
        for (const Node& n : all) {
            by_id[n.id] = &n;
        }
        const std::set<int>& closed = collapsed();
        std::vector<LayerEntry> result;
        const std::function<void(int, int)> visit = [&](int parent, int depth) {
            const auto it = children.find(parent);
            if (it == children.end()) {
                return;
            }
            for (auto child = it->second.rbegin(); child != it->second.rend(); ++child) {
                const auto kids = children.find(*child);
                const bool has_children = kids != children.end() && !kids->second.empty();
                result.push_back({*by_id.at(*child), depth, has_children});
                if (has_children && !closed.contains(*child)) {
                    visit(*child, depth + 1);
                }
            }
        };
        visit(0, 0);
        return result;
    };
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                  Section("Pages", Text("Page 1").font(Font::system(12)).padding(EdgeInsets{.leading = 2})),
                  SectionTitle("Layers").padding(EdgeInsets{.top = 12, .leading = 14, .bottom = 6}),
                  ScrollView(VStack({.spacing = 1}, ForEach(rows, [](const LayerEntry& e) { return e.node.id; },
                                                            [=](const LayerEntry& e) { return LayerRow(doc, e, collapsed); }))
                                 .padding(EdgeInsets{.leading = 6, .bottom = 12, .trailing = 6})))
        .frame({.min_width = 240, .max_width = 240, .max_height = infinity, .alignment = Alignment::top_leading})
        .background(ThemeColor::surface)
        .overlay(Rectangle().fill(ThemeColor::outline).frame({.min_width = 1, .max_width = 1, .max_height = infinity}),
                 Alignment::trailing);
}

// --- Properties ---

namespace {

// Moves and scales the selection (with everything inside it) so its bounds become `next`.
void set_selection_bounds(const Document& doc, Rect next) {
    const std::optional<Rect> current = untrack([&] { return doc.selection_bounds(); });
    if (!current) {
        return;
    }
    doc.checkpoint();
    doc.transform(doc.selection_roots(), *current, next);
}

// Group and ungroup; auto layout for a frame, or wrapping the selection in one.
View Structure(const Document& doc) {
    const auto first = [=] {
        const std::vector<Node> s = doc.selected();
        return s.empty() ? Node{} : s.front();
    };
    const auto single_frame = [=] {
        const std::vector<Node> s = doc.selected();
        return s.size() == 1 && s.front().kind == Kind::frame;
    };
    const auto any_group = [=] {
        return std::ranges::any_of(doc.selected(), [](const Node& n) { return n.kind == Kind::group; });
    };
    const auto set_frame = [=](std::function<void(Node&)> change) {
        doc.checkpoint();
        doc.update_selected(change);
    };
    const View auto_layout = VStack(
        {.alignment = HorizontalAlignment::leading, .spacing = 10},
        Text("Direction").font(Font::system(12)).foreground_style(ThemeColor::secondary),
        Picker(Binding<bool>([=] { return first().horizontal; }, [=](bool row) { set_frame([=](Node& n) { n.horizontal = row; }); }),
               {{false, "Column"}, {true, "Row"}})
            .frame({.max_width = infinity}),
        HStack({.spacing = 8},
               NumberField("Gap", [=] { return first().gap; }, [=](float v) { set_frame([=](Node& n) { n.gap = std::max(v, 0.f); }); }),
               NumberField("Pad", [=] { return first().padding; },
                           [=](float v) { set_frame([=](Node& n) { n.padding = std::max(v, 0.f); }); })),
        Text("Align children").font(Font::system(12)).foreground_style(ThemeColor::secondary),
        Picker(Binding<CrossAlign>([=] { return first().align; }, [=](CrossAlign a) { set_frame([=](Node& n) { n.align = a; }); }),
               {{CrossAlign::start, "Start"}, {CrossAlign::center, "Center"}, {CrossAlign::end, "End"}, {CrossAlign::stretch, "Fill"}})
            .frame({.max_width = infinity}),
        Toggle("Hug contents", Binding<bool>([=] { return first().hug; }, [=](bool on) { set_frame([=](Node& n) { n.hug = on; }); })),
        Button("Remove auto layout", [=] { set_frame([](Node& n) { n.auto_layout = false; }); }).button_style(BorderedButtonStyle{}));
    return Section(
        "Structure",
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               HStack({.spacing = 8}, Button("Group", [=] { doc.group_selection(); }).button_style(BorderedButtonStyle{}).help("Ctrl+G"),
                      If(any_group, Button("Ungroup", [=] { doc.ungroup_selection(); }).button_style(BorderedButtonStyle{}).help("Ctrl+Shift+G"))),
               If(single_frame,
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                         Toggle("Clip content", Binding<bool>([=] { return first().clips; }, [=](bool on) { set_frame([=](Node& n) { n.clips = on; }); })),
                         If([=] { return first().auto_layout; }, auto_layout,
                            Button("Add auto layout", [=] { doc.add_auto_layout(); }).button_style(BorderedButtonStyle{}).help("Shift+A"))),
                  Button("Wrap in auto layout", [=] { doc.add_auto_layout(); }).button_style(BorderedButtonStyle{}).help("Shift+A"))));
}

View NoSelection(const Document& doc) {
    const auto shortcut = [](std::string keys, std::string what) {
        return HStack(Text(std::move(what)).font(Font::system(12)).foreground_style(ThemeColor::secondary), Spacer(),
                      Text(std::move(keys)).font(Font::system(11, Weight::medium)));
    };
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                  Section("Page", VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                         ColorRow(doc, [=] { return doc.background(); }, [=](Color c) { doc.background = c; }),
                                         Toggle("Show grid", doc.show_grid.binding()),
                                         Toggle("Snap to layers", doc.snapping.binding()))),
                  Section("Shortcuts", VStack({.spacing = 7}, shortcut("V  F  R  O  L  T  H", "Tools"),
                                              shortcut("Space + drag", "Pan"), shortcut("Ctrl + wheel", "Zoom"),
                                              shortcut("Shift + 1", "Zoom to fit"), shortcut("Ctrl + D", "Duplicate"),
                                              shortcut("Ctrl + G", "Group"), shortcut("Shift + A", "Auto layout"),
                                              shortcut("Enter / Esc", "Into / out of a group"),
                                              shortcut("Ctrl + Z", "Undo"), shortcut("Arrows", "Nudge"),
                                              shortcut("Double-click", "Edit text"))));
}

View SelectionProperties(const Document& doc) {
    const auto first = [=] {
        const std::vector<Node> s = doc.selected();
        return s.empty() ? Node{} : s.front();
    };
    const auto bounds = [=] { return doc.selection_bounds().value_or(Rect{}); };
    const auto align = [=](std::string icon, std::string help, Document::Align how) {
        return IconButton(std::move(icon), std::move(help), [=] { doc.align(how); });
    };
    const auto set_all = [=](std::function<void(Node&)> change) {
        doc.update_selected(change);
    };
    const auto title = [=] {
        const std::vector<Node> s = doc.selected();
        return s.size() == 1 ? s.front().name : std::format("{} layers", s.size());
    };
    const View layout = Section(
        "Layout",
        VStack({.spacing = 8},
               HStack({.spacing = 8},
                      NumberField("X", [=] { return bounds().x; }, [=](float v) { Rect b = untrack(bounds); b.x = v; set_selection_bounds(doc, b); }),
                      NumberField("Y", [=] { return bounds().y; }, [=](float v) { Rect b = untrack(bounds); b.y = v; set_selection_bounds(doc, b); })),
               HStack({.spacing = 8},
                      NumberField("W", [=] { return bounds().width; },
                                  [=](float v) { Rect b = untrack(bounds); b.width = std::max(v, 1.f); set_selection_bounds(doc, b); }),
                      NumberField("H", [=] { return bounds().height; },
                                  [=](float v) { Rect b = untrack(bounds); b.height = std::max(v, 1.f); set_selection_bounds(doc, b); })),
               HStack({.spacing = 8},
                      NumberField("R", [=] { return first().radius; },
                                  [=](float v) {
                                      doc.checkpoint();
                                      set_all([=](Node& n) { n.radius = std::max(v, 0.f); });
                                  }),
                      NumberField("%", [=] { return first().opacity * 100; },
                                  [=](float v) {
                                      doc.checkpoint();
                                      set_all([=](Node& n) { n.opacity = std::clamp(v / 100, 0.f, 1.f); });
                                  }))));
    const auto toggle_row = [=](std::string title, std::function<bool(const Node&)> on, std::function<void(Node&, bool)> set, View content) {
        return Section(std::move(title),
                       VStack({.spacing = 8},
                              Toggle("Enabled", Binding<bool>([=] { return on(first()); },
                                                              [=](bool value) {
                                                                  doc.checkpoint();
                                                                  set_all([=](Node& n) { set(n, value); });
                                                              })),
                              If([=] { return on(first()); }, std::move(content))));
    };
    const View fill = toggle_row(
        "Fill", [](const Node& n) { return n.has_fill; }, [](Node& n, bool v) { n.has_fill = v; },
        ColorRow(doc, [=] { return first().fill; }, [=](Color c) { set_all([=](Node& n) { n.fill = c; }); }));
    const View stroke = toggle_row(
        "Stroke", [](const Node& n) { return n.has_stroke; }, [](Node& n, bool v) { n.has_stroke = v; },
        VStack({.spacing = 8}, ColorRow(doc, [=] { return first().stroke; }, [=](Color c) { set_all([=](Node& n) { n.stroke = c; }); }),
               NumberField("W", [=] { return first().stroke_width; },
                           [=](float v) {
                               doc.checkpoint();
                               set_all([=](Node& n) { n.stroke_width = std::max(v, 0.f); });
                           })));
    const View text = If([=] { return first().kind == Kind::text; },
                         Section("Text", VStack({.spacing = 8},
                                                NumberField("Aa", [=] { return first().font_size; },
                                                            [=](float v) {
                                                                doc.checkpoint();
                                                                set_all([=](Node& n) {
                                                                    n.font_size = std::clamp(v, 4.f, 400.f);
                                                                    const Size s = estimate_text_size(n.text, n.font_size, n.bold);
                                                                    n.w = s.width;
                                                                    n.h = s.height;
                                                                });
                                                            }),
                                                Toggle("Bold", Binding<bool>([=] { return first().bold; },
                                                                             [=](bool value) {
                                                                                 doc.checkpoint();
                                                                                 set_all([=](Node& n) { n.bold = value; });
                                                                             })))));
    const View arrange = Section(
        "Arrange", HStack({.spacing = 2}, IconButton("arrow-line-up", "Bring to front", [=] { doc.bring_to_front(); }),
                          IconButton("arrow-up", "Bring forward", [=] { doc.bring_forward(); }),
                          IconButton("arrow-down", "Send backward", [=] { doc.send_backward(); }),
                          IconButton("arrow-line-down", "Send to back", [=] { doc.send_to_back(); }), Spacer(),
                          IconButton("copy", "Duplicate (Ctrl+D)", [=] { doc.duplicate_selected(); }),
                          IconButton("trash", "Delete", [=] { doc.delete_selected(); })));
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                  Text([=] { return title(); })
                      .font(Font::system(13, Weight::semibold))
                      .line_limit(1)
                      .padding(EdgeInsets{.top = 12, .leading = 14, .bottom = 4, .trailing = 14}),
                  HStack({.spacing = 2}, align("align-left", "Align left", Document::Align::left),
                         align("align-center-horizontal", "Align horizontal centers", Document::Align::center_x),
                         align("align-right", "Align right", Document::Align::right),
                         align("align-top", "Align top", Document::Align::top),
                         align("align-center-vertical", "Align vertical centers", Document::Align::center_y),
                         align("align-bottom", "Align bottom", Document::Align::bottom))
                      .padding(EdgeInsets{.leading = 10, .bottom = 6})
                      .overlay(Rectangle().fill(ThemeColor::outline).frame({.max_width = infinity, .min_height = 1, .max_height = 1}),
                               Alignment::bottom),
                  layout, Structure(doc), fill, stroke, text, arrange);
}

} // namespace

View PropertiesPanel(const Document& doc) {
    // Rebuilt when the selection changes, so its fields start from the new layers.
    return ScrollView(ForEach(
               [=] {
                   std::string key;
                   for (const int id : doc.selection()) {
                       key += std::to_string(id) + ",";
                   }
                   return std::vector<std::string>{key};
               },
               [=](const std::string& key) { return key.empty() ? NoSelection(doc) : SelectionProperties(doc); }))
        .frame({.min_width = 260, .max_width = 260, .max_height = infinity, .alignment = Alignment::top_leading})
        .background(ThemeColor::surface)
        .overlay(Rectangle().fill(ThemeColor::outline).frame({.min_width = 1, .max_width = 1, .max_height = infinity}),
                 Alignment::leading);
}

} // namespace design
