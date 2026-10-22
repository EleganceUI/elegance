// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Music: a desktop music player in the style of the big streaming apps. Nothing actually plays,
// but everything behaves as if it did: the position moves, tracks advance, shuffle and repeat
// work, and the queue, lyrics, love, and playlists all respond. Uses only the public API.
//
//   music                       open the window
//   music --smoke-test          render the first frame, then exit
//   music --screen <name>       start on a page: browse, radio, recently-added, artists, albums,
//                               songs, playlist, album, artist, search
//   music --playing             start with a song playing (and the lyrics showing)

#include "model.h"
#include "pages.h"
#include "ui.h"

#include <elg/elegence.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace elg;
using namespace music;

// --- The player bar ---

View TransportControls(const Model& model) {
    const Player player = model.player;
    return HStack({.spacing = 2},
                  Button(Image("shuffle"), [=] { player.set_shuffle(!player.shuffle()); })
                      .button_style(IconButtonStyle(15, [=] { return player.shuffle(); }))
                      .accessibility_label("Shuffle")
                      .help("Shuffle"),
                  Button(Image("skip-back.fill"), [=] { player.previous(); })
                      .button_style(IconButtonStyle(18))
                      .accessibility_label("Previous"),
                  Button(If([=] { return player.playing(); }, Image("pause.fill"), Image("play.fill")),
                         [=] { player.toggle(); })
                      .button_style(IconButtonStyle(24))
                      .accessibility_label("Play/Pause"),
                  Button(Image("skip-forward.fill"), [=] { player.next(); })
                      .button_style(IconButtonStyle(18))
                      .disabled([=] { return player.index() < 0; })
                      .accessibility_label("Next"),
                  Button(If([=] { return player.repeat() == Repeat::one; }, Image("repeat-once"), Image("repeat")),
                         [=] { player.cycle_repeat(); })
                      .button_style(IconButtonStyle(15, [=] { return player.repeat() != Repeat::off; }))
                      .accessibility_label("Repeat")
                      .help("Repeat"));
}

// The display in the middle of the bar: what's playing, and how far along.
View NowPlaying(const Model& model, int track_id) {
    const Player player = model.player;
    const Track& t = track(track_id);
    const double length = t.seconds;
    const Binding<float> progress([=] { return static_cast<float>(player.position() / length); },
                                  [=](float fraction) { player.seek(fraction * length); });
    const int album_id = t.album;
    return HStack({.spacing = 10},
                  AlbumArtwork(t.album, 4).frame(42, 42).on_tap_gesture([=] {
                      model.open({Route::Kind::album, album_id});
                  }),
                  VStack({.spacing = 0},
                         HStack({.spacing = 5}, Text(t.title).font(Font::system(13, Weight::semibold)).line_limit(1),
                                Text("—").foreground_style(ThemeColor::tertiary),
                                Text(artist_of(t).name).font(Font::system(13)).foreground_style(ThemeColor::secondary).line_limit(1)),
                         HStack({.spacing = 8},
                                Text([=] { return format_time(player.position()); })
                                    .font(Font::caption2)
                                    .foreground_style(ThemeColor::secondary)
                                    .frame({.width = 34, .alignment = Alignment::trailing}),
                                Slider(progress).accessibility_label("Position"),
                                Text([=] { return "-" + format_time(length - player.position()); })
                                    .font(Font::caption2)
                                    .foreground_style(ThemeColor::secondary)
                                    .frame({.width = 38, .alignment = Alignment::leading})))
                      .frame({.max_width = infinity}),
                  Button(If([=] { return player.is_loved(track_id); }, Image("heart.fill"), Image("heart")),
                         [=] { player.toggle_love(track_id); })
                      .button_style(IconButtonStyle(14, [=] { return player.is_loved(track_id); }))
                      .accessibility_label("Love"));
}

View Display(const Model& model) {
    const Player player = model.player;
    const View idle = Image("music-note").font(Font::system(22)).foreground_style(ThemeColor::tertiary).frame(
        {.max_width = infinity, .max_height = infinity});
    // Rebuilt for each track; the parts inside follow the position on their own.
    return ForEach([=] { return std::vector<int>{player.current()}; },
                   [=](int id) { return id == 0 ? idle : NowPlaying(model, id); })
        .padding(EdgeInsets::symmetric(8, 5))
        .frame({.min_width = 220, .ideal_width = 480, .max_width = 580, .min_height = 54, .max_height = 54})
        .background([](const EnvironmentValues& env) { return env.color(ThemeColor::surface); }, RoundedRectangle(8))
        .overlay(RoundedRectangle(8).stroke_border(ThemeColor::outline));
}

