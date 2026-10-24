// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef MUSIC_PAGES_H
#define MUSIC_PAGES_H

// The detail's pages: one per sidebar section (the roots), the pages pushed over them (albums,
// artists, playlists), and search results.

#include "model.h"

#include <elg/elegance.h>

#include <string>

namespace music {

elg::View SectionPage(const Model& model, Section section);
elg::View RoutePage(const Model& model, Route route);
// Results for model.search, as it's typed.
elg::View SearchPage(const Model& model);

std::string section_title(const Model& model, Section section);
std::string route_title(const Model& model, Route route);

} // namespace music

#endif // MUSIC_PAGES_H
