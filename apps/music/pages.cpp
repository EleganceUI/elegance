// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "pages.h"

#include "ui.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace music {

using namespace elg;

namespace {

// A page's content, scrolling, with the usual margins.
View Page(View content) {
    return ScrollView(std::move(content)
                          .padding(EdgeInsets{.top = 4, .leading = 28, .bottom = 32, .trailing = 28})
                          .frame({.max_width = infinity, .alignment = Alignment::top_leading}));
}

View Column(std::vector<View> children, float spacing = 32) {
    return VStackLayout(HorizontalAlignment::leading, spacing)(std::move(children));
}

std::string upper(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The album colors of a list of tracks: the first album's, else gray.
std::vector<Color> colors_of(const std::vector<int>& track_ids) {
    if (track_ids.empty()) {
        return {Color::hex(0x5A5A5E), Color::hex(0x2C2C2E)};
    }
    return album_of(track(track_ids.front())).colors;
}

View PlayButtons(std::function<void()> play, std::function<void()> shuffle) {
    return HStack({.spacing = 12},
                  Button(Label("Play", {.system_name = "play.fill"}), std::move(play)).button_style(PillButtonStyle(true)),
                  Button(Label("Shuffle", {.system_name = "shuffle"}), std::move(shuffle))
                      .button_style(PillButtonStyle(false)));
}

// A big card with a gradient and words on it (Top Picks, banners).
View FeatureCard(std::vector<Color> colors, std::string kicker, std::string title, std::string subtitle, float height,
                 std::function<void()> open) {
    return RoundedRectangle(12)
        .fill(Gradient(colors))
        .frame({.max_width = infinity, .min_height = height, .max_height = height})
        .overlay(VStack({.alignment = HorizontalAlignment::leading, .spacing = 3},
                        Text(upper(std::move(kicker))).font(Font::caption).bold().foreground_style(Color::white.opacity(0.75f)),
                        Text(std::move(title)).font(Font::system(22, Weight::bold)).foreground_style(Color::white),
                        Text(std::move(subtitle)).font(Font::subheadline).foreground_style(Color::white.opacity(0.85f)).line_limit(2))
                     .padding(18),
                 Alignment::bottom_leading)
        .shadow({.color = Color::black.opacity(0.25f), .radius = 10, .y = 4})
        .content_shape(RoundedRectangle(12))
        .on_tap_gesture(std::move(open))
        .accessibility_element();
}

View LiveBadge() {
    return Text("LIVE")
        .font(Font::caption2)
        .bold()
        .foreground_style(Color::white)
        .padding(EdgeInsets::symmetric(6, 2))
        .background(kAccent, Capsule());
}

View StationCard(const Model& model, const Station& station) {
    const Player player = model.player;
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 3},
                  Artwork(station.colors, 10, 34, "broadcast")
                      .overlay(LiveBadge().padding(8), Alignment::top_leading)
                      .padding(EdgeInsets{.bottom = 5}),
                  Text(station.name).font(Font::subheadline).line_limit(1),
                  Text(station.tagline).font(Font::subheadline).foreground_style(ThemeColor::secondary).line_limit(1))
        .content_shape(Rectangle())
        .on_tap_gesture([=] { player.play_shuffled(all_tracks()); })
        .accessibility_element();
}

View ArtistCard(const Model& model, const Artist& a) {
    const int id = a.id;
    std::string initials;
    for (std::size_t i = 0; i < a.name.size(); ++i) {
        if ((i == 0 || a.name[i - 1] == ' ') && initials.size() < 2) {
            initials += a.name[i];
        }
    }
    return VStack({.spacing = 8},
                  Circle()
                      .fill(LinearGradient({a.color.mix(Color::white, 0.25f), a.color.mix(Color::black, 0.35f)},
                                           UnitPoint::top_leading, UnitPoint::bottom_trailing))
                      .shadow({.color = Color::black.opacity(0.25f), .radius = 8, .y = 3})
                      .aspect_ratio(1)
                      .overlay(Text(initials).font(Font::system(34, Weight::bold)).foreground_style(Color::white.opacity(0.9f))),
                  Text(a.name).font(Font::subheadline).line_limit(1))
        .content_shape(Rectangle())
        .on_tap_gesture([=] { model.open({Route::Kind::artist, id}); })
        .accessibility_element();
}

std::vector<int> album_ids(bool newest_first) {
    std::vector<int> ids;
    for (const Album& a : albums()) {
        ids.push_back(a.id);
    }
    if (newest_first) {
        std::ranges::stable_sort(ids, std::ranges::greater{}, [](int id) { return album(id).year; });
    }
    return ids;
}

// --- Sections ---

