// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "model.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <random>

namespace music {

using namespace elg;

namespace {

std::mt19937& random() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

constexpr double kTick = 0.25; // seconds between clock ticks

} // namespace

// --- Player ---

int Player::current() const {
    const std::vector<int>& q = queue();
    const int i = index();
    return i >= 0 && static_cast<std::size_t>(i) < q.size() ? q[static_cast<std::size_t>(i)] : 0;
}

std::vector<int> Player::up_next() const {
    const std::vector<int>& q = queue();
    const auto from = static_cast<std::size_t>(std::max(index() + 1, 0));
    return from < q.size() ? std::vector<int>(q.begin() + static_cast<std::ptrdiff_t>(from), q.end())
                           : std::vector<int>{};
}

void Player::play(std::vector<int> tracks, std::size_t start) const {
    if (tracks.empty()) {
        return;
    }
    start = std::min(start, tracks.size() - 1);
    if (shuffle.peek()) {
        // The chosen track first, the rest in random order.
        std::swap(tracks[0], tracks[start]);
        std::shuffle(tracks.begin() + 1, tracks.end(), random());
        start = 0;
    }
    batch([&] {
        queue = std::move(tracks);
        index = static_cast<int>(start);
        position = 0.0;
        playing = true;
    });
}

void Player::play_shuffled(std::vector<int> tracks) const {
    std::shuffle(tracks.begin(), tracks.end(), random());
    batch([&] {
        shuffle = true;
        queue = std::move(tracks);
        index = 0;
        position = 0.0;
        playing = true;
    });
}

void Player::toggle() const {
    if (index.peek() < 0) {
        play(all_tracks()); // nothing loaded: the whole library
        return;
    }
    playing = !playing.peek();
}

void Player::next() const {
    const int count = static_cast<int>(queue.peek().size());
    if (count == 0) {
        return;
    }
    batch([&] {
        position = 0.0;
        if (index.peek() + 1 < count) {
            index = index.peek() + 1;
        } else if (repeat.peek() == Repeat::all) {
            index = 0;
        } else {
            playing = false; // the end of the queue: stop on the last track
        }
    });
}

void Player::previous() const {
    // Like every player: back to the start, unless it just started.
    if (position.peek() > 3 || index.peek() <= 0) {
        position = 0.0;
        return;
    }
    batch([&] {
        index = index.peek() - 1;
        position = 0.0;
    });
}

void Player::seek(double seconds) const {
    position = std::max(seconds, 0.0);
}

void Player::set_shuffle(bool on) const {
    batch([&] {
        shuffle = on;
        if (on && index.peek() >= 0) {
            // Turning it on reshuffles what's still to come.
            queue.update([&](std::vector<int>& q) {
                std::shuffle(q.begin() + index.peek() + 1, q.end(), random());
            });
        }
    });
}

void Player::cycle_repeat() const {
    switch (repeat.peek()) {
    case Repeat::off: repeat = Repeat::all; break;
    case Repeat::all: repeat = Repeat::one; break;
    case Repeat::one: repeat = Repeat::off; break;
    }
}

void Player::play_next(int track) const {
    if (index.peek() < 0) {
        play({track});
        return;
    }
    queue.update([&](std::vector<int>& q) { q.insert(q.begin() + index.peek() + 1, track); });
}

void Player::add_to_queue(int track) const {
    if (index.peek() < 0) {
        play({track});
        return;
    }
    queue.update([&](std::vector<int>& q) { q.push_back(track); });
}

bool Player::is_loved(int track) const {
    return std::ranges::contains(loved(), track);
}

void Player::toggle_love(int track) const {
    loved.update([&](std::vector<int>& list) {
        if (std::erase(list, track) == 0) {
            list.push_back(track);
        }
    });
}

void Player::tick(double seconds) const {
    if (!playing.peek() || index.peek() < 0) {
        return;
    }
    const int id = untrack([&] { return current(); });
    const double next_position = position.peek() + seconds;
    if (next_position < music::track(id).seconds) {
        position = next_position;
    } else if (repeat.peek() == Repeat::one) {
        position = 0.0;
    } else {
        next();
    }
}

// --- PlaybackClock ---

PlaybackClock::PlaybackClock(Player player)
    : player_(std::move(player)), thread_([player = player_, running = running_](std::stop_token stop) {
          while (!stop.stop_requested()) {
              std::this_thread::sleep_for(std::chrono::duration<double>(kTick));
              if (running->load()) {
                  post_to_ui_thread([player] { player.tick(kTick); });
              }
          }
      }) {}

void PlaybackClock::follow() const {
    effect([player = player_, running = running_] { running->store(player.playing()); });
}

// --- Model ---

void Model::toggle_panel(Panel which) const {
    with_animation(Animation::smooth(), [&] { panel = panel.peek() == which ? Panel::none : which; });
}

void Model::open(Route route) const {
    path.update([&](std::vector<Route>& p) { p.push_back(route); });
}

void Model::select(Section next) const {
    batch([&] {
        section = next;
        path = std::vector<Route>();
        search = std::string();
    });
}

Playlist Model::playlist(int id) const {
    const std::vector<Playlist>& all = playlists();
    const auto it = std::ranges::find(all, id, &Playlist::id);
    return it != all.end() ? *it : Playlist{id, "Playlist", "", {}};
}

void Model::add_to_playlist(int id, int track) const {
    playlists.update([&](std::vector<Playlist>& all) {
        if (const auto it = std::ranges::find(all, id, &Playlist::id); it != all.end()) {
            it->tracks.push_back(track);
        }
    });
}

void Model::new_playlist() const {
    int id = 1;
    for (const Playlist& p : playlists.peek()) {
        id = std::max(id, p.id + 1);
    }
    const std::string name = std::format("Untitled Playlist {}", id - starting_playlists().size());
    playlists.update([&](std::vector<Playlist>& all) { all.push_back({id, name, "", {}}); });
    select({Section::Kind::playlist, id});
}

} // namespace music
