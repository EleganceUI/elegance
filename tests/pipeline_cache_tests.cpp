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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

namespace {

constexpr std::uint64_t four_kib = 4096;
constexpr std::uint64_t eight_kib = 8192;
constexpr std::size_t large_value = 8192;

std::uint64_t size_of_entries(const Folder& folder) {
    std::uint64_t total = 0;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder.path(), error)) {
        if (entry.path().extension() == pipeline_cache_extension) {
            total += entry.file_size();
        }
    }
    return total;
}

// The entries' files, newest last, from times that the test sets: the clock of the disk can't tell entries that
// were written within the same moment.
void set_age(const Folder& folder, int index, int seconds_ago) {
    std::error_code error;
    fs::last_write_time(file_of(folder, index), fs::file_time_type::clock::now() - std::chrono::seconds(seconds_ago),
                        error);
}

} // namespace

TEST_CASE("the entries stay within the limit, and the ones used least recently go first", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path(), .limit = eight_kib});
    constexpr int count = 16;
    for (int i = 0; i < count; ++i) {
        cache.store(identity, key_of(i), value_of(i, 1024));
        cache.flush();
        set_age(folder, i, (count - i) * 10); // each one newer than the one before
        CHECK(size_of_entries(folder) <= eight_kib);
    }
    CHECK(cache.stats().evicted > 0);
    CHECK_FALSE(fs::exists(file_of(folder, 0)));
    CHECK(fs::exists(file_of(folder, count - 1)));
    CHECK(fs::exists(file_of(folder, count - 2)));
    // Those that are left are the newest ones, without a gap.
    int oldest_left = count;
    for (int i = 0; i < count; ++i) {
        if (fs::exists(file_of(folder, i))) {
            oldest_left = std::min(oldest_left, i);
        }
    }
    for (int i = oldest_left; i < count; ++i) {
        CHECK(fs::exists(file_of(folder, i)));
    }
}

TEST_CASE("an entry that is used survives the entries that were used before it", "[pipeline-cache]") {
    const Folder folder;
    constexpr int count = 6;
    {
        PipelineCache cache({.directory = folder.path(), .limit = eight_kib});
        for (int i = 0; i < count; ++i) {
            cache.store(identity, key_of(i), value_of(i, 1024));
            cache.flush();
        }
    }
    for (int i = 0; i < count; ++i) {
        set_age(folder, i, 1000 - i); // entry 0 is the oldest
    }
    PipelineCache cache({.directory = folder.path(), .limit = eight_kib});
    CHECK(lookup(cache, 0) == value_of(0, 1024)); // used: it is as new as now
    cache.flush();
    for (int i = count; i < count + 4; ++i) {
        cache.store(identity, key_of(i), value_of(i, 1024));
        cache.flush();
    }
    CHECK(fs::exists(file_of(folder, 0)));
    CHECK_FALSE(fs::exists(file_of(folder, 1)));
    CHECK(size_of_entries(folder) <= eight_kib);
}

TEST_CASE("an entry larger than the limit is not kept, and doesn't take the others with it", "[pipeline-cache]") {
    const Folder folder;
    PipelineCache cache({.directory = folder.path(), .limit = four_kib});
    cache.store(identity, key_of(1), value_of(1, 1024));
    cache.flush();
    cache.store(identity, key_of(2), value_of(2, large_value));
    cache.flush();
    CHECK(cache.stats().dropped == 1);
    CHECK(fs::exists(file_of(folder, 1)));
    CHECK_FALSE(fs::exists(file_of(folder, 2)));
}

TEST_CASE("temporary files are removed when they are old, and not when a run may still be writing them",
          "[pipeline-cache]") {
    const Folder folder;
    fs::create_directories(folder.path());
    const fs::path old_file = folder.path() / "entry.1.1.tmp";
    const fs::path new_file = folder.path() / "entry.2.1.tmp";
    for (const fs::path& file : {old_file, new_file}) {
        std::ofstream(file) << "half an entry";
    }
    fs::last_write_time(old_file, fs::file_time_type::clock::now() - std::chrono::hours(1));
    PipelineCache cache({.directory = folder.path()});
    cache.store(identity, key_of(1), value_of(1));
    cache.flush();
    CHECK_FALSE(fs::exists(old_file));
    CHECK(fs::exists(new_file));
}

