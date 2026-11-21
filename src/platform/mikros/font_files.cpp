// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's fonts on Mikros, found without fontconfig's library (platform/mikros/font_files.h).

#include "platform/mikros/font_files.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <tuple>

namespace elg::detail::platform::mikros {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t max_configuration_size = std::size_t{1} << 20;
constexpr std::size_t max_nesting = 32;
constexpr std::size_t max_font_files = 2048;
constexpr std::uint32_t max_tables = 512;
constexpr std::uint32_t max_name_table = std::uint32_t{1} << 20;

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

// --- Font files ---

namespace {

std::uint16_t be16(const unsigned char* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

// `size` bytes of the file from `offset`, or none if they aren't all there.
std::optional<std::vector<unsigned char>> read_range(std::ifstream& in, std::uint64_t file_size, std::uint64_t offset,
                                                     std::uint64_t size) {
    if (offset > file_size || size > file_size - offset) {
        return std::nullopt;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!in) {
        return std::nullopt;
    }
    return bytes;
}

struct Table {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    bool present = false;
};

std::string from_utf16_be(const unsigned char* p, std::size_t bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        std::uint32_t unit = be16(p + i);
        if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < bytes) {
            const std::uint32_t low = be16(p + i + 2);
            if (low >= 0xDC00 && low < 0xE000) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, unit);
    }
    return out;
}

// The string of the best record for name ID `wanted`, or empty.
std::string name_string(const std::vector<unsigned char>& name, std::uint16_t wanted) {
    if (name.size() < 6) {
        return {};
    }
    const std::size_t count = be16(name.data() + 2);
    const std::size_t strings = be16(name.data() + 4);
    int best_priority = 99;
    std::string best;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 6 + i * 12;
        if (record + 12 > name.size()) {
            break;
        }
        const unsigned char* r = name.data() + record;
        const std::uint16_t platform = be16(r);
        const std::uint16_t encoding = be16(r + 2);
        const std::uint16_t language = be16(r + 4);
        if (be16(r + 6) != wanted) {
            continue;
        }
        int priority = 99;
        if (platform == 3 && (encoding == 1 || encoding == 10) && language == 0x409) {
            priority = 0;
        } else if (platform == 3) {
            priority = 1;
        } else if (platform == 0) {
            priority = 2;
        } else if (platform == 1 && encoding == 0) {
            priority = 3;
        }
        if (priority >= best_priority) {
            continue;
        }
        const std::size_t length = be16(r + 8);
        const std::size_t offset = strings + be16(r + 10);
        if (offset > name.size() || length > name.size() - offset) {
            continue;
        }
        std::string value;
        if (platform == 1) {
            for (std::size_t k = 0; k < length; ++k) {
                append_utf8(value, name[offset + k]);
            }
        } else {
            value = from_utf16_be(name.data() + offset, length);
        }
        best_priority = priority;
        best = std::move(value);
    }
    return best;
}

} // namespace

std::optional<FontFace> read_font_face(const fs::path& file) {
    std::error_code error;
    const std::uint64_t size = fs::file_size(file, error);
    if (error || size < 12) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const auto header = read_range(in, size, 0, 12);
    if (!header) {
        return std::nullopt;
    }
    std::uint64_t directory = 0; // the offset of the font's table directory
    const std::uint32_t version = be32(header->data());
    if (version == 0x74746366) { // "ttcf": the first of the fonts in the collection
        if (be32(header->data() + 8) == 0) {
            return std::nullopt;
        }
        const auto offset = read_range(in, size, 12, 4);
        if (!offset) {
            return std::nullopt;
        }
        directory = be32(offset->data());
    } else if (version != 0x00010000 && version != 0x4F54544F && version != 0x74727565) { // 1.0, "OTTO", "true"
        return std::nullopt;
    }
    const auto directory_header = read_range(in, size, directory, 12);
    if (!directory_header) {
        return std::nullopt;
    }
    const std::uint32_t table_count = be16(directory_header->data() + 4);
    if (table_count == 0 || table_count > max_tables) {
        return std::nullopt;
    }
    const auto entries = read_range(in, size, directory + 12, std::uint64_t{table_count} * 16);
    if (!entries) {
        return std::nullopt;
    }
    Table name_table;
    Table os2_table;
    Table head_table;
    for (std::uint32_t i = 0; i < table_count; ++i) {
        const unsigned char* e = entries->data() + std::size_t{i} * 16;
        const std::uint32_t tag = be32(e);
        Table table{be32(e + 8), be32(e + 12), true};
        if (table.offset > size || table.length > size - table.offset) {
            table.present = false;
        }
        if (tag == 0x6E616D65) { // "name"
            name_table = table;
        } else if (tag == 0x4F532F32) { // "OS/2"
            os2_table = table;
        } else if (tag == 0x68656164) { // "head"
            head_table = table;
        }
    }
    if (!name_table.present || name_table.length > max_name_table) {
        return std::nullopt;
    }
    const auto name = read_range(in, size, name_table.offset, name_table.length);
    if (!name) {
        return std::nullopt;
    }
    FontFace face;
    face.file = file;
    face.family = name_string(*name, 1);
    face.typographic = name_string(*name, 16);
    if (face.family.empty() && face.typographic.empty()) {
        return std::nullopt;
    }
    std::uint16_t mac_style = 0;
    if (head_table.present && head_table.length >= 46) {
        if (const auto head = read_range(in, size, head_table.offset, 46)) {
            mac_style = be16(head->data() + 44);
        }
    }
    face.weight = (mac_style & 1) != 0 ? 700 : 400;
    face.italic = (mac_style & 2) != 0;
    if (os2_table.present && os2_table.length >= 8) {
        if (const auto os2 = read_range(in, size, os2_table.offset, std::min<std::uint32_t>(os2_table.length, 64))) {
            int weight = be16(os2->data() + 4);
            if (weight > 0) {
                weight = weight < 10 ? weight * 100 : weight;
                face.weight = std::clamp(weight, 100, 900);
            }
            const int width = be16(os2->data() + 6);
            face.width = width >= 1 && width <= 9 ? width : 5;
            if (os2->size() >= 64) {
                const std::uint16_t selection = be16(os2->data() + 62);
                face.italic = (selection & 1) != 0 || (selection & (1 << 9)) != 0 || (mac_style & 2) != 0;
            }
        }
    }
    return face;
}

