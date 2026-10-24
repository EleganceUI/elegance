// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "chrome.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace shell {

using namespace elg;

namespace {

constexpr float kTopBarHeight = 30;
// The dock floats this far above the bottom; windows stay above the dock and its margin.
constexpr float kDockMargin = 12;
constexpr float kDockReserve = 76;
constexpr float kDockIcon = 32;
constexpr float kTitleBarHeight = 38;
constexpr float kWindowRadius = 10;
constexpr float kMinWindowWidth = 280;
constexpr float kMinWindowHeight = 180;

// Panels over the wallpaper: the theme's surface, slightly see-through.
ShapeStyle glass(const EnvironmentValues& env, float alpha = 0.86f) {
    return env.color(ThemeColor::surface).opacity(alpha);
}

// --- Drag gesture ---

class DragHandler final : public EventHandler {
public:
    explicit DragHandler(DragActions actions) : actions_(std::move(actions)) {}

    EventResult pointer(const PointerEvent& event, Rect) override {
        switch (event.kind) {
        case PointerEvent::Kind::down:
            if (event.button != MouseButton::left) {
                return {};
            }
            if (event.click_count == 2 && actions_.double_clicked) {
                actions_.double_clicked();
                return {.consumed = true, .redraw = true};
            }
            dragging_ = true;
            start_ = event.location;
            if (actions_.began) {
                actions_.began();
            }
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::move:
            if (!dragging_) {
                return {};
            }
            if (actions_.changed) {
                actions_.changed({event.location.x - start_.x, event.location.y - start_.y});
            }
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel:
            if (!dragging_) {
                return {};
            }
            dragging_ = false;
            if (actions_.ended) {
                actions_.ended();
            }
            return {.consumed = true, .redraw = true};
        }
        return {};
    }

private:
    DragActions actions_;
    bool dragging_ = false;
    Point start_;
};

class DragModifier final : public EventModifier {
public:
    explicit DragModifier(DragActions actions) : actions_(std::move(actions)) {}
    std::unique_ptr<EventHandler> make_handler() const override { return std::make_unique<DragHandler>(actions_); }

private:
    DragActions actions_;
};

// --- The window area: reports its size, for maximizing and placing windows ---

class AreaLayout final : public Layout {
public:
    explicit AreaLayout(Desktop desktop) : desktop_(std::move(desktop)) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild>) const override {
        return proposal.replacing_unspecified({1280, 720});
    }

    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        for (const LayoutChild& child : children) {
            child.place({bounds.x, bounds.y}, ProposedSize{bounds.width, bounds.height});
        }
        const Size size{bounds.width, bounds.height};
        const Size known = desktop_.area.peek();
        if (size.width > 0 && size.height > 0 &&
            (!*desktop_.area_measured || size.width != known.width || size.height != known.height)) {
            // Not during layout: on the next turn of the loop.
            post_to_ui_thread([desktop = desktop_, size] { desktop.set_area(size); });
        }
    }

private:
    Desktop desktop_;
};

// --- Windows ---

// An icon whose name follows state (Image takes a fixed name): rebuilt when the name changes.
View DynamicImage(std::function<std::string()> name) {
    return ForEach([name] { return std::vector<std::string>{name()}; }, [](const std::string& icon) { return Image(icon); });
}

View CaptionButton(std::string icon, std::function<void()> action, std::string label) {
    return Button(Image(std::move(icon)).font(Font::system(13)), std::move(action))
        .button_style(HoverButtonStyle(6))
        .frame(30, 26)
        .foreground_style(ThemeColor::secondary)
        .accessibility_label(std::move(label));
}

