// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Sandbox: a real app built against the public Elegance API, used to exercise features
// as they land. Only include public headers here, never anything from src/.
//
//   sandbox                  open the demo windows
//   sandbox --smoke-test     render the first frame, then exit (for automated checks)
//   sandbox --light          start in light mode
//   sandbox --only-main      open only the main window

#include "contacts.h"
#include "palette.h"
#include "todos.h"

#include <elg/elegance.h>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace {

using namespace elg;

// A custom ButtonStyle: a tinted circle that deepens on hover and press.
struct PillButtonStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        const auto tint = Environment<TintKey>();
        return button.label.font(Font::headline)
            .foreground_style([=] { return button.is_enabled() ? tint() : tint().opacity(0.35f); })
            .frame(32, 32)
            .background(
                [=] {
                    const float strength = !button.is_enabled() ? 0.06f
                                           : button.is_pressed() ? 0.4f
                                           : button.is_hovered() ? 0.24f
                                                                 : 0.14f;
                    return tint().opacity(strength);
                },
                Capsule());
    }
};

View TitleBar(State<ColorScheme> scheme) {
    // Animated: every theme color cross-fades.
    const auto toggle = [=] {
        with_animation(Animation::ease_in_out(0.35), [=] {
            scheme = scheme.peek() == ColorScheme::dark ? ColorScheme::light : ColorScheme::dark;
        });
    };
    return HStack({.alignment = VerticalAlignment::center, .spacing = 12},
                  Text("Elegance").font(Font::title2),
                  Text("Sandbox").foreground_style(ThemeColor::secondary),
                  Spacer(),
                  Button(Text([=] { return scheme() == ColorScheme::dark ? "Light mode" : "Dark mode"; }), toggle)
                      .button_style(BorderedButtonStyle{}),
                  Circle().fill(palette::red).frame(14, 14),
                  Circle().fill(palette::peach).frame(14, 14),
                  Circle().fill(palette::green).frame(14, 14))
        .padding(Edges::horizontal, 32)
        .frame({.height = 64})
        .background(ThemeColor::surface);
}

// A shape above its caption.
View Captioned(View shape, std::string caption) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  shape,
                  Text(std::move(caption)).font(Font::caption).foreground_style(ThemeColor::secondary));
}

// Three equally flexible cards share the row's width.
View ShapeCards() {
    return HStack({.spacing = 24},
                  Captioned(RoundedRectangle({.top_left = 48, .top_right = 4, .bottom_right = 48, .bottom_left = 4})
                                .fill(palette::mauve),
                            "RoundedRectangle with per-corner radii")
                      .card(),
                  Captioned(RoundedRectangle(28).stroke_border(palette::green, 6), "stroke_border(green, 6)").card(),
                  Captioned(Circle().fill(palette::sky).stroke_border(Color::white.opacity(0.85f), 3),
                            "Circle: fill + stroke_border")
                      .card())
        .frame({.height = 170});
}

// Wrapping, truncation, and alignment.
// Popups: a context menu with a submenu, a menu button with checkable items, a menu Picker, and
// a tooltip. Each reports what was chosen.
View MenuCards() {
    const auto last = State(std::string("Nothing chosen yet"));
    const auto say = [=](std::string what) { return [=] { last = what; }; };

    const View target =
        Text("Right-click me")
            .font(Font::headline)
            .frame({.max_width = infinity, .min_height = 64})
            .background(ThemeColor::surface_raised, RoundedRectangle(10))
            .context_menu([=] {
                return Menu(MenuItem("Rename", say("Rename"), {.shortcut = "F2", .image = {{.system_name = "pencil-simple"}}}),
                            MenuItem("Duplicate", say("Duplicate"),
                                     {.shortcut = "Ctrl+D", .image = {{.system_name = "copy-simple"}}}),
                            Separator(),
                            Submenu("Move to", [=] {
                                return Menu(MenuItem("Documents", say("Moved to Documents")),
                                            MenuItem("Pictures", say("Moved to Pictures")),
                                            MenuItem("Archive", say("Moved to Archive")).disabled());
                            }),
                            Separator(),
                            MenuItem("Delete", say("Delete"), {.destructive = true, .image = {{.system_name = "trash"}}}));
            });

    const auto grid = State(true);
    const auto rulers = State(false);
    const View view_menu = MenuButton("View", [=] {
        return Menu(MenuItem("Show grid", [=] { grid = !grid(); }, {.checked = grid}),
                    MenuItem("Show rulers", [=] { rulers = !rulers(); }, {.checked = rulers}),
                    Separator(),
                    MenuItem("Zoom to fit", say("Zoom to fit"), {.shortcut = "Ctrl+0"}));
    }).button_style(BorderedButtonStyle{});

    const auto size = State(std::string("Medium"));
    const View picker = Picker(size.binding(), {{"Small", "Small"}, {"Medium", "Medium"}, {"Large", "Large"}},
                               PickerStyle::menu);

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                   target,
                                   Text([=] { return last(); }).foreground_style(ThemeColor::secondary))
                                .frame({.width = 260}),
                            "context_menu + Submenu")
                      .card(),
                  Captioned(VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                   HStack({.spacing = 10}, view_menu, picker),
                                   Text([=] {
                                       return std::format("Grid {}, rulers {}, size {}", grid() ? "on" : "off",
                                                          rulers() ? "on" : "off", size());
                                   }).foreground_style(ThemeColor::secondary)),
                            "MenuButton, Picker(.menu)")
                      .card(),
                  Captioned(Button("Hover for help", say("Help button")).button_style(BorderedButtonStyle{})
                                .tooltip("Tooltips open after a short rest"),
                            ".tooltip()")
                      .card());
}

