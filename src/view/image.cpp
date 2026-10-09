// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Image and the icon-set registry (design.md §13.2).

#include <elg/image.h>

#include "core/crash.h"
#include "text/font_library.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/views.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>

#if defined(ELG_HAS_PHOSPHOR)
namespace elg::detail::embedded {
extern const unsigned char phosphor_regular[];
extern const std::size_t phosphor_regular_size;
extern const unsigned char phosphor_bold[];
extern const std::size_t phosphor_bold_size;
extern const unsigned char phosphor_fill[];
extern const std::size_t phosphor_fill_size;
extern const unsigned char phosphor_css[];
extern const std::size_t phosphor_css_size;
} // namespace elg::detail::embedded
#endif

namespace elg {

namespace {

struct IconSet {
    std::string family;                                 // the regular (and weights) font family
    std::map<std::string, std::string, std::less<>> variants; // variant -> font family
    std::map<std::string, char32_t, std::less<>> glyphs;      // icon name -> codepoint
};

std::mutex g_mutex;
std::map<std::string, IconSet, std::less<>> g_sets;
std::string g_default = "phosphor";

// --- Name maps ---

std::optional<char32_t> parse_codepoint(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && (text[start] == '\\' || text[start] == 'u' || text[start] == 'U')) {
        ++start;
    }
    if (text.substr(start, 2) == "0x" || text.substr(start, 2) == "0X") {
        start += 2;
    }
    char32_t value = 0;
    std::size_t digits = 0;
    for (std::size_t i = start; i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i])); ++i, ++digits) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        value = value * 16 + static_cast<char32_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    if (digits == 0 || value > 0x10FFFF) {
        return std::nullopt;
    }
    return value;
}

// `.prefix-name:before { content: "\e900"; }`: the class right before ":before" names the icon.
std::map<std::string, char32_t, std::less<>> parse_css(std::string_view css) {
    std::map<std::string, char32_t, std::less<>> glyphs;
    std::size_t at = 0;
    while ((at = css.find(":before", at)) != std::string_view::npos) {
        const std::size_t dot = css.rfind('.', at);
        const std::size_t content = css.find("content", at);
        const std::size_t close = css.find('}', at);
        if (dot == std::string_view::npos || content == std::string_view::npos || content > close) {
            at += 7;
            continue;
        }
        const std::string_view name = css.substr(dot + 1, at - dot - 1);
        const std::size_t quote = css.find_first_of("\"'", content);
        if (quote != std::string_view::npos && quote < close) {
            if (const auto code = parse_codepoint(css.substr(quote + 1, 12))) {
                glyphs.emplace(std::string(name), *code);
            }
        }
        at = close;
    }
    // Drop the class prefix every name shares ("ph-", "fa-", ...).
    if (glyphs.size() > 1) {
        std::string prefix = glyphs.begin()->first;
        for (const auto& [name, code] : glyphs) {
            std::size_t n = 0;
            while (n < prefix.size() && n < name.size() && prefix[n] == name[n]) {
                ++n;
            }
            prefix.resize(n);
        }
        if (const std::size_t dash = prefix.rfind('-'); dash != std::string::npos) {
            prefix.resize(dash + 1);
            std::map<std::string, char32_t, std::less<>> stripped;
            for (const auto& [name, code] : glyphs) {
                stripped.emplace(name.substr(prefix.size()), code);
            }
            return stripped;
        }
    }
    return glyphs;
}

// A flat JSON object: {"name": "e900"} or {"name": 59648}.
std::map<std::string, char32_t, std::less<>> parse_json(std::string_view json) {
    std::map<std::string, char32_t, std::less<>> glyphs;
    std::size_t at = 0;
    while ((at = json.find('"', at)) != std::string_view::npos) {
        const std::size_t end = json.find('"', at + 1);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string name(json.substr(at + 1, end - at - 1));
        std::size_t value = json.find_first_not_of(" \t\r\n:", end + 1);
        if (value == std::string_view::npos || json[end + 1 + json.substr(end + 1).find_first_not_of(" \t\r\n")] != ':') {
            at = end + 1;
            continue;
        }
        std::optional<char32_t> code;
        std::size_t next = value;
        if (json[value] == '"') {
            next = json.find('"', value + 1);
            code = parse_codepoint(json.substr(value + 1, next - value - 1));
            ++next;
        } else {
            char32_t number = 0;
            while (next < json.size() && std::isdigit(static_cast<unsigned char>(json[next]))) {
                number = number * 10 + static_cast<char32_t>(json[next] - '0');
                ++next;
            }
            if (next > value) {
                code = number;
            }
        }
        if (code) {
            glyphs.emplace(name, *code);
        }
        at = next;
    }
    return glyphs;
}

