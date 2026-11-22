// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The files of the pipeline cache (gpu/pipeline_cache_format.h): what is written is read back whole, and a
// file that is cut off, changed or another entry's is told from it (ELGSPEC-0036.R5, ELGSPEC-0036.R7).

#include "gpu/pipeline_cache_format.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace elg::detail::gpu;

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    const auto view = std::as_bytes(std::span(text.data(), text.size()));
    return {view.begin(), view.end()};
}

constexpr std::string_view identity = "elegance 0.1.0; dawn 20260911; vulkan; 10005:0; SwiftShader";

} // namespace

TEST_CASE("a pipeline cache entry is read back whole", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("the key of a shader module");
    const std::vector<std::byte> value = bytes("compiled code, of some length");
    const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, value);
    CHECK(file.size() == pipeline_cache_header_size + identity.size() + key.size() + value.size());
    const DecodedPipelineCacheEntry entry = decode_pipeline_cache_entry(file, identity, key);
    CHECK(entry.state == PipelineCacheEntryState::found);
    CHECK(std::vector<std::byte>(entry.value.begin(), entry.value.end()) == value);
}

TEST_CASE("a pipeline cache entry can have an empty key or an empty value", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> none;
    const std::vector<std::byte> some = bytes("x");
    for (const auto& [key, value] : {std::pair{none, some}, std::pair{some, none}, std::pair{none, none}}) {
        const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, value);
        const DecodedPipelineCacheEntry entry = decode_pipeline_cache_entry(file, identity, key);
        CHECK(entry.state == PipelineCacheEntryState::found);
        CHECK(entry.value.size() == value.size());
    }
}

TEST_CASE("a pipeline cache entry's file is named by its identity and its key", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("key");
    const std::string name = pipeline_cache_entry_name(identity, key);
    CHECK(name.size() == 32 + pipeline_cache_extension.size());
    CHECK(name.ends_with(pipeline_cache_extension));
    CHECK(name.find_first_not_of("0123456789abcdef") == 32);
    CHECK(name == pipeline_cache_entry_name(identity, key));
    CHECK(name != pipeline_cache_entry_name("another identity", key));
    CHECK(name != pipeline_cache_entry_name(identity, bytes("another key")));
    // Where the identity ends and the key begins counts.
    CHECK(pipeline_cache_entry_name("ab", bytes("c")) != pipeline_cache_entry_name("a", bytes("bc")));
}

TEST_CASE("a pipeline cache file that is cut off, has more, or is empty is damaged", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("key");
    const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, bytes("value of the entry"));
    CHECK(decode_pipeline_cache_entry({}, identity, key).state == PipelineCacheEntryState::damaged);
    for (std::size_t length = 0; length < file.size(); ++length) {
        const std::span<const std::byte> cut(file.data(), length);
        CHECK(decode_pipeline_cache_entry(cut, identity, key).state == PipelineCacheEntryState::damaged);
    }
    std::vector<std::byte> longer = file;
    longer.push_back(std::byte{0});
    CHECK(decode_pipeline_cache_entry(longer, identity, key).state == PipelineCacheEntryState::damaged);
}

TEST_CASE("a pipeline cache file with any one byte changed is damaged", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("key");
    const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, bytes("value of the entry"));
    for (std::size_t i = 0; i < file.size(); ++i) {
        std::vector<std::byte> changed = file;
        changed[i] ^= std::byte{0x01};
        CHECK(decode_pipeline_cache_entry(changed, identity, key).state == PipelineCacheEntryState::damaged);
    }
}

TEST_CASE("a pipeline cache file whose sizes lie is damaged, without reading past its end", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("key");
    const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, bytes("value"));
    // The identity's size at 12, the key's at 16, the value's at 24: each as large as it can be.
    for (const std::size_t at : {std::size_t{12}, std::size_t{16}, std::size_t{24}}) {
        std::vector<std::byte> lying = file;
        for (std::size_t i = 0; i < (at == 24 ? 8U : 4U); ++i) {
            lying[at + i] = std::byte{0xFF};
        }
        CHECK(decode_pipeline_cache_entry(lying, identity, key).state == PipelineCacheEntryState::damaged);
    }
}

TEST_CASE("a whole pipeline cache file of another identity or key is another entry's", "[gpu][pipeline-cache]") {
    const std::vector<std::byte> key = bytes("key");
    const std::vector<std::byte> file = encode_pipeline_cache_entry(identity, key, bytes("value"));
    CHECK(decode_pipeline_cache_entry(file, "elegance 0.2.0; dawn 20260911; vulkan; 10005:0; SwiftShader", key).state ==
          PipelineCacheEntryState::other_entry);
    CHECK(decode_pipeline_cache_entry(file, identity, bytes("another key")).state ==
          PipelineCacheEntryState::other_entry);
}
