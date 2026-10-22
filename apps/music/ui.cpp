// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "ui.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace music {

using namespace elg;

// --- Look ---

LinearGradient Gradient(const std::vector<Color>& colors, UnitPoint start, UnitPoint end) {
    std::vector<GradientStop> stops;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        stops.push_back({colors[i], colors.size() > 1 ? static_cast<float>(i) / static_cast<float>(colors.size() - 1) : 0.f});
    }
    return LinearGradient(std::move(stops), start, end);
}

View Artwork(std::vector<Color> colors, float corner_radius, float icon_size, std::string icon) {
    View art = RoundedRectangle(corner_radius)
                   .fill(Gradient(colors))
                   .shadow({.color = Color::black.opacity(0.25f), .radius = 8, .y = 3})
                   // A sheen across the top, like light on a sleeve.
                   .overlay(RoundedRectangle(corner_radius)
                                .fill(LinearGradient({Color::white.opacity(0.18f), Color::white.opacity(0.f)},
                                                     UnitPoint::top, UnitPoint::center)));
    if (icon_size > 0) {
        art = art.overlay(Image(icon).font(Font::system(icon_size)).foreground_style(Color::white.opacity(0.55f)));
    }
    return art.aspect_ratio(1);
}

View AlbumArtwork(int album_id, float corner_radius, float icon_size) {
    return Artwork(album(album_id).colors, corner_radius, icon_size);
}

View RowButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto selected = selected_;
    return button.label.padding(EdgeInsets::symmetric(10, 6))
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .background(
            [=](const EnvironmentValues& env) {
                if (selected && selected()) {
                    return env.color(ThemeColor::outline);
                }
                return button.is_hovered() ? env.color(ThemeColor::outline).opacity(0.5f) : Color::clear;
            },
            RoundedRectangle(6));
}

View PillButtonStyle::make_body(const ButtonConfiguration& button) const {
    const bool filled = filled_;
    return button.label.font(Font::system(13, Weight::semibold))
        .foreground_style(filled ? Color::white : kAccent)
        .padding(EdgeInsets::symmetric(22, 7))
        .background(
            [=](const EnvironmentValues& env) {
                const Color base = filled ? kAccent : env.color(ThemeColor::surface_raised);
                return button.is_pressed() ? base.mix(Color::black, 0.15f)
                       : button.is_hovered() ? base.mix(Color::white, 0.08f)
                                             : base;
            },
            Capsule());
}

View IconButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto active = active_;
    const float box = size_ + 14;
    return button.label.font(Font::system(size_))
        .foreground_style([=](const EnvironmentValues& env) {
            if (!button.is_enabled()) {
                return env.color(ThemeColor::tertiary);
            }
            return active && active() ? kAccent : env.color(ThemeColor::primary);
        })
        .frame(box, box)
        .background(
            [=](const EnvironmentValues& env) {
                if (button.is_pressed()) {
                    return env.color(ThemeColor::outline);
                }
                return button.is_hovered() ? env.color(ThemeColor::outline).opacity(0.5f) : Color::clear;
            },
            Circle());
}

// --- GridLayout ---

GridLayout::Metrics GridLayout::metrics(std::optional<float> width) const {
    const float available = width && std::isfinite(*width) ? *width : min_width_ * 4 + spacing_ * 3;
    const auto columns =
        static_cast<std::size_t>(std::max(1.f, std::floor((available + spacing_) / (min_width_ + spacing_))));
    const float item = (available - spacing_ * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    return {columns, std::max(item, 0.f)};
}

Size GridLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    const Metrics m = metrics(proposal.width);
    float height = 0;
    for (std::size_t row = 0; row * m.columns < children.size(); ++row) {
        float row_height = 0;
        for (std::size_t i = row * m.columns; i < std::min(children.size(), (row + 1) * m.columns); ++i) {
            row_height = std::max(row_height, children[i].size_that_fits({m.width, std::nullopt}).height);
        }
        height += row_height + (row > 0 ? spacing_ : 0);
    }
    const float width = proposal.width && std::isfinite(*proposal.width) ? *proposal.width
                                                                          : m.width * static_cast<float>(m.columns) +
                                                                                spacing_ * static_cast<float>(m.columns - 1);
    return {width, height};
}

void GridLayout::place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const {
    const Metrics m = metrics(bounds.width);
    float y = bounds.y;
    for (std::size_t row = 0; row * m.columns < children.size(); ++row) {
        float row_height = 0;
        const std::size_t end = std::min(children.size(), (row + 1) * m.columns);
        for (std::size_t i = row * m.columns; i < end; ++i) {
            const float x = bounds.x + static_cast<float>(i - row * m.columns) * (m.width + spacing_);
            children[i].place({x, y}, {m.width, std::nullopt});
            row_height = std::max(row_height, children[i].size_that_fits({m.width, std::nullopt}).height);
        }
        y += row_height + spacing_;
    }
}