View WindowView(const Desktop& desktop, int id) {
    const Window w = desktop.window(id);
    const Desktop d = desktop;
    const auto focused = [=] { return d.focused() == id; };
    const auto maximized = [=] { return w.maximized(); };
    const auto size = [=]() -> Size {
        if (w.maximized()) {
            return d.area();
        }
        return {w.width(), w.height()};
    };

    // Dragging the title bar moves the window; from maximized, it restores under the pointer.
    const auto origin = std::make_shared<Point>();
    const View title_bar =
        Draggable(HStack({.spacing = 8}, Image(app_info(w.app).icon).font(Font::system(15)).foreground_style(ThemeColor::secondary),
                         Text([=] { return w.title(); }).font(Font::system(13, Weight::semibold)).line_limit(1),
                         Spacer(),
                         HStack({.spacing = 2},
                                CaptionButton("minus", [=] { d.minimize(id); }, "Minimize"),
                                CaptionButton("corners-out", [=] { d.toggle_maximized(id); }, "Maximize"),
                                CaptionButton("x", [=] { d.close(id); }, "Close")))
                      .padding(EdgeInsets{.leading = 12, .trailing = 6})
                      .frame({.max_width = infinity, .min_height = kTitleBarHeight, .max_height = kTitleBarHeight})
                      .content_shape(Rectangle()),
                  {.began =
                       [=] {
                           d.focus(id);
                           if (w.maximized.peek()) {
                               const Size area = d.area.peek();
                               batch([&] {
                                   w.x = std::max(0.f, (area.width - w.width.peek()) / 2);
                                   w.y = 0.f;
                                   w.maximized = false;
                               });
                           }
                           *origin = {w.x.peek(), w.y.peek()};
                       },
                   .changed =
                       [=](Point delta) {
                           const Size area = d.area.peek();
                           batch([&] {
                               w.x = std::clamp(origin->x + delta.x, 80 - w.width.peek(), area.width - 80);
                               w.y = std::clamp(origin->y + delta.y, 0.f, area.height - kTitleBarHeight);
                           });
                       },
                   .double_clicked = [=] { d.toggle_maximized(id); }});

    // The corner handle resizes.
    const auto start_size = std::make_shared<Size>();
    const View resize_handle =
        If([=] { return !w.maximized(); },
           Draggable(Rectangle().fill(Color::clear).frame(18, 18).content_shape(Rectangle()),
                     {.began =
                          [=] {
                              d.focus(id);
                              *start_size = {w.width.peek(), w.height.peek()};
                          },
                      .changed =
                          [=](Point delta) {
                              const Size area = d.area.peek();
                              batch([&] {
                                  w.width = std::clamp(start_size->width + delta.x, kMinWindowWidth, area.width - w.x.peek());
                                  w.height = std::clamp(start_size->height + delta.y, kMinWindowHeight, area.height - w.y.peek());
                              });
                          }})
               .help("Resize"));

    // An inactive window's first click on its content brings it forward (title bars do that
    // themselves, and still drag).
    const View activation_shield =
        If([=] { return !focused(); },
           Draggable(Rectangle().fill(Color::clear).content_shape(Rectangle()), {.began = [=] { d.focus(id); }})
               .padding(EdgeInsets{.top = kTitleBarHeight}));

    const View body = VStack({.spacing = 0}, title_bar, Divider(),
                             AppContent(d, w).frame({.max_width = infinity, .max_height = infinity}));

    return body.clip_shape(RoundedRectangle(kWindowRadius))
        .background(View(RoundedRectangle(kWindowRadius).fill(ThemeColor::background))
                        .shadow({.color = Color::black.opacity(0.4f), .radius = 22, .y = 8}))
        .overlay(RoundedRectangle(kWindowRadius).stroke_border([](const EnvironmentValues& env) {
            return env.color(ThemeColor::outline);
        }))
        .overlay(activation_shield)
        .overlay(resize_handle, Alignment::bottom_trailing)
        .frame(Prop<Size>(size), Alignment::top_leading)
        .opacity([=] { return w.minimized() ? 0.f : 1.f; })
        .z_index([=] {
            const std::vector<int>& order = d.stack();
            return static_cast<float>(std::ranges::find(order, id) - order.begin());
        })
        // Last before the transition, so it scales the window about its own middle.
        .offset([=] { return maximized() ? 0.f : w.x(); },
                [=] { return (maximized() ? 0.f : w.y()) + (w.minimized() ? d.area().height : 0.f); })
        .transition(Transition::scale(0.92f).combined(Transition::opacity()));
}

// --- Desktop ---

