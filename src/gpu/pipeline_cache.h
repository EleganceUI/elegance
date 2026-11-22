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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace elg::detail::gpu {

struct PipelineCacheSettings {
    // The folder of this app's entries, or nullopt: no cache.
    std::optional<std::filesystem::path> directory;
};

// What a cache has done, for the tests.
struct PipelineCacheStats {
    std::uint64_t asked = 0;          // lookups that asked for the size of an entry
    std::uint64_t found = 0;          // of them, those that found it
    std::uint64_t missing = 0;        // and those that didn't, damaged ones included
    std::uint64_t damaged = 0;        // files that were removed, or left where they were, as damaged
    std::uint64_t stored = 0;         // entries written
    std::uint64_t dropped = 0;        // entries not kept for writing: too many waiting
    std::uint64_t write_failures = 0; // entries that couldn't be written
};

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
    bool write_entry(const std::string& name, const File& file);
    void write_loop();
    void warn_once(std::string_view what) noexcept;

    const PipelineCacheSettings settings_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::map<std::string, std::shared_ptr<const File>> pending_; // offered, not written yet
    std::size_t pending_bytes_ = 0;
    bool stopping_ = false;
    bool writing_ = false;
    std::chrono::steady_clock::time_point deadline_;
    std::thread writer_;

    std::atomic<bool> warned_{false};
    std::atomic<std::uint64_t> next_temporary_{0};
    std::atomic<std::uint64_t> asked_{0};
    std::atomic<std::uint64_t> found_{0};
    std::atomic<std::uint64_t> missing_{0};
    std::atomic<std::uint64_t> damaged_{0};
    std::atomic<std::uint64_t> stored_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> write_failures_{0};
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_PIPELINE_CACHE_H