// Vector paths (GPU stencil-then-cover): custom shapes, SVG path data, fill rules, strokes with
// caps, joins, and dashes, and paths in a Canvas.
View PathCards() {
    // A five-pointed star that fits whatever frame it gets.
    const Shape star = CustomShape([](Rect r) {
        Path path;
        const Point c = r.center();
        const float outer = std::min(r.width, r.height) / 2;
        for (int i = 0; i < 10; ++i) {
            const float angle = -std::numbers::pi_v<float> / 2 + static_cast<float>(i) * std::numbers::pi_v<float> / 5;
            const float radius = i % 2 == 0 ? outer : outer * 0.45f;
            const Point p{c.x + radius * std::cos(angle), c.y + radius * std::sin(angle)};
            i == 0 ? path.move_to(p) : path.line_to(p);
        }
        return path.close();
    });
    const Shape heart = PathShape(Path::svg("M12 21 C5 15 2 11.5 2 8 A5 5 0 0 1 12 5.5 A5 5 0 0 1 22 8 C22 11.5 19 15 12 21 Z"),
                                  {.view_box = Rect{2, 3, 20, 18}});
    Path rings;
    rings.add_circle({20, 20}, 20).add_circle({20, 20}, 11);
    const Shape ring = PathShape(rings, {.fill_rule = FillRule::even_odd});
    Path zigzag;
    zigzag.move_to({2, 30}).line_to({14, 6}).line_to({26, 30}).line_to({38, 6}).line_to({50, 30});

    const View shapes =
        HStack({.spacing = 16},
               star.fill(LinearGradient({palette::peach, palette::red}, UnitPoint::top, UnitPoint::bottom)).frame(56, 56),
               heart.fill(palette::red).frame(56, 56),
               ring.fill(palette::sky).frame(40, 40),
               PathShape(zigzag).stroke(palette::green, {.width = 4, .cap = LineCap::round, .join = LineJoin::round})
                   .frame(52, 36));
    const View strokes =
        HStack({.spacing = 16},
               RoundedRectangle(10).stroke(palette::mauve, {.width = 2, .dash = {6, 4}}).frame(64, 48),
               Circle().stroke(palette::blue, {.width = 3, .cap = LineCap::round, .dash = {0.1f, 7}}).frame(48, 48),
               Canvas([](GraphicsContext& context) {
                   Path wave;
                   const Size size = context.size();
                   for (int i = 0; i <= 48; ++i) {
                       const float x = size.width * static_cast<float>(i) / 48;
                       const float y = size.height / 2 + std::sin(static_cast<float>(i) / 48 * 4 * std::numbers::pi_v<float>) * size.height * 0.35f;
                       i == 0 ? wave.move_to({x, y}) : wave.line_to({x, y});
                   }
                   context.stroke_path(wave, palette::peach, {.width = 2.5f, .cap = LineCap::round, .join = LineJoin::round});
               }).frame(96, 48));
    // Content clipped to paths: everything inside (gradients, text, icons) is masked by the shape.
    const auto clipped = [](const Shape& shape) {
        return ZStack(Rectangle().fill(LinearGradient({palette::mauve, palette::blue, palette::sky}, UnitPoint::top_leading,
                                                      UnitPoint::bottom_trailing)),
                      Text("Elegance").foreground_style(Color::white))
            .frame(72, 72)
            .clip_shape(shape);
    };
    const View clips = HStack({.spacing = 16}, clipped(star), clipped(heart), clipped(ring));
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(shapes, "CustomShape, Path::svg, even-odd, stroke caps/joins").card(),
                  Captioned(strokes, "Dashed strokes, Canvas paths").card(),
                  Captioned(clips, "clip_shape(path)").card());
}

// --emoji-font <file>: the large emoji below use this font (e.g. a COLRv1 emoji font).
std::optional<std::string> g_emoji_family;

// The built-in icon set (Phosphor): weights follow the font, variants by name, Label for icon + text.
View IconCards() {
    const auto row = [](std::string suffix, Weight weight) {
        std::vector<View> icons;
        for (const char* name : {"house", "magnifying-glass", "bell", "gear", "user", "star", "heart"}) {
            icons.push_back(Image(std::string(name) + suffix).font(Font::system(24, weight)));
        }
        return HStackLayout(VerticalAlignment::center, 14)(std::move(icons));
    };
    const View weights = VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                                row("", Weight::thin),
                                row("", Weight::light),
                                row("", Weight::regular),
                                row("", Weight::bold).foreground_style(palette::blue),
                                row(".fill", Weight::regular).foreground_style(palette::peach),
                                row(".duotone", Weight::regular).foreground_style(palette::mauve));
    const View labels = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                               Button(Label("Search", {.system_name = "magnifying-glass"}), [] {})
                                   .button_style(BorderedButtonStyle{}),
                               Button(Label("Delete", {.system_name = "phosphor.trash"}), [] {}).tint(palette::red));
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(weights, "Image: thin, light, regular, bold, .fill, .duotone").card(),
                  Captioned(labels, "Label + Button").card(),
                  // Characters Segoe UI lacks come from the system's fallback fonts.
                  Captioned(VStack({.alignment = HorizontalAlignment::leading, .spacing = 6},
                                   Text("Symbols: ✓ ★ → ∑ ♥ ∞"),
                                   Text("中文  日本語  한국어  हिन्दी"),
                                   Text("Emoji: 😀 🎉 🚀 ☀️"),
                                   Text("🦊🌈🎨🍩🧪").font(g_emoji_family ? Font::custom(*g_emoji_family, 36)
                                                                         : Font::system(36)),
                                   Text("文字はどこでも改行できます。この文は折り返して表示されます。")
                                       .frame({.width = 220}))
                                .frame({.width = 240}),
                            "Font fallback")
                      .card());
}