// A track in the queue, counted from the one after the current track.
struct QueueEntry {
    std::size_t offset = 0;
    int track = 0;
    friend bool operator==(const QueueEntry&, const QueueEntry&) = default;
};

// The queue, for the panel.
View UpNext(const Model& model) {
    const Player player = model.player;
    using Entry = QueueEntry;
    const auto entries = [=] {
        std::vector<Entry> result;
        const std::vector<int> ids = player.up_next();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            result.push_back({i, ids[i]});
        }
        return result;
    };
    const View list = ScrollView(VStack({.spacing = 0}, ForEach(entries, &Entry::offset, [=](const Entry& e) {
        const Track& t = track(e.track);
        const std::size_t offset = e.offset;
        return Button(HStack({.spacing = 10}, AlbumArtwork(t.album, 3).frame(34, 34),
                             VStack({.alignment = HorizontalAlignment::leading, .spacing = 1},
                                    Text(t.title).line_limit(1),
                                    Text(artist_of(t).name).font(Font::footnote).foreground_style(ThemeColor::secondary).line_limit(1)),
                             Spacer(), Text(format_time(t.seconds)).font(Font::footnote).foreground_style(ThemeColor::secondary)),
                      [=] {
                          batch([&] {
                              player.index = player.index.peek() + 1 + static_cast<int>(offset);
                              player.position = 0.0;
                              player.playing = true;
                          });
                      })
            .button_style(RowButtonStyle{});
    })).padding(6));
    return If([=] { return player.up_next().empty(); },
              Text("Nothing up next. Play an album or playlist, or add songs with Play Next.")
                  .foreground_style(ThemeColor::secondary)
                  .padding(20),
              list.padding(EdgeInsets{.leading = 6, .trailing = 6}));
}

View Volume(const Model& model) {
    const Player player = model.player;
    const State<float> volume = player.volume;
    return HStack({.spacing = 6},
                  If([=] { return volume() < 0.01f; }, Image("speaker-none"),
                     If([=] { return volume() < 0.5f; }, Image("speaker-low"), Image("speaker-high")))
                      .foreground_style(ThemeColor::secondary)
                      .frame({.width = 18}),
                  Slider(volume.binding()).frame({.width = 96}).accessibility_label("Volume"));
}

// The window's caption buttons (WindowControls): 3 of 46 x 32, in the top-right corner.
constexpr float kCaptionButtonsWidth = 3 * 46;

// The player bar's three parts, spaced like CSS `justify-content: space-evenly`: the same gap
// before the first, between each two, and after the last.
class SpaceEvenlyBarLayout final : public Layout {
public:
    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        float height = 0;
        float width = 0;
        for (const LayoutChild& child : children) {
            const Size size = child.size_that_fits({std::nullopt, proposal.height});
            height = std::max(height, size.height);
            width += size.width;
        }
        return {proposal.width && std::isfinite(*proposal.width) ? *proposal.width : width, height};
    }

    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        if (children.size() != 3) {
            return;
        }
        constexpr float kGap = 8; // the least of each gap
        const auto place = [&](const LayoutChild& child, Size size, float x) {
            child.place({x, bounds.y + (bounds.height - size.height) / 2}, {size.width, size.height});
        };
        const Size left = children[0].size_that_fits({std::nullopt, bounds.height});
        const Size right = children[2].size_that_fits({std::nullopt, bounds.height});
        // The middle takes its ideal width, or what fits once every gap has its least; what's left is
        // split evenly into the four gaps (before, between, and after the parts).
        const float room = std::max(bounds.width - left.width - right.width - 4 * kGap, 0.f);
        const Size ideal = children[1].size_that_fits({std::nullopt, bounds.height});
        const Size middle = children[1].size_that_fits({std::min(ideal.width, room), bounds.height});
        const float gap = std::max((bounds.width - left.width - middle.width - right.width) / 4, 0.f);
        place(children[0], left, bounds.x + gap);
        place(children[1], middle, bounds.x + 2 * gap + left.width);
        place(children[2], right, bounds.max_x() - gap - right.width);
    }
};