View WallpaperView(const Desktop& d) {
    const Settings s = d.settings;
    return ZStack(Rectangle().fill([=] { return ShapeStyle(Gradient(wallpapers()[static_cast<std::size_t>(s.wallpaper())].colors)); }),
                  // A soft light from the top, for depth.
                  Rectangle().fill(RadialGradient({Color::white.opacity(0.16f), Color::white.opacity(0.f)},
                                                  UnitPoint{0.3f, 0.0f}, 0, 900)))
        .animation(Animation::ease_in_out(0.6), [=] { return s.wallpaper(); });
}

View DesktopIcon(const Desktop& d, AppKind app, std::string label, State<int> selected, int index) {
    const auto is_selected = [=] { return selected() == index; };
    return VStack({.spacing = 6}, AppIcon(app, 48),
                  Text(std::move(label))
                      .font(Font::system(12, Weight::medium))
                      .foreground_style(Color::white)
                      .line_limit(2))
        .padding(8)
        .frame({.width = 92})
        .background([=](const EnvironmentValues&) {
            return is_selected() ? ShapeStyle(Color::white.opacity(0.22f)) : ShapeStyle(Color::clear);
        }, RoundedRectangle(8))
        .content_shape(Rectangle())
        .on_tap_gesture([=] { d.open(app); }, 2)
        .on_tap_gesture([=] { selected = index; })
        .accessibility_element()
        .accessibility_action([=] { d.open(app); });
}

View DesktopIcons(const Desktop& d) {
    const State<int> selected(-1);
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  DesktopIcon(d, AppKind::files, "Home", selected, 0),
                  DesktopIcon(d, AppKind::notes, "Welcome.txt", selected, 1),
                  DesktopIcon(d, AppKind::terminal, "Terminal", selected, 2),
                  DesktopIcon(d, AppKind::monitor, "System Monitor", selected, 3))
        .padding(20)
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading})
        .content_shape(Rectangle())
        .on_tap_gesture([=] {
            selected = -1;
            d.launcher_open = false;
            d.quick_settings_open = false;
        });
}

// --- Top bar and dock ---

// A translucent panel: the theme's surface, mostly see-through, with a thin light edge.
View Glass(View content, float radius, float alpha) {
    return std::move(content)
        .background([=](const EnvironmentValues& env) { return glass(env, alpha); }, RoundedRectangle(radius))
        .overlay(RoundedRectangle(radius).stroke_border(Color::white.opacity(0.14f)));
}

// The dock's tiles lift while hovered.
View LiftOnHover(View tile, State<bool> hovered) {
    return std::move(tile)
        .offset(0.f, [=] { return hovered() ? -6.f : 0.f; })
        .shadow({.color = Color::black.opacity(0.25f), .radius = 6, .y = 3});
}

View DockIcon(const Desktop& d, AppKind app) {
    const State<bool> hovered(false);
    const auto running = [=] { return d.is_running(app); };
    const auto in_front = [=] {
        const int id = d.focused();
        return id != 0 && d.window(id).app == app;
    };
    // The name floats above the icon while hovered.
    const View name = Glass(Text(app_info(app).name).font(Font::system(12, Weight::medium)).padding(EdgeInsets::symmetric(10, 4)),
                            8, 0.85f)
                          .fixed_size()
                          .offset(0.f, -32.f)
                          .allows_hit_testing(false);
    return Button(VStack({.spacing = 5}, LiftOnHover(AppIcon(app, kDockIcon), hovered),
                         Circle()
                             .fill([=](const EnvironmentValues& env) {
                                 return ShapeStyle(running() ? env.color(ThemeColor::primary).opacity(in_front() ? 0.95f : 0.55f)
                                                             : Color::clear);
                             })
                             .frame(5, 5)),
                  [=] { d.activate_app(app); })
        .button_style(PlainButtonStyle{})
        .overlay(If([=] { return hovered(); }, name.transition(Transition::opacity())), Alignment::top)
        .on_hover([=](bool inside) { hovered = inside; }, HoverDelay{0, 0})
        .animation(Animation::spring(0.28, 0.7), [=] { return hovered(); })
        .accessibility_label(app_info(app).name);
}