TEST_CASE("two caches on one folder, from several threads, read only whole entries and lose none", "[pipeline-cache]") {
    const Folder folder;
    constexpr int keys = 40;
    constexpr int threads = 4;
    std::atomic<int> wrong{0};
    {
        PipelineCache first({.directory = folder.path()});
        PipelineCache second({.directory = folder.path()});
        std::vector<std::thread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([&, t] {
                PipelineCache& cache = t % 2 == 0 ? first : second;
                for (int round = 0; round < 3; ++round) {
                    for (int i = 0; i < keys; ++i) {
                        const int index = (i + t * 7) % keys; // the threads overlap on every key
                        cache.store(identity, key_of(index), value_of(index));
                        if (const auto value = lookup(cache, (index + 3) % keys);
                            value && *value != value_of((index + 3) % keys)) {
                            wrong.fetch_add(1);
                        }
                    }
                }
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        first.flush();
        second.flush();
        CHECK(first.stats().damaged == 0);
        CHECK(second.stats().damaged == 0);
    }
    CHECK(wrong == 0);
    PipelineCache later({.directory = folder.path()});
    for (int i = 0; i < keys; ++i) {
        CHECK(lookup(later, i) == value_of(i));
    }
}

namespace {

// Every file of a folder with its contents and the time it was written.
std::map<std::string, std::pair<std::vector<char>, fs::file_time_type>> snapshot(const Folder& folder) {
    std::map<std::string, std::pair<std::vector<char>, fs::file_time_type>> files;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder.path(), error)) {
        std::ifstream in(entry.path(), std::ios::binary);
        files[entry.path().filename().string()] = {std::vector<char>(std::istreambuf_iterator<char>(in), {}),
                                                   fs::last_write_time(entry.path())};
    }
    return files;
}

// A cache to ship: made as a run makes its own, and not used after.
void make_shipped(const Folder& folder, int count, int salt = 0) {
    PipelineCache cache({.directory = folder.path()});
    for (int i = 0; i < count; ++i) {
        cache.store(identity, key_of(i), value_of(i + salt));
    }
    cache.flush();
}

} // namespace

TEST_CASE("an entry in the shipped folder is found there, and not stored again", "[pipeline-cache]") {
    const Folder shipped;
    const Folder own;
    make_shipped(shipped, 4);
    const auto before = snapshot(shipped);
    {
        PipelineCache cache({.directory = own.path(), .shipped = shipped.path()});
        for (int i = 0; i < 4; ++i) {
            CHECK(lookup(cache, i) == value_of(i));
        }
        cache.flush();
        const PipelineCacheStats stats = cache.stats();
        CHECK(stats.found == 4);
        CHECK(stats.found_shipped == 4);
        CHECK(stats.stored == 0);
    }
    CHECK_FALSE(fs::exists(own.path()));
    CHECK(snapshot(shipped) == before);
}

TEST_CASE("the shipped folder is looked in first, and the app's own after it", "[pipeline-cache]") {
    const Folder shipped;
    const Folder own;
    make_shipped(shipped, 2);        // entries 0 and 1, as shipped
    make_shipped(own, 4, 1000);      // entries 0 to 3, of another run, with other values
    PipelineCache cache({.directory = own.path(), .shipped = shipped.path()});
    CHECK(lookup(cache, 0) == value_of(0));
    CHECK(lookup(cache, 1) == value_of(1));
    CHECK(lookup(cache, 2) == value_of(2 + 1000));
    CHECK(lookup(cache, 3) == value_of(3 + 1000));
    CHECK(cache.stats().found_shipped == 2);
}

TEST_CASE("a damaged entry in the shipped folder is a miss, and stays where it is", "[pipeline-cache]") {
    const Folder shipped;
    const Folder own;
    make_shipped(shipped, 2);
    fs::resize_file(file_of(shipped, 0), 30);
    {
        std::fstream changed(file_of(shipped, 1), std::ios::in | std::ios::out | std::ios::binary);
        changed.seekp(100);
        changed.put('\x7f');
    }
    const auto before = snapshot(shipped);
    const std::size_t warnings = warnings_of_damage();
    PipelineCache cache({.directory = own.path(), .shipped = shipped.path()});
    CHECK_FALSE(lookup(cache, 0).has_value());
    CHECK_FALSE(lookup(cache, 1).has_value());
    CHECK(cache.stats().damaged == 2);
    CHECK(warnings_of_damage() == warnings + 1);
    CHECK(snapshot(shipped) == before);
    // What the own folder has of the same entry is found.
    cache.store(identity, key_of(0), value_of(0));
    cache.flush();
    CHECK(lookup(cache, 0) == value_of(0));
    CHECK(cache.stats().found_shipped == 0);
}

TEST_CASE("what is stored goes to the app's own folder only, and a shipped folder that isn't there stays so", "[pipeline-cache]") {
    const Folder shipped;
    const Folder own;
    PipelineCache cache({.directory = own.path(), .shipped = shipped.path()});
    cache.store(identity, key_of(1), value_of(1));
    cache.flush();
    CHECK(lookup(cache, 1) == value_of(1));
    CHECK(fs::exists(file_of(own, 1)));
    CHECK_FALSE(fs::exists(shipped.path()));
}

TEST_CASE("a cache with only a shipped folder reads it, and keeps nothing", "[pipeline-cache]") {
    const Folder shipped;
    make_shipped(shipped, 1);
    const auto before = snapshot(shipped);
    PipelineCache cache({.shipped = shipped.path()});
    CHECK(lookup(cache, 0) == value_of(0));
    cache.store(identity, key_of(5), value_of(5));
    cache.flush();
    CHECK(cache.stats().stored == 0);
    CHECK(snapshot(shipped) == before);
}
