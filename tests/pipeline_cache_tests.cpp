// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The pipeline cache (gpu/pipeline_cache.h), without a GPU: its callbacks called directly, on folders that the
// tests make (ELGSPEC-0036.R3, ELGSPEC-0036.R7, ELGSPEC-0036.R9).

#include "gpu/pipeline_cache.h"

#include "core/log.h"
#include "gpu/pipeline_cache_format.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace elg::detail::gpu;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view identity = "elegance 0.1.0; dawn 20260911; vulkan; 10005:0; SwiftShader";

// A folder that is removed, with what is in it, at the end of the test. The caches made on it end first.
class Folder {
public:
    Folder() {
        // Test cases run in processes of their own, at the same time: the name has to differ between them.
        std::random_device random;
        do {
            path_ = fs::temp_directory_path() /
                    ("elegance-pipeline-cache-" + std::to_string(random()) + std::to_string(random()));
        } while (fs::exists(path_));
    }
    ~Folder() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<std::byte> bytes(std::string_view text) {
    const auto view = std::as_bytes(std::span(text.data(), text.size()));
    return {view.begin(), view.end()};
}

std::vector<std::byte> key_of(int index) {
    return bytes("key " + std::to_string(index));
}

// A value of some size that tells its index.
std::vector<std::byte> value_of(int index, std::size_t size = 300) {
    std::vector<std::byte> value(size);
    for (std::size_t i = 0; i < size; ++i) {
        value[i] = static_cast<std::byte>((static_cast<std::size_t>(index) * 31 + i) & 0xFF);
    }
    return value;
}

// What the implementation does: a call for the size, and one for the value.
std::optional<std::vector<std::byte>> lookup(PipelineCache& cache, int index) {
    const std::vector<std::byte> key = key_of(index);
    const std::size_t size = cache.load(identity, key, {});
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> value(size);
    if (cache.load(identity, key, value) != size) {
        return std::nullopt;
    }
    return value;
}

fs::path file_of(const Folder& folder, int index) {
    return folder.path() / pipeline_cache_entry_name(identity, key_of(index));
}

std::size_t warnings_of_damage() {
    const std::vector<std::string> lines = elg::detail::recent_log_lines();
    return static_cast<std::size_t>(std::ranges::count_if(
        lines, [](const std::string& line) { return line.contains("pipeline cache: a damaged entry"); }));
}

} // namespace

TEST_CASE("a second run finds every entry that the first stored", "[pipeline-cache]") {
    const Folder folder;
    constexpr int count = 6;
    {
        PipelineCache cache({.directory = folder.path()});
        for (int i = 0; i < count; ++i) {
            cache.store(identity, key_of(i), value_of(i));
        }
        cache.flush();
        CHECK(cache.stats().stored == count);
    }
    PipelineCache second({.directory = folder.path()});
    for (int i = 0; i < count; ++i) {
        CHECK(lookup(second, i) == value_of(i));
    }
    const PipelineCacheStats stats = second.stats();
    CHECK(stats.asked == count);
    CHECK(stats.found == count);
    CHECK(stats.missing == 0);
    CHECK(stats.stored == 0);
    // An entry that nobody stored is missing.
    CHECK_FALSE(lookup(second, 100).has_value());
}

TEST_CASE("an entry that was offered is found before it is written", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path()});
    cache.store(identity, key_of(1), value_of(1));
    CHECK(lookup(cache, 1) == value_of(1));
    cache.flush();
    CHECK(lookup(cache, 1) == value_of(1));
    CHECK(fs::exists(file_of(folder, 1)));
}

TEST_CASE("a lookup's second call gets the value of its first, and a size that changed is a miss", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path()});
    cache.store(identity, key_of(1), value_of(1, 100));
    cache.flush();
    const std::vector<std::byte> key = key_of(1);
    REQUIRE(cache.load(identity, key, {}) == 100);
    std::vector<std::byte> wrong(99);
    CHECK(cache.load(identity, key, wrong) == 0);
    // The value asked for by size, without a first call, is found too.
    std::vector<std::byte> right(100);
    CHECK(cache.load(identity, key, right) == 100);
    CHECK(right == value_of(1, 100));
}

