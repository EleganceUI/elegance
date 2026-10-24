// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef MUSIC_LIBRARY_H
#define MUSIC_LIBRARY_H

// The music library: a fixed, made-up catalog (artists, albums, tracks, stations), and the
// playlists the user can add to.

#include <elg/elegance.h>

#include <string>
#include <vector>

namespace music {

struct Artist {
    int id = 0;
    std::string name;
    std::string genre;
    elg::Color color; // artwork placeholder
};

struct Album {
    int id = 0;
    int artist = 0;
    std::string title;
    std::string genre;
    int year = 0;
    std::vector<elg::Color> colors; // the artwork placeholder's gradient
    std::vector<int> tracks;
};

struct Track {
    int id = 0;
    int album = 0;
    int number = 0; // on its album
    std::string title;
    int seconds = 0;
};

struct Playlist {
    int id = 0;
    std::string name;
    std::string description;
    std::vector<int> tracks;

    friend bool operator==(const Playlist&, const Playlist&) = default;
};

struct Station {
    int id = 0;
    std::string name;
    std::string tagline;
    std::vector<elg::Color> colors;
};

const std::vector<Artist>& artists();
const std::vector<Album>& albums();
const std::vector<Track>& tracks();
const std::vector<Station>& stations();
std::vector<Playlist> starting_playlists();

const Artist& artist(int id);
const Album& album(int id);
const Track& track(int id);
const Artist& artist_of(const Track& track);
const Album& album_of(const Track& track);
std::vector<int> albums_by(int artist);
// The artist's most played songs: the first track of each album, then the second, ...
std::vector<int> top_songs(int artist, std::size_t count);
// Every track, in library order (by artist, album, number).
std::vector<int> all_tracks();
// "3:41"
std::string format_time(double seconds);
// "12 songs, 48 minutes"
std::string describe(const std::vector<int>& track_ids);

} // namespace music

#endif // MUSIC_LIBRARY_H
