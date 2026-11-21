// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's fonts on Mikros, found without fontconfig's library (platform/mikros/font_files.h).

#include "platform/mikros/font_files.h"

#include <cstdint>
#include <cstdlib>

namespace elg::detail::platform::mikros {

namespace {

constexpr std::size_t max_nesting = 32;

// --- The configuration ---

std::string_view trimmed(std::string_view text) {
    constexpr std::string_view space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x110000) {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// The text with its character and numeric references replaced: &amp; &lt; &gt; &quot; &apos; &#65; &#x41;.
std::string decoded(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t semicolon = text.substr(i, 11).find(';');
        const std::size_t end = semicolon == std::string_view::npos ? semicolon : i + semicolon;
        if (end == std::string_view::npos) {
            out.push_back('&');
            continue;
        }
        const std::string_view name = text.substr(i + 1, end - i - 1);
        if (name == "amp") {
            out.push_back('&');
        } else if (name == "lt") {
            out.push_back('<');
        } else if (name == "gt") {
            out.push_back('>');
        } else if (name == "quot") {
            out.push_back('"');
        } else if (name == "apos") {
            out.push_back('\'');
        } else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits(name.substr(hex ? 2 : 1));
            char* last = nullptr;
            const bool plain = !digits.empty() && digits.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789") ==
                                                       std::string::npos;
            const unsigned long code = plain ? std::strtoul(digits.c_str(), &last, hex ? 16 : 10) : 0;
            if (!plain || code == 0 || code > 0x10FFFF || (code >= 0xD800 && code < 0xE000)) {
                out.push_back('&');
                continue;
            }
            append_utf8(out, static_cast<std::uint32_t>(code));
        } else {
            out.push_back('&');
            continue;
        }
        i = end;
    }
    return out;
}

} // namespace

std::string normalized_family(std::string_view family) {
    std::string out;
    out.reserve(family.size());
    for (const char c : family) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

FontAliases parse_font_configuration(std::string_view text) {
    FontAliases result;
    std::vector<std::string> stack;
    std::vector<std::string> generics;
    std::vector<std::string> prefer;
    std::vector<std::string> accept;
    std::vector<std::string> defaults;
    std::string element_text;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('<', i);
        if (open == std::string_view::npos) {
            break;
        }
        element_text.append(text.substr(i, open - i));
        const std::string_view rest = text.substr(open);
        if (rest.starts_with("<!--")) {
            const std::size_t end = text.find("-->", open + 4);
            if (end == std::string_view::npos) {
                break;
            }
            i = end + 3;
            continue;
        }
        if (rest.starts_with("<?") || rest.starts_with("<!")) {
            const std::size_t end = text.find(rest.starts_with("<?") ? "?>" : ">", open + 2);
            if (end == std::string_view::npos) {
                break;
            }
            i = end + (rest.starts_with("<?") ? 2 : 1);
            continue;
        }
        // A tag, up to the '>' that no quote encloses.
        std::size_t close = open + 1;
        char quote = 0;
        while (close < text.size() && (quote != 0 || text[close] != '>')) {
            if (quote != 0) {
                quote = text[close] == quote ? '\0' : quote;
            } else if (text[close] == '"' || text[close] == '\'') {
                quote = text[close];
            }
            ++close;
        }
        if (close >= text.size()) {
            break;
        }
        std::string_view tag = text.substr(open + 1, close - open - 1);
        i = close + 1;
        const bool closing = tag.starts_with('/');
        const bool self_closing = tag.ends_with('/');
        if (closing) {
            tag.remove_prefix(1);
        }
        const std::string_view name = tag.substr(0, tag.find_first_of(" \t\r\n/"));
        if (!closing) {
            if (stack.size() >= max_nesting) {
                break;
            }
            if (name == "alias" && !self_closing) {
                generics.clear();
                prefer.clear();
                accept.clear();
                defaults.clear();
            }
            if (!self_closing) {
                stack.emplace_back(name);
            }
            element_text.clear();
            continue;
        }
        if (stack.empty() || stack.back() != name) {
            continue;
        }
        stack.pop_back();
        if (name == "family") {
            const std::string family(trimmed(decoded(element_text)));
            if (!family.empty() && !stack.empty()) {
                if (stack.back() == "alias") {
                    generics.push_back(family);
                } else if (stack.size() >= 2 && stack[stack.size() - 2] == "alias") {
                    if (stack.back() == "prefer") {
                        prefer.push_back(family);
                    } else if (stack.back() == "accept") {
                        accept.push_back(family);
                    } else if (stack.back() == "default") {
                        defaults.push_back(family);
                    }
                }
            }
        } else if (name == "alias") {
            for (const std::string& generic : generics) {
                std::vector<std::string>& families = result[normalized_family(generic)];
                families.insert(families.end(), prefer.begin(), prefer.end());
                families.insert(families.end(), accept.begin(), accept.end());
                families.insert(families.end(), defaults.begin(), defaults.end());
            }
        }
        element_text.clear();
    }
    return result;
}

} // namespace elg::detail::platform::mikros
