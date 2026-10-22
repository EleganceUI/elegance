// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "library.h"

#include <algorithm>
#include <format>

namespace music {

using elg::Color;

namespace {

struct AlbumSpec {
    int artist;
    std::string title;
    int year;
    std::vector<Color> colors;
    std::vector<std::string> songs;
};

// Everything here is made up.
const std::vector<AlbumSpec> kAlbums{
    {1, "Glass Gardens", 2024, {Color::hex(0x7B6CF6), Color::hex(0xF29BD4)},
     {"Afterglow", "Paper Moons", "Slow Orbit", "Glass Gardens", "Neon Tides", "Lanterns", "Halcyon", "Soft Static"}},
    {1, "Midnight Postcards", 2021, {Color::hex(0x1F2A6B), Color::hex(0x6D5BD0), Color::hex(0xE6A4C4)},
     {"Postcard No. 1", "Velvet Hours", "Satellite Hearts", "Snowglobe", "Northbound", "Echo Lake"}},
    {2, "Saltwater Season", 2023, {Color::hex(0x0E7C86), Color::hex(0x8EE3C8)},
     {"Low Tide", "Harbor Lights", "Sunburnt", "Rope Swing", "Seaglass", "Long Way Home", "Gulls"}},
    {2, "Basement Tapes Vol. 2", 2019, {Color::hex(0x3A3A3A), Color::hex(0xC9A227)},
     {"Fuzz Pedal Love", "Cassette Summer", "Garage Door", "Static on the Radio", "Last Bus", "Feedback"}},
    {3, "Honey & Smoke", 2025, {Color::hex(0xB8572B), Color::hex(0xF5C16C)},
     {"Honey", "Smoke Signals", "Slow Dance", "Late Checkout", "Gold Chains", "Window Seat", "Sundown", "Amber"}},
    {3, "Blue Room", 2022, {Color::hex(0x0B3D91), Color::hex(0x4FA3E0)},
     {"Blue Room", "Overthinking", "Caller ID", "Rain Check", "2 A.M.", "Softly"}},
    {4, "Pine & Copper", 2020, {Color::hex(0x4E6B3A), Color::hex(0xC28A4C)},
     {"Woodsmoke", "Copper Kettle", "Mountain Hymn", "Old Roads", "River Song", "Lantern Light", "Homeward"}},
    {5, "Pulse Theory", 2024, {Color::hex(0x00C2A8), Color::hex(0x3B1C8C), Color::hex(0xFF4F9A)},
     {"Pulse", "Grid Runner", "Chromatic", "Overclock", "Afterimage", "Signal Loss", "Bloom", "Reboot", "Night Bus"}},
    {5, "Lowlight", 2021, {Color::hex(0x151515), Color::hex(0x5A2E98)},
     {"Lowlight", "Subway Ghosts", "Monochrome", "Drift", "Static Bloom"}},
    {6, "Quiet Hours", 2023, {Color::hex(0x2D2A26), Color::hex(0xD9B98C)},
     {"Blue in Green Tea", "Brushwork", "Quiet Hours", "Rainy Tuesday", "The Long Note", "Nightcap"}},
    {7, "Sunday Service Club", 2022, {Color::hex(0xE4572E), Color::hex(0xFFC914)},
     {"Good Morning", "Hold On", "Front Porch", "Testify", "Sweet Thing", "Golden Hour", "Amen"}},
    {8, "Paper Planes & Satellites", 2025, {Color::hex(0x2E86AB), Color::hex(0xF6F5AE), Color::hex(0xF24236)},
     {"Paper Planes", "Satellite", "Gravity Well", "Kite Theory", "Weather Balloon", "Headwind", "Landing Lights"}},
};

std::vector<Artist> make_artists() {
    return {
        {1, "Luna Vale", "Dream Pop", Color::hex(0x7B6CF6)},
        {2, "Harbor Lights", "Indie Rock", Color::hex(0x0E7C86)},
        {3, "Nova Reyes", "R&B", Color::hex(0xB8572B)},
        {4, "Copperline", "Folk", Color::hex(0x4E6B3A)},
        {5, "Static Bloom", "Electronic", Color::hex(0x5A2E98)},
        {6, "Mira Okafor Trio", "Jazz", Color::hex(0x8C6A43)},
        {7, "Golden Hour Assembly", "Soul", Color::hex(0xE4572E)},
        {8, "Kite Theory", "Alternative", Color::hex(0x2E86AB)},
    };
}

struct Catalog {
    std::vector<Artist> artists = make_artists();
    std::vector<Album> albums;
    std::vector<Track> tracks;

