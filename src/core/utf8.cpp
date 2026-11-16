// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/utf8.h"

namespace elg::detail {

namespace {

// Not a character: what decode() returns for a maximal subpart.
constexpr char32_t kInvalid = 0x110000;

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// The character at `at`, or kInvalid; `at` moves past it, or past the maximal subpart.
char32_t decode(std::string_view text, std::size_t& at) noexcept {
    const auto lead = static_cast<unsigned char>(text[at++]);
    if (lead < 0x80) {
        return lead;
    }
    // The continuation bytes allowed after the lead: 80-BF, but for the second byte after E0
    // (A0-BF: no overlong forms), ED (80-9F: no surrogates), F0 (90-BF: no overlong forms) and
    // F4 (80-8F: nothing past U+10FFFF).
    int extra = 0;
    char32_t code = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        code = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        code = lead & 0x0Fu;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        code = lead & 0x07u;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
        return kInvalid; // a continuation byte, C0, C1 or F5-FF: never in well-formed text
    }
    for (int i = 0; i < extra; ++i) {
        if (at >= text.size()) {
            return kInvalid; // cut short by the end
        }
        const auto next = static_cast<unsigned char>(text[at]);
        if (next < low || next > high) {
            return kInvalid; // `next` isn't part of this subpart; it starts the next character
        }
        code = (code << 6) | (next & 0x3Fu);
        ++at;
        low = 0x80;
        high = 0xBF;
    }
    return code;
}

} // namespace

char32_t decode_utf8(std::string_view text, std::size_t& at) noexcept {
    const char32_t code = decode(text, at);
    return code == kInvalid ? kReplacementCharacter : code;
}

bool is_valid_utf8(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        if (decode(text, at) == kInvalid) {
            return false;
        }
    }
    return true;
}

std::string valid_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = at;
        if (decode(text, at) == kInvalid) {
            out += kReplacement;
        } else {
            out += text.substr(start, at - start);
        }
    }
    return out;
}

void make_valid_utf8(std::string& text) {
    if (!is_valid_utf8(text)) {
        text = valid_utf8(text);
    }
}

} // namespace elg::detail