View LauncherDockIcon(const Desktop& d) {
    const State<bool> hovered(false);
    const Settings s = d.settings;
    const View tile = Image("squares-four.fill")
                          .font(Font::system(kDockIcon * 0.5f))
                          .foreground_style(Color::white)
                          .frame(kDockIcon, kDockIcon)
                          .background([=] {
                              return ShapeStyle(Gradient({accents()[static_cast<std::size_t>(s.accent())], Color::hex(0x5E5CE6)}));
                          }, RoundedRectangle(kDockIcon * 0.24f));
    return Button(VStack({.spacing = 5}, LiftOnHover(tile, hovered), Rectangle().fill(Color::clear).frame(5, 5)),
                  [=] {
                      with_animation(Animation::spring(0.3, 0.85), [&] {
                          d.launcher_open = !d.launcher_open.peek();
                          d.quick_settings_open = false;
                          d.launcher_search = std::string();
                      });
                  })
        .button_style(PlainButtonStyle{})
        .on_hover([=](bool inside) { hovered = inside; }, HoverDelay{0, 0})
        .animation(Animation::spring(0.28, 0.7), [=] { return hovered(); })
        .accessibility_label("Launcher");
}

View Dock(const Desktop& d) {
    std::vector<int> kinds;
    for (const AppInfo& info : all_apps()) {
        kinds.push_back(static_cast<int>(info.kind));
    }
    return Glass(HStack({.alignment = VerticalAlignment::bottom, .spacing = 8}, LauncherDockIcon(d),
                        Rectangle().fill(Color::white.opacity(0.22f)).frame(1, 24).padding(EdgeInsets{.bottom = 9}),
                        ForEach(kinds, [=](int kind) { return DockIcon(d, static_cast<AppKind>(kind)); }))
                     .padding(EdgeInsets{.top = 8, .leading = 10, .bottom = 4, .trailing = 10}),
                 18, 0.22f)
        .shadow({.color = Color::black.opacity(0.3f), .radius = 24, .y = 8});
}

View Tray(const Desktop& d) {
    const Settings s = d.settings;
    const auto speaker = [=] {
        if (s.muted() || s.volume() < 0.01f) {
            return std::string("speaker-slash");
        }
        return std::string(s.volume() < 0.5f ? "speaker-low" : "speaker-high");
    };
    return Button(HStack({.spacing = 10},
                         DynamicImage([=] { return std::string(s.wifi() ? "wifi-high" : "wifi-slash"); }),
                         DynamicImage(speaker),
                         HStack({.spacing = 4}, Image("battery-high"), Text("82%").font(Font::system(12, Weight::medium))))
                      .font(Font::system(15))
                      .padding(EdgeInsets::symmetric(9, 3)),
                  [=] {
                      with_animation(Animation::spring(0.3, 0.85), [&] {
                          d.quick_settings_open = !d.quick_settings_open.peek();
                          d.launcher_open = false;
                      });
                  })
        .button_style(HoverButtonStyle(6, [=] { return d.quick_settings_open(); }))
        .help("Quick settings");
}

View TopBar(const Desktop& d) {
    const auto front_app = [=] {
        const int id = d.focused();
        return id == 0 ? std::string("Desktop") : app_info(d.window(id).app).name;
    };
    const View menu =
        Button(HStack({.spacing = 6}, Image("squares-four.fill").foreground_style(ThemeColor::accent),
                      Text("Elegance").font(Font::system(13, Weight::bold)))
                   .padding(EdgeInsets::symmetric(9, 3)),
               [=] {
                   with_animation(Animation::spring(0.3, 0.85), [&] {
                       d.launcher_open = !d.launcher_open.peek();
                       d.quick_settings_open = false;
                       d.launcher_search = std::string();
                   });
               })
            .button_style(HoverButtonStyle(6, [=] { return d.launcher_open(); }));
    const View clock = Text([=] { return format_time(d.now(), "%a %d %b   %H:%M"); })
                           .font(Font::system(13, Weight::semibold))
                           .allows_hit_testing(false);
    return HStack({.spacing = 6}, menu, Text([=] { return front_app(); }).font(Font::system(13, Weight::semibold)), Spacer(),
                  Tray(d),
                  Button(DynamicImage([=] { return std::string(d.settings.do_not_disturb() ? "bell-slash" : "bell"); })
                             .font(Font::system(15))
                             .padding(EdgeInsets::symmetric(8, 3)),
                         [=] { d.notify("Notifications", "You're all caught up.", "check-circle"); })
                      .button_style(HoverButtonStyle(6))
                      .help("Notifications"))
        .padding(EdgeInsets::symmetric(6, 0))
        .frame({.max_width = infinity, .min_height = kTopBarHeight, .max_height = kTopBarHeight})
        .overlay(clock)
        .background([](const EnvironmentValues& env) { return glass(env, 0.5f); })
        .overlay(Rectangle().fill(Color::white.opacity(0.08f)).frame({.max_width = infinity, .min_height = 1, .max_height = 1}),
                 Alignment::bottom);
}

