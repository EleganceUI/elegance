// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Where the pipeline cache is, and the settings that move it, turn it off or change its limit
// (ELGSPEC-0036.R4, ELGSPEC-0036.R6): from environments that the tests give it.

#include "gpu/pipeline_cache.h"

#include "gpu/pipeline_cache_backend.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace elg::detail::gpu;
namespace fs = std::filesystem;

namespace {

using Environment = std::map<std::string, std::string>;

std::function<std::optional<std::string>(const char*)> reading(const Environment& environment) {
    return [&environment](const char* name) -> std::optional<std::string> {
        const auto found = environment.find(name);
        return found == environment.end() ? std::nullopt : std::optional(found->second);
    };
}

// What `value` holds. The test fails when it holds nothing.
template <typename T>
T required(std::optional<T> value) {
    if (!value) {
        throw std::runtime_error("a value that the test needs is missing");
    }
    return std::move(*value);
}

} // namespace

TEST_CASE("the app's folder is named by its program and a hash of its path", "[pipeline-cache]") {
    const std::string name = pipeline_cache_app_name("/apps/one/player");
    CHECK(name.starts_with("player-"));
    CHECK(name.size() == std::string("player-").size() + 8);
    CHECK(name == pipeline_cache_app_name("/apps/one/player"));
    // Two apps with one name, or one app in two places, have two folders.
    CHECK(name != pipeline_cache_app_name("/apps/two/player"));
    CHECK(pipeline_cache_app_name("/apps/one/player.exe").starts_with("player-"));
}

TEST_CASE("the limit is a number of bytes, with K, M or G for units", "[pipeline-cache]") {
    CHECK(parse_pipeline_cache_limit("4096") == 4096);
    CHECK(parse_pipeline_cache_limit("8K") == 8 * 1024);
    CHECK(parse_pipeline_cache_limit("8k") == 8 * 1024);
    CHECK(parse_pipeline_cache_limit("64M") == std::uint64_t{64} << 20);
    CHECK(parse_pipeline_cache_limit("2G") == std::uint64_t{2} << 30);
    CHECK(parse_pipeline_cache_limit("0") == 0);
    // The largest size in bytes that fits is a limit; one more unit is not, and doesn't wrap around to a small one.
    CHECK(parse_pipeline_cache_limit("17179869183G") == (std::uint64_t{17179869183} << 30));
    for (const char* bad : {"17179869184G", "17179869185G", "999999999999999G", "999999999999999M", "",
                            "K", "M", "-5", "5X", "5 M", "1.5M", "M5", "5MM", "99999999999999999999"}) {
        CHECK_FALSE(parse_pipeline_cache_limit(bad).has_value());
    }
}

TEST_CASE("a setting moves the cache, turns it off, or changes its limit", "[pipeline-cache]") {
    const fs::path program = "/apps/one/player";
    const Environment base{{"HOME", "/home/me"}, {"XDG_CACHE_HOME", "/xdg"}, {"LOCALAPPDATA", "C:/Local"}};

    const PipelineCacheSettings normal = pipeline_cache_settings(program, reading(base));
    CHECK(normal.limit == std::uint64_t{32} << 20);

    Environment moved = base;
    moved["ELG_PIPELINE_CACHE"] = "/somewhere/else";
    const PipelineCacheSettings elsewhere = pipeline_cache_settings(program, reading(moved));
    CHECK(required(elsewhere.directory).generic_string() == "/somewhere/else");

    for (const char* off : {"off", ""}) {
        Environment disabled = base;
        disabled["ELG_PIPELINE_CACHE"] = off;
        const PipelineCacheSettings none = pipeline_cache_settings(program, reading(disabled));
        CHECK_FALSE(none.directory.has_value());
        CHECK_FALSE(none.shipped.has_value());
    }

    Environment limited = base;
    limited["ELG_PIPELINE_CACHE_LIMIT"] = "64M";
    CHECK(pipeline_cache_settings(program, reading(limited)).limit == std::uint64_t{64} << 20);
    // A value that isn't a size is not used.
    limited["ELG_PIPELINE_CACHE_LIMIT"] = "a lot";
    CHECK(pipeline_cache_settings(program, reading(limited)).limit == std::uint64_t{32} << 20);
    limited["ELG_PIPELINE_CACHE_LIMIT"] = "0";
    CHECK(pipeline_cache_settings(program, reading(limited)).limit == std::uint64_t{32} << 20);
}

