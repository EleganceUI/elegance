// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// SVG path data: Path::svg, which may only fail with UsageError, then what's done with the path
// it returns: its bounds, flattening, and a dashed stroke.

#include <elg/error.h>
#include <elg/path.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    try {
        const elg::Path path = elg::Path::svg(text);
        static_cast<void>(path.bounds());
        static_cast<void>(path.flatten());
        static_cast<void>(path.stroked({.width = 2, .cap = elg::LineCap::round, .dash = {3, 1}}));
    } catch (const elg::UsageError&) {
        // Not path data it takes.
    }
    return 0;
}