// A custom layout (design.md §7): children flow left to right and wrap into rows, like words.
// The cache keeps each child's ideal size, measured once per change instead of on every pass;
// a layout value starts a new row before a child.
struct RowBreak {
    using Value = bool;
    static bool default_value() { return false; }
};

struct FlowCache {
    std::vector<Size> sizes;
    std::vector<bool> breaks;
};

struct FlowLayout : CachedLayout<FlowCache> {
    float spacing = 8;

    FlowCache make_cache(std::span<const LayoutChild> children) const override {
        FlowCache cache;
        for (const LayoutChild& child : children) {
            cache.sizes.push_back(child.size_that_fits({}));
            cache.breaks.push_back(child.value<RowBreak>());
        }
        return cache;
    }

    // Each child's position for a width, and the size of the whole.
    std::vector<Point> flow(float max_width, const FlowCache& cache, Size& size) const {
        std::vector<Point> positions;
        float x = 0, y = 0, row_height = 0;
        size = {};
        for (std::size_t i = 0; i < cache.sizes.size(); ++i) {
            const Size child = cache.sizes[i];
            if (x > 0 && (cache.breaks[i] || x + child.width > max_width)) {
                x = 0;
                y += row_height + spacing;
                row_height = 0;
            }
            positions.push_back({x, y});
            x += child.width + spacing;
            row_height = std::max(row_height, child.height);
            size.width = std::max(size.width, x - spacing);
        }
        size.height = positions.empty() ? 0 : y + row_height;
        return positions;
    }

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild>, FlowCache& cache) const override {
        Size size;
        flow(proposal.width.value_or(infinity), cache, size);
        return size;
    }

    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children,
                        FlowCache& cache) const override {
        Size size;
        const std::vector<Point> positions = flow(bounds.width, cache, size);
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i].place({bounds.x + positions[i].x, bounds.y + positions[i].y},
                              ProposedSize{cache.sizes[i].width, cache.sizes[i].height});
        }
    }
};

View Tag(std::string title, Color color) {
    return Text(std::move(title))
        .font(Font::callout)
        .padding(EdgeInsets{.top = 4, .leading = 10, .bottom = 4, .trailing = 10})
        .background(Capsule().fill(color.opacity(0.25f)));
}

View LayoutCards() {
    const auto tags = [] {
        return FlowLayout{}(Tag("C++23", palette::blue), Tag("WebGPU", palette::red), Tag("SwiftUI-style", palette::mauve),
                            Tag("reactive", palette::green), Tag("MSDF text", palette::peach), Tag("GPU paths", palette::sky),
                            Tag("new row", palette::green).layout_value<RowBreak>(true), Tag("layout values", palette::mauve),
                            Tag("cached", palette::blue));
    };
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(tags().frame({.width = 220, .alignment = Alignment::top_leading}),
                            "FlowLayout: CachedLayout, 220 pt").card(),
                  Captioned(tags().frame({.width = 380, .alignment = Alignment::top_leading}),
                            "The same at 380 pt; layout_value<RowBreak>").card());
}

// Raster images (design.md §13.3): files decode in the background; resizable + aspect_ratio
// for fit / fill; PNG transparency; EXIF orientation.
View ImageCards() {
    const std::string assets = ELG_SANDBOX_ASSETS;
    const auto landscape = [&] {
        return Image({.file = assets + "/landscape.jpg"}).resizable().accessibility_label("Mountains at dusk");
    };
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(landscape()
                                .aspect_ratio(ContentMode::fill)
                                .frame(220, 120)
                                .clip_shape(RoundedRectangle(14)),
                            "JPEG: resizable, fill, clip_shape")
                      .card(),
                  Captioned(landscape()
                                .aspect_ratio(ContentMode::fit)
                                .frame(160, 120)
                                .background(RoundedRectangle(8).fill(ThemeColor::surface_raised)),
                            "fit in 160 × 120")
                      .card(),
                  Captioned(HStack({.spacing = 16}, Image({.file = assets + "/badge.png", .scale = 2}),
                                   Image({.file = assets + "/exif_rotated.jpg"}).resizable().aspect_ratio(ContentMode::fit).frame(80, 120)),
                            "PNG alpha @2x; EXIF-rotated photo")
                      .card());
}