TEST_CASE("a damaged entry is a miss and is removed, and the first one of a run is logged once", "[pipeline-cache]") {
    const Folder folder;
    {
        PipelineCache cache({.directory = folder.path()});
        for (int i = 0; i < 3; ++i) {
            cache.store(identity, key_of(i), value_of(i));
        }
        cache.flush();
    }
    // One cut off, one with a byte changed, one that isn't a cache file.
    fs::resize_file(file_of(folder, 0), 50);
    {
        std::fstream changed(file_of(folder, 1), std::ios::in | std::ios::out | std::ios::binary);
        changed.seekp(100);
        changed.put('\x7f');
    }
    {
        std::ofstream garbage(file_of(folder, 2), std::ios::binary | std::ios::trunc);
        garbage << "not a pipeline cache file";
    }
    const std::size_t before = warnings_of_damage();
    PipelineCache cache({.directory = folder.path()});
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(lookup(cache, i).has_value());
        CHECK_FALSE(fs::exists(file_of(folder, i)));
    }
    CHECK(cache.stats().damaged == 3);
    CHECK(warnings_of_damage() == before + 1);
    // What was removed can be stored again.
    cache.store(identity, key_of(0), value_of(0));
    cache.flush();
    CHECK(lookup(cache, 0) == value_of(0));
}

TEST_CASE("an entry of another identity or key is a miss, and its file stays", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path()});
    cache.store(identity, key_of(1), value_of(1));
    cache.store(identity, key_of(2), value_of(2));
    cache.flush();
    // The entry of another identity is another file, and isn't found.
    const std::vector<std::byte> key = key_of(1);
    CHECK(cache.load("elegance 0.2.0; dawn 20260911; vulkan; 10005:0; SwiftShader", key, {}) == 0);
    // A whole file under another entry's name is that entry's miss, and is kept.
    fs::copy_file(file_of(folder, 1), file_of(folder, 3));
    CHECK_FALSE(lookup(cache, 3).has_value());
    CHECK(fs::exists(file_of(folder, 3)));
    CHECK(cache.stats().damaged == 0);
}

TEST_CASE("a folder that can't be used doesn't stop the cache, and one that was removed is made again", "[pipeline-cache]") {
    const Folder folder;
    // A file where the folder should be.
    {
        std::ofstream blocker(folder.path(), std::ios::binary);
        blocker << "a file";
    }
    {
        PipelineCache cache({.directory = folder.path()});
        CHECK_FALSE(lookup(cache, 1).has_value());
        CHECK_NOTHROW(cache.store(identity, key_of(1), value_of(1)));
        cache.flush();
        CHECK(cache.stats().write_failures >= 1);
        CHECK(cache.stats().stored == 0);
    }
    fs::remove(folder.path());
    // Made again when it is gone between two entries.
    PipelineCache cache({.directory = folder.path()});
    cache.store(identity, key_of(1), value_of(1));
    cache.flush();
    fs::remove_all(folder.path());
    cache.store(identity, key_of(2), value_of(2));
    cache.flush();
    CHECK(cache.stats().stored == 2);
    CHECK(fs::exists(file_of(folder, 2)));
}

TEST_CASE("the cache does nothing with files until an entry is stored", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path()});
    CHECK_FALSE(fs::exists(folder.path()));
    CHECK_FALSE(lookup(cache, 1).has_value());
    CHECK_FALSE(fs::exists(folder.path()));
    cache.store(identity, key_of(1), value_of(1));
    cache.flush();
    CHECK(fs::exists(folder.path()));
}

TEST_CASE("a cache without a folder finds nothing and keeps nothing", "[pipeline-cache]") {
    PipelineCache cache({});
    cache.store(identity, key_of(1), value_of(1));
    cache.flush();
    CHECK_FALSE(lookup(cache, 1).has_value());
    CHECK(cache.stats().stored == 0);
}