// --- The catalog ---

FontCatalog::FontCatalog(const fs::path& folder) {
    std::ifstream configuration(folder / "fonts.conf", std::ios::binary);
    if (configuration) {
        std::string text(max_configuration_size, '\0');
        configuration.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(configuration.gcount()));
        aliases_ = parse_font_configuration(text);
    }
    std::vector<fs::path> files;
    std::error_code error;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    for (; !error && it != end && files.size() < max_font_files; it.increment(error)) {
        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    for (const fs::path& file : files) {
        if (std::optional<FontFace> face = read_font_face(file)) {
            faces_.push_back(std::move(*face));
        }
    }
}

std::vector<std::string> FontCatalog::aliased(std::string_view generic) const {
    const auto found = aliases_.find(normalized_family(generic));
    return found == aliases_.end() ? std::vector<std::string>() : found->second;
}

std::vector<fs::path> FontCatalog::candidates(std::span<const std::string> families, int weight, bool italic) const {
    std::vector<const FontFace*> ordered;
    ordered.reserve(faces_.size());
    for (const FontFace& face : faces_) {
        ordered.push_back(&face);
    }
    const auto rank = [&](const FontFace* face) {
        return std::tuple(face->italic != italic, std::abs(face->weight - weight), std::abs(face->width - 5),
                          face->file.generic_string());
    };
    std::ranges::sort(ordered, [&](const FontFace* a, const FontFace* b) { return rank(a) < rank(b); });

    std::vector<fs::path> result;
    const auto add = [&](const FontFace* face) {
        if (std::ranges::find(result, face->file) == result.end()) {
            result.push_back(face->file);
        }
    };
    for (const std::string& family : families) {
        const std::string wanted = normalized_family(family);
        for (const FontFace* face : ordered) {
            if (normalized_family(face->family) == wanted ||
                (!face->typographic.empty() && normalized_family(face->typographic) == wanted)) {
                add(face);
            }
        }
    }
    for (const FontFace* face : ordered) {
        add(face);
    }
    return result;
}

std::vector<fs::path> FontCatalog::fallbacks() const {
    std::vector<fs::path> result;
    const auto add = [&](const fs::path& file) {
        if (std::ranges::find(result, file) == result.end()) {
            result.push_back(file);
        }
    };
    for (const char* generic : {"sans-serif", "monospace"}) {
        const std::vector<std::string> families = aliased(generic);
        const std::vector<fs::path> regular = candidates(families, 400, false);
        if (regular.empty()) {
            continue;
        }
        const auto face = std::ranges::find(faces_, regular.front(), &FontFace::file);
        const auto of_family = [&](const FontFace& f) {
            return std::ranges::any_of(families, [&](const std::string& family) {
                const std::string wanted = normalized_family(family);
                return normalized_family(f.family) == wanted ||
                       (!f.typographic.empty() && normalized_family(f.typographic) == wanted);
            });
        };
        if (face != faces_.end() && of_family(*face)) {
            add(regular.front());
        }
    }
    for (const FontFace& face : faces_) {
        add(face.file);
    }
    return result;
}

const FontCatalog& system_font_catalog() {
    static const FontCatalog catalog("/system/share/fonts");
    return catalog;
}

} // namespace elg::detail::platform::mikros