// Audio (design.md §13.3): a Sound plays overlapping voices; an AudioPlayer streams, with
// reactive time for a scrubber.
View AudioCards() {
    const std::string assets = ELG_SANDBOX_ASSETS;
    const Sound click = Sound::load(assets + "/click.wav");
    const AudioPlayer tune(assets + "/music.wav");
    const auto clock = [](double seconds) {
        const int s = static_cast<int>(seconds);
        return std::format("{}:{:02}", s / 60, s % 60);
    };
    const View sounds = HStack({.spacing = 8}, Button("Click", [=] { click.play(); }),
                               Button("Low", [=] { click.play({.pitch = 0.5f}); }),
                               Button("Left", [=] { click.play({.pan = -1}); }),
                               Button("Right", [=] { click.play({.pan = 1}); }))
                            .button_style(BorderedButtonStyle{});
    const View player =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               HStack({.spacing = 10},
                      Button(Text([=] { return std::string(tune.is_playing() ? "Pause" : "Play"); }).frame({.width = 44}),
                             [=] { tune.toggle(); })
                          .button_style(BorderedButtonStyle{}),
                      Slider(tune.time_binding(), {.max = static_cast<float>(tune.duration())})
                          .frame({.width = 200})
                          .accessibility_label("Position"),
                      Text([=] { return clock(tune.time()) + " / " + clock(tune.duration()); })
                          .font(Font::callout)
                          .foreground_style(ThemeColor::secondary)),
               HStack({.spacing = 8}, Image("speaker-low"), Slider(tune.volume_binding()).frame({.width = 120}).accessibility_label("Volume"),
                      Image("speaker-high"),
                      Toggle("Loop", Binding<bool>([=] { return tune.is_looping(); }, [=](bool on) { tune.set_looping(on); }))
                          .fixed_size()));
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(sounds, "Sound: overlapping voices, pitch, pan").card(),
                  Captioned(player, "AudioPlayer: streamed, scrub, volume, loop").card());
}

// Video (design.md §13.3): Media Foundation decodes; frames go straight to the GPU; the render
// thread redraws them without a new layout.
View VideoCards() {
    const VideoPlayer player(std::string(ELG_SANDBOX_ASSETS) + "/clip.mp4");
    player.set_looping(true);
    player.set_muted(true);
    const View video = ZStack({.alignment = Alignment::bottom}, VideoView(player).aspect_ratio(16.f / 9.f),
                              VideoControls(player).padding(10))
                           .frame({.width = 400})
                           .clip_shape(RoundedRectangle(14));
    const View filled = VideoView(player, ContentMode::fill).frame(140, 140).clip_shape(Circle());
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(video, "VideoView + VideoControls (muted, looping)").card(),
                  Captioned(filled, "The same video, fill + Circle").card());
}

// Remote images (design.md §13.3): AsyncImage downloads when shown (spinner, then the image, or a
// placeholder on failure).
View NetworkCards() {
    const std::string landscape = "https://upload.wikimedia.org/wikipedia/commons/thumb/3/3f/Fronalpstock_big.jpg/500px-Fronalpstock_big.jpg";
    const std::string avatar = "https://avatars.githubusercontent.com/u/9919?s=96";
    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(AsyncImage(landscape, {.content_mode = ContentMode::fill})
                                .frame(200, 120)
                                .clip_shape(RoundedRectangle(12)),
                            "AsyncImage (HTTPS), fill")
                      .card(),
                  Captioned(HStack({.spacing = 16},
                                   AsyncImage(avatar, {.content_mode = ContentMode::fill}).frame(56, 56).clip_shape(Circle()),
                                   AsyncImage("https://example.invalid/missing.png").frame(56, 56)),
                            "Avatar; a failing URL")
                      .card());
}

// Desktop integration (design.md §13.4): dropping files, the system's file dialogs,
// notifications, and a tray icon.
View DesktopCards() {
    const State<bool> targeted(false);
    const State<std::string> dropped(std::string("Nothing dropped yet"));
    const auto names = [](const std::vector<std::filesystem::path>& files) {
        std::string text;
        for (const auto& file : files) {
            text += (text.empty() ? "" : ", ") + file.filename().string();
        }
        return text;
    };
    const View zone =
        ZStack(RoundedRectangle(12)
                   .fill([=] { return ShapeStyle(palette::blue.opacity(targeted() ? 0.18f : 0.f)); })
                   .stroke(palette::blue, {.width = 2, .dash = {6, 4}}),
               VStack({.spacing = 4}, Image("download-simple").font(Font::title2),
                      Text([=] { return dropped(); }).font(Font::callout).line_limit(2).multiline_text_alignment(TextAlignment::center))
                   .padding(12))
            .frame(240, 110)
            .on_drop({.files = [=](const std::vector<std::filesystem::path>& files, Point) {
                          dropped = std::format("{} file{}: {}", files.size(), files.size() == 1 ? "" : "s", names(files));
                          return true;
                      },
                      .text = [=](const std::string& text, Point) {
                          dropped = "Text: " + text.substr(0, 60);
                          return true;
                      },
                      .targeted = [=](bool on) { targeted = on; }});

    const State<std::vector<std::string>> picked(std::vector<std::string>{});
    const State<std::string> saved(std::string("No file chosen"));
    const View dialogs = VStack(
        {.alignment = HorizontalAlignment::leading, .spacing = 8},
        HStack({.spacing = 8},
               Button("Open image…", [=] {
                   choose_files({.filters = {{"Images", {"png", "jpg", "jpeg", "gif", "webp", "bmp"}}}},
                                [=](std::vector<std::filesystem::path> files) {
                                    if (!files.empty()) {
                                        picked = std::vector<std::string>{files.front().string()};
                                    }
                                });
               }),
               Button("Save as…", [=] {
                   choose_save_file({.filters = {{"Text", {"txt"}}}, .name = "notes.txt"},
                                    [=](std::optional<std::filesystem::path> file) {
                                        saved = file ? file->string() : std::string("Cancelled");
                                    });
               }))
            .button_style(BorderedButtonStyle{}),
        HStack({.spacing = 10},
               ForEach([=] { return picked(); },
                       [](const std::string& file) {
                           return Image({.file = file}).resizable().aspect_ratio(ContentMode::fill).frame(44, 44).clip_shape(RoundedRectangle(8));
                       }),
               Text([=] { return saved(); }).font(Font::caption).foreground_style(ThemeColor::secondary).line_limit(2))
            .frame({.width = 240, .alignment = Alignment::leading}));

    const State<std::string> status{std::string()};
    const State<bool> tray_on(false);
    const auto tray = std::make_shared<std::optional<TrayIcon>>();
    const View system = VStack(
        {.alignment = HorizontalAlignment::leading, .spacing = 8},
        Button("Notify", [=] {
            notify({.title = "Elegance", .body = "A notification from the sandbox", .on_click = [=] { status = "Notification status"; }});
        }).button_style(BorderedButtonStyle{}),
        Toggle("Tray icon", Binding<bool>([=] { return tray_on(); },
                                          [=](bool on) {
                                              tray_on = on;
                                              if (!on) {
                                                  tray->reset();
                                                  return;
                                              }
                                              tray->emplace(TrayIconOptions{
                                                  .tooltip = "Elegance Sandbox",
                                                  .menu = {{.title = "Say hello", .action = [=] { status = "Hello from the tray"; }},
                                                           {.title = "Notifications", .checked = true,
                                                            .toggled = [=](bool checked) { status = checked ? "Checked" : "Unchecked"; }},
                                                           {},
                                                           {.title = "Hide icon", .action = [=] { tray_on = false; tray->reset(); }}}});
                                          }))
            .fixed_size(),
        Text([=] { return status(); }).font(Font::caption).foreground_style(ThemeColor::secondary));

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(zone, "on_drop: files or text from other apps").card(),
                  Captioned(dialogs, "choose_files, choose_save_file").card(),
                  Captioned(system, "notify, TrayIcon").card());
}