View ListenNow(const Model& model) {
    const Playlist favorites = model.playlists.peek()[0];
    const Playlist focus = model.playlists.peek()[1];
    const auto open_playlist = [=](int id) { return [=] { model.open({Route::Kind::playlist, id}); }; };
    std::vector<int> recent{101, 107, 105, 110, 103, 112, 108};
    return Page(Column({
        PageTitle("Listen Now"),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Top Picks"),
               HStack({.spacing = 18},
                      FeatureCard(colors_of(favorites.tracks), "Made for You", favorites.name, favorites.description, 200,
                                  open_playlist(favorites.id)),
                      FeatureCard({Color::hex(0x0B3D91), Color::hex(0x00C2A8)}, "Featuring Static Bloom", focus.name,
                                  focus.description, 200, open_playlist(focus.id)),
                      FeatureCard(album(112).colors, "New Release", album(112).title, artist(album(112).artist).name, 200,
                                  [=] { model.open({Route::Kind::album, 112}); }))),
        Shelf(model, "Recently Played", recent),
        Shelf(model, "New Releases", album_ids(true)),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Stations for You"),
               HStack({.alignment = VerticalAlignment::top, .spacing = 18},
                      ForEach(stations(), &Station::id,
                              [=](const Station& s) { return StationCard(model, s).frame({.width = 170}); }))),
    }));
}

View Browse(const Model& model) {
    const Album& spotlight = album(108);
    const Player player = model.player;
    std::vector<int> hot;
    for (const Artist& a : artists()) {
        hot.push_back(top_songs(a.id, 1).front());
    }
    return Page(Column({
        PageTitle("Browse"),
        FeatureCard(spotlight.colors, "New Album", spotlight.title,
                    std::format("{} · {} · {}", artist(spotlight.artist).name, spotlight.genre, spotlight.year), 240,
                    [=] { model.open({Route::Kind::album, 108}); })
            .overlay(Button(Label("Play", {.system_name = "play.fill"}), [=] { player.play(album(108).tracks); })
                         .button_style(PillButtonStyle(true))
                         .padding(18),
                     Alignment::bottom_trailing),
        Shelf(model, "New Music", album_ids(true)),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 8}, SectionHeader("Hot Tracks"),
               TrackList(model, [hot] { return hot; }, {.numbers = false, .artist = true})),
        Shelf(model, "Fresh Picks: Electronic", {108, 109, 102, 106}),
    }));
}

View Radio(const Model& model) {
    const Player player = model.player;
    return Page(Column({
        PageTitle("Radio"),
        FeatureCard({Color::hex(0xFA2D48), Color::hex(0x7A1FA2)}, "Live · Music 1", "The Morning Mix",
                    "New music and old favorites, with Ava and friends.", 220, [=] { player.play_shuffled(all_tracks()); })
            .overlay(LiveBadge().padding(18), Alignment::top_trailing),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Stations"),
               GridLayout(170, 22)(ForEach(stations(), &Station::id, [=](const Station& s) { return StationCard(model, s); }))),
    }));
}

View AlbumsPage(const Model& model, std::string title, std::vector<int> ids) {
    return Page(Column({PageTitle(std::move(title)), AlbumGrid(model, std::move(ids))}, 20));
}

View Artists(const Model& model) {
    return Page(Column({PageTitle("Artists"),
                        GridLayout(140, 26)(ForEach(artists(), &Artist::id,
                                                    [=](const Artist& a) { return ArtistCard(model, a); }))},
                       20));
}

View Songs(const Model& model) {
    const View header = HStack({.spacing = 12}, Text("Title").frame({.max_width = infinity, .alignment = Alignment::leading}),
                               Text("Album").frame({.min_width = 120, .ideal_width = 220, .max_width = 260,
                                                    .alignment = Alignment::leading}),
                               Spacer().frame({.width = kLoveColumn}),
                               Text("Time").frame({.width = kTimeColumn, .alignment = Alignment::trailing}),
                               Spacer().frame({.width = kMoreColumn}))
                            .font(Font::caption)
                            .foreground_style(ThemeColor::secondary)
                            .padding(EdgeInsets{.leading = 56, .trailing = 10});
    return Page(Column({PageTitle("Songs"),
                        VStack({.spacing = 6}, header, Divider(),
                               TrackList(model, [] { return all_tracks(); },
                                         {.numbers = false, .artist = true, .album_column = true}))},
                       16));
}

// --- Pushed pages ---

