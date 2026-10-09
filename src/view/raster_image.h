// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_RASTER_IMAGE_H
#define ELG_VIEW_RASTER_IMAGE_H

#include <elg/image.h>
#include <elg/view.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace elg::detail {

// Image({.file}) / Image({.data}): a raster image view (design.md §13.3).
View raster_image(const ImageSource& source);
// From encoded bytes already in memory (shared, not copied).
View raster_image(std::shared_ptr<const std::vector<std::uint8_t>> data, float scale);

} // namespace elg::detail

#endif // ELG_VIEW_RASTER_IMAGE_H
