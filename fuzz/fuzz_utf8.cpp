// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// UTF-8 decoding (core/utf8.h). The decoder must move forward on every input, stay within it, and
// return only scalar values; replacing must give well-formed text, which it leaves as it is.

#include "core/utf8.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace elg::detail;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t before = at;
        const char32_t code = decode_utf8(text, at);
        if (at <= before || at > text.size() || at - before > 4 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            std::abort();
        }
    }
    const std::string valid = valid_utf8(text);
    if (!is_valid_utf8(valid) || valid_utf8(valid) != valid || is_valid_utf8(text) != (valid == text)) {
        std::abort();
    }
    std::string in_place(text);
    make_valid_utf8(in_place);
    if (in_place != valid) {
        std::abort();
    }
    return 0;
}
