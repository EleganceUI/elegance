// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_HTTP_CACHE_H
#define ELG_NET_HTTP_CACHE_H

// AsyncImage's disk cache (design.md §13.3): a private HTTP cache after RFC 9111. Responses are
// kept while Cache-Control (or Expires, or a Last-Modified heuristic) says they're fresh, and
// revalidated with If-None-Match / If-Modified-Since after. One file per URL, named by its
// SHA-256; the least recently used go first past the size limit. Blocking: call off the network
// thread.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::net {

// The response headers caching looks at.
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
    std::string url;
    bool redirected = false;      // the body came from another URL (no revalidation then)
    std::int64_t received = 0;    // Unix seconds
    std::int64_t initial_age = 0; // its age when received (Age, or the Date's lag)
    std::int64_t lifetime = 0;    // fresh for this many seconds
    bool no_cache = false;        // revalidated before every use
    bool must_revalidate = false; // never used stale, not even offline
    std::string etag;
    std::string last_modified;
    std::vector<std::uint8_t> body;

    bool fresh(std::int64_t now) const { return !no_cache && initial_age + (now - received) < lifetime; }
    bool revalidates() const { return !redirected && (!etag.empty() || !last_modified.empty()); }
    // Offline, a stale copy beats none, unless the server said otherwise.
    bool usable_stale() const { return !no_cache && !must_revalidate; }
};

// What a 200 response would be cached as, if it may be (no `no-store`, no `Vary: *`, and either
// fresh for a while or revalidatable).
std::optional<CacheEntry> make_cache_entry(std::string url, bool redirected, const CacheHeaders& headers,
                                           std::vector<std::uint8_t> body, std::int64_t now);
// A 304's headers renew an entry's freshness.
void refresh_cache_entry(CacheEntry& entry, const CacheHeaders& headers, std::int64_t now);
// An HTTP date ("Sun, 06 Nov 1994 08:49:37 GMT", or the RFC 850 and asctime forms) in Unix
// seconds.
std::optional<std::int64_t> parse_http_date(std::string_view text);
std::int64_t unix_now();

class HttpCache {
public:
    HttpCache(std::filesystem::path directory, std::uint64_t limit);

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::optional<CacheEntry> find(const std::string& url);
    // Skipped if the cache was cleared (or retired) since `generation`.
    void store(const CacheEntry& entry, std::uint64_t generation);
    // Removes every entry (only the cache's own files), and makes downloads in flight skip storing.
    void clear();
    std::uint64_t generation() const noexcept { return generation_.load(); }
    // Replaced (set_image_cache): stores nothing more.
    void retire() noexcept { ++generation_; retired_ = true; }

    // The default place: <the user's cache folder>/Elegance/<this executable's name>/Images.
    static std::optional<std::filesystem::path> default_directory();

private:
    struct File {
        std::uint64_t size = 0;
        std::filesystem::file_time_type used;
    };
    std::filesystem::path path_for(const std::string& url) const;
    void load_index();
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