// --- Launcher ---

View Avatar(const Desktop& d, float size) {
    const Settings s = d.settings;
    return Text([=] {
               std::string initials;
               const std::string& name = s.user_name();
               for (std::size_t i = 0; i < name.size(); ++i) {
                   if ((i == 0 || name[i - 1] == ' ') && name[i] != ' ' && initials.size() < 2) {
                       initials += static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
                   }
               }
               return initials;
           })
        .font(Font::system(size * 0.38f, Weight::semibold))
        .foreground_style(Color::white)
        .frame(size, size)
        .background([=] { return ShapeStyle(Gradient({accents()[static_cast<std::size_t>(s.accent())], Color::hex(0x5E5CE6)})); },
                    Circle());
}

View LauncherTile(const Desktop& d, const AppInfo& info) {
    const AppKind kind = info.kind;
    return Button(VStack({.spacing = 8}, AppIcon(kind, 52), Text(info.name).font(Font::system(12)).line_limit(1))
                      .frame({.width = 96})
                      .padding(EdgeInsets::symmetric(4, 10)),
                  [=] { d.open(kind); })
        .button_style(HoverButtonStyle(10));
}

View Launcher(const Desktop& d) {
    const State<bool> search_focused(true);
    const State<std::string> search = d.launcher_search;
    const auto matches = [=] {
        std::vector<int> result;
        std::string query = search();
        std::ranges::transform(query, query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (std::size_t i = 0; i < all_apps().size(); ++i) {
            std::string name = all_apps()[i].name;
            std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (query.empty() || name.find(query) != std::string::npos) {
                result.push_back(static_cast<int>(i));
            }
        }
        return result;
    };
    const auto open_first = [=] {
        if (const std::vector<int> found = matches(); !found.empty()) {
            d.open(all_apps()[static_cast<std::size_t>(found.front())].kind);
        }
    };
    const auto power_button = [](std::string icon, std::string label, std::function<void()> action) {
        return Button(Image(std::move(icon)).font(Font::system(18)).padding(8), std::move(action))
            .button_style(HoverButtonStyle(8))
            .help(label)
            .accessibility_label(label);
    };
    App* const app = d.app;
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 14},
                  TextField("Search apps", search.binding(), {.on_submit = open_first})
                      .focused(search_focused.binding()),
                  Text("Apps").font(Font::system(13, Weight::semibold)).foreground_style(ThemeColor::secondary),
                  If([=] { return matches().empty(); },
                     Text("No apps match.").foreground_style(ThemeColor::secondary).padding(EdgeInsets::symmetric(0, 30)),
                     HStack({.spacing = 6}, ForEach(matches, [=](int index) {
                                return LauncherTile(d, all_apps()[static_cast<std::size_t>(index)]);
                            }))),
                  Spacer(),
                  Divider(),
                  HStack({.spacing = 10}, Avatar(d, 34),
                         Text([s = d.settings] { return s.user_name(); }).font(Font::system(14, Weight::medium)),
                         Spacer(),
                         power_button("lock", "Lock", [=] { d.lock(); }),
                         power_button("sign-out", "Sign out", [=] { d.sign_out(); }),
                         power_button("power", "Exit the demo", [app] { app->quit(); })))
        .padding(22)
        .frame({.width = 700, .height = 330})
        .background([](const EnvironmentValues& env) { return glass(env, 0.95f); }, RoundedRectangle(14))
        .overlay(RoundedRectangle(14).stroke_border([](const EnvironmentValues& env) { return env.color(ThemeColor::outline); }))
        .shadow({.color = Color::black.opacity(0.35f), .radius = 24, .y = 10})
        .on_key_press(Key::escape, [=] { with_animation(Animation::smooth(0.25), [&] { d.launcher_open = false; }); });
}

