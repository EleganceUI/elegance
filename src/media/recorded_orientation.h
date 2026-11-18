// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_RECORDED_ORIENTATION_H
#define ELG_MEDIA_RECORDED_ORIENTATION_H

// The EXIF orientation an image file records, read from the file itself (ELGSPEC-0005.R6): for the
// codecs that don't report it, so that a file shows the same way on every platform.

#include <cstdint>
#include <span>

namespace elg::detail {

// The EXIF orientation (1-8) recorded in a JPEG file (an APP1 segment), a PNG file (its eXIf chunk)
// or a WebP file (its EXIF chunk), or 1: another format, none recorded, or data that isn't whole.
// Every read is checked against `data`, which comes from files and the network.
int recorded_orientation(std::span<const std::uint8_t> data);

} // namespace elg::detail

#endif // ELG_MEDIA_RECORDED_ORIENTATION_H
