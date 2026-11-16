// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/glyph_cache.h"

#include "core/log.h"
#include "platform/platform.h"
#include "text/atlas.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace elg::detail {

namespace {

// Bump when generated pixels change for the same font and glyph (generator settings, fixes).
// 6: files by distance field kind and every variation coordinate; 5: thin overlaps found; 4: overlapping
// contours resolved; 3: color glyphs single-channel
constexpr std::uint32_t kVersion = 6;
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
        std::uint64_t word = 0;
        std::memcpy(&word, bytes.data() + i * 8, 8);
        hash = (hash ^ word) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    return fnv1a(bytes.data() + words * 8, bytes.size() - words * 8, hash);
}

// Deletes the files that cache versions before 6 kept for the font whose bytes hash to `hash`
// (hexadecimal): "<hash>.glyphs" and "<hash>-wght<weight>.glyphs".
void remove_old_files(const std::filesystem::path& directory, const std::string& hash) {
    constexpr std::string_view kExtension = ".glyphs";
    constexpr std::string_view kWeight = "-wght";
    std::vector<std::filesystem::path> old;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.starts_with(hash) || !name.ends_with(kExtension)) {
            continue;
        }
        // Between the hash and the extension: nothing, or "-wght" and digits.
        std::string_view rest = name;
        rest = rest.substr(hash.size(), rest.size() - hash.size() - kExtension.size());
        if (!rest.empty()) {
            if (!rest.starts_with(kWeight) || rest.size() == kWeight.size()) {
                continue;
            }
            rest.remove_prefix(kWeight.size());
        }
        if (std::ranges::all_of(rest, [](char c) { return c >= '0' && c <= '9'; })) {
            old.push_back(it->path());
        }
    }
    for (const std::filesystem::path& file : old) {
        std::filesystem::remove(file, error);
    }
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

GlyphDiskCache::FontCache& GlyphDiskCache::font(const FontFace& face, DistanceField field) {
    FontCache*& slot = fonts_[(std::uint64_t{face.id()} << 1) | (field == DistanceField::single_channel ? 1u : 0u)];
    if (slot) {
        return *slot;
    }
    // A variable font's instances are different glyphs from the same file, and each kind of
    // distance field is a file of its own.
    const std::string hash = std::format("{:016x}", hash_font(face.bytes()));
    std::string name = hash;
    if (const std::span<const std::int64_t> coordinates = face.variation_coordinates(); !coordinates.empty()) {
        name += std::format("-v{:016x}", fnv1a(coordinates.data(), coordinates.size_bytes()));
    }
    name += field == DistanceField::single_channel ? "-sdf.glyphs" : "-mtsdf.glyphs";
    std::unique_ptr<FontCache>& file = files_[name];
    if (file) {
        slot = file.get();
        return *slot; // another face with the same glyphs opened it
    }
    file = std::make_unique<FontCache>();
    slot = file.get();
    FontCache& cache = *file;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): find() and store() return before this without directory_
    const std::filesystem::path& directory = *directory_;
    cache.file = directory / name;

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
                RecordHeader record{};
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
        remove_old_files(directory, hash); // a new file replaces them
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

std::optional<std::vector<std::uint8_t>> GlyphDiskCache::find(const FontFace& face, DistanceField field,
                                                               std::uint32_t glyph, std::uint32_t width,
                                                               std::uint32_t height) {
    if (!directory_) {
        return std::nullopt;
    }
    const std::lock_guard lock(mutex_);
    FontCache& cache = font(face, field);
    if (const auto it = cache.records.find(glyph); it != cache.records.end()) {
        if (it->second.width == width && it->second.height == height) {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(cache.contents.data() + it->second.offset);
            return std::vector<std::uint8_t>(begin, begin + std::size_t{width} * height * 4);
        }
    }
    return std::nullopt;
}

void GlyphDiskCache::store(const FontFace& face, DistanceField field, std::uint32_t glyph, std::uint32_t width,
                           std::uint32_t height, std::span<const std::uint8_t> rgba) {
    if (!directory_) {
        return;
    }
    const std::lock_guard lock(mutex_);
    FontCache& cache = font(face, field);
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