// Four album covers in a square: a playlist's artwork.
View Mosaic(const std::vector<int>& track_ids, float size) {
    std::vector<int> covers;
    for (const int id : track_ids) {
        if (const int a = track(id).album; !std::ranges::contains(covers, a)) {
            covers.push_back(a);
        }
    }
    if (covers.size() < 4) {
        return Artwork(colors_of(track_ids), 10, size * 0.28f, "playlist").frame(size, size);
    }
    const auto tile = [&](std::size_t i) {
        return Rectangle()
            .fill(Gradient(album(covers[i]).colors))
            .frame(size / 2, size / 2);
    };
    return VStack({.spacing = 0}, HStack({.spacing = 0}, tile(0), tile(1)), HStack({.spacing = 0}, tile(2), tile(3)))
        .clip_shape(RoundedRectangle(10))
        .shadow({.color = Color::black.opacity(0.25f), .radius = 10, .y = 4});
}

View Header(View artwork, std::string kicker, Prop<std::string> title, View subtitle, Prop<std::string> details,
            View buttons) {
    return HStack({.alignment = VerticalAlignment::bottom, .spacing = 26}, std::move(artwork),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 5},
                         Text(upper(std::move(kicker))).font(Font::caption).bold().foreground_style(ThemeColor::secondary),
                         Text(std::move(title)).font(Font::system(30, Weight::bold)).line_limit(2), std::move(subtitle),
                         Text(std::move(details)).font(Font::footnote).foreground_style(ThemeColor::tertiary),
                         std::move(buttons).padding(EdgeInsets{.top = 12})),
                  Spacer());
}

View AlbumPage(const Model& model, int id) {
    const Album& a = album(id);
    const Player player = model.player;
    const int artist_id = a.artist;
    const View artist_link = Button(Text(artist(a.artist).name).font(Font::title3), [=] {
                                 model.open({Route::Kind::artist, artist_id});
                             })
                                 .button_style(PlainButtonStyle{})
                                 .foreground_style(kAccent);
    std::vector<int> more = albums_by(a.artist);
    std::erase(more, id);
    std::vector<View> parts{
        Header(AlbumArtwork(id, 10, 64).frame(230, 230), "Album", a.title, artist_link,
               std::format("{} · {}", upper(a.genre), a.year),
               PlayButtons([=] { player.play(album(id).tracks); }, [=] { player.play_shuffled(album(id).tracks); })),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               TrackList(model, [id] { return album(id).tracks; }),
               Text(std::format("Released {}\n{}", a.year, describe(a.tracks)))
                   .font(Font::footnote)
                   .foreground_style(ThemeColor::secondary)
                   .padding(EdgeInsets{.leading = 10})),
    };
    if (!more.empty()) {
        parts.push_back(Shelf(model, "More by " + artist(a.artist).name, more));
    }
    return Page(Column(std::move(parts), 28));
}

View ArtistPage(const Model& model, int id) {
    const Artist& a = artist(id);
    const Player player = model.player;
    const std::vector<int> top = top_songs(id, 5);
    const View banner =
        RoundedRectangle(14)
            .fill(LinearGradient({a.color.mix(Color::white, 0.15f), a.color.mix(Color::black, 0.55f)},
                                 UnitPoint::top_trailing, UnitPoint::bottom_leading))
            .frame({.max_width = infinity, .min_height = 240, .max_height = 240})
            .overlay(HStack({.alignment = VerticalAlignment::bottom, .spacing = 16},
                            VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                   Text(upper(a.genre)).font(Font::caption).bold().foreground_style(Color::white.opacity(0.75f)),
                                   Text(a.name).font(Font::system(40, Weight::heavy)).foreground_style(Color::white)),
                            Spacer(),
                            Button(Label("Play", {.system_name = "play.fill"}), [=] { player.play(top_songs(id, 50)); })
                                .button_style(PillButtonStyle(true)))
                         .padding(22),
                     Alignment::bottom_leading);
    return Page(Column({
        banner,
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 8}, SectionHeader("Top Songs"),
               TrackList(model, [top] { return top; }, {.numbers = false})),
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Albums"),
               AlbumGrid(model, albums_by(id))),
    }));
}

View PlaylistPage(const Model& model, int id) {
    const Player player = model.player;
    const auto tracks = [=] { return model.playlist(id).tracks; };
    // The artwork follows the songs as they're added: rebuilt when its first albums change.
    const View artwork = ForEach([=] { return std::vector<std::vector<int>>{tracks()}; },
                                 [](const std::vector<int>&) { return std::string("artwork"); },
                                 [](const std::vector<int>& ids) { return Mosaic(ids, 230); });
    return Page(Column({
        Header(artwork.frame(230, 230), "Playlist", [=] { return model.playlist(id).name; },
               Text([=] { return model.playlist(id).description; }).foreground_style(ThemeColor::secondary),
               [=] { return describe(tracks()); },
               PlayButtons([=] { player.play(tracks()); }, [=] { player.play_shuffled(tracks()); })),
        If([=] { return tracks().empty(); },
           Text("This playlist is empty. Right-click a song anywhere and choose Add to Playlist.")
               .foreground_style(ThemeColor::secondary)
               .padding(EdgeInsets{.leading = 10}),
           TrackList(model, tracks, {.numbers = false, .artist = true, .album_column = true})),
    }));
}

} // namespace

