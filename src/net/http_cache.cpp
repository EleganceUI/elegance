// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "net/http_cache.h"

#include "core/log.h"
#include "platform/generic/platform.h"

#include <openssl/evp.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>

namespace elg::detail::net {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kMagic = "ELGCACHE 1";
constexpr std::int64_t kHeuristicCap = 24 * 60 * 60; // a Last-Modified guess stays fresh at most a day

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::int64_t> parse_number(std::string_view text) {
    text = trim(text);
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return value;
}

struct Directives {
    bool no_store = false;
    bool no_cache = false;
    bool must_revalidate = false;
    std::optional<std::int64_t> max_age;
};

Directives parse_cache_control(std::string_view text) {
    Directives out;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = trim(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        const std::size_t equals = part.find('=');
        const std::string name = lower(trim(part.substr(0, equals)));
        std::string_view value = equals == std::string_view::npos ? std::string_view() : trim(part.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (name == "no-store") {
            out.no_store = true;
        } else if (name == "no-cache") {
            out.no_cache = true; // also with field names: revalidating every time covers them
        } else if (name == "must-revalidate" || name == "proxy-revalidate") {
            out.must_revalidate = true;
        } else if (name == "max-age") {
            // An invalid max-age makes the response stale (RFC 9111 §4.2.1).
            out.max_age = parse_number(value).value_or(0);
        }
    }
    return out;
}

int month_number(std::string_view name) {
    static constexpr std::array<std::string_view, 12> kMonths{"jan", "feb", "mar", "apr", "may", "jun",
                                                               "jul", "aug", "sep", "oct", "nov", "dec"};
    const std::string key = lower(name);
    for (std::size_t i = 0; i < kMonths.size(); ++i) {
        if (key == kMonths[i]) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

// Fills in age and lifetime from the headers.
void apply_freshness(CacheEntry& entry, const CacheHeaders& headers, const Directives& directives, std::int64_t now) {
    entry.received = now;
    const std::int64_t date = parse_http_date(headers.date).value_or(now);
    const std::int64_t apparent_age = std::max<std::int64_t>(0, now - date);
    entry.initial_age = std::max(apparent_age, parse_number(headers.age).value_or(0));
    if (directives.max_age) {
        entry.lifetime = *directives.max_age;
    } else if (!headers.expires.empty()) {
        // An invalid Expires (often "0") means already expired.
        entry.lifetime = std::max<std::int64_t>(0, parse_http_date(headers.expires).value_or(0) - date);
    } else if (const auto modified = parse_http_date(headers.last_modified); modified && *modified < date) {
        entry.lifetime = std::min((date - *modified) / 10, kHeuristicCap);
    } else {
        entry.lifetime = 0;
    }
    entry.no_cache = directives.no_cache;
    entry.must_revalidate = directives.must_revalidate;
}

bool has_line_break(std::string_view text) {
    return text.find_first_of("\r\n") != std::string_view::npos;
}

std::string hex_digest(std::string_view text) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    EVP_Digest(text.data(), text.size(), digest.data(), &size, EVP_sha256(), nullptr);
    std::string out;
    for (unsigned int i = 0; i < size; ++i) {
        out += std::format("{:02x}", digest[i]);
    }
    return out;
}

bool is_entry_name(const std::string& name) {
    return name.size() == 64 && std::ranges::all_of(name, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

bool is_bucket_name(const std::string& name) {
    return name.size() == 2 && std::ranges::all_of(name, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

std::string executable_name() {
    fs::path path;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        path = buffer.c_str();
    }
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    if (const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH); length > 0 && length < MAX_PATH) {
        path = std::wstring(buffer, length);
    }
#else
    std::error_code error;
    path = fs::read_symlink("/proc/self/exe", error);
#endif
    std::string name = path.stem().string();
    return name.empty() ? "app" : name;
}

} // namespace

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::optional<std::int64_t> parse_http_date(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    // The weekday goes: "Sun, ..." and "Sunday, ..." have a comma; asctime's "Sun Nov  6 ..." doesn't.
    const bool asctime = text.find(',') == std::string_view::npos;
    if (!asctime) {
        text.remove_prefix(text.find(',') + 1);
    }
    std::vector<std::string_view> tokens;
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t end = text.find_first_of(" -", start);
        if (end != start) {
            tokens.push_back(text.substr(start, end - start));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    std::string_view day_text, month_text, year_text, time_text;
    if (asctime) { // Sun Nov 6 08:49:37 1994
        if (tokens.size() != 5) {
            return std::nullopt;
        }
        month_text = tokens[1];
        day_text = tokens[2];
        time_text = tokens[3];
        year_text = tokens[4];
    } else { // 06 Nov 1994 08:49:37 GMT, 06-Nov-94 08:49:37 GMT
        if (tokens.size() != 5 || tokens[4] != "GMT") {
            return std::nullopt;
        }
        day_text = tokens[0];
        month_text = tokens[1];
        year_text = tokens[2];
        time_text = tokens[3];
    }
    const auto day = parse_number(day_text);
    auto year = parse_number(year_text);
    const int month = month_number(month_text);
    if (!day || !year || month == 0 || time_text.size() != 8 || time_text[2] != ':' || time_text[5] != ':') {
        return std::nullopt;
    }
    if (year_text.size() == 2) {
        *year += *year < 70 ? 2000 : 1900;
    }
    const auto hours = parse_number(time_text.substr(0, 2));
    const auto minutes = parse_number(time_text.substr(3, 2));
    const auto seconds = parse_number(time_text.substr(6, 2));
    if (!hours || !minutes || !seconds || *hours > 23 || *minutes > 59 || *seconds > 60) {
        return std::nullopt;
    }
    const std::chrono::year_month_day date{std::chrono::year(static_cast<int>(*year)),
                                           std::chrono::month(static_cast<unsigned>(month)),
                                           std::chrono::day(static_cast<unsigned>(*day))};
    if (!date.ok()) {
        return std::nullopt;
    }
    const std::int64_t days = std::chrono::sys_days(date).time_since_epoch().count();
    return days * 86400 + *hours * 3600 + *minutes * 60 + *seconds;
}

std::optional<CacheEntry> make_cache_entry(std::string url, bool redirected, const CacheHeaders& headers,
                                           std::vector<std::uint8_t> body, std::int64_t now) {
    const Directives directives = parse_cache_control(headers.cache_control);
    if (directives.no_store || trim(headers.vary) == "*" || has_line_break(url) || has_line_break(headers.etag) ||
        has_line_break(headers.last_modified)) {
        return std::nullopt;
    }
    CacheEntry entry;
    entry.url = std::move(url);
    entry.redirected = redirected;
    entry.etag = std::string(trim(headers.etag));
    entry.last_modified = std::string(trim(headers.last_modified));
    apply_freshness(entry, headers, directives, now);
    if (!entry.fresh(now) && !entry.revalidates()) {
        return std::nullopt; // it would never be used
    }
    entry.body = std::move(body);
    return entry;
}

void refresh_cache_entry(CacheEntry& entry, const CacheHeaders& headers, std::int64_t now) {
    // A 304 may leave out headers it doesn't change (RFC 9111 §4.3.4).
    CacheHeaders merged = headers;
    if (merged.etag.empty()) {
        merged.etag = entry.etag;
    }
    if (merged.last_modified.empty()) {
        merged.last_modified = entry.last_modified;
    }
    entry.etag = std::string(trim(merged.etag));
    entry.last_modified = std::string(trim(merged.last_modified));
    apply_freshness(entry, merged, parse_cache_control(merged.cache_control), now);
}

// --- HttpCache ---

HttpCache::HttpCache(fs::path directory, std::uint64_t limit) : directory_(std::move(directory)), limit_(limit) {}

std::optional<fs::path> HttpCache::default_directory() {
    const std::string app = executable_name();
#if defined(_WIN32)
    if (const auto base = platform::environment_variable("LOCALAPPDATA")) {
        return fs::path(*base) / "Elegance" / app / "Images";
    }
#elif defined(__APPLE__)
    if (const auto home = platform::environment_variable("HOME")) {
        return fs::path(*home) / "Library" / "Caches" / "Elegance" / app / "Images";
    }
#else
    if (const auto xdg = platform::environment_variable("XDG_CACHE_HOME"); xdg && !xdg->empty()) {
        return fs::path(*xdg) / "elegance" / app / "images";
    }
    if (const auto home = platform::environment_variable("HOME")) {
        return fs::path(*home) / ".cache" / "elegance" / app / "images";
    }
#endif
    return std::nullopt;
}

fs::path HttpCache::path_for(const std::string& url) const {
    const std::string name = hex_digest(url);
    return directory_ / name.substr(0, 2) / name;
}

void HttpCache::load_index() {
    if (std::exchange(indexed_, true)) {
        return;
    }
    std::error_code error;
    for (const auto& bucket : fs::directory_iterator(directory_, error)) {
        if (!bucket.is_directory(error) || !is_bucket_name(bucket.path().filename().string())) {
            continue;
        }
        for (const auto& file : fs::directory_iterator(bucket.path(), error)) {
            const std::string name = file.path().filename().string();
            if (is_entry_name(name)) {
                const std::uint64_t size = file.file_size(error);
                files_[file.path()] = {size, file.last_write_time(error)};
                total_ += size;
            } else if (name.ends_with(".tmp")) {
                fs::remove(file.path(), error); // a write that didn't finish
            }
        }
    }
}

std::optional<CacheEntry> HttpCache::find(const std::string& url) {
    const std::lock_guard lock(mutex_);
    load_index();
    const fs::path path = path_for(url);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    CacheEntry entry;
    std::string line;
    if (!std::getline(in, line) || line != kMagic) {
        return std::nullopt;
    }
    while (std::getline(in, line) && !line.empty()) {
        const std::size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string value = space == std::string::npos ? std::string() : line.substr(space + 1);
        const std::int64_t number = parse_number(value).value_or(0);
        if (key == "url") {
            entry.url = value;
        } else if (key == "redirected") {
            entry.redirected = number != 0;
        } else if (key == "received") {
            entry.received = number;
        } else if (key == "initial-age") {
            entry.initial_age = number;
        } else if (key == "lifetime") {
            entry.lifetime = number;
        } else if (key == "no-cache") {
            entry.no_cache = number != 0;
        } else if (key == "must-revalidate") {
            entry.must_revalidate = number != 0;
        } else if (key == "etag") {
            entry.etag = value;
        } else if (key == "last-modified") {
            entry.last_modified = value;
        }
    }
    if (entry.url != url) {
        return std::nullopt; // damaged, or another URL's (a hash collision)
    }
    entry.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    std::error_code error;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(path, now, error); // recently used
    if (const auto it = files_.find(path); it != files_.end()) {
        it->second.used = now;
    }
    return entry;
}

void HttpCache::store(const CacheEntry& entry, std::uint64_t generation) {
    const std::lock_guard lock(mutex_);
    if (retired_ || generation != generation_.load() || entry.body.size() > limit_ / 8) {
        return;
    }
    load_index();
    const fs::path path = path_for(entry.url);
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            log_warning("net: can't write to the image cache at {}", directory_.string());
            return;
        }
        out << kMagic << '\n'
            << "url " << entry.url << '\n'
            << "redirected " << (entry.redirected ? 1 : 0) << '\n'
            << "received " << entry.received << '\n'
            << "initial-age " << entry.initial_age << '\n'
            << "lifetime " << entry.lifetime << '\n'
            << "no-cache " << (entry.no_cache ? 1 : 0) << '\n'
            << "must-revalidate " << (entry.must_revalidate ? 1 : 0) << '\n'
            << "etag " << entry.etag << '\n'
            << "last-modified " << entry.last_modified << '\n'
            << '\n';
        out.write(reinterpret_cast<const char*>(entry.body.data()), static_cast<std::streamsize>(entry.body.size()));
        if (!out) {
            out.close();
            fs::remove(temporary, error);
            return;
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        return;
    }
    const std::uint64_t size = fs::file_size(path, error);
    if (const auto it = files_.find(path); it != files_.end()) {
        total_ -= it->second.size;
    }
    files_[path] = {size, fs::file_time_type::clock::now()};
    total_ += size;
    trim();
}

void HttpCache::trim() {
    if (total_ <= limit_) {
        return;
    }
    std::vector<std::pair<fs::file_time_type, fs::path>> by_use;
    by_use.reserve(files_.size());
    for (const auto& [path, file] : files_) {
        by_use.emplace_back(file.used, path);
    }
    std::ranges::sort(by_use);
    const std::uint64_t target = limit_ / 10 * 9; // some room, so the next store doesn't trim again
    std::error_code error;
    for (const auto& [used, path] : by_use) {
        if (total_ <= target) {
            break;
        }
        fs::remove(path, error);
        total_ -= files_[path].size;
        files_.erase(path);
    }
}

void HttpCache::clear() {
    const std::lock_guard lock(mutex_);
    ++generation_;
    std::error_code error;
    std::vector<fs::path> buckets;
    for (const auto& bucket : fs::directory_iterator(directory_, error)) {
        if (bucket.is_directory(error) && is_bucket_name(bucket.path().filename().string())) {
            buckets.push_back(bucket.path());
        }
    }
    for (const fs::path& bucket : buckets) {
        std::vector<fs::path> doomed;
        for (const auto& file : fs::directory_iterator(bucket, error)) {
            const std::string name = file.path().filename().string();
            if (is_entry_name(name) || (name.ends_with(".tmp") && is_entry_name(name.substr(0, name.size() - 4)))) {
                doomed.push_back(file.path());
            }
        }
        for (const fs::path& file : doomed) {
            fs::remove(file, error);
        }
        fs::remove(bucket, error); // only if now empty
    }
    files_.clear();
    total_ = 0;
    indexed_ = true;
}

} // namespace elg::detail::net