// Capture (design.md §13.3): the microphone into memory (with a live level), and a camera's
// picture straight to the GPU, with snapshots.
View CaptureCards() {
    const AudioRecorder recorder;
    const std::size_t inputs = audio_input_devices().size();
    const auto clock = [](double seconds) { return std::format("{:.1f} s", seconds); };
    const View recording = VStack(
        {.alignment = HorizontalAlignment::leading, .spacing = 8},
        HStack({.spacing = 8},
               Button(Text([=] { return std::string(recorder.is_recording() ? "Stop" : "Record"); }).frame({.width = 52}),
                      [=] { recorder.toggle(); }),
               Button("Play", [=] { recorder.sound().play(); }).disabled([=] { return recorder.duration() <= 0; }),
               Button("Clear", [=] { recorder.clear(); }))
            .button_style(BorderedButtonStyle{}),
        Progress([=] { return recorder.level(); }).frame({.width = 220}).accessibility_label("Input level"),
        Text([=] {
            const std::string error = recorder.error();
            return error.empty() ? clock(recorder.duration()) + std::format(" · {} inputs", inputs) : error;
        })
            .font(Font::caption)
            .foreground_style(ThemeColor::secondary));

    const Camera camera;
    const std::vector<CameraDevice> cameras = camera_devices();
    const State<int> shots(0);
    const auto last_shot = std::make_shared<std::vector<unsigned char>>();
    const View preview = VStack(
        {.alignment = HorizontalAlignment::leading, .spacing = 8},
        CameraView(camera).frame(240, 135).clip_shape(RoundedRectangle(10)),
        HStack({.spacing = 8},
               Button(Text([=] { return std::string(camera.is_running() ? "Stop" : "Start"); }).frame({.width = 44}),
                      [=] { camera.is_running() ? camera.stop() : camera.start(); }),
               Button("Snapshot", [=] {
                   if (auto png = camera.snapshot()) {
                       *last_shot = std::move(*png);
                       shots += 1;
                   }
               }),
               ForEach([=] { return shots() > 0 ? std::vector<int>{shots()} : std::vector<int>{}; },
                       [=](int) {
                           return Image({.data = *last_shot}).resizable().aspect_ratio(ContentMode::fill).frame(48, 27)
                               .clip_shape(RoundedRectangle(4));
                       }))
            .button_style(BorderedButtonStyle{}),
        Text([=] {
            const std::string error = camera.error();
            if (!error.empty()) {
                return error;
            }
            const Size size = camera.size();
            return (cameras.empty() ? std::string("No camera found") : cameras.front().name) +
                   (size.width > 0 ? std::format(" · {}×{}", size.width, size.height) : std::string());
        })
            .font(Font::caption)
            .foreground_style(ThemeColor::secondary)
            .frame({.width = 240, .alignment = Alignment::leading}));

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(recording, "AudioRecorder: level, play back, clear").card(),
                  Captioned(preview, "Camera + CameraView (mirrored), snapshot").card());
}

