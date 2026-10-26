// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/glyph_cache.h"

#include "core/log.h"
#include "platform/platform.h"
#include "text/atlas.h"

#include <cstring>
#include <format>

namespace elg::detail {

namespace {

// Bump when generated pixels change for the same font and glyph (generator settings, fixes).
constexpr std::uint32_t kVersion = 4; // 4: overlapping contours resolved; 3: color glyphs single-channel
constexpr char kFileMagic[4] = {'E', 'L', 'G', 'G'};
constexpr std::uint32_t kRecordMagic = 0x504C5947; // "GYLP"
constexpr std::uint64_t kMaxFileBytes = 64ull << 20;

struct FileHeader {
    char magic[4];
    std::uint32_t version;
    std::uint32_t em_pixels;      // kGlyphEmPixels
    std::uint32_t distance_range; // kDistanceRange
};

struct RecordHeader {
    std::uint32_t magic;
    std::uint32_t glyph;
    std::uint32_t width;
    std::uint32_t height;
    std::uint64_t checksum; // of the pixels
};

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t hash = 14695981039346656037ull) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

// A fast 64-bit hash of a font's bytes (8 at a time; fonts are tens of MB).
std::uint64_t hash_font(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash = 0x9E3779B97F4A7C15ull ^ bytes.size();
    const std::size_t words = bytes.size() / 8;
    for (std::size_t i = 0; i < words; ++i) {
        std::uint64_t word;
        std::memcpy(&word, bytes.data() + i * 8, 8);
        hash = (hash ^ word) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    return fnv1a(bytes.data() + words * 8, bytes.size() - words * 8, hash);
}

FileHeader expected_header() {
    FileHeader header{};
    std::memcpy(header.magic, kFileMagic, 4);
    header.version = kVersion;
    header.em_pixels = static_cast<std::uint32_t>(kGlyphEmPixels);
    header.distance_range = static_cast<std::uint32_t>(kDistanceRange);
    return header;
}

} // namespace

std::optional<std::filesystem::path> GlyphDiskCache::default_directory() {
    if (const std::optional<std::string> setting = platform::environment_variable("ELG_GLYPH_CACHE")) {
        if (*setting == "off" || setting->empty()) {
            return std::nullopt;
        }
        return std::filesystem::path(*setting);
    }
#if defined(_WIN32)
    if (const auto base = platform::environment_variable("LOCALAPPDATA")) {
        return std::filesystem::path(*base) / "Elegance" / "GlyphCache";
    }
#elif defined(__APPLE__)
    if (const auto home = platform::environment_variable("HOME")) {
        return std::filesystem::path(*home) / "Library" / "Caches" / "Elegance" / "GlyphCache";
    }
#else
    if (const auto xdg = platform::environment_variable("XDG_CACHE_HOME")) {
        return std::filesystem::path(*xdg) / "elegance" / "glyphs";
    }
    if (const auto home = platform::environment_variable("HOME")) {
        return std::filesystem::path(*home) / ".cache" / "elegance" / "glyphs";
    }
#endif
    return std::nullopt;
}

GlyphDiskCache::GlyphDiskCache(std::optional<std::filesystem::path> directory) : directory_(std::move(directory)) {
    if (!directory_) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(*directory_, error);
    if (error) {
        log_warning("glyph cache disabled: can't create {}: {}", directory_->string(), error.message());
        directory_.reset();
    }
}

GlyphDiskCache::~GlyphDiskCache() = default;

GlyphDiskCache::FontCache& GlyphDiskCache::font(const FontFace& face) {
    auto& slot = fonts_[face.id()];
    if (slot) {
        return *slot;
    }
    slot = std::make_unique<FontCache>();
    FontCache& cache = *slot;
    // A variable font's weights are different glyphs from the same file.
    cache.file = *directory_ / (face.variation_weight() > 0 ? std::format("{:016x}-wght{}.glyphs", hash_font(face.bytes()),
                                                                          face.variation_weight())
                                                            : std::format("{:016x}.glyphs", hash_font(face.bytes())));

    // Load what's there; anything unreadable from some point on is ignored (and overwritten).
    std::ifstream in(cache.file, std::ios::binary | std::ios::ate);
    bool valid_header = false;
    std::size_t valid_end = 0;
    if (in) {
        const std::streamsize size = in.tellg();
        cache.contents.resize(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
        in.seekg(0);
        in.read(cache.contents.data(), size);
        const FileHeader expected = expected_header();
        if (cache.contents.size() >= sizeof(FileHeader) &&
            std::memcmp(cache.contents.data(), &expected, sizeof(FileHeader)) == 0) {
            valid_header = true;
            std::size_t at = sizeof(FileHeader);
            while (at + sizeof(RecordHeader) <= cache.contents.size()) {
                RecordHeader record;
                std::memcpy(&record, cache.contents.data() + at, sizeof(record));
                const std::size_t size_bytes = std::size_t{record.width} * record.height * 4;
                const std::size_t pixels = at + sizeof(record);
                if (record.magic != kRecordMagic || pixels + size_bytes > cache.contents.size() ||
                    fnv1a(cache.contents.data() + pixels, size_bytes) != record.checksum) {
                    break; // torn or interleaved write: stop here
                }
                cache.records[record.glyph] = {record.width, record.height, pixels};
                at = pixels + size_bytes;
            }
            valid_end = at;
        }
    }
    in.close();

    // Append after the last good record (rewriting the file when it's unusable).
    std::error_code error;
    if (!valid_header) {
        cache.out.open(cache.file, std::ios::binary | std::ios::trunc);
        const FileHeader header = expected_header();
        cache.out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        cache.bytes = sizeof(header);
    } else {
        if (valid_end < cache.contents.size()) {
            std::filesystem::resize_file(cache.file, valid_end, error);
        }
        cache.out.open(cache.file, std::ios::binary | std::ios::app);
        cache.bytes = valid_end;
    }
    if (!cache.out) {
        log_warning("glyph cache: can't write {}", cache.file.string());
    }
    return cache;
}

std::optional<std::vector<std::uint8_t>> GlyphDiskCache::find(const FontFace& face, std::uint32_t glyph,
                                                               std::uint32_t width, std::uint32_t height) {
    if (!directory_) {
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);
    FontCache& cache = font(face);
    if (const auto it = cache.records.find(glyph); it != cache.records.end()) {
        if (it->second.width == width && it->second.height == height) {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(cache.contents.data() + it->second.offset);
            return std::vector<std::uint8_t>(begin, begin + std::size_t{width} * height * 4);
        }
    }
    return std::nullopt;
}

void GlyphDiskCache::store(const FontFace& face, std::uint32_t glyph, std::uint32_t width, std::uint32_t height,
                           std::span<const std::uint8_t> rgba) {
    if (!directory_) {
        return;
    }
    std::lock_guard lock(mutex_);
    FontCache& cache = font(face);
    if (!cache.out || cache.bytes + sizeof(RecordHeader) + rgba.size() > kMaxFileBytes || cache.records.contains(glyph)) {
        return;
    }
    const RecordHeader record{kRecordMagic, glyph, width, height, fnv1a(rgba.data(), rgba.size())};
    // One write per record, so concurrent appenders interleave whole records at worst.
    std::vector<char> buffer(sizeof(record) + rgba.size());
    std::memcpy(buffer.data(), &record, sizeof(record));
    std::memcpy(buffer.data() + sizeof(record), rgba.data(), rgba.size());
    cache.out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    cache.out.flush();
    cache.bytes += buffer.size();
}

} // namespace elg::detail
