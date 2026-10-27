// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AsyncImage (design.md §13.3): fetched when first shown (lowered), cancelled when it goes. The
// phase's view is the single item of a ForEach keyed by the phase, so the placeholder fades into
// the image like any other change.

#include <elg/image.h>

#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "net/fetch.h"
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
};

struct Loader {
    std::string url;
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
        task = net::fetch(url, [weak = std::weak_ptr(self)](net::FetchResult result) {
            post_ui_task([weak, result = std::move(result)] { finish(weak, result); });
        });
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
        DownloadCache::instance().store(loader->url, data);
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
    AsyncImageNode(std::string url, std::function<View(const AsyncImagePhase&)> content, float scale)
        : url_(std::move(url)), content_(std::move(content)), scale_(scale) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto loader = std::make_shared<Loader>();
        loader->url = url_;
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
};

} // namespace

} // namespace elg::detail

namespace elg {

View AsyncImage(std::string url, std::function<View(const AsyncImagePhase&)> content, float scale) {
    if (!content) {
        throw UsageError("AsyncImage: the content function is empty");
    }
    return detail::ViewAccess::make(std::make_shared<const detail::AsyncImageNode>(std::move(url), std::move(content), scale));
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
        options.scale);
}

} // namespace elg
