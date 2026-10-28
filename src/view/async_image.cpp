// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AsyncImage (design.md §13.3): fetched when first shown (lowered), cancelled when it goes. The
// phase's view is the single item of a ForEach keyed by the phase, so the placeholder fades into
// the image like any other change. Downloads are kept in memory for the session and on disk
// (net/http_cache.h) for as long as their server allows.

#include <elg/image.h>

#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "net/fetch.h"
#include "net/http_cache.h"
#include "platform/platform.h"
#include "view/raster_image.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/shapes.h>
#include <elg/theme.h>
#include <elg/views.h>

#include <format>
#include <list>
#include <map>

namespace elg::detail {

namespace {

using Bytes = std::vector<std::uint8_t>;

// Downloaded images by URL, kept within a budget (least recently used out). UI thread.
class DownloadCache {
public:
    static DownloadCache& instance() {
        static DownloadCache cache;
        return cache;
    }

    // Bumped by clear(): downloads started before store nothing.
    std::uint64_t generation() const noexcept { return generation_; }
    void clear() {
        entries_.clear();
        order_.clear();
        bytes_ = 0;
        ++generation_;
    }

    std::shared_ptr<const Bytes> find(const std::string& url) {
        const auto it = entries_.find(url);
        if (it == entries_.end()) {
            return nullptr;
        }
        order_.splice(order_.begin(), order_, it->second.position);
        return it->second.bytes;
    }

    void store(const std::string& url, std::shared_ptr<const Bytes> bytes) {
        if (entries_.contains(url)) {
            return;
        }
        bytes_ += bytes->size();
        order_.push_front(url);
        entries_[url] = {std::move(bytes), order_.begin()};
        while (bytes_ > kBudget && order_.size() > 1) {
            const auto oldest = entries_.find(order_.back());
            bytes_ -= oldest->second.bytes->size();
            entries_.erase(oldest);
            order_.pop_back();
        }
    }

private:
    static constexpr std::size_t kBudget = 64ull << 20;
    struct Entry {
        std::shared_ptr<const Bytes> bytes;
        std::list<std::string>::iterator position;
    };
    std::map<std::string, Entry> entries_;
    std::list<std::string> order_; // most recent first
    std::size_t bytes_ = 0;
    std::uint64_t generation_ = 0;
};

// The disk cache set_image_cache() describes, made when first needed. UI thread.
class DiskCache {
public:
    static DiskCache& instance() {
        static DiskCache cache;
        return cache;
    }

    std::shared_ptr<net::HttpCache> get() {
        if (!made_) {
            made_ = true;
            make();
        }
        return cache_;
    }

    void configure(ImageCacheOptions options) {
        options_ = std::move(options);
        if (made_) {
            make();
        }
    }

    void clear() {
        if (const std::shared_ptr<net::HttpCache> cache = get()) {
            cache->clear();
        }
    }

private:
    void make() {
        if (cache_) {
            cache_->retire();
        }
        cache_.reset();
        const std::optional<std::string> setting = platform::environment_variable("ELG_IMAGE_CACHE");
        if (!options_.disk || (setting && *setting == "off")) {
            return;
        }
        std::optional<std::filesystem::path> directory =
            options_.directory.empty() ? net::HttpCache::default_directory() : std::optional(options_.directory);
        if (directory) {
            cache_ = std::make_shared<net::HttpCache>(std::move(*directory), options_.disk_limit);
        }
    }

    ImageCacheOptions options_;
    bool made_ = false;
    std::shared_ptr<net::HttpCache> cache_;
};

struct Loader {
    std::string url;
    bool disk_cache = true;
    std::uint64_t generation = 0; // the memory cache's, when it started
    net::FetchTask task;
    State<int> phase{0}; // AsyncImagePhase::Kind
    std::shared_ptr<const Bytes> bytes;
    std::string error;

    ~Loader() { task.cancel(); }

    void start(const std::shared_ptr<Loader>& self) {
        if ((bytes = DownloadCache::instance().find(url))) {
            phase = static_cast<int>(AsyncImagePhase::Kind::success);
            return;
        }
        generation = DownloadCache::instance().generation();
        task = net::fetch(
            url,
            [weak = std::weak_ptr(self)](net::FetchResult result) {
                post_ui_task([weak, result = std::move(result)] { finish(weak, result); });
            },
            {.cache = disk_cache ? DiskCache::instance().get() : nullptr});
    }

    static void finish(const std::weak_ptr<Loader>& weak, const net::FetchResult& result) {
        const auto loader = weak.lock();
        if (!loader) {
            return;
        }
        if (!result) {
            loader->fail(result.error());
            return;
        }
        auto data = std::make_shared<const Bytes>(*result);
        try {
            probe_image(*data); // an error page or HTML isn't an image
        } catch (const std::exception& failure) {
            loader->fail(failure.what());
            return;
        }
        if (loader->generation == DownloadCache::instance().generation()) {
            DownloadCache::instance().store(loader->url, data);
        }
        loader->bytes = std::move(data);
        loader->phase = static_cast<int>(AsyncImagePhase::Kind::success);
    }

    void fail(std::string why) {
        error = std::move(why);
        phase = static_cast<int>(AsyncImagePhase::Kind::failure);
    }
};

class AsyncImageNode final : public ViewNode {
public:
    AsyncImageNode(std::string url, std::function<View(const AsyncImagePhase&)> content, float scale, bool disk_cache)
        : url_(std::move(url)), content_(std::move(content)), scale_(scale), disk_cache_(disk_cache) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto loader = std::make_shared<Loader>();
        loader->url = url_;
        loader->disk_cache = disk_cache_;
        loader->start(loader);
        const View view = ForEach([loader] { return std::vector<int>{loader->phase()}; },
                                  [loader, content = content_, scale = scale_](int kind) {
                                      AsyncImagePhase phase;
                                      phase.kind = static_cast<AsyncImagePhase::Kind>(kind);
                                      if (phase.kind == AsyncImagePhase::Kind::success && loader->bytes) {
                                          phase.image = raster_image(loader->bytes, scale);
                                      }
                                      phase.error = loader->error;
                                      return content(phase);
                                  });
        return detail::lower(view, context);
    }

private:
    std::string url_;
    std::function<View(const AsyncImagePhase&)> content_;
    float scale_;
    bool disk_cache_;
};

} // namespace

} // namespace elg::detail

namespace elg {

View AsyncImage(std::string url, std::function<View(const AsyncImagePhase&)> content, AsyncImageOptions options) {
    if (!content) {
        throw UsageError("AsyncImage: the content function is empty");
    }
    return detail::ViewAccess::make(std::make_shared<const detail::AsyncImageNode>(std::move(url), std::move(content),
                                                                                   options.scale, options.disk_cache));
}

void set_image_cache(ImageCacheOptions options) {
    detail::DiskCache::instance().configure(std::move(options));
}

void clear_image_cache() {
    detail::DownloadCache::instance().clear();
    detail::DiskCache::instance().clear();
}

View AsyncImage(std::string url, AsyncImageOptions options) {
    return AsyncImage(
        std::move(url),
        [mode = options.content_mode](const AsyncImagePhase& phase) -> View {
            switch (phase.kind) {
            case AsyncImagePhase::Kind::loading: return Progress();
            case AsyncImagePhase::Kind::failure:
                return Image("image-broken").foreground_style(ThemeColor::tertiary).tooltip(phase.error);
            case AsyncImagePhase::Kind::success:
                return mode ? phase.image.resizable().aspect_ratio(*mode) : phase.image;
            }
            return View();
        },
        options);
}

} // namespace elg