// --- Quick settings ---

View QuickTile(std::string title, std::string icon, State<bool> on, std::function<void(bool)> changed = {}) {
    return Button(HStack({.spacing = 10},
                         Image(std::move(icon))
                             .font(Font::system(17))
                             .foreground_style([=](const EnvironmentValues& env) {
                                 return on() ? env.color(ThemeColor::on_accent) : env.color(ThemeColor::primary);
                             })
                             .frame(34, 34)
                             .background([=](const EnvironmentValues& env) {
                                 return on() ? ShapeStyle(env.color(ThemeColor::accent))
                                             : ShapeStyle(env.color(ThemeColor::surface_raised));
                             }, Circle()),
                         VStack({.alignment = HorizontalAlignment::leading, .spacing = 1},
                                Text(std::move(title)).font(Font::system(13, Weight::medium)),
                                Text([=] { return std::string(on() ? "On" : "Off"); })
                                    .font(Font::system(11))
                                    .foreground_style(ThemeColor::secondary)),
                         Spacer())
                      .padding(8),
                  [=] {
                      on = !on.peek();
                      if (changed) {
                          changed(on.peek());
                      }
                  })
        .button_style(HoverButtonStyle(10))
        .frame({.max_width = infinity});
}

View QuickSettings(const Desktop& d) {
    const Settings s = d.settings;
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  HStack({.spacing = 8}, QuickTile("Wi-Fi", "wifi-high", s.wifi), QuickTile("Bluetooth", "bluetooth", s.bluetooth)),
                  HStack({.spacing = 8}, QuickTile("Dark mode", "moon", s.dark),
                         QuickTile("Night light", "sun-horizon", s.night_light)),
                  HStack({.spacing = 8},
                         QuickTile("Do not disturb", "bell-slash", s.do_not_disturb,
                                   [=](bool on) {
                                       if (on) {
                                           with_animation(Animation::ease_out(0.2), [&] { d.toasts = std::vector<Toast>{}; });
                                       }
                                   }),
                         QuickTile("Mute", "speaker-slash", s.muted)),
                  HStack({.spacing = 10}, Image("speaker-high").foreground_style(ThemeColor::secondary).frame({.width = 20}),
                         Slider(s.volume.binding())),
                  Divider(),
                  HStack({.spacing = 8}, Image("battery-high"), Text("82% - about 4 h left").font(Font::system(12)),
                         Spacer(),
                         Button(Image("gear-six").font(Font::system(16)).padding(6),
                                [=] {
                                    d.quick_settings_open = false;
                                    d.open(AppKind::settings);
                                })
                             .button_style(HoverButtonStyle(8))
                             .help("All settings"),
                         Button(Image("power").font(Font::system(16)).padding(6), [=] { d.lock(); })
                             .button_style(HoverButtonStyle(8))
                             .help("Lock")))
        .padding(16)
        .frame({.width = 380})
        .background([](const EnvironmentValues& env) { return glass(env, 0.95f); }, RoundedRectangle(14))
        .overlay(RoundedRectangle(14).stroke_border([](const EnvironmentValues& env) { return env.color(ThemeColor::outline); }))
        .shadow({.color = Color::black.opacity(0.35f), .radius = 24, .y = 10});
}

// --- Notifications ---