// --- Pieces ---

View PageTitle(std::string title) {
    return Text(std::move(title)).font(Font::system(28, Weight::bold));
}

View SectionHeader(std::string title, std::function<void()> see_all) {
    View heading = Text(std::move(title)).font(Font::title3).bold();
    if (!see_all) {
        return HStack(heading, Spacer());
    }
    return HStack(heading, Spacer(),
                  Button(Text("See All").font(Font::subheadline), std::move(see_all))
                      .button_style(PlainButtonStyle{})
                      .foreground_style(kAccent));
}

View AlbumCard(const Model& model, int album_id) {
    const Album& a = album(album_id);
    const State<bool> hovered(false);
    const Player player = model.player;
    const View play = Button(Image("play.fill"), [=] { player.play(album(album_id).tracks); })
                          .button_style(PlainButtonStyle{})
                          .font(Font::system(13))
                          .foreground_style(Color::white)
                          .frame(32, 32)
                          .background(kAccent, Circle())
                          .shadow({.color = Color::black.opacity(0.35f), .radius = 6, .y = 2})
                          .accessibility_label("Play " + a.title)
                          .padding(10);
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 3},
                  AlbumArtwork(album_id, 8, 36)
                      .overlay(If([=] { return hovered(); }, play.transition(Transition::opacity())),
                               Alignment::bottom_leading)
                      .animation(Animation::ease_out(0.15), [=] { return hovered(); })
                      .padding(EdgeInsets{.bottom = 5}),
                  Text(a.title).font(Font::subheadline).line_limit(1),
                  Text(artist(a.artist).name).font(Font::subheadline).foreground_style(ThemeColor::secondary).line_limit(1))
        .content_shape(Rectangle())
        .on_hover([=](bool inside) { hovered = inside; })
        .on_tap_gesture([=] { model.open({Route::Kind::album, album_id}); })
        .accessibility_element()
        .accessibility_action([=] { model.open({Route::Kind::album, album_id}); });
}

View Shelf(const Model& model, std::string title, std::vector<int> album_ids) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  SectionHeader(std::move(title)),
                  ScrollView({.axes = ScrollAxes::horizontal, .shows_indicators = false},
                             HStack({.alignment = VerticalAlignment::top, .spacing = 18},
                                    ForEach(album_ids, [=](int id) { return AlbumCard(model, id).frame({.width = 170}); }))
                                 .padding(EdgeInsets{.top = 4, .bottom = 10})));
}

View AlbumGrid(const Model& model, std::vector<int> album_ids) {
    return GridLayout(160, 22)(ForEach(album_ids, [=](int id) { return AlbumCard(model, id); }));
}

View NowPlayingBars(const Player& player) {
    const Player p = player;
    return Canvas([p](GraphicsContext& g) {
               const bool on = p.playing.peek();
               const double t = g.time();
               const Size size = g.size();
               constexpr float kBar = 3;
               constexpr float kGap = 2;
               constexpr double kSpeeds[] = {7.3, 9.1, 6.2};
               constexpr double kPhases[] = {0.0, 1.7, 3.1};
               for (int i = 0; i < 3; ++i) {
                   const double bounce = on ? std::abs(std::sin(t * kSpeeds[i] + kPhases[i])) : 0.3 + 0.2 * i;
                   const float height = size.height * static_cast<float>(0.25 + 0.75 * bounce);
                   g.fill_rect({static_cast<float>(i) * (kBar + kGap), size.height - height, kBar, height}, kAccent, 1);
               }
               if (on) {
                   g.request_frame();
               }
           })
        .frame(13, 12)
        .accessibility_hidden();
}

View TrackMenu(const Model& model, int track_id) {
    const Player player = model.player;
    const Track& t = track(track_id);
    const int album_id = t.album;
    const int artist_id = album_of(t).artist;
    return Menu(MenuItem("Play Next", [=] { player.play_next(track_id); }, {.image = ImageSource{.system_name = "queue"}}),
                MenuItem("Add to Queue", [=] { player.add_to_queue(track_id); }),
                Divider(),
                MenuItem(player.is_loved(track_id) ? "Unlove" : "Love", [=] { player.toggle_love(track_id); },
                         {.image = ImageSource{.system_name = "heart"}}),
                Submenu("Add to Playlist",
                        [=] {
                            return Menu(ForEach(model.playlists, &Playlist::id, [=](const Playlist& p) {
                                const int playlist_id = p.id;
                                return MenuItem(p.name, [=] { model.add_to_playlist(playlist_id, track_id); });
                            }));
                        }),
                Divider(),
                MenuItem("Go to Album", [=] { model.open({Route::Kind::album, album_id}); }),
                MenuItem("Go to Artist", [=] { model.open({Route::Kind::artist, artist_id}); }));
}