// ZStack: layering with alignment, z_index, and priority sizing.
View StackCards() {
    // A badge pinned to the corner of what it labels.
    const View badge = ZStack({.alignment = Alignment::top_trailing},
                              RoundedRectangle(14)
                                  .fill(ThemeColor::surface_raised)
                                  .frame(76, 76)
                                  .overlay(Text("Inbox").font(Font::callout)),
                              Text("3")
                                  .font(Font::system(12, Weight::bold))
                                  .foreground_style(Color::white)
                                  .padding(EdgeInsets::symmetric(7, 2))
                                  .background(palette::red, Capsule())
                                  .offset(8, -8));

    // Overlapping cards: tapping one raises it with z_index.
    const auto front = State(2);
    const auto card = [=](int index, Color color, std::string name) {
        return RoundedRectangle(10)
            .fill(color)
            .frame(92, 62)
            .overlay(Text(std::move(name)).font(Font::headline).foreground_style(Color::hex(0x24273A)))
            .shadow({.color = Color::black.opacity(0.3f), .radius = 6, .y = 2})
            .offset(static_cast<float>(index) * 34, static_cast<float>(index) * 16)
            .z_index([=] { return front() == index ? 1.f : 0.f; })
            .on_click([=] { front = index; });
    };
    const View layers = ZStack({.alignment = Alignment::top_leading},
                               card(0, palette::peach, "One"),
                               card(1, palette::green, "Two"),
                               card(2, palette::sky, "Three"))
                            .frame(170, 100, Alignment::top_leading);

    // The text has the higher priority, so it sizes the stack and the capsule fills that.
    const View sized = ZStack(Capsule().fill(LinearGradient({palette::mauve, palette::blue}, UnitPoint::leading,
                                                            UnitPoint::trailing)),
                              Text("Sized by its text")
                                  .font(Font::headline)
                                  .foreground_style(Color::hex(0x24273A))
                                  .padding(EdgeInsets::symmetric(18, 10))
                                  .layout_priority(1));

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(badge.padding(8), "ZStack(top_trailing) badge").card(),
                  Captioned(layers, "z_index: tap a card to raise it").card(),
                  Captioned(sized.frame({.height = 100}), "ZStack + layout_priority").card());
}

View TextCards() {
    const std::string story =
        "Elegance lays text out with HarfBuzz and draws it from a multi-channel signed distance "
        "field atlas, so the same glyphs stay sharp at every size and scale. Words wrap to the "
        "width the layout proposes, and text truncates with an ellipsis when it runs out of room.";

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                         Text("Wrapping").font(Font::headline),
                         Text(story))
                      .frame({.max_width = infinity, .alignment = Alignment::top_leading})
                      .card(),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                         Text("line_limit(2)").font(Font::headline),
                         Text(story).line_limit(2).foreground_style(ThemeColor::secondary))
                      .frame({.max_width = infinity, .alignment = Alignment::top_leading})
                      .card(),
                  VStack({.spacing = 6},
                         Text("Aa").font(Font::system(56, Weight::light)),
                         Text("Centered\nmulti-line").multiline_text_alignment(TextAlignment::center))
                      .frame({.max_width = infinity})
                      .card());
}

// Clipping, gradients, group opacity, and a layered shadow.
View EffectCards() {
    // Stripes that overflow their frame, clipped to a circle.
    const View stripes = HStack({.spacing = 0},
                                Rectangle().fill(palette::red),
                                Rectangle().fill(palette::peach),
                                Rectangle().fill(palette::green),
                                Rectangle().fill(palette::sky))
                             .frame(140, 140)
                             .offset(-20, -10)
                             .frame(96, 96)
                             .clip_shape(Circle());

    const View gradients = HStack({.spacing = 12},
                                  RoundedRectangle(16).fill(LinearGradient({palette::blue, palette::mauve},
                                                                           UnitPoint::top_leading,
                                                                           UnitPoint::bottom_trailing)),
                                  Circle().fill(RadialGradient({Color::white, palette::sky, palette::blue.opacity(0)},
                                                               UnitPoint::center, 0, 48)))
                               .frame({.height = 96});

    // Left: the pair fades as a group, so the overlap doesn't show through.
    // Right: each circle fades on its own, so the overlap is darker.
    const auto pair = [](float each) {
        return ZStack({.alignment = Alignment::leading},
                      Circle().fill(palette::red.opacity(each)).frame(56, 56),
                      Circle().fill(palette::blue.opacity(each)).frame(56, 56).offset(32, 0))
            .frame(88, 56, Alignment::leading);
    };
    const View opacity = HStack({.spacing = 16}, pair(1).opacity(0.55f), pair(0.55f)).frame({.height = 96});

    // A composite view (shape + text) casting one shadow: rendered as an offscreen layer.
    const View raised = VStack({.spacing = 6},
                               Circle().fill(palette::green).frame(28, 28),
                               Text("Layer").font(Font::headline))
                            .padding(14)
                            .background(ThemeColor::surface_raised, RoundedRectangle(12))
                            .shadow({.color = Color::black.opacity(0.6f), .radius = 18, .x = 0, .y = 8})
                            .frame({.height = 96});

    return HStack({.spacing = 24},
                  Captioned(stripes.frame({.max_width = infinity}), "clip_shape(Circle())").card(),
                  Captioned(gradients, "Linear + radial gradients").card(),
                  Captioned(opacity, "Group vs per-shape opacity").card(),
                  Captioned(raised.frame({.max_width = infinity}), "shadow() on a composite").card());
}

void log(const char* message) {
    std::printf("sandbox: %s\n", message);
    std::fflush(stdout);
}

// A child component that edits its parent's state through a Binding.
View CountStepper(Binding<int> value) {
    return HStack({.spacing = 8},
                  Button("-", [=] { value -= 1; }).disabled([=] { return value() <= 0; }),
                  Text([=] { return std::to_string(value()); })
                      .font(Font::title3)
                      .multiline_text_alignment(TextAlignment::center)
                      .frame({.min_width = 32}),
                  Button("+", [=] { value += 1; }))
        .button_style(PillButtonStyle{});
}

