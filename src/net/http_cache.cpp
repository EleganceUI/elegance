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
#include <optional>
#include <string_view>
#include <utility>

namespace elg::detail::net {

namespace {

namespace fs = std::filesystem;

// 2: files record their body's size, so a truncated one is noticed. 3: files keep the response's
// status, Location and the header fields freshness is computed from, instead of the result.
constexpr std::string_view kMagic = "ELGCACHE 3";
constexpr std::int64_t kHeuristicCap = std::int64_t{24} * 60 * 60; // a Last-Modified guess stays fresh at most a day
// Delta-seconds (max-age, Age) larger than this count as this (RFC 9111 §1.2.2), so ages and
// lifetimes add up without overflowing.
constexpr std::int64_t kMaxDeltaSeconds = std::int64_t{1} << 31;
// Later times received (Unix seconds, past the year 36000) mean a damaged file: ages are computed
// from them without overflowing up to there.
constexpr std::int64_t kMaxReceived = std::int64_t{1} << 40;

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

// A delta-seconds value: a non-negative integer, capped at kMaxDeltaSeconds, also when it's too
// large to parse.
std::optional<std::int64_t> parse_delta_seconds(std::string_view text) {
    text = trim(text);
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc::result_out_of_range && end == text.data() + text.size() && !text.starts_with('-')) {
        return kMaxDeltaSeconds;
    }
    if (error != std::errc() || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return std::min(value, kMaxDeltaSeconds);
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
            out.max_age = parse_delta_seconds(value).value_or(0);
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

// Whether the fields give a lifetime themselves (max-age, Expires), rather than leaving it to a
// heuristic.
bool explicit_freshness(const CacheHeaders& headers, const Directives& directives) {
    return directives.max_age.has_value() || !headers.expires.empty();
}

// Fills in age, lifetime and the directives from the entry's status, fields and time received.
void apply_freshness(CacheEntry& entry) {
    const CacheHeaders& headers = entry.headers;
    const Directives directives = parse_cache_control(headers.cache_control);
    const std::int64_t now = entry.received;
    const std::int64_t date = parse_http_date(headers.date).value_or(now);
    const std::int64_t apparent_age = std::max<std::int64_t>(0, now - date);
    entry.initial_age = std::min(std::max(apparent_age, parse_delta_seconds(headers.age).value_or(0)), kMaxDeltaSeconds);
    entry.permanent = false;
    if (directives.max_age) {
        entry.lifetime = *directives.max_age;
    } else if (!headers.expires.empty()) {
        // An invalid Expires (often "0") means already expired.
        entry.lifetime = std::max<std::int64_t>(0, parse_http_date(headers.expires).value_or(0) - date);
    } else if (entry.redirect()) {
        entry.lifetime = 0; // no heuristic for redirects
        entry.permanent = entry.status == 301 || entry.status == 308;
    } else if (const auto modified = parse_http_date(headers.last_modified); modified && *modified < date) {
        entry.lifetime = std::min((date - *modified) / 10, kHeuristicCap);
    } else {
        entry.lifetime = 0;
    }
    entry.lifetime = std::min(entry.lifetime, kMaxDeltaSeconds); // a far-off Expires
    entry.no_cache = directives.no_cache;
    entry.must_revalidate = directives.must_revalidate;
}

// Whether `text` can be a value in a cache file, whose lines end with LF: no CR, LF or NUL.
bool writable(std::string_view text) {
    return text.find_first_of(std::string_view("\r\n\0", 3)) == std::string_view::npos;
}

bool writable(const CacheHeaders& headers) {
    return writable(headers.cache_control) && writable(headers.expires) && writable(headers.date) &&
           writable(headers.age) && writable(headers.etag) && writable(headers.last_modified) &&
           writable(headers.vary);
}

bool writable(const CacheEntry& entry) {
    return writable(entry.url) && writable(entry.location) && writable(entry.headers);
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
    const std::string name = path.stem().string();
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
    std::string_view day_text;
    std::string_view month_text;
    std::string_view year_text;
    std::string_view time_text;
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

bool is_redirect_status(unsigned status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool forbids_storing(const CacheHeaders& headers) {
    return parse_cache_control(headers.cache_control).no_store;
}

std::optional<CacheEntry> make_cache_entry(std::string url, unsigned status, std::string location,
                                           const CacheHeaders& headers, std::vector<std::uint8_t> body,
                                           std::int64_t now) {
    const Directives directives = parse_cache_control(headers.cache_control);
    const bool redirect = is_redirect_status(status);
    if ((status != 200 && !redirect) || (redirect && location.empty()) || directives.no_store ||
        trim(headers.vary) == "*") {
        return std::nullopt;
    }
    if (redirect && !explicit_freshness(headers, directives) && status != 301 && status != 308) {
        return std::nullopt; // a temporary redirect with no lifetime of its own
    }
    CacheEntry entry;
    entry.url = std::move(url);
    entry.status = status;
    entry.location = std::move(location);
    entry.headers = headers;
    entry.received = now;
    if (!writable(entry)) {
        return std::nullopt;
    }
    apply_freshness(entry);
    if (!redirect && !entry.fresh(now) && !entry.revalidates()) {
        return std::nullopt; // it would never be used
    }
    if (!redirect) {
        entry.body = std::move(body);
    }
    return entry;
}

void refresh_cache_entry(CacheEntry& entry, const CacheHeaders& headers, std::int64_t now) {
    // A 304 may leave out the fields it doesn't change (RFC 9111 §4.3.4): those it has replace the
    // stored ones, unless they can't be written to the cache file.
    const auto update = [](std::string& stored, const std::string& value) {
        if (!value.empty() && writable(value)) {
            stored = value;
        }
    };
    update(entry.headers.cache_control, headers.cache_control);
    update(entry.headers.expires, headers.expires);
    update(entry.headers.date, headers.date);
    update(entry.headers.age, headers.age);
    update(entry.headers.etag, headers.etag);
    update(entry.headers.last_modified, headers.last_modified);
    update(entry.headers.vary, headers.vary);
    entry.received = now;
    apply_freshness(entry);
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

std::optional<CacheEntry> read_cache_entry(std::istream& in, const std::string& url) {
    CacheEntry entry;
    std::optional<std::int64_t> size;
    std::string line;
    if (!std::getline(in, line) || line != kMagic) {
        return std::nullopt;
    }
    std::optional<std::int64_t> status;
    std::optional<std::int64_t> received;
    while (std::getline(in, line) && !line.empty()) {
        const std::size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        std::string value = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (key == "url") {
            entry.url = std::move(value);
        } else if (key == "status") {
            status = parse_number(value);
        } else if (key == "location") {
            entry.location = std::move(value);
        } else if (key == "received") {
            received = parse_number(value);
        } else if (key == "cache-control") {
            entry.headers.cache_control = std::move(value);
        } else if (key == "expires") {
            entry.headers.expires = std::move(value);
        } else if (key == "date") {
            entry.headers.date = std::move(value);
        } else if (key == "age") {
            entry.headers.age = std::move(value);
        } else if (key == "etag") {
            entry.headers.etag = std::move(value);
        } else if (key == "last-modified") {
            entry.headers.last_modified = std::move(value);
        } else if (key == "vary") {
            entry.headers.vary = std::move(value);
        } else if (key == "size") {
            size = parse_number(value);
        }
    }
    if (entry.url != url || !size || !status || !received) {
        return std::nullopt; // or another URL's: a SHA-256 collision
    }
    entry.status = static_cast<unsigned>(std::min<std::int64_t>(*status, 999));
    if ((entry.status != 200 && !is_redirect_status(entry.status)) || (entry.redirect() && entry.location.empty()) ||
        *received > kMaxReceived) {
        return std::nullopt;
    }
    entry.received = *received;
    apply_freshness(entry);
    entry.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (std::cmp_not_equal(entry.body.size(), *size)) {
        return std::nullopt; // truncated
    }
    return entry;
}

std::optional<CacheEntry> HttpCache::find(const std::string& url) {
    const std::lock_guard lock(mutex_);
    load_index();
    const fs::path path = path_for(url);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::optional<CacheEntry> entry = read_cache_entry(in, url);
    if (!entry) {
        in.close();
        discard(path); // a miss, and gone
        return std::nullopt;
    }
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
    if (retired_ || generation != generation_.load() || entry.body.size() > limit_ / 8 || !writable(entry)) {
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
        const CacheHeaders& headers = entry.headers;
        out << kMagic << '\n'
            << "url " << entry.url << '\n'
            << "status " << entry.status << '\n'
            << "location " << entry.location << '\n'
            << "received " << entry.received << '\n'
            << "cache-control " << headers.cache_control << '\n'
            << "expires " << headers.expires << '\n'
            << "date " << headers.date << '\n'
            << "age " << headers.age << '\n'
            << "etag " << headers.etag << '\n'
            << "last-modified " << headers.last_modified << '\n'
            << "vary " << headers.vary << '\n'
            << "size " << entry.body.size() << '\n'
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

void HttpCache::remove(const std::string& url) {
    const std::lock_guard lock(mutex_);
    load_index();
    discard(path_for(url));
}

void HttpCache::discard(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
    if (const auto it = files_.find(path); it != files_.end()) {
        total_ -= it->second.size;
        files_.erase(it);
    }
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