View PlayerBar(const Model& model) {
    const State<bool> sidebar = model.sidebar_shown;
    const State<Panel> panel = model.panel;
    const auto window = Environment<WindowKey>();
    const View leading = HStack({.spacing = 14},
                                Button(Image("sidebar-simple"),
                                       [=] { with_animation(Animation::smooth(), [&] { sidebar = !sidebar(); }); })
                                    .button_style(IconButtonStyle(16))
                                    .accessibility_label("Sidebar")
                                    .help("Show or hide the sidebar"),
                                TransportControls(model));
    const View trailing = HStack({.spacing = 6}, Volume(model),
                  Button(Image("quotes"), [=] { model.toggle_panel(Panel::lyrics); })
                      .button_style(IconButtonStyle(16, [=] { return panel() == Panel::lyrics; }))
                      .accessibility_label("Lyrics")
                      .help("Lyrics"),
                  Button(Image("list-bullets"), [=] { model.toggle_panel(Panel::queue); })
                      .button_style(IconButtonStyle(16, [=] { return panel() == Panel::queue; }))
                      .accessibility_label("Playing Next")
                      .help("Playing Next"))
                              // The caption buttons sit above this end of the bar, in the window's corner.
                              .padding(EdgeInsets{.trailing = kCaptionButtonsWidth});
    return SpaceEvenlyBarLayout{}(leading, Display(model), trailing)
        .padding(EdgeInsets{.leading = 10, .trailing = 10})
        .frame({.max_width = infinity, .min_height = 68, .max_height = 68})
        .opacity([=] { return window().is_active() ? 1.f : 0.75f; })
        .window_drag_area();
}

// --- Sidebar ---

View SidebarHeading(std::string title) {
    return Text(std::move(title))
        .font(Font::caption)
        .bold()
        .foreground_style(ThemeColor::tertiary)
        .padding(EdgeInsets{.top = 16, .leading = 10, .bottom = 4});
}

View SidebarItem(const Model& model, Prop<std::string> title, std::string icon, Section section) {
    return Button(HStack({.spacing = 9}, Image(icon).foreground_style(kAccent).frame({.width = 18}),
                         Text(std::move(title)).line_limit(1)),
                  [=] { model.select(section); })
        .button_style(RowButtonStyle([=] { return model.section() == section && model.search().empty(); }));
}

View SearchField(const Model& model) {
    // Typing searches from the root: whatever was pushed goes.
    const Binding<std::string> text([=] { return model.search(); },
                                    [=](std::string value) {
                                        batch([&] {
                                            model.search = std::move(value);
                                            model.path = std::vector<Route>();
                                        });
                                    });
    return HStack({.spacing = 6}, Image("magnifying-glass").foreground_style(ThemeColor::secondary),
                  TextField("Search", text).text_field_style(PlainTextFieldStyle{}))
        .padding(EdgeInsets::symmetric(9, 6))
        .background([](const EnvironmentValues& env) { return env.color(ThemeColor::outline).opacity(0.55f); },
                    RoundedRectangle(7));
}

View Sidebar(const Model& model) {
    using Kind = Section::Kind;
    // The sidebar reaches the top of the window: its top strip, around the search field, drags it.
    const View top = SearchField(model).padding(EdgeInsets{.top = 18, .leading = 10, .bottom = 6, .trailing = 10})
                         .window_drag_area();
    return VStack({.spacing = 0}, top, ScrollView(
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 1},
               SidebarHeading("Music"),
               SidebarItem(model, "Listen Now", "play-circle", {Kind::listen_now}),
               SidebarItem(model, "Browse", "squares-four", {Kind::browse}),
               SidebarItem(model, "Radio", "radio", {Kind::radio}),
               SidebarHeading("Library"),
               SidebarItem(model, "Recently Added", "clock", {Kind::recently_added}),
               SidebarItem(model, "Artists", "microphone-stage", {Kind::artists}),
               SidebarItem(model, "Albums", "vinyl-record", {Kind::albums}),
               SidebarItem(model, "Songs", "music-note", {Kind::songs}),
               SidebarHeading("Playlists"),
               ForEach(model.playlists, &Playlist::id,
                       [=](const Playlist& p) {
                           const int id = p.id;
                           return SidebarItem(model, [=] { return model.playlist(id).name; }, "playlist",
                                              {Kind::playlist, id})
                               .context_menu([=] {
                                   return Menu(MenuItem("Play", [=] { model.player.play(model.playlist(id).tracks); }),
                                               MenuItem("Shuffle", [=] { model.player.play_shuffled(model.playlist(id).tracks); }));
                               });
                       }),
               Button(HStack({.spacing = 9}, Image("plus").frame({.width = 18}), Text("New Playlist")),
                      [=] { model.new_playlist(); })
                   .button_style(RowButtonStyle{})
                   .foreground_style(ThemeColor::secondary))
            .padding(EdgeInsets{.leading = 10, .bottom = 10, .trailing = 10})));
}