// "name e900" per line.
std::map<std::string, char32_t, std::less<>> parse_codepoints(std::string_view text) {
    std::map<std::string, char32_t, std::less<>> glyphs;
    std::istringstream lines{std::string(text)};
    std::string name;
    std::string code;
    while (lines >> name >> code) {
        if (const auto value = parse_codepoint(code)) {
            glyphs.emplace(name, *value);
        }
    }
    return glyphs;
}

std::map<std::string, char32_t, std::less<>> parse_names(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (first != std::string_view::npos && text[first] == '{' && text.find(":before") == std::string_view::npos) {
        return parse_json(text);
    }
    if (text.find(":before") != std::string_view::npos) {
        return parse_css(text);
    }
    return parse_codepoints(text);
}

std::string read_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw ResourceError(std::format("register_icon_set(): can't read \"{}\"", file.string()));
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void register_icon_font(const std::string& family, const IconFont& font, Weight weight) {
    detail::mark_icon_font_family(family);
    if (!font.data().empty()) {
        register_font(family, font.data(), weight);
    } else {
        register_font(family, font.file(), weight);
    }
}

// --- Built-in sets ---

void register_builtin_sets() {
#if defined(ELG_HAS_PHOSPHOR)
    using namespace detail::embedded;
    // Phosphor (MIT), third_party/phosphor. Registered like any app's set would be.
    register_icon_set("phosphor",
                      {.font = std::span(phosphor_regular, phosphor_regular_size),
                       .weights = {{Weight::bold, std::span(phosphor_bold, phosphor_bold_size)}},
                       .variants = {{"fill", std::span(phosphor_fill, phosphor_fill_size)}},
                       .names_data = std::string_view(reinterpret_cast<const char*>(phosphor_css), phosphor_css_size)});
#endif
}

void ensure_builtin_sets() {
    static std::once_flag once;
    std::call_once(once, register_builtin_sets);
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// A resolved icon: the font family to draw it with and its character.
struct ResolvedIcon {
    std::string family;
    std::string glyph; // UTF-8
};

std::string to_utf8(char32_t code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

// "<set>.<icon>[.<variant>]" or "<icon>[.<variant>]" in the default set.
ResolvedIcon resolve(std::string_view system_name) {
    ensure_builtin_sets();
    std::lock_guard lock(g_mutex);
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const std::size_t dot = system_name.find('.', start);
        parts.push_back(system_name.substr(start, dot - start));
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }

    std::string set_name = g_default;
    std::size_t first = 0;
    if (parts.size() > 1 && g_sets.contains(parts.front())) {
        set_name = std::string(parts.front());
        first = 1;
    }
    const auto set = g_sets.find(set_name);
    if (set == g_sets.end()) {
        throw UsageError(std::format("Image(\"{}\"): no icon set \"{}\" is registered", system_name, set_name));
    }
    std::string family = set->second.family;
    std::size_t last = parts.size();
    if (last - first >= 2) {
        if (const auto variant = set->second.variants.find(parts.back()); variant != set->second.variants.end()) {
            family = variant->second;
            --last;
        }
    }
    std::string icon;
    for (std::size_t i = first; i < last; ++i) {
        icon += (i > first ? "." : "");
        icon += parts[i];
    }

    const auto glyph = set->second.glyphs.find(icon);
    if (glyph == set->second.glyphs.end()) {
        // Suggest the nearest names.
        std::vector<std::pair<std::size_t, const std::string*>> nearest;
        for (const auto& [name, code] : set->second.glyphs) {
            nearest.emplace_back(edit_distance(icon, name), &name);
        }
        std::ranges::partial_sort(nearest, nearest.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(3, nearest.size())));
        std::string hint;
        for (std::size_t i = 0; i < std::min<std::size_t>(3, nearest.size()); ++i) {
            hint += std::format("{}\"{}.{}\"", i ? ", " : "", set_name, *nearest[i].second);
        }
        throw UsageError(std::format("Image(\"{}\"): the icon set \"{}\" has no icon \"{}\"{}", system_name, set_name,
                                     icon, hint.empty() ? "" : "; did you mean " + hint + "?"));
    }
    return {family, to_utf8(glyph->second)};
}

} // namespace

