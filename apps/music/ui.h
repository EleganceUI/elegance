// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef MUSIC_UI_H
#define MUSIC_UI_H

// Building blocks shared by the pages: artwork, cards, track lists, buttons, and a grid.

#include "model.h"

#include <elg/elegance.h>

#include <functional>
#include <string>
#include <vector>

namespace music {

// The app's accent, as in the music app it's modeled on.
inline constexpr elg::Color kAccent = elg::Color::hex(0xFA2D48);

// --- Look ---

// A gradient through `colors`, evenly spaced.
elg::LinearGradient Gradient(const std::vector<elg::Color>& colors, elg::UnitPoint start = elg::UnitPoint::top_leading,
                             elg::UnitPoint end = elg::UnitPoint::bottom_trailing);

// Album art standing in for real artwork: a gradient with a soft sheen and a faint icon.
elg::View Artwork(std::vector<elg::Color> colors, float corner_radius = 6, float icon_size = 0,
                  std::string icon = "music-notes");
elg::View AlbumArtwork(int album, float corner_radius = 6, float icon_size = 0);

// Rows that light up under the pointer (sidebar items, menus of rows).
class RowButtonStyle final : public elg::ButtonStyle {
public:
    explicit RowButtonStyle(std::function<bool()> selected = {}) : selected_(std::move(selected)) {}
    elg::View make_body(const elg::ButtonConfiguration& button) const override;

private:
    std::function<bool()> selected_;
};

// Capsule buttons: Play (filled with the accent) and Shuffle (tinted).
class PillButtonStyle final : public elg::ButtonStyle {
public:
    explicit PillButtonStyle(bool filled) : filled_(filled) {}
    elg::View make_body(const elg::ButtonConfiguration& button) const override;

private:
    bool filled_;
};

// Round icon buttons for the transport and toolbar; `active` tints them (shuffle on, a panel open).
class IconButtonStyle final : public elg::ButtonStyle {
public:
    explicit IconButtonStyle(float size = 16, std::function<bool()> active = {})
        : size_(size), active_(std::move(active)) {}
    elg::View make_body(const elg::ButtonConfiguration& button) const override;

private:
    float size_;
    std::function<bool()> active_;
};

// --- Layout ---

// Items in as many equal columns as fit at `min_width` each, row after row.
class GridLayout final : public elg::Layout {
public:
    explicit GridLayout(float min_width = 160, float spacing = 20) : min_width_(min_width), spacing_(spacing) {}

    elg::Size size_that_fits(elg::ProposedSize proposal, std::span<const elg::LayoutChild> children) const override;
    void place_children(elg::Rect bounds, elg::ProposedSize proposal,
                        std::span<const elg::LayoutChild> children) const override;

private:
    struct Metrics {
        std::size_t columns = 1;
        float width = 0; // of each item
    };
    Metrics metrics(std::optional<float> width) const;

    float min_width_;
    float spacing_;
};

// --- Pieces ---

// A large page title, as at the top of every page.
elg::View PageTitle(std::string title);
// A section heading inside a page, with an optional "See All" on the right.
elg::View SectionHeader(std::string title, std::function<void()> see_all = {});

// An album's artwork and names. Clicking opens the album; the play button that appears over the
// artwork on hover plays it.
elg::View AlbumCard(const Model& model, int album);
// Albums side by side, scrolling sideways.
elg::View Shelf(const Model& model, std::string title, std::vector<int> albums);
// Albums in a grid filling the width.
elg::View AlbumGrid(const Model& model, std::vector<int> albums);

// Three bars bouncing while the player plays (frozen while paused).
elg::View NowPlayingBars(const Player& player);

// The widths of a track row's columns after the album (for a header over a TrackList).
inline constexpr float kLoveColumn = 14;
inline constexpr float kTimeColumn = 40;
inline constexpr float kMoreColumn = 22;

struct TrackListOptions {
    bool numbers = true;       // track numbers (albums); false: artwork (playlists, songs)
    bool artist = false;       // the artist under the title
    bool album_column = false; // the album in a column of its own
};

// Tracks in rows: double-click (or the play button on hover) plays from that track, with the rest
// of the list queued after it. Right-click or "…" for more.
elg::View TrackList(const Model& model, std::function<std::vector<int>()> tracks, TrackListOptions options = {});

// The menu for a track: queueing, love, playlists, and going to its album or artist.
elg::View TrackMenu(const Model& model, int track);

} // namespace music

#endif // MUSIC_UI_H