// --- Lyrics ---

std::vector<std::string> lyrics_for(int track_id) {
    static const std::vector<std::string> kLines{
        "Streetlights hum a borrowed tune",       "We left the window open wide",
        "Counting cars beneath the moon",         "Every echo on our side",
        "Hold the moment, let it ring",           "Paper boats on silver streams",
        "All the small and shining things",       "Stitched together into dreams",
        "Turn it up and let it go",               "Somewhere warmer than the snow",
        "Say it softer, say it twice",            "Midnight tastes like lemon ice",
        "And the radio plays on",                 "Long after the night is gone",
    };
    const Track& t = track(track_id);
    std::vector<std::string> lines{"♪"};
    for (std::size_t i = 0; i < 12; ++i) {
        lines.push_back(i == 4 || i == 9 ? t.title + ", " + t.title
                                         : kLines[(static_cast<std::size_t>(track_id) * 5 + i * 3) % kLines.size()]);
    }
    lines.push_back("♪");
    return lines;
}

View LyricsFor(const Model& model, int track_id) {
    const Player player = model.player;
    const std::vector<std::string> lines = lyrics_for(track_id);
    const double per_line = static_cast<double>(track(track_id).seconds) / static_cast<double>(lines.size());
    const auto line_now = [=] { return static_cast<std::size_t>(player.position() / per_line); };
    std::vector<std::size_t> indices(lines.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    return ScrollView(VStack({.alignment = HorizontalAlignment::leading, .spacing = 14},
                             ForEach(indices, [=](std::size_t i) {
                                 return Text(lines[i])
                                     .font(Font::system(19, Weight::bold))
                                     .foreground_style([=](const EnvironmentValues& env) {
                                         const std::size_t now = line_now();
                                         if (i == now) {
                                             return env.color(ThemeColor::primary);
                                         }
                                         return env.color(ThemeColor::secondary).opacity(i < now ? 0.45f : 0.7f);
                                     })
                                     .animation(Animation::ease_out(0.3), line_now)
                                     .on_tap_gesture([=] { player.seek(static_cast<double>(i) * per_line); });
                             }))
                          .padding(EdgeInsets{.top = 4, .leading = 20, .bottom = 40, .trailing = 20})
                          .frame({.max_width = infinity, .alignment = Alignment::leading}));
}

View Lyrics(const Model& model) {
    const Player player = model.player;
    return ForEach([=] { return std::vector<int>{player.current()}; },
                   [=](int id) {
                       return id == 0 ? View(Text("Lyrics show up here while a song plays.")
                                                 .foreground_style(ThemeColor::secondary)
                                                 .padding(20))
                                      : LyricsFor(model, id);
                   });
}

// --- Side panel ---

// Beside the page: the lyrics or the queue, whichever was asked for last.
View SidePanel(const Model& model) {
    const Player player = model.player;
    const State<Panel> panel = model.panel;
    const auto title = [=] { return panel() == Panel::queue ? std::string("Playing Next") : std::string("Lyrics"); };
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                  HStack(Text(title).font(Font::headline),
                         Text([=] { return panel() == Panel::queue && player.shuffle() ? "Shuffled" : ""; })
                             .font(Font::footnote)
                             .foreground_style(kAccent),
                         Spacer(),
                         Button(Image("x"), [=] { with_animation(Animation::smooth(), [&] { panel = Panel::none; }); })
                             .button_style(IconButtonStyle(12))
                             .accessibility_label([=] { return "Close " + title(); }))
                      .padding(EdgeInsets{.top = 10, .leading = 20, .bottom = 6, .trailing = 8}),
                  // Keyed by what it shows: switching swaps the content (and its scroll position).
                  ForEach([=] { return std::vector<Panel>{panel() == Panel::queue ? Panel::queue : Panel::lyrics}; },
                          [=](Panel which) { return which == Panel::queue ? UpNext(model) : Lyrics(model); }))
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading})
        .background(ThemeColor::surface);
}

