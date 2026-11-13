// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Font files on Linux (text/font_library.h): the system's choice through fontconfig, loaded when
// it's there (every desktop has it), then DejaVu, Noto or Liberation where distributions usually
// put them.

#include "text/font_library.h"

#include "platform/generic/dynamic_library.h"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

// The few fontconfig calls used, loaded from the system's library: building against it would
// tie Elegance to one fontconfig, and a copy of our own wouldn't know the system's configuration.
class Fontconfig {
public:
    // nullptr when the library isn't there.
    static Fontconfig* get() {
        static Fontconfig* const instance = [] {
            auto loaded = std::make_unique<Fontconfig>();
            return loaded->load() ? loaded.release() : nullptr; // kept for the process
        }();
        return instance;
    }

    // The file of the system's sans-serif face closest to `weight` (100-900) and `italic`.
    std::optional<std::filesystem::path> match(int weight, bool italic) {
        const std::lock_guard lock(mutex_);
        const auto key = std::pair(weight, italic);
        if (const auto known = matches_.find(key); known != matches_.end()) {
            return known->second;
        }
        std::optional<std::filesystem::path> file;
        if (Pattern* pattern = name_parse_(reinterpret_cast<const unsigned char*>("sans-serif"))) {
            pattern_add_integer_(pattern, "weight", fontconfig_weight(weight));
            pattern_add_integer_(pattern, "slant", italic ? kSlantItalic : kSlantRoman);
            pattern_add_bool_(pattern, "scalable", 1);
            config_substitute_(nullptr, pattern, kMatchPattern);
            default_substitute_(pattern);
            int result = 0;
            if (Pattern* found = font_match_(nullptr, pattern, &result)) {
                unsigned char* path = nullptr;
                if (pattern_get_string_(found, "file", 0, &path) == kResultMatch && path) {
                    file = std::filesystem::path(reinterpret_cast<const char*>(path));
                }
                pattern_destroy_(found);
            }
            pattern_destroy_(pattern);
        }
        matches_.emplace(key, file);
        return file;
    }

private:
    struct Pattern;
    struct Config;
    static constexpr int kMatchPattern = 0; // FcMatchPattern
    static constexpr int kResultMatch = 0;  // FcResultMatch
    static constexpr int kSlantRoman = 0;
    static constexpr int kSlantItalic = 100;

    // CSS weights to fontconfig's scale (FC_WEIGHT_THIN ... FC_WEIGHT_BLACK).
    static int fontconfig_weight(int weight) {
        static constexpr std::pair<int, int> kWeights[] = {{100, 0},   {200, 40},  {300, 50},  {400, 80}, {500, 100},
                                                           {600, 180}, {700, 200}, {800, 205}, {900, 210}};
        for (const auto& [css, fc] : kWeights) {
            if (weight <= css) {
                return fc;
            }
        }
        return 210;
    }

    template <class F>
    bool find(F& function, const char* name) {
        function = reinterpret_cast<F>(library_->symbol(name));
        return function != nullptr;
    }

    bool load() {
        try {
            library_ = std::make_unique<platform::DynamicLibrary>("libfontconfig.so.1");
        } catch (...) {
            return false;
        }
        return find(name_parse_, "FcNameParse") && find(pattern_add_integer_, "FcPatternAddInteger") &&
               find(pattern_add_bool_, "FcPatternAddBool") && find(config_substitute_, "FcConfigSubstitute") &&
               find(default_substitute_, "FcDefaultSubstitute") && find(font_match_, "FcFontMatch") &&
               find(pattern_get_string_, "FcPatternGetString") && find(pattern_destroy_, "FcPatternDestroy");
    }

    std::unique_ptr<platform::DynamicLibrary> library_;
    Pattern* (*name_parse_)(const unsigned char*) = nullptr;
    int (*pattern_add_integer_)(Pattern*, const char*, int) = nullptr;
    int (*pattern_add_bool_)(Pattern*, const char*, int) = nullptr;
    int (*config_substitute_)(Config*, Pattern*, int) = nullptr;
    void (*default_substitute_)(Pattern*) = nullptr;
    Pattern* (*font_match_)(Config*, Pattern*, int*) = nullptr;
    int (*pattern_get_string_)(const Pattern*, const char*, int, unsigned char**) = nullptr;
    void (*pattern_destroy_)(Pattern*) = nullptr;

    std::mutex mutex_;
    std::map<std::pair<int, bool>, std::optional<std::filesystem::path>> matches_;
};

} // namespace

std::vector<std::filesystem::path> system_font_candidates(Weight weight, bool italic) {
    const int w = static_cast<int>(weight);
    std::vector<std::filesystem::path> files;
    if (Fontconfig* fontconfig = Fontconfig::get()) {
        if (std::optional<std::filesystem::path> file = fontconfig->match(w, italic)) {
            files.push_back(std::move(*file));
        }
    }
    if (w >= 600) {
        files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf");
        files.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf");
    }
    files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    files.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    return files;
}

std::vector<std::filesystem::path> fallback_font_candidates() {
    std::vector<std::filesystem::path> files;
    for (const char* file : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                             "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
                             "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"}) {
        files.push_back(file);
    }
    return files;
}

} // namespace elg::detail