namespace {

View TrackRow(const Model& model, std::function<std::vector<int>()> list, std::size_t position, int track_id,
              TrackListOptions options) {
    const Track& t = track(track_id);
    const Player player = model.player;
    const State<bool> hovered(false);
    const auto is_current = [=] { return player.current() == track_id; };
    const auto play = [=] { player.play(list(), position); };

    // The leading slot: a play button while hovered, the bars while it's the one playing, else the
    // number or the artwork. Its size is fixed, so the row's height never changes with what's in it.
    const auto play_button = [=](Color color) {
        return Button(Image("play.fill").foreground_style(color), play)
            .button_style(PlainButtonStyle{})
            .font(Font::system(12))
            .accessibility_label("Play");
    };
    View leading;
    if (options.numbers) {
        leading = If([=] { return hovered(); }, play_button(kAccent),
                     If(is_current, NowPlayingBars(player),
                        Text(std::to_string(t.number)).font(Font::subheadline).foreground_style(ThemeColor::secondary)))
                      .frame(26, 20);
    } else {
        // The artwork stays; the button or the bars go over it, on a darkened layer.
        const auto darkened = [](View content) {
            return ZStack(RoundedRectangle(4).fill(Color::black.opacity(0.45f)), std::move(content));
        };
        leading = ZStack(AlbumArtwork(t.album, 4),
                         If([=] { return hovered(); }, darkened(play_button(Color::white)),
                            If(is_current, darkened(NowPlayingBars(player)))))
                      .frame(34, 34);
    }

    View title = Text(t.title).line_limit(1).foreground_style(
        [=](const EnvironmentValues& env) { return is_current() ? kAccent : env.color(ThemeColor::primary); });
    if (options.artist) {
        title = VStack({.alignment = HorizontalAlignment::leading, .spacing = 1}, title,
                       Text(artist_of(t).name).font(Font::footnote).foreground_style(ThemeColor::secondary).line_limit(1));
    }
    const View album_column = options.album_column
                                  ? Text(album_of(t).title)
                                        .font(Font::subheadline)
                                        .foreground_style(ThemeColor::secondary)
                                        .line_limit(1)
                                        .frame({.min_width = 120, .ideal_width = 220, .max_width = 260,
                                                .alignment = Alignment::leading})
                                  : View();
    const View more = MenuButton(Image("dots-three"), [=] { return TrackMenu(model, track_id); },
                                 {.shows_indicator = false})
                          .button_style(PlainButtonStyle{})
                          .accessibility_label("More")
                          .opacity([=] { return hovered() ? 1.f : 0.f; });

    return HStack({.spacing = 12}, leading, title.frame({.max_width = infinity, .alignment = Alignment::leading}),
                  album_column,
                  // Fixed widths on the right, so the columns line up from row to row (and with
                  // a header: kTrailingColumns).
                  If([=] { return player.is_loved(track_id); },
                     Image("heart.fill").font(Font::system(12)).foreground_style(kAccent))
                      .frame({.width = kLoveColumn}),
                  Text(format_time(t.seconds))
                      .font(Font::subheadline)
                      .foreground_style(ThemeColor::secondary)
                      .frame({.width = kTimeColumn, .alignment = Alignment::trailing}),
                  more.frame({.width = kMoreColumn}))
        .padding(EdgeInsets::symmetric(10, options.numbers ? 9.f : 5.f))
        .content_shape(Rectangle())
        .background(
            [=](const EnvironmentValues& env) {
                return hovered() ? env.color(ThemeColor::outline).opacity(0.45f) : Color::clear;
            },
            RoundedRectangle(6))
        .on_hover([=](bool inside) { hovered = inside; }, HoverDelay{0, 0})
        .on_tap_gesture(play, 2)
        .context_menu([=] { return TrackMenu(model, track_id); })
        .accessibility_element()
        .accessibility_action(play);
}

// A track at a position in a list: keyed by position, so a track that's there twice gets two rows.
struct Entry {
    std::size_t position = 0;
    int track = 0;
    friend bool operator==(const Entry&, const Entry&) = default;
};

} // namespace

View TrackList(const Model& model, std::function<std::vector<int>()> tracks, TrackListOptions options) {
    const auto entries = [tracks] {
        std::vector<Entry> result;
        const std::vector<int> ids = tracks();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            result.push_back({i, ids[i]});
        }
        return result;
    };
    return VStack({.spacing = 0}, ForEach(entries, &Entry::position, [=](const Entry& e) {
                      return TrackRow(model, tracks, e.position, e.track, options);
                  }));
}

} // namespace music