View SectionPage(const Model& model, Section section) {
    switch (section.kind) {
    case Section::Kind::listen_now: return ListenNow(model);
    case Section::Kind::browse: return Browse(model);
    case Section::Kind::radio: return Radio(model);
    case Section::Kind::recently_added: return AlbumsPage(model, "Recently Added", album_ids(true));
    case Section::Kind::artists: return Artists(model);
    case Section::Kind::albums: {
        std::vector<int> ids = album_ids(false);
        std::ranges::sort(ids, {}, [](int id) { return album(id).title; });
        return AlbumsPage(model, "Albums", std::move(ids));
    }
    case Section::Kind::songs: return Songs(model);
    case Section::Kind::playlist: return PlaylistPage(model, section.id);
    }
    return View();
}

View RoutePage(const Model& model, Route route) {
    switch (route.kind) {
    case Route::Kind::album: return AlbumPage(model, route.id);
    case Route::Kind::artist: return ArtistPage(model, route.id);
    case Route::Kind::playlist: return PlaylistPage(model, route.id);
    }
    return View();
}

View SearchPage(const Model& model) {
    const auto query = [=] { return lower(model.search()); };
    const auto matches = [](const std::string& text, const std::string& q) { return lower(text).find(q) != std::string::npos; };
    const auto found_artists = [=] {
        std::vector<Artist> result;
        for (const Artist& a : artists()) {
            if (matches(a.name, query()) || matches(a.genre, query())) {
                result.push_back(a);
            }
        }
        return result;
    };
    const auto found_albums = [=] {
        std::vector<int> result;
        for (const Album& a : albums()) {
            if (matches(a.title, query()) || matches(artist(a.artist).name, query())) {
                result.push_back(a.id);
            }
        }
        return result;
    };
    const auto found_songs = [=] {
        std::vector<int> result;
        for (const int id : all_tracks()) {
            if (matches(track(id).title, query())) {
                result.push_back(id);
            }
        }
        return result;
    };
    const auto nothing = [=] { return found_artists().empty() && found_albums().empty() && found_songs().empty(); };
    return Page(Column({
        PageTitle("Search"),
        Text([=] { return std::format("Results for “{}”", model.search()); }).foreground_style(ThemeColor::secondary),
        If(nothing,
           VStack({.spacing = 8}, Image("magnifying-glass").font(Font::system(40)).foreground_style(ThemeColor::tertiary),
                  Text("No Results").font(Font::title3).bold(),
                  Text("Try a different search.").foreground_style(ThemeColor::secondary))
               .frame({.max_width = infinity})
               .padding(EdgeInsets{.top = 60})),
        If([=] { return !found_artists().empty(); },
           VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Artists"),
                  GridLayout(120, 22)(ForEach(found_artists, &Artist::id,
                                              [=](const Artist& a) { return ArtistCard(model, a); })))),
        If([=] { return !found_albums().empty(); },
           VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, SectionHeader("Albums"),
                  GridLayout(150, 22)(ForEach(found_albums, [=](int id) { return AlbumCard(model, id); })))),
        If([=] { return !found_songs().empty(); },
           VStack({.alignment = HorizontalAlignment::leading, .spacing = 8}, SectionHeader("Songs"),
                  TrackList(model, found_songs, {.numbers = false, .artist = true, .album_column = true}))),
    }));
}

std::string section_title(const Model& model, Section section) {
    switch (section.kind) {
    case Section::Kind::listen_now: return "Listen Now";
    case Section::Kind::browse: return "Browse";
    case Section::Kind::radio: return "Radio";
    case Section::Kind::recently_added: return "Recently Added";
    case Section::Kind::artists: return "Artists";
    case Section::Kind::albums: return "Albums";
    case Section::Kind::songs: return "Songs";
    case Section::Kind::playlist: return model.playlist(section.id).name;
    }
    return {};
}

std::string route_title(const Model& model, Route route) {
    switch (route.kind) {
    case Route::Kind::album: return album(route.id).title;
    case Route::Kind::artist: return artist(route.id).name;
    case Route::Kind::playlist: return model.playlist(route.id).name;
    }
    return {};
}

} // namespace music