// State drives the UI: each reactive argument updates exactly one property of one element.
View StateCards(std::function<void()> quit) {
    const View buttons = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                Button("Say hello", [] { log("hello"); }),
                                Button("Quit", std::move(quit)).tint(palette::red));

    // The count lives here; CountStepper changes it through a Binding.
    const auto count = State(0);
    const View counter = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                Text([=] { return std::format("Count: {}", count()); }).font(Font::headline),
                                CountStepper(count.binding()));

    // Hover changes the fill and the label; taps update a count.
    const auto hovered = State(false);
    const auto taps = State(0);
    const View hover = RoundedRectangle(12)
                           .fill([=](const EnvironmentValues& env) {
                               return hovered() ? palette::peach : env.color(ThemeColor::surface_raised);
                           })
                           .frame({.height = 56})
                           .overlay(Text([=] {
                               return hovered() ? std::format("Tapped {} times", taps()) : std::string("Hover or tap");
                           }).foreground_style([=](const EnvironmentValues& env) {
                               return env.color(hovered() ? ThemeColor::background : ThemeColor::primary);
                           }))
                           .animation(Animation::ease_out(0.18), hovered) // implicit: hover changes ease
                           .on_pointer_enter([=] { hovered = true; })
                           .on_pointer_leave([=] { hovered = false; })
                           .on_click([=] { taps += 1; });

    // A reactive opacity, toggled by a button whose label follows the same state.
    const auto dimmed = State(false);
    const View fade = HStack({.spacing = 16},
                             Circle().fill(palette::mauve).frame(44, 44).opacity([=] { return dimmed() ? 0.2f : 1.f; }),
                             Button(Text([=] { return dimmed() ? "Show" : "Dim"; }),
                                    [=] { with_animation(Animation::smooth(), [=] { dimmed = !dimmed(); }); }));

    return HStack({.spacing = 24},
                  Captioned(buttons, "Button + tint").card(),
                  Captioned(counter, "State + Binding").card(),
                  Captioned(hover.frame({.max_width = infinity}), "pointer enter/leave + on_click").card(),
                  Captioned(fade, "Reactive opacity").card());
}

// Switches, check boxes (one mixed), and keyboard focus: Tab / Shift+Tab move between controls,
// Space and Enter press them.
View ControlCards() {
    const auto wifi = State(true);
    const auto sounds = State(false);
    const View switches = VStack({.spacing = 10},
                                 Toggle("Wi-Fi", wifi.binding()),
                                 Toggle("Sounds", sounds.binding()).tint(palette::green),
                                 Toggle("Airplane mode", constant(false)).disabled())
                              .toggle_style(SwitchToggleStyle{{.fills_width = true}})
                              .frame({.width = 220});

    const auto remember = State(true);
    const auto reports = State(false);
    const Binding<bool> both([=] { return remember() && reports(); },
                             [=](bool on) {
                                 remember = on;
                                 reports = on;
                             });
    const View checks = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                               Checkbox("All", both, {.mixed = [=] { return remember() != reports(); }}),
                               Checkbox("Remember me", remember.binding()).padding(Edges::leading, 26),
                               Checkbox("Send crash reports", reports.binding()).padding(Edges::leading, 26));

    // A focusable view with its own keys; the button focuses it through the binding.
    const auto is_focused = State(false);
    const auto value = State(0);
    const View keys =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               Text([=] {
                   return is_focused() ? std::format("Left/Right: {}", value()) : std::string("Click or Tab here");
               })
                   .padding(EdgeInsets::symmetric(12, 8))
                   .background(ThemeColor::surface_raised, RoundedRectangle(8))
                   .focused(is_focused.binding())
                   .on_key([=](const KeyEvent& event) {
                       if (event.kind != KeyEvent::Kind::down) {
                           return false;
                       }
                       switch (event.key) {
                       case Key::left: value -= 1; return true;
                       case Key::right: value += 1; return true;
                       case Key::escape: is_focused = false; return true;
                       default: return false;
                       }
                   }),
               Button("Focus it", [=] { is_focused = true; }).button_style(BorderedButtonStyle{}));

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(switches, "Toggle (switch)").card(),
                  Captioned(checks, "Toggle (checkbox)").card(),
                  Captioned(keys, "focused + on_key").card());
}

// Text fields: the default rounded style, the plain style, and a disabled one.
View TextFieldCards() {
    const auto name = State(std::string());
    const auto greeting = State(std::string("Type a name and press Enter"));
    const View greeter =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               TextField("Your name", name.binding(),
                         {.on_submit = [=] {
                              greeting = name().empty() ? std::string("Type a name first") : std::format("Hello, {}!", name());
                          }}),
               Text([=] { return greeting(); }).foreground_style(ThemeColor::secondary))
            .frame({.width = 260});

    const auto note = State(std::string("Plain style: no box"));
    const auto locked = State(std::string("Disabled"));
    const View styles = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                               TextField("Note", note.binding()).text_field_style(PlainTextFieldStyle{}),
                               TextField("Locked", locked.binding()).disabled())
                            .frame({.width = 260});

    // Two fields bound to the same state stay in sync.
    const auto shared = State(std::string("Edit me in either field"));
    const View mirrored = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                 TextField("Shared", shared.binding()),
                                 TextField("Shared", shared.binding()),
                                 Text([=] { return std::format("{} bytes", shared().size()); })
                                     .foreground_style(ThemeColor::secondary))
                              .frame({.width = 260});

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(greeter, "TextField + on_submit").card(),
                  Captioned(styles, "Plain style, disabled").card(),
                  Captioned(mirrored, "One State, two fields").card());
}

enum class Accent { blue, green, peach };

