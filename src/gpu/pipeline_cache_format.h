// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PIPELINE_CACHE_FORMAT_H
#define ELG_GPU_PIPELINE_CACHE_FORMAT_H

// The files of the pipeline cache (gpu/pipeline_cache.h): one file for each entry that the WebGPU
// implementation stores, named from the entry's key and the identity of the run that stored it, and
// holding that identity, the key, the value and a checksum of them, so that a file that is cut off, changed
// or from another run is told from one that is good. Integers are little-endian.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::gpu {

// The size of the header that every file starts with: its magic, the format's version, the sizes of the
// identity and the key, a reserved word, the size of the value and the checksum.
inline constexpr std::size_t pipeline_cache_header_size = 40;
inline constexpr std::uint32_t pipeline_cache_format_version = 1;
inline constexpr std::string_view pipeline_cache_extension = ".elgpc";

// The name of the file for the entry `key` of a run with `identity`: 32 hexadecimal digits of two hashes of
// both, and the extension. Another entry can have the same name, which the identity and key in the file tell.
[[nodiscard]] std::string pipeline_cache_entry_name(std::string_view identity, std::span<const std::byte> key);

// The contents of the file for an entry.
[[nodiscard]] std::vector<std::byte> encode_pipeline_cache_entry(std::string_view identity,
                                                                 std::span<const std::byte> key,
                                                                 std::span<const std::byte> value);

enum class PipelineCacheEntryState {
    found,      // the file is the entry asked for
    damaged,    // it can't be read whole, or doesn't match its checksum
    other_entry // it is whole, and another entry's: another identity or key with the same file name
};

struct DecodedPipelineCacheEntry {
    PipelineCacheEntryState state = PipelineCacheEntryState::damaged;
    std::span<const std::byte> value; // the entry's value, in `file`, when it was found
};

// What the contents of a file are, read as the entry `key` of a run with `identity`.
[[nodiscard]] DecodedPipelineCacheEntry decode_pipeline_cache_entry(std::span<const std::byte> file,
                                                                    std::string_view identity,
                                                                    std::span<const std::byte> key) noexcept;

} // namespace elg::detail::gpu

#endif // ELG_GPU_PIPELINE_CACHE_FORMAT_H
