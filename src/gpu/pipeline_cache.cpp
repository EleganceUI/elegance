// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The pipeline cache (gpu/pipeline_cache.h).

#include "gpu/pipeline_cache.h"

#include "core/log.h"
#include "core/os_backend.h"
#include "gpu/pipeline_cache_format.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <ios>
#include <system_error>
#include <utility>

namespace elg::detail::gpu {

namespace {

namespace fs = std::filesystem;

// What the cache keeps waiting to be written, and what one file may be.
constexpr std::size_t max_pending_bytes = std::size_t{16} << 20;
constexpr std::uint64_t max_file_size = std::uint64_t{256} << 20;
constexpr auto drain_time = std::chrono::seconds(2);

// The value that a first call of load() found, for the second call on the same thread.
struct FoundValue {
    const PipelineCache* cache = nullptr;
    std::string name;
    std::vector<std::byte> value;
};

FoundValue& found_value() {
    thread_local FoundValue found;
    return found;
}

std::optional<std::vector<std::byte>> read_file(const fs::path& path, std::uint64_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || in.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

PipelineCache::PipelineCache(PipelineCacheSettings settings) : settings_(std::move(settings)) {}

PipelineCache::~PipelineCache() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        deadline_ = std::chrono::steady_clock::now() + drain_time;
    }
    wake_.notify_all();
    if (writer_.joinable()) {
        writer_.join();
    }
}

void PipelineCache::warn_once(std::string_view what) noexcept {
    try {
        if (!warned_.exchange(true)) {
            log_warning("pipeline cache: {}", what);
        }
    } catch (...) {
        warned_ = true; // logging failed: it isn't tried again
    }
}

PipelineCacheStats PipelineCache::stats() const {
    return {asked_, found_, missing_, damaged_, stored_, dropped_, write_failures_};
}

// --- Reading ---

std::optional<PipelineCache::File> PipelineCache::find_value(const std::string& name, std::string_view identity,
                                                             std::span<const std::byte> key) {
    {
        const std::lock_guard lock(mutex_);
        if (const auto waiting = pending_.find(name); waiting != pending_.end()) {
            const DecodedPipelineCacheEntry entry = decode_pipeline_cache_entry(*waiting->second, identity, key);
            if (entry.state == PipelineCacheEntryState::found) {
                return File(entry.value.begin(), entry.value.end());
            }
            return std::nullopt;
        }
    }
    if (!settings_.directory) {
        return std::nullopt;
    }
    const fs::path path = *settings_.directory / name;
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        return std::nullopt; // there is no such entry (or the folder isn't there)
    }
    const fs::file_time_type time = fs::last_write_time(path, error);
    if (error || size > max_file_size) {
        return std::nullopt;
    }
    const std::optional<File> file = read_file(path, size);
    DecodedPipelineCacheEntry entry;
    if (file) {
        entry = decode_pipeline_cache_entry(*file, identity, key);
        if (entry.state == PipelineCacheEntryState::found) {
            return File(entry.value.begin(), entry.value.end());
        }
        if (entry.state == PipelineCacheEntryState::other_entry) {
            return std::nullopt;
        }
    }
    // It can't be read whole, or it doesn't match its checksum: it is removed, unless it was written again
    // since it was looked at.
    damaged_.fetch_add(1);
    warn_once("a damaged entry was found and removed");
    std::error_code again;
    if (fs::file_size(path, again) == size && !again && fs::last_write_time(path, again) == time && !again) {
        fs::remove(path, again);
    }
    return std::nullopt;
}