// Slider, Progress, NumberInput, and Picker.
View MoreControlCards() {
    const auto volume = State(40.f);
    const View slider = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                               Text([=] { return std::format("Volume: {:.0f}", volume()); }),
                               Slider(volume.binding(), {.min = 0, .max = 100, .step = 1}),
                               HStack({.spacing = 12}, Progress([=] { return volume() / 100; }),
                                      Progress([=] { return volume() / 100; }).progress_style(CircularProgressStyle{})))
                            .frame({.width = 220});

    const auto guests = State(2);
    const auto loading = State(false);
    const View stepper = VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                                HStack({.spacing = 8}, Text("Guests"),
                                       NumberInput(guests.binding(), {.min = 1, .max = 8}).frame({.width = 72})),
                                Toggle("Loading", loading.binding()),
                                // Indeterminate progress renders continuously, so it's only shown on request.
                                If(loading, HStack({.spacing = 12}, Progress(),
                                                   Progress().progress_style(CircularProgressStyle{}))))
                             .frame({.width = 220});

    const auto accent = State(Accent::blue);
    const View picker = VStack({.spacing = 14},
                               Picker(accent.binding(), {{Accent::blue, "Blue"}, {Accent::green, "Green"}, {Accent::peach, "Peach"}}),
                               Circle()
                                   .fill([=] {
                                       switch (accent()) {
                                       case Accent::green: return palette::green;
                                       case Accent::peach: return palette::peach;
                                       default: return palette::blue;
                                       }
                                   })
                                   .frame(44, 44))
                            .frame({.width = 240});

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(slider, "Slider + Progress").card(),
                  Captioned(stepper, "NumberInput, indeterminate progress").card(),
                  Captioned(picker, "Picker (segmented)").card());
}

// A transparent, undecorated window: only the rounded glass panel is drawn, the desktop shows
// around and (faintly) through it. Drag it anywhere on the panel.
View WidgetWindow(State<ColorScheme> scheme) {
    const auto taps = State(0);
    const View panel =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
               HStack({.spacing = 8},
                      WindowControls({.style = WindowControlsStyle::traffic_lights, .minimize = false, .maximize = false}),
                      Text("Widget").font(Font::headline),
                      Spacer()),
               Text("Transparent window, custom title bar").foreground_style(ThemeColor::secondary),
               Button(Text([=] { return std::format("Tapped {}", taps()); }), [=] { taps += 1; })
                   .button_style(BorderedButtonStyle{}))
            .padding(16)
            .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading})
            .background([](const EnvironmentValues& environment) { return environment.color(ThemeColor::surface).opacity(0.82f); },
                        RoundedRectangle(16))
            .overlay(RoundedRectangle(16).stroke_border(Color::white.opacity(0.18f), 1))
            .window_drag_area();
    // Room around the panel for its shadow, which is drawn over the (clear) window.
    return panel.shadow({.color = Color::black.opacity(0.35f), .radius = 14, .y = 4})
        .padding(16)
        .theme(palette::theme)
        .color_scheme(scheme);
}

View ContentView(State<ColorScheme> scheme, std::function<void()> quit) {
    return VStack({.spacing = 0},
                  TitleBar(scheme),
                  // The title bar stays put; the cards scroll under it.
                  ScrollView(VStack({.alignment = HorizontalAlignment::leading, .spacing = 20},
                         ImageCards(),
                         AudioCards(),
                         VideoCards(),
                         NetworkCards(),
                         DesktopCards(),
                         CaptureCards(),
                         PathCards(),
                         MenuCards(),
                         IconCards(),
                         ShapeCards(),
                         StackCards(),
                         LayoutCards(),
                         // Like SwiftUI's .fixedSize(horizontal: false, vertical: true): take the
                         // text's full height instead of an equal share against the Spacer.
                         TextCards().fixed_size(false, true),
                         EffectCards(),
                         StateCards(std::move(quit)),
                         ControlCards(),
                         TextFieldCards(),
                         MoreControlCards())
                      .padding(32)))
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top})
        .background(ThemeColor::background)
        .theme(palette::theme)
        .color_scheme(scheme);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool light = false;
    bool only_main = false; // just the main window (handy when testing input)
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--emoji-font" && i + 1 < argc) {
            g_emoji_family = "Sandbox Emoji";
            elg::register_font(*g_emoji_family, argv[++i]);
            continue;
        }
        smoke_test |= std::string_view(argv[i]) == "--smoke-test";
        light |= std::string_view(argv[i]) == "--light";
        only_main |= std::string_view(argv[i]) == "--only-main";
    }

    try {
        App app;
        // Shared by both windows: the title bar's toggle switches them together.
        const State<ColorScheme> scheme(light ? ColorScheme::light : ColorScheme::dark);
        app.window({.title = "Elegance Sandbox", .width = 1100, .height = 860},
                   [&] {
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return ContentView(scheme, [&app] { app.quit(); });
                   });
        if (only_main) {
            return app.run();
        }
        // A second window: If and ForEach.
        app.window({.title = "Elegance Todos", .width = 460, .height = 560}, [scheme] { return TodoWindow(scheme); });
        // A transparent widget with its own title bar.
        app.window({.title = "Elegance Widget",
                    .width = 340,
                    .height = 190,
                    .resizable = false,
                    .title_bar = TitleBar::hidden,
                    .transparent = true},
                   [scheme] { return WidgetWindow(scheme); });
        // A third: a 10,000-row List (lazy stacks).
        app.window({.title = "Elegance Contacts", .width = 520, .height = 640},
                   [scheme] { return ContactsWindow(scheme); });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "sandbox: %s\n", e.what());
        return 1;
    }
}