TEST_CASE("off turns the shipped folder off too, and a moved cache keeps it", "[pipeline-cache]") {
    // Only some systems have a shipped folder, so the folders are given.
    const PipelineCacheFolders folders{fs::path("/cache/own"), fs::path("/package/pipelines")};
    const auto settings_with = [&](const Environment& environment) {
        return pipeline_cache_settings(folders, reading(environment));
    };
    const PipelineCacheSettings normal = settings_with({});
    CHECK(required(normal.directory).generic_string() == "/cache/own");
    CHECK(required(normal.shipped).generic_string() == "/package/pipelines");
    for (const char* off : {"off", ""}) {
        const PipelineCacheSettings none = settings_with({{"ELG_PIPELINE_CACHE", off}});
        CHECK_FALSE(none.directory.has_value());
        CHECK_FALSE(none.shipped.has_value());
    }
    const PipelineCacheSettings moved = settings_with({{"ELG_PIPELINE_CACHE", "/elsewhere"}});
    CHECK(required(moved.directory).generic_string() == "/elsewhere");
    CHECK(required(moved.shipped).generic_string() == "/package/pipelines");
}

TEST_CASE("the system's folder for the app's pipelines is in its cache folder", "[pipeline-cache]") {
    const Environment every{{"HOME", "/home/me"}, {"XDG_CACHE_HOME", "/xdg"}, {"LOCALAPPDATA", "C:/Local"}};
    const PipelineCacheFolders folders = pipeline_cache_folders("app", reading(every));
    const fs::path own = required(folders.own);
    CHECK_FALSE(folders.shipped.has_value());
#if defined(_WIN32)
    CHECK(own.generic_string() == "C:/Local/Elegance/app/Pipelines");
    CHECK_FALSE(pipeline_cache_folders("app", reading({})).own.has_value());
#elif defined(__APPLE__)
    CHECK(own.generic_string() == "/home/me/Library/Caches/Elegance/app/Pipelines");
    CHECK_FALSE(pipeline_cache_folders("app", reading({})).own.has_value());
#else
    CHECK(own.generic_string() == "/xdg/elegance/app/pipelines");
    // An empty or relative XDG_CACHE_HOME counts as unset.
    for (const char* xdg : {"", "relative/cache"}) {
        const Environment unset{{"HOME", "/home/me"}, {"XDG_CACHE_HOME", xdg}};
        CHECK(required(pipeline_cache_folders("app", reading(unset)).own).generic_string() ==
              "/home/me/.cache/elegance/app/pipelines");
    }
    CHECK_FALSE(pipeline_cache_folders("app", reading({})).own.has_value());
#endif
}

TEST_CASE("the folders are named from the environment's UTF-8 text, as the paths it names", "[pipeline-cache]") {
    // The environment's text is UTF-8: where the system reads paths in a code page, it must not be taken as one.
    const Environment accented{{"HOME", "/home/\xC3\xA9"},
                               {"XDG_CACHE_HOME", "/xdg/\xC3\xA9"},
                               {"LOCALAPPDATA", "C:/Local/\xC3\xA9"}};
    const fs::path own = required(pipeline_cache_folders("app-\xC3\xA9", reading(accented)).own);
#if defined(_WIN32)
    CHECK(own.generic_u8string() == u8"C:/Local/\u00e9/Elegance/app-\u00e9/Pipelines");
#elif defined(__APPLE__)
    CHECK(own.generic_u8string() == u8"/home/\u00e9/Library/Caches/Elegance/app-\u00e9/Pipelines");
#else
    CHECK(own.generic_u8string() == u8"/xdg/\u00e9/elegance/app-\u00e9/pipelines");
#endif
    // And the app's name is the program's name as that text.
    CHECK(pipeline_cache_app_name(fs::path(u8"/apps/\u00e9/player-\u00e9")).starts_with("player-\xC3\xA9-"));
}
