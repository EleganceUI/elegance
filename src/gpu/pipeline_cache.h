// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PIPELINE_CACHE_H
#define ELG_GPU_PIPELINE_CACHE_H

// The pipeline cache: what the WebGPU implementation lets Elegance keep of its compiled shaders and
// pipelines, on disk between runs, and gives back when it asks (ELGSPEC-0036.R3). The implementation asks
// through load() and offers through store(), on the thread that creates the shader or the pipeline: the
// render thread. Reading is done there; every write, and so every wait for the disk, is on a thread of the
// cache's own, and the constructor does nothing with files (ELGSPEC-0036.R9). Each entry is a file
// (gpu/pipeline_cache_format.h). A file that can't be read whole or doesn't match its checksum is removed and
// counts as missing, and no failure to read or write ends the run or stops a frame (ELGSPEC-0036.R7): the
// first one of a run is logged.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gpu/pipeline_cache_backend.h"

namespace elg::detail::gpu {

struct PipelineCacheSettings {
    // The folder of this app's entries, or nullopt: no cache.
    std::optional<std::filesystem::path> directory;
    // A folder of entries that ship with the app, in the same format, or nullopt: none. Entries are looked
    // for there first, and then in `directory`. It is only read: nothing is written to it, removed from it or
    // made in it, and an entry in it that is damaged is a miss that stays where it is.
    std::optional<std::filesystem::path> shipped;
    // The most that the entries in the folder take on disk, as the sizes of their files: 32 MiB unless set.
    // When an entry would take it over, the entries used least recently are removed first, judged by the
    // time their files were written or last used, and one that is larger than the limit is not kept.
    std::uint64_t limit = std::uint64_t{32} << 20;
};

// What a cache has done, for the tests.
struct PipelineCacheStats {
    std::uint64_t asked = 0;          // lookups that asked for the size of an entry
    std::uint64_t found = 0;          // of them, those that found it
    std::uint64_t found_shipped = 0;  // and, of those, the ones that were found in the shipped folder
    std::uint64_t missing = 0;        // and those that didn't, damaged ones included
    std::uint64_t damaged = 0;        // files that were removed, or left where they were, as damaged
    std::uint64_t stored = 0;         // entries written
    std::uint64_t dropped = 0;        // entries not kept for writing: too many waiting, or too large
    std::uint64_t evicted = 0;        // files removed to keep to the limit
    std::uint64_t write_failures = 0; // entries that couldn't be written
};

// The name of the folder of the app that runs `executable`, in the system's cache folder: the program's name, and
// eight hexadecimal digits of a hash of its path, so that two apps with one name don't share a cache.
std::string pipeline_cache_app_name(const std::filesystem::path& executable);

// A limit as the setting writes it: a number of bytes, with K, M or G after it for units of 1024, 1024 squared or
// cubed. nullopt for anything else.
std::optional<std::uint64_t> parse_pipeline_cache_limit(std::string_view text);

// The settings of the app that runs `executable`, from the environment variables that `get` reads:
//   ELG_PIPELINE_CACHE        the cache's folder, in place of the system's for this app. `off`, or empty: no
//                             cache, and no shipped one either.
//   ELG_PIPELINE_CACHE_LIMIT  the limit on the entries' size on disk (PipelineCacheSettings::limit), such as
//                             `64M`; 32M by default. A value that isn't one is logged and not used.
// The folders that the system names (gpu/pipeline_cache_backend.h) are the default.
PipelineCacheSettings pipeline_cache_settings(const std::filesystem::path& executable,
                                              const std::function<std::optional<std::string>(const char*)>& get);

// The same, from folders that are already named, for the tests of what the settings do to them.
PipelineCacheSettings pipeline_cache_settings(const PipelineCacheFolders& folders,
                                              const std::function<std::optional<std::string>(const char*)>& get);

// The same, for this program, from its environment.
PipelineCacheSettings pipeline_cache_settings_from_environment();

class PipelineCache {
public:
    // Does nothing with files.
    explicit PipelineCache(PipelineCacheSettings settings);
    // Writes what is waiting, for a little while, and ends the writer.
    ~PipelineCache();
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) = delete;
    PipelineCache& operator=(PipelineCache&&) = delete;

    // The implementation's lookup of the entry `key` of a run with `identity`, in two calls: the first, with
    // an empty `value`, returns the size of the entry, or 0 if there is none; the second, with a `value` of that
    // size, copies it there. The files are read in the first call, on the calling thread. Any thread.
    std::size_t load(std::string_view identity, std::span<const std::byte> key, std::span<std::byte> value) noexcept;

    // The implementation's offer of the entry `key`, which the cache writes, as it can, on its own thread.
    void store(std::string_view identity, std::span<const std::byte> key, std::span<const std::byte> value) noexcept;

    // Returns once what was offered is written. For the tests.
    void flush();

    [[nodiscard]] PipelineCacheStats stats() const;

private:
    using File = std::vector<std::byte>;

    std::optional<File> find_value(const std::string& name, std::string_view identity, std::span<const std::byte> key);
    std::optional<File> read_entry(const std::filesystem::path& directory, bool own, const std::string& name,
                                   std::string_view identity, std::span<const std::byte> key);
    bool write_entry(const std::string& name, const File& file, std::uint64_t& replaced);
    void write_loop();
    void warn_once(std::string_view what) noexcept;
    void request_touch(const std::string& name) noexcept;
    void scan();
    bool make_room(std::uint64_t incoming);

    // A file of the folder, as the last scan saw it.
    struct KnownFile {
        std::filesystem::path path;
        std::uint64_t size = 0;
        std::filesystem::file_time_type time;
    };

    const PipelineCacheSettings settings_;
    const std::uint64_t serial_; // told apart from every other cache of the run, whatever the address

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::map<std::string, std::shared_ptr<const File>> pending_; // offered, not written yet
    std::size_t pending_bytes_ = 0;
    std::vector<std::string> touches_;  // files used, which get the time of their use
    std::set<std::string> touched_;     // the files that this run has asked to touch
    bool stopping_ = false;
    bool writing_ = false;
    std::chrono::steady_clock::time_point deadline_;
    std::thread writer_;

    // The writer's own: what the folder took at the last scan, and what was written since.
    bool scanned_ = false;
    std::uint64_t total_ = 0;
    std::uint64_t written_since_scan_ = 0;
    std::vector<KnownFile> files_;

    std::atomic<bool> warned_{false};
    std::atomic<std::uint64_t> asked_{0};
    std::atomic<std::uint64_t> found_{0};
    std::atomic<std::uint64_t> found_shipped_{0};
    std::atomic<std::uint64_t> missing_{0};
    std::atomic<std::uint64_t> damaged_{0};
    std::atomic<std::uint64_t> stored_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> evicted_{0};
    std::atomic<std::uint64_t> write_failures_{0};
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_PIPELINE_CACHE_H
