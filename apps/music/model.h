// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef MUSIC_MODEL_H
#define MUSIC_MODEL_H

// The app's state: where the user is (sidebar section, pages pushed), the playlists, and the
// player. Everything is State, so copies of these structs share it.

#include "library.h"

#include <elg/elegence.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace music {

// A page pushed onto the detail's NavigationStack.
struct Route {
    enum class Kind { album, artist, playlist };
    Kind kind = Kind::album;
    int id = 0;

    friend bool operator==(const Route&, const Route&) = default;
};

// What the sidebar has selected: the root of the detail's stack.
struct Section {
    enum class Kind { listen_now, browse, radio, recently_added, artists, albums, songs, playlist };
    Kind kind = Kind::listen_now;
    int id = 0; // playlists

    friend bool operator==(const Section&, const Section&) = default;
};

enum class Repeat { off, all, one };

// Playback, pretended: there's no audio, but the position moves while playing, tracks advance
// at their end, and shuffle, repeat, and the queue behave as they would.
struct Player {
    elg::State<std::vector<int>> queue; // track ids, in the order they play
    elg::State<int> index{-1};          // into the queue; -1: nothing loaded
    elg::State<bool> playing{false};
    elg::State<double> position{0.0}; // seconds into the current track
    elg::State<bool> shuffle{false};
    elg::State<Repeat> repeat{Repeat::off};
    elg::State<float> volume{0.7f};
    elg::State<std::vector<int>> loved;

    // The current track's id, or 0. A tracked read.
    int current() const;
    // The tracks after the current one.
    std::vector<int> up_next() const;

    // Replaces the queue with `tracks` and plays from `start` (shuffled if shuffle is on).
    void play(std::vector<int> tracks, std::size_t start = 0) const;
    void play_shuffled(std::vector<int> tracks) const;
    void toggle() const;
    void next() const;
    void previous() const;
    void seek(double seconds) const;
    void set_shuffle(bool on) const;
    void cycle_repeat() const;
    void play_next(int track) const;
    void add_to_queue(int track) const;
    bool is_loved(int track) const;
    void toggle_love(int track) const;
    // Moves the position on by `seconds` while playing (the clock calls this).
    void tick(double seconds) const;
};

// Ticks the player a few times a second while it plays, from a thread of its own (the ticks
// run on the UI thread). Idle while paused: the UI thread stays asleep.
class PlaybackClock {
public:
    explicit PlaybackClock(Player player);

    // Call from the UI (in a component body): keeps the clock in step with play/pause.
    void follow() const;

private:
    Player player_;
    std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
    std::jthread thread_;
};

// What the panel beside the page shows, if anything.
enum class Panel { none, lyrics, queue };

struct Model {
    Player player;
    elg::State<Section> section;
    elg::State<std::vector<Route>> path;
    elg::State<std::vector<Playlist>> playlists{starting_playlists()};
    elg::State<std::string> search;
    elg::State<bool> sidebar_shown{true};
    elg::State<Panel> panel{Panel::none};

    // Shows `which` in the panel, or closes the panel when it's already showing it.
    void toggle_panel(Panel which) const;

    // Opens a page on top of the current one.
    void open(Route route) const;
    // Shows a sidebar section: back to its root page, search cleared.
    void select(Section section) const;
    // The playlist with this id (empty if it was removed). A tracked read.
    Playlist playlist(int id) const;
    void add_to_playlist(int playlist, int track) const;
    // Adds "Untitled Playlist N" and opens it.
    void new_playlist() const;
};

} // namespace music

#endif // MUSIC_MODEL_H