View ToastView(const Desktop& d, const Toast& toast) {
    const int id = toast.id;
    return HStack({.alignment = VerticalAlignment::top, .spacing = 12},
                  Image(toast.icon)
                      .font(Font::system(18))
                      .foreground_style(ThemeColor::on_accent)
                      .frame(36, 36)
                      .background(ThemeColor::accent, RoundedRectangle(9)),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 3},
                         Text(toast.title).font(Font::system(13, Weight::semibold)),
                         Text(toast.body).font(Font::system(12)).foreground_style(ThemeColor::secondary).line_limit(3)),
                  Spacer(),
                  Button(Image("x").font(Font::system(11)).padding(4),
                         [=] {
                             with_animation(Animation::ease_out(0.2), [&] {
                                 d.toasts.update([&](std::vector<Toast>& list) {
                                     std::erase_if(list, [&](const Toast& t) { return t.id == id; });
                                 });
                             });
                         })
                      .button_style(HoverButtonStyle(6))
                      .foreground_style(ThemeColor::secondary))
        .padding(14)
        .frame({.width = 360})
        .background([](const EnvironmentValues& env) { return glass(env, 0.96f); }, RoundedRectangle(12))
        .overlay(RoundedRectangle(12).stroke_border([](const EnvironmentValues& env) { return env.color(ThemeColor::outline); }))
        .shadow({.color = Color::black.opacity(0.3f), .radius = 16, .y = 6})
        .transition(Transition::move(Edges::trailing).combined(Transition::opacity()));
}

View Toasts(const Desktop& d) {
    return VStack({.alignment = HorizontalAlignment::trailing, .spacing = 10},
                  ForEach([=] { return d.toasts(); }, &Toast::id, [=](const Toast& toast) { return ToastView(d, toast); }))
        .padding(16);
}

// --- Login ---

View LoginScreen(const Desktop& d) {
    const State<std::string> password;
    const State<bool> focused(true);
    const State<bool> wrong(false);
    const auto submit = [=] {
        if (password.peek().empty()) {
            with_animation(Animation::spring(0.25, 0.4), [&] { wrong = true; });
            return;
        }
        batch([&] {
            password = std::string();
            wrong = false;
        });
        d.sign_in();
    };
    App* const app = d.app;
    const View center =
        VStack({.spacing = 14}, Avatar(d, 96).shadow({.color = Color::black.opacity(0.3f), .radius = 14, .y = 4}),
               Text([s = d.settings] { return s.user_name(); }).font(Font::system(24, Weight::semibold)),
               HStack({.spacing = 8},
                      SecureField("Password", password.binding(), {.on_submit = submit})
                          .focused(focused.binding())
                          .frame({.width = 240}),
                      Button(Image("arrow-right").font(Font::system(16, Weight::bold)).padding(8), submit)
                          .button_style(HoverButtonStyle(8))
                          .accessibility_label("Sign in")),
               Text([=] { return std::string(wrong() ? "Type any password to sign in." : "Any password works in this demo."); })
                   .font(Font::system(12))
                   .foreground_style([=](const EnvironmentValues&) {
                       return wrong() ? Color::hex(0xFF8A80) : Color::white.opacity(0.7f);
                   }))
            .foreground_style(Color::white)
            .color_scheme(ColorScheme::dark);
    return ZStack(WallpaperView(d), Rectangle().fill(Color::black.opacity(0.42f)),
                  VStack({.spacing = 0},
                         VStack({.spacing = 2},
                                Text([=] { return format_time(d.now(), "%H:%M"); }).font(Font::system(96, Weight::thin)),
                                Text([=] { return format_time(d.now(), "%A, %d %B"); }).font(Font::system(20, Weight::medium)))
                             .foreground_style(Color::white)
                             .padding(EdgeInsets{.top = 90}),
                         Spacer(), center, Spacer(),
                         HStack(Text("Elegance Desktop").font(Font::system(12)).foreground_style(Color::white.opacity(0.6f)),
                                Spacer(),
                                Button(Image("power").font(Font::system(20)).foreground_style(Color::white).padding(10),
                                       [app] { app->quit(); })
                                    .button_style(HoverButtonStyle(10))
                                    .help("Exit the demo"))
                             .padding(24)))
        .content_shape(Rectangle())
        .transition(Transition::opacity());
}

// Closes the launcher and quick settings when clicking anywhere else.
View DismissLayer(const Desktop& d) {
    return If([=] { return d.launcher_open() || d.quick_settings_open(); },
              Rectangle()
                  .fill(Color::clear)
                  .content_shape(Rectangle())
                  .on_tap_gesture([=] {
                      with_animation(Animation::smooth(0.25), [&] {
                          d.launcher_open = false;
                          d.quick_settings_open = false;
                      });
                  }));
}

} // namespace

