// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The files of the pipeline cache (gpu/pipeline_cache_format.h).

#include "gpu/pipeline_cache_format.h"

#include <algorithm>
#include <array>
#include <format>

namespace elg::detail::gpu {

namespace {

constexpr std::array<char, 8> magic = {'E', 'L', 'G', 'P', 'C', 'A', 'C', 'H'};
constexpr std::uint64_t fnv_basis = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::uint64_t other_basis = fnv_basis ^ 0x9E3779B97F4A7C15ULL;

std::span<const std::byte> bytes_of(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::uint64_t read_le(std::span<const std::byte> bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = bytes.size(); i > 0; --i) {
        value = (value << 8) | std::to_integer<std::uint64_t>(bytes[i - 1]);
    }
    return value;
}

void append_le(std::vector<std::byte>& out, std::uint64_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
}

// FNV-1a over `data`, a word at a time, with its size: the checksum of a file's contents.
std::uint64_t mix(std::uint64_t hash, std::span<const std::byte> data) noexcept {
    std::size_t i = 0;
    for (; i + 8 <= data.size(); i += 8) {
        hash = (hash ^ read_le(data.subspan(i, 8))) * fnv_prime;
    }
    for (; i < data.size(); ++i) {
        hash = (hash ^ std::to_integer<std::uint64_t>(data[i])) * fnv_prime;
    }
    return (hash ^ data.size()) * fnv_prime;
}

std::uint64_t checksum(std::span<const std::byte> identity, std::span<const std::byte> key,
                       std::span<const std::byte> value) noexcept {
    return mix(mix(mix(fnv_basis, identity), key), value);
}

// FNV-1a byte by byte over the identity, a zero byte and the key.
std::uint64_t name_hash(std::uint64_t hash, std::string_view identity, std::span<const std::byte> key) noexcept {
    const auto add = [&hash](std::byte byte) { hash = (hash ^ std::to_integer<std::uint64_t>(byte)) * fnv_prime; };
    for (const std::byte byte : bytes_of(identity)) {
        add(byte);
    }
    add(std::byte{0});
    for (const std::byte byte : key) {
        add(byte);
    }
    return hash;
}

} // namespace

std::string pipeline_cache_entry_name(std::string_view identity, std::span<const std::byte> key) {
    return std::format("{:016x}{:016x}{}", name_hash(fnv_basis, identity, key), name_hash(other_basis, identity, key),
                       pipeline_cache_extension);
}

std::vector<std::byte> encode_pipeline_cache_entry(std::string_view identity, std::span<const std::byte> key,
                                                   std::span<const std::byte> value) {
    std::vector<std::byte> file;
    file.reserve(pipeline_cache_header_size + identity.size() + key.size() + value.size());
    for (const char c : magic) {
        file.push_back(static_cast<std::byte>(c));
    }
    append_le(file, pipeline_cache_format_version, 4);
    append_le(file, identity.size(), 4);
    append_le(file, key.size(), 4);
    append_le(file, 0, 4);
    append_le(file, value.size(), 8);
    append_le(file, checksum(bytes_of(identity), key, value), 8);
    const std::span<const std::byte> identity_bytes = bytes_of(identity);
    file.insert(file.end(), identity_bytes.begin(), identity_bytes.end());
    file.insert(file.end(), key.begin(), key.end());
    file.insert(file.end(), value.begin(), value.end());
    return file;
}

DecodedPipelineCacheEntry decode_pipeline_cache_entry(std::span<const std::byte> file, std::string_view identity,
                                                      std::span<const std::byte> key) noexcept {
    if (file.size() < pipeline_cache_header_size ||
        !std::ranges::equal(file.first(magic.size()), bytes_of(std::string_view(magic.data(), magic.size())))) {
        return {};
    }
    const std::uint64_t version = read_le(file.subspan(8, 4));
    const std::uint64_t identity_size = read_le(file.subspan(12, 4));
    const std::uint64_t key_size = read_le(file.subspan(16, 4));
    const std::uint64_t reserved = read_le(file.subspan(20, 4));
    const std::uint64_t value_size = read_le(file.subspan(24, 8));
    const std::uint64_t stored_checksum = read_le(file.subspan(32, 8));
    if (version != pipeline_cache_format_version || reserved != 0) {
        return {};
    }
    // The sizes the header gives are exactly what follows it: they can't say more than the file has.
    const std::uint64_t after_header = file.size() - pipeline_cache_header_size;
    if (value_size > after_header || identity_size > after_header - value_size ||
        key_size != after_header - value_size - identity_size) {
        return {};
    }
    const std::span<const std::byte> body = file.subspan(pipeline_cache_header_size);
    const std::span<const std::byte> stored_identity = body.first(static_cast<std::size_t>(identity_size));
    const std::span<const std::byte> stored_key = body.subspan(stored_identity.size(), static_cast<std::size_t>(key_size));
    const std::span<const std::byte> stored_value = body.subspan(stored_identity.size() + stored_key.size());
    if (checksum(stored_identity, stored_key, stored_value) != stored_checksum) {
        return {};
    }
    if (!std::ranges::equal(stored_identity, bytes_of(identity)) || !std::ranges::equal(stored_key, key)) {
        return {PipelineCacheEntryState::other_entry, {}};
    }
    return {PipelineCacheEntryState::found, stored_value};
}

} // namespace elg::detail::gpu