std::size_t PipelineCache::load(std::string_view identity, std::span<const std::byte> key,
                                std::span<std::byte> value) noexcept {
    try {
        const std::string name = pipeline_cache_entry_name(identity, key);
        FoundValue& found = found_value();
        if (value.empty()) {
            asked_.fetch_add(1);
            std::optional<File> entry = find_value(name, identity, key);
            if (!entry || entry->empty()) {
                missing_.fetch_add(1);
                found = {};
                return 0;
            }
            found_.fetch_add(1);
            const std::size_t size = entry->size();
            found = {this, name, std::move(*entry)};
            return size;
        }
        // The second call: the value of the first call, if that is what is asked for.
        if (found.cache == this && found.name == name && found.value.size() == value.size()) {
            std::ranges::copy(found.value, value.begin());
            found = {};
            return value.size();
        }
        found = {};
        const std::optional<File> entry = find_value(name, identity, key);
        if (!entry || entry->size() != value.size()) {
            return 0;
        }
        std::ranges::copy(*entry, value.begin());
        return value.size();
    } catch (...) {
        return 0;
    }
}

// --- Writing ---

void PipelineCache::store(std::string_view identity, std::span<const std::byte> key,
                          std::span<const std::byte> value) noexcept {
    if (!settings_.directory) {
        return;
    }
    try {
        std::string name = pipeline_cache_entry_name(identity, key);
        auto file = std::make_shared<const File>(encode_pipeline_cache_entry(identity, key, value));
        {
            const std::lock_guard lock(mutex_);
            const auto waiting = pending_.find(name);
            const std::size_t replaced = waiting == pending_.end() ? 0 : waiting->second->size();
            if (pending_bytes_ - replaced + file->size() > max_pending_bytes) {
                dropped_.fetch_add(1);
                return;
            }
            pending_bytes_ = pending_bytes_ - replaced + file->size();
            pending_[std::move(name)] = std::move(file);
            if (!writer_.joinable() && !stopping_) {
                writer_ = std::thread([this] { write_loop(); });
            }
        }
        wake_.notify_one();
    } catch (...) {
        dropped_.fetch_add(1);
    }
}

bool PipelineCache::write_entry(const std::string& name, const File& file) {
    const fs::path directory = settings_.directory.value_or(fs::path()); // store() keeps entries only with a folder
    const fs::path path = directory / name;
    std::error_code error;
    // An entry that is already there with these bytes is not written again.
    if (fs::file_size(path, error) == file.size() && !error) {
        if (const std::optional<File> existing = read_file(path, file.size()); existing && *existing == file) {
            return true;
        }
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        // The folder may be made, or made again after the system emptied it, between the attempts.
        fs::create_directories(directory, error);
        const fs::path temporary =
            directory / std::format("{}.{}.{}.tmp", name, os::process_id(), next_temporary_.fetch_add(1));
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
            out.flush();
            if (!out) {
                fs::remove(temporary, error);
                continue;
            }
        }
        fs::rename(temporary, path, error);
        if (!error) {
            return true;
        }
        std::error_code ignored;
        fs::remove(temporary, ignored);
    }
    return false;
}

void PipelineCache::write_loop() {
    std::unique_lock lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return !pending_.empty() || stopping_; });
        if (pending_.empty()) {
            idle_.notify_all();
            if (stopping_) {
                return;
            }
            continue;
        }
        if (stopping_ && std::chrono::steady_clock::now() >= deadline_) {
            return; // what is left isn't written: the program is ending
        }
        const std::string name = pending_.begin()->first;
        const std::shared_ptr<const File> file = pending_.begin()->second;
        writing_ = true;
        lock.unlock();
        bool written = false;
        try {
            written = write_entry(name, *file);
        } catch (...) {
            written = false;
        }
        if (written) {
            stored_.fetch_add(1);
        } else {
            write_failures_.fetch_add(1);
            warn_once("an entry couldn't be written");
        }
        lock.lock();
        // The entry stays visible while it is written. It leaves unless an offer replaced it meanwhile.
        if (const auto done = pending_.find(name); done != pending_.end() && done->second == file) {
            pending_bytes_ -= file->size();
            pending_.erase(done);
        }
        writing_ = false;
        idle_.notify_all();
    }
}

void PipelineCache::flush() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return pending_.empty() && !writing_; });
}

} // namespace elg::detail::gpu