// --- Shared pieces ---

LinearGradient Gradient(const std::vector<Color>& colors, UnitPoint start, UnitPoint end) {
    std::vector<GradientStop> stops;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        stops.push_back({colors[i], colors.size() > 1 ? static_cast<float>(i) / static_cast<float>(colors.size() - 1) : 0.f});
    }
    return LinearGradient(std::move(stops), start, end);
}

View AppIcon(AppKind app, float size) {
    const AppInfo& info = app_info(app);
    return Image(info.icon + ".fill")
        .font(Font::system(size * 0.5f))
        .foreground_style(Color::white)
        .frame(size, size)
        .background(ShapeStyle(Gradient(info.colors)), RoundedRectangle(size * 0.24f))
        .accessibility_label(info.name);
}

View HoverButtonStyle::make_body(const ButtonConfiguration& button) const {
    const std::function<bool()> selected = selected_;
    return button.label.background(
        [=](const EnvironmentValues& env) {
            const Color base = env.color(ThemeColor::primary);
            if (button.is_pressed()) {
                return ShapeStyle(base.opacity(0.16f));
            }
            if (selected && selected()) {
                return ShapeStyle(base.opacity(0.12f));
            }
            return ShapeStyle(button.is_hovered() ? base.opacity(0.08f) : Color::clear);
        },
        RoundedRectangle(corner_radius_));
}

View Draggable(View content, DragActions actions) {
    return content.modifier(DragModifier(std::move(actions)));
}

View Shell(const Desktop& desktop) {
    const Desktop d = desktop;
    const Settings s = d.settings;
    const View windows = AreaLayout(d)(
        ZStack({.alignment = Alignment::top_leading},
               Rectangle().fill(Color::clear).allows_hit_testing(false),
               ForEach([=] { return d.open_windows(); }, [=](int id) { return WindowView(d, id); })));
    const auto gap = [](float height) {
        return Rectangle()
            .fill(Color::clear)
            .frame({.max_width = infinity, .min_height = height, .max_height = height})
            .allows_hit_testing(false);
    };
    const View desktop_view =
        ZStack({.alignment = Alignment::top_leading}, WallpaperView(d),
               DesktopIcons(d).padding(EdgeInsets{.top = kTopBarHeight}),
               VStack({.spacing = 0}, gap(kTopBarHeight), windows, gap(kDockReserve)),
               TopBar(d),
               Dock(d)
                   .padding(EdgeInsets{.bottom = kDockMargin})
                   .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::bottom}),
               DismissLayer(d),
               If([=] { return d.launcher_open(); },
                  Launcher(d)
                      .padding(EdgeInsets{.bottom = kDockReserve + 4})
                      .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::bottom})
                      .transition(Transition::move(Edges::bottom).combined(Transition::opacity()))),
               If([=] { return d.quick_settings_open(); },
                  QuickSettings(d)
                      .padding(EdgeInsets{.top = kTopBarHeight + 8, .trailing = 8})
                      .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_trailing})
                      .transition(Transition::move(Edges::top).combined(Transition::opacity()))),
               Toasts(d)
                   .padding(EdgeInsets{.top = kTopBarHeight})
                   .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_trailing}));
    return ZStack(If([=] { return !d.locked(); }, desktop_view.transition(Transition::opacity())),
                  If([=] { return d.locked(); }, LoginScreen(d)),
                  // Night light: a warm tint over everything, letting clicks through.
                  Rectangle()
                      .fill(Color::hex(0xFF8A00))
                      .opacity([=] { return s.night_light() ? s.night_strength() * 0.45f : 0.f; })
                      .animation(Animation::ease_in_out(0.5), [=] { return s.night_light(); })
                      .allows_hit_testing(false))
        .color_scheme([=] { return s.dark() ? ColorScheme::dark : ColorScheme::light; })
        .tint([=] { return accents()[static_cast<std::size_t>(s.accent())]; })
        .on_key_press(Key::escape, [=] {
            with_animation(Animation::smooth(0.25), [&] {
                d.launcher_open = false;
                d.quick_settings_open = false;
            });
        });
}

} // namespace shell