    Catalog() {
        int album_id = 100;
        int track_id = 1000;
        for (const AlbumSpec& spec : kAlbums) {
            Album album{++album_id, spec.artist, spec.title, artist(spec.artist).genre, spec.year, spec.colors, {}};
            int number = 0;
            for (const std::string& song : spec.songs) {
                ++track_id;
                // Lengths between about 2:20 and 5:00, fixed per track.
                tracks.push_back({track_id, album.id, ++number, song, 140 + (track_id * 37) % 160});
                album.tracks.push_back(track_id);
            }
            albums.push_back(std::move(album));
        }
    }

    const Artist& artist(int id) const { return *std::ranges::find(artists, id, &Artist::id); }
};

const Catalog& catalog() {
    static const Catalog instance;
    return instance;
}

} // namespace

const std::vector<Artist>& artists() {
    return catalog().artists;
}

const std::vector<Album>& albums() {
    return catalog().albums;
}

const std::vector<Track>& tracks() {
    return catalog().tracks;
}

const std::vector<Station>& stations() {
    static const std::vector<Station> instance{
        {1, "Chill Radio", "Easy listening, all day", {Color::hex(0x5B8DEF), Color::hex(0xA7E0F5)}},
        {2, "Pulse 24/7", "Electronic, nonstop", {Color::hex(0x3B1C8C), Color::hex(0xFF4F9A)}},
        {3, "Front Porch", "Folk and acoustic", {Color::hex(0x4E6B3A), Color::hex(0xE8C07D)}},
        {4, "After Hours", "Late-night R&B", {Color::hex(0x1F2A6B), Color::hex(0xB8572B)}},
    };
    return instance;
}

std::vector<Playlist> starting_playlists() {
    const auto pick = [](std::initializer_list<int> indices) {
        std::vector<int> ids;
        for (const int i : indices) {
            ids.push_back(tracks()[static_cast<std::size_t>(i) % tracks().size()].id);
        }
        return ids;
    };
    return {
        {1, "Favorites Mix", "Songs you keep coming back to.", pick({0, 9, 21, 30, 44, 51, 60, 70})},
        {2, "Focus Flow", "Instrumental-ish, for getting things done.", pick({52, 53, 58, 59, 60, 61, 62, 63})},
        {3, "Road Trip", "Windows down.", pick({14, 15, 16, 22, 75, 76, 77, 78, 79, 23})},
        {4, "Late Night", "For the hours after midnight.", pick({8, 10, 33, 38, 64, 67, 69})},
    };
}

const Artist& artist(int id) {
    return catalog().artist(id);
}

const Album& album(int id) {
    return *std::ranges::find(albums(), id, &Album::id);
}

const Track& track(int id) {
    return *std::ranges::find(tracks(), id, &Track::id);
}

const Album& album_of(const Track& t) {
    return album(t.album);
}

const Artist& artist_of(const Track& t) {
    return artist(album_of(t).artist);
}

std::vector<int> albums_by(int artist_id) {
    std::vector<int> result;
    for (const Album& a : albums()) {
        if (a.artist == artist_id) {
            result.push_back(a.id);
        }
    }
    return result;
}

std::vector<int> top_songs(int artist_id, std::size_t count) {
    std::vector<int> result;
    const std::vector<int> theirs = albums_by(artist_id);
    for (std::size_t round = 0; result.size() < count; ++round) {
        bool any = false;
        for (const int id : theirs) {
            const Album& a = album(id);
            if (round < a.tracks.size() && result.size() < count) {
                result.push_back(a.tracks[round]);
                any = true;
            }
        }
        if (!any) {
            break;
        }
    }
    return result;
}

std::vector<int> all_tracks() {
    std::vector<int> result;
    for (const Artist& a : artists()) {
        for (const int album_id : albums_by(a.id)) {
            const Album& al = album(album_id);
            result.insert(result.end(), al.tracks.begin(), al.tracks.end());
        }
    }
    return result;
}

std::string format_time(double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    return std::format("{}:{:02}", total / 60, total % 60);
}

std::string describe(const std::vector<int>& track_ids) {
    int seconds = 0;
    for (const int id : track_ids) {
        seconds += track(id).seconds;
    }
    const int minutes = (seconds + 30) / 60;
    const std::string songs = std::format("{} song{}", track_ids.size(), track_ids.size() == 1 ? "" : "s");
    if (minutes >= 60) {
        return std::format("{}, {} hr {} min", songs, minutes / 60, minutes % 60);
    }
    return std::format("{}, {} minutes", songs, minutes);
}

} // namespace music