void register_icon_set(std::string name, IconSetOptions options) {
    if (name.empty() || name.find('.') != std::string::npos) {
        throw UsageError(std::format("register_icon_set(): \"{}\" isn't a valid set name (non-empty, no dots)", name));
    }
    if (options.font.empty()) {
        throw UsageError(std::format("register_icon_set(\"{}\"): no font", name));
    }
    const std::string names_text = options.names_data.empty() && !options.names.empty() ? read_text(options.names)
                                                                                          : std::string(options.names_data);
    if (names_text.empty()) {
        throw UsageError(std::format("register_icon_set(\"{}\"): no name map (.names or .names_data)", name));
    }
    IconSet set;
    set.glyphs = parse_names(names_text);
    if (set.glyphs.empty()) {
        throw ResourceError(std::format("register_icon_set(\"{}\"): no icons found in the name map", name));
    }
    {
        std::lock_guard lock(g_mutex);
        if (g_sets.contains(name) && !options.replace) {
            throw UsageError(std::format("register_icon_set(): \"{}\" is already registered", name));
        }
    }

    // Each registration gets fresh font families, so replacing a set never mixes fonts.
    static int generation = 0;
    const int id = ++generation;
    set.family = std::format("elg.icons.{}.{}", name, id);
    register_icon_font(set.family, options.font, Weight::regular);
    for (const auto& [weight, font] : options.weights) {
        register_icon_font(set.family, font, weight);
    }
    for (const auto& [variant, font] : options.variants) {
        const std::string family = std::format("elg.icons.{}.{}.{}", name, id, variant);
        register_icon_font(family, font, Weight::regular);
        set.variants.emplace(variant, family);
    }

    std::lock_guard lock(g_mutex);
    g_sets.insert_or_assign(std::move(name), std::move(set));
}

void set_default_icon_set(std::string name) {
    std::lock_guard lock(g_mutex);
    g_default = std::move(name);
}

} // namespace elg

namespace elg::detail {

namespace {

// An icon: one glyph of the set's font, sized and colored by the environment like text.
class IconNode final : public ViewNode {
public:
    IconNode(std::string family, std::string glyph) : family_(std::move(family)), glyph_(std::move(glyph)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        ELG_ASSERT(context.text, "lowering an Image without a text system");
        const EnvironmentValues environment = context.environment;
        auto element = std::make_unique<TextElement>(*context.text, 1, TextAlignment::center);
        TextElement* const target = element.get();
        detail::bind(Prop<Font>([environment, family = family_] {
                         const Font font = environment.get<FontKey>();
                         return Font::custom(family, font.size(), font.weight());
                     }),
                     context, [target](const Font& font) { target->set_font(font); });
        target->set_content(glyph_);
        detail::bind(Prop<Color>([environment] { return environment.get<ForegroundKey>(); }), context,
                     [target](Color color, const Animation* animation) { target->set_color(color, animation); });
        return element;
    }

private:
    std::string family_;
    std::string glyph_;
};

} // namespace

} // namespace elg::detail

namespace elg {

View Image(ImageSource source) {
    ResolvedIcon icon = resolve(source.system_name);
    return detail::ViewAccess::make(std::make_shared<const detail::IconNode>(std::move(icon.family), std::move(icon.glyph)));
}

View Image(std::string_view system_name) {
    return Image(ImageSource{.system_name = std::string(system_name)});
}

View Label(std::string title, ImageSource icon) {
    return HStack({.spacing = 6}, Image(std::move(icon)), Text(std::move(title)));
}

} // namespace elg
