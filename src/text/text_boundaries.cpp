// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/text_boundaries.h"

#include <algorithm>
#include <cctype>

namespace elg::detail {

namespace {

bool is_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

} // namespace

bool is_word_byte(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) != 0 || c == '_';
}

std::size_t character_boundary(std::string_view text, std::size_t byte) noexcept {
    byte = std::min(byte, text.size());
    while (byte > 0 && byte < text.size() && is_continuation(text[byte])) {
        --byte;
    }
    return byte;
}

std::pair<std::size_t, std::size_t> word_around(std::string_view text, std::size_t byte) noexcept {
    byte = std::min(byte, text.size());
    // A word under `byte`, or the one it ends.
    const bool word = byte < text.size() ? is_word_byte(text[byte]) : byte > 0 && is_word_byte(text[byte - 1]);
    std::size_t begin = byte;
    std::size_t end = byte;
    while (begin > 0 && is_word_byte(text[begin - 1]) == word && text[begin - 1] != '\n') {
        --begin;
    }
    while (end < text.size() && is_word_byte(text[end]) == word && text[end] != '\n') {
        ++end;
    }
    return {character_boundary(text, begin), character_boundary(text, end)};
}

std::pair<std::size_t, std::size_t> paragraph_around(std::string_view text, std::size_t byte) noexcept {
    byte = std::min(byte, text.size());
    const std::size_t before = byte == 0 ? std::string_view::npos : text.rfind('\n', byte - 1);
    const std::size_t begin = before == std::string_view::npos ? 0 : before + 1;
    const std::size_t after = text.find('\n', byte);
    const std::size_t end = after == std::string_view::npos ? text.size() : after;
    return {begin, std::max(begin, end)};
}

} // namespace elg::detail
