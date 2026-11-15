// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_HTTP_CACHE_H
#define ELG_NET_HTTP_CACHE_H

// AsyncImage's disk cache (design.md §13.3): a private HTTP cache after RFC 9111. Responses (200s,
// and redirects) are kept under the URL that returned them while Cache-Control (or Expires, or a
// Last-Modified heuristic) says they're fresh, and revalidated with If-None-Match /
// If-Modified-Since after. One file per URL, named by its SHA-256, with the header fields that
// freshness and revalidation read; the least recently used go first past the size limit.
// Blocking: call off the network thread.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::net {

// The response header fields caching looks at, and keeps.
struct CacheHeaders {
    std::string cache_control;
    std::string expires;
    std::string date;
    std::string age;
    std::string etag;
    std::string last_modified;
    std::string vary;
};

struct CacheEntry {
    std::string url;           // the URL that returned it
    unsigned status = 200;     // 200, or a redirect's 301, 302, 303, 307 or 308
    std::string location;      // a redirect's Location, as the server sent it
    CacheHeaders headers;      // as stored: what freshness is computed from
    std::int64_t received = 0; // Unix seconds
    // From `headers` and `received`:
    std::int64_t initial_age = 0; // its age when received (Age, or the Date's lag)
    std::int64_t lifetime = 0;    // fresh for this many seconds
    bool permanent = false;       // a 301 or 308 without max-age or Expires: fresh until it's evicted
    bool no_cache = false;        // revalidated before every use
    bool must_revalidate = false; // never used stale, not even offline
    std::vector<std::uint8_t> body; // a 200's

    [[nodiscard]] bool redirect() const { return status != 200; }
    [[nodiscard]] bool fresh(std::int64_t now) const {
        return !no_cache && (permanent || initial_age + (now - received) < lifetime);
    }
    [[nodiscard]] bool revalidates() const { return !headers.etag.empty() || !headers.last_modified.empty(); }
    // Offline, a stale copy beats none, unless the server said otherwise.
    [[nodiscard]] bool usable_stale() const { return !no_cache && !must_revalidate; }
};

// Whether `status` is a redirect the cache can keep: 301, 302, 303, 307 or 308.
bool is_redirect_status(unsigned status);
// What a 200 response (with its body) or a redirect (to `location`) would be cached as, if it may
// be: no `no-store`, no `Vary: *`, nothing a cache file can't hold (CR, LF, NUL), and
//  - a 200: fresh for a while, or revalidatable;
//  - a redirect: with max-age or Expires, or a 301 or 308, which without them is fresh until it's
//    evicted.
std::optional<CacheEntry> make_cache_entry(std::string url, unsigned status, std::string location,
                                           const CacheHeaders& headers, std::vector<std::uint8_t> body,
                                           std::int64_t now);
// A 304 validated `entry`: each field the 304 carries replaces the entry's (but for values a cache
// file can't hold), the others stay, and freshness is computed again from the result, as for a
// response received `now`.
void refresh_cache_entry(CacheEntry& entry, const CacheHeaders& headers, std::int64_t now);
// Whether these fields say not to store the response: `no-store` in Cache-Control, or a `*`
// member in Vary.
bool forbids_storing(const CacheHeaders& headers);
// The entry a cache file's contents hold for `url`, or nullopt if they're damaged, cut short, of
// another version of the format, or another URL's.
std::optional<CacheEntry> read_cache_entry(std::istream& in, const std::string& url);
// An HTTP date ("Sun, 06 Nov 1994 08:49:37 GMT", or the RFC 850 and asctime forms) in Unix
// seconds.
std::optional<std::int64_t> parse_http_date(std::string_view text);
std::int64_t unix_now();

class HttpCache {
public:
    HttpCache(std::filesystem::path directory, std::uint64_t limit);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    std::optional<CacheEntry> find(const std::string& url);
    // Skipped if the cache was cleared (or retired) since `generation`.
    void store(const CacheEntry& entry, std::uint64_t generation);
    // Forgets the entry for `url`, if there's one.
    void remove(const std::string& url);
    // Removes every entry (only the cache's own files), and makes downloads in flight skip storing.
    void clear();
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_.load(); }
    // Replaced (set_image_cache): stores nothing more.
    void retire() noexcept { ++generation_; retired_ = true; }

    // The default place: <the user's cache folder>/Elegance/<this executable's name>/Images.
    static std::optional<std::filesystem::path> default_directory();

private:
    struct File {
        std::uint64_t size = 0;
        std::filesystem::file_time_type used;
    };
    [[nodiscard]] std::filesystem::path path_for(const std::string& url) const;
    void load_index();
    // With the mutex held: removes a file, and forgets it.
    void discard(const std::filesystem::path& path);
    void trim();

    std::filesystem::path directory_;
    std::uint64_t limit_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> retired_{false};
    std::mutex mutex_;
    bool indexed_ = false;
    std::map<std::filesystem::path, File> files_;
    std::uint64_t total_ = 0;
};

} // namespace elg::detail::net

#endif // ELG_NET_HTTP_CACHE_H