// --- Detail ---

int section_key(Section section) {
    return static_cast<int>(section.kind) * 100000 + section.id;
}

Section section_from(int key) {
    return {static_cast<Section::Kind>(key / 100000), key % 100000};
}

View Detail(const Model& model) {
    // The stack's root is the sidebar's section, or search results while there's a search.
    const View root = ForEach([=] { return std::vector<int>{model.search().empty() ? section_key(model.section()) : -1}; },
                              [=](int key) { return key == -1 ? SearchPage(model) : SectionPage(model, section_from(key)); });
    const View stack = NavigationStack(model.path, root, [=](const Route& route) { return RoutePage(model, route); },
                                       {.root_title = [=] { return section_title(model, model.section()); },
                                        .title = [=](const Route& route) { return route_title(model, route); },
                                        .shows_bar = false});
    const State<std::vector<Route>> path = model.path;
    const View back = HStack(Button(Image("caret-left"),
                                    [=] {
                                        path.update([](std::vector<Route>& p) {
                                            if (!p.empty()) {
                                                p.pop_back();
                                            }
                                        });
                                    })
                                 .button_style(IconButtonStyle(15))
                                 .disabled([=] { return path().empty(); })
                                 .accessibility_label("Back")
                                 .help("Back (Alt+Left)"),
                             Spacer())
                          .padding(EdgeInsets{.top = 6, .leading = 14})
                          .frame({.height = 40});
    const State<Panel> panel = model.panel;
    return HSplitView(VStack({.spacing = 0}, back, stack).frame({.max_width = infinity, .max_height = infinity}).layout_priority(1),
                      If([=] { return panel() != Panel::none; },
                         SidePanel(model)
                             .frame({.min_width = 240, .ideal_width = 300, .max_width = 440, .max_height = infinity})
                             .transition(Transition::move(Edges::trailing))));
}

View MusicApp(const Model& model) {
    return NavigationSplitView(Sidebar(model), VStack({.spacing = 0}, PlayerBar(model), Divider(), Detail(model)),
                               {.sidebar_visible = model.sidebar_shown.binding(), .sidebar_width = {180, 220, 320}})
        .overlay(WindowControls(), Alignment::top_trailing)
        .tint(kAccent)
        .on_key_press(Key::space, [player = model.player] { player.toggle(); });
}

// Where --screen starts.
void apply_screen(const Model& model, std::string_view screen) {
    using Kind = Section::Kind;
    if (screen == "browse") model.section = Section{Kind::browse};
    else if (screen == "radio") model.section = Section{Kind::radio};
    else if (screen == "recently-added") model.section = Section{Kind::recently_added};
    else if (screen == "artists") model.section = Section{Kind::artists};
    else if (screen == "albums") model.section = Section{Kind::albums};
    else if (screen == "songs") model.section = Section{Kind::songs};
    else if (screen == "playlist") model.section = Section{Kind::playlist, 1};
    else if (screen == "album") model.path = std::vector<Route>{{Route::Kind::album, 105}};
    else if (screen == "artist") model.path = std::vector<Route>{{Route::Kind::artist, 5}};
    else if (screen == "search") model.search = std::string("ha");
    else std::fprintf(stderr, "music: unknown screen \"%.*s\"\n", static_cast<int>(screen.size()), screen.data());
}

} // namespace

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool playing = false;
    std::string_view screen;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        smoke_test |= arg == "--smoke-test";
        playing |= arg == "--playing";
        if (arg == "--screen" && i + 1 < argc) {
            screen = argv[++i];
        }
    }
    try {
        App app;
        const Model model;
        PlaybackClock clock(model.player);
        app.window({.title = "Music", .width = 1280, .height = 800, .min_size = Size{1040, 620},
                    .title_bar = TitleBar::hidden},
                   [&] {
                       clock.follow();
                       if (!screen.empty()) {
                           apply_screen(model, screen);
                       }
                       if (playing) {
                           model.player.play(album(108).tracks, 1);
                           model.player.seek(64);
                           model.panel = Panel::lyrics;
                       }
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return MusicApp(model);
                   });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "music: %s\n", e.what());
        return 1;
    }
}
