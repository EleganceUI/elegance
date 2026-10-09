// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Raster images (design.md §13.3). An image's size is read from its header when it's first
// shown (so layout is right at once); its pixels decode on a background thread and appear when
// ready. Images are shared by source: the same file shown twice decodes once.

#include "view/raster_image.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "view/view_node.h"

#include <elg/error.h>

#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace elg::detail {

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

// A few threads that decode images, so the UI thread never does.
class DecodeQueue {
public:
    static DecodeQueue& instance() {
        static DecodeQueue queue;
        return queue;
    }

    void post(std::function<void()> task) {
        {
            std::lock_guard lock(mutex_);
            tasks_.push_back(std::move(task));
            if (workers_.empty()) {
                const unsigned count = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 3u);
                for (unsigned i = 0; i < count; ++i) {
                    workers_.emplace_back([this](std::stop_token stop) { run(stop); });
                }
            }
        }
        ready_.notify_one();
    }

    ~DecodeQueue() {
        {
            std::lock_guard lock(mutex_);
            tasks_.clear();
        }
        for (std::jthread& worker : workers_) {
            worker.request_stop();
        }
        ready_.notify_all();
    }

private:
    void run(std::stop_token stop) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, stop, [&] { return !tasks_.empty(); });
                if (stop.stop_requested()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::jthread> workers_;
};

// One image source, shared by the views showing it. UI thread only (but for the decode task).
struct ImageAsset {
    std::string name; // for messages: the file name, or "image data"
    ImageInfo info;
    bool failed = false;
    State<std::shared_ptr<const DecodedImage>> pixels{std::shared_ptr<const DecodedImage>()};
};

std::optional<Bytes> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    const std::streamsize size = in.tellg();
    Bytes bytes(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return in ? std::optional(std::move(bytes)) : std::nullopt;
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const std::uint8_t b : bytes) {
        hash = (hash ^ b) * 1099511628211ull;
    }
    return hash ^ bytes.size();
}

// Loads (or finds) the asset for a source: reads and probes it now, decodes in the background.
std::shared_ptr<ImageAsset> load_asset(const fs::path& file, const std::shared_ptr<const Bytes>& data) {
    static std::map<std::string, std::weak_ptr<ImageAsset>> cache;
    std::string key;
    std::error_code error;
    if (data) {
        key = std::format("data:{:016x}", hash_bytes(*data));
    } else {
        const auto time = fs::last_write_time(file, error);
        key = std::format("file:{}:{}", fs::absolute(file, error).string(),
                          error ? 0 : time.time_since_epoch().count());
    }
    if (auto found = cache[key].lock()) {
        return found;
    }
    std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });

    auto asset = std::make_shared<ImageAsset>();
    asset->name = data ? "image data" : file.filename().string();
    std::shared_ptr<const Bytes> bytes = data;
    if (!bytes) {
        std::optional<Bytes> read = read_file(file);
        if (!read) {
            log_warning("Image: can't read \"{}\"", file.string());
            asset->failed = true;
            return asset;
        }
        bytes = std::make_shared<const Bytes>(std::move(*read));
    }
    try {
        asset->info = probe_image(*bytes);
    } catch (const std::exception& failure) {
        log_warning("Image: {}: {}", asset->name, failure.what());
        asset->failed = true;
        return asset;
    }
    cache[key] = asset;

    DecodeQueue::instance().post([weak = std::weak_ptr(asset), bytes, name = asset->name] {
        if (weak.expired()) {
            return; // nobody shows it anymore
        }
        std::shared_ptr<const DecodedImage> decoded;
        try {
            decoded = decode_image(*bytes);
        } catch (const std::exception& failure) {
            log_warning("Image: {}: {}", name, failure.what());
            return;
        }
        post_ui_task([weak, decoded] {
            if (const auto asset = weak.lock()) {
                asset->pixels = decoded;
            }
        });
    });
    return asset;
}

class RasterImageElement final : public Element {
public:
    RasterImageElement(std::shared_ptr<ImageAsset> asset, float scale, bool resizable)
        : asset_(std::move(asset)), scale_(scale > 0 ? scale : 1), resizable_(resizable) {}

    void set_pixels(std::shared_ptr<const DecodedImage> pixels) { pixels_ = std::move(pixels); }

    void inspect(Inspection& out) const override {
        out.name = "Image";
        out.detail = asset_->name;
        out.properties.emplace_back("pixels", std::format("{} × {}", asset_->info.width, asset_->info.height));
        out.properties.emplace_back("state", asset_->failed ? "failed" : pixels_ ? "decoded" : "decoding");
        if (resizable_) {
            out.properties.emplace_back("resizable", "yes");
        }
    }

protected:
    Size measure(ProposedSize proposal) override {
        const Size natural = asset_->failed ? Size{}
                                            : Size{static_cast<float>(asset_->info.width) / scale_,
                                                   static_cast<float>(asset_->info.height) / scale_};
        return resizable_ ? proposal.replacing_unspecified(natural) : natural;
    }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override {
        if (pixels_) {
            list.commands.push_back(ImageCommand{frame(), {0, 0, 1, 1}, pixels_, 1});
        }
    }
    bool hit_impl(Point point, std::vector<Element*>&) override {
        const Rect r = frame();
        return point.x >= r.x && point.x < r.max_x() && point.y >= r.y && point.y < r.max_y();
    }

private:
    std::shared_ptr<ImageAsset> asset_;
    std::shared_ptr<const DecodedImage> pixels_;
    float scale_;
    bool resizable_;
};

} // namespace

class RasterImageNode final : public ViewNode {
public:
    RasterImageNode(fs::path file, std::shared_ptr<const Bytes> data, float scale, bool resizable)
        : file_(std::move(file)), data_(std::move(data)), scale_(scale), resizable_(resizable) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        std::shared_ptr<ImageAsset> asset = load_asset(file_, data_);
        auto element = std::make_unique<RasterImageElement>(asset, scale_, resizable_);
        RasterImageElement* const target = element.get();
        // Pixels arrive (or change) later: draw them then.
        effect([target, asset, needs_frame = context.needs_frame] {
            target->set_pixels(asset->pixels());
            if (needs_frame) {
                *needs_frame = true;
            }
        });
        return element;
    }

    std::shared_ptr<const RasterImageNode> resized() const {
        return std::make_shared<const RasterImageNode>(file_, data_, scale_, true);
    }

private:
    fs::path file_;
    std::shared_ptr<const Bytes> data_;
    float scale_;
    bool resizable_;
};

View raster_image(const ImageSource& source) {
    std::shared_ptr<const Bytes> data;
    if (!source.data.empty()) {
        data = std::make_shared<const Bytes>(source.data.begin(), source.data.end()); // copied (design.md §9.6)
    }
    return ViewAccess::make(std::make_shared<const RasterImageNode>(source.file, std::move(data), source.scale, false));
}

View raster_image(std::shared_ptr<const Bytes> data, float scale) {
    return ViewAccess::make(std::make_shared<const RasterImageNode>(fs::path(), std::move(data), scale, false));
}

} // namespace elg::detail

namespace elg {

View View::resizable() const {
    const auto image = std::dynamic_pointer_cast<const detail::RasterImageNode>(detail::ViewAccess::node(*this));
    if (!image) {
        throw UsageError("resizable() applies to file and data Images, before other modifiers "
                         "(icons size with .font())");
    }
    return detail::ViewAccess::make(image->resized());
}

} // namespace elg
