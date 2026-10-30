// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras on Linux (design.md §13.3): Video4Linux2, streaming through memory-mapped buffers on a
// capture thread. Cameras are /dev/video* nodes that capture video (others are metadata or
// output nodes). Frames stay in the camera's format (NV12, YUYV, YUV420) for the renderer's YUV
// pass; MJPEG, which many USB cameras need for larger sizes, is decoded to BGRA here. Access is
// the device node's permissions (usually the "video" group).

#include "media/camera_source.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/image_decode.h"

#include <linux/videodev2.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace elg::detail {

namespace {

// ioctl, retried when a signal interrupts it.
int xioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string errno_text() {
    return std::strerror(errno);
}

// Owns a file descriptor.
class FileHandle {
public:
    explicit FileHandle(int fd = -1) noexcept : fd_(fd) {}
    ~FileHandle() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    FileHandle(FileHandle&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    FileHandle& operator=(FileHandle&& other) noexcept {
        std::swap(fd_, other.fd_);
        return *this;
    }
    int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

private:
    int fd_;
};

// Whether `fd` is a video capture node (and not, say, a camera's metadata node).
std::optional<v4l2_capability> capture_capability(int fd) {
    v4l2_capability capability{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &capability) == -1) {
        return std::nullopt;
    }
    const std::uint32_t caps =
        (capability.capabilities & V4L2_CAP_DEVICE_CAPS) ? capability.device_caps : capability.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        return std::nullopt;
    }
    return capability;
}

// /dev/video0, /dev/video1, ... in number order.
std::vector<std::filesystem::path> video_nodes() {
    std::vector<std::pair<int, std::filesystem::path>> nodes;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("video") && name.size() > 5 &&
            std::all_of(name.begin() + 5, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            nodes.emplace_back(std::stoi(name.substr(5)), entry.path());
        }
    }
    std::ranges::sort(nodes);
    std::vector<std::filesystem::path> paths;
    for (auto& [number, path] : nodes) {
        paths.push_back(std::move(path));
    }
    return paths;
}

// The formats we take, best first: what the renderer converts as is, then MJPEG.
constexpr std::uint32_t kFormats[] = {V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MJPEG};

int format_rank(std::uint32_t format) {
    const auto found = std::ranges::find(kFormats, format);
    return found == std::end(kFormats) ? -1 : static_cast<int>(std::end(kFormats) - found);
}

// One camera, captured on its own thread; frames reach the GPU through update().
class V4L2Camera final : public CameraSource {
public:
    explicit V4L2Camera(CameraOptions options) : options_(std::move(options)) {}

    ~V4L2Camera() override { stop_capture(); }

    // --- UI thread ---

    void play() override {
        if (thread_.joinable()) {
            return;
        }
        error = std::string();
        playing = true;
        playing_now_ = true;
        thread_ = std::jthread([this, weak = std::weak_ptr<VideoSource>(shared_from_this())](std::stop_token stop) {
            capture(stop, weak);
        });
    }

    void pause() override {
        stop_capture();
        playing = false;
    }

    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    std::optional<std::vector<unsigned char>> snapshot() const override {
        std::optional<VideoFrame> frame;
        {
            std::lock_guard lock(mutex_);
            frame = current_;
        }
        if (!frame || !frame->pixels) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> bgra =
            frame_to_bgra(frame->format, frame->matrix, frame->full_range, *frame->pixels, frame->width, frame->height);
        if (bgra.empty()) {
            return std::nullopt;
        }
        return encode_png(bgra, frame->width, frame->height);
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(mutex_);
        if (frame_number_ == reported_) {
            return false;
        }
        reported_ = frame_number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        return current_;
    }

private:
    struct Mapping {
        void* data = MAP_FAILED;
        std::size_t length = 0;
    };

    void stop_capture() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        playing_now_ = false;
    }

    // Capture thread: opens the camera, streams until `stop`, and closes it.
    void capture(const std::stop_token& stop, const std::weak_ptr<VideoSource>& weak) {
        const std::string path = options_.device_id.empty() ? first_camera() : options_.device_id;
        if (path.empty()) {
            fail(weak, "no camera was found");
            return;
        }
        FileHandle fd(open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        if (!fd) {
            fail(weak, errno == EACCES ? std::format("no permission to use the camera {} (is the user in the video group?)", path)
                                       : std::format("can't open the camera {}: {}", path, errno_text()));
            return;
        }
        if (!capture_capability(fd.get())) {
            fail(weak, std::format("{} isn't a camera", path));
            return;
        }
        const std::optional<v4l2_format> format = choose_format(fd.get());
        if (!format) {
            fail(weak, std::format("the camera {} has no format we can show (NV12, YUYV, YUV420 or MJPEG)", path));
            return;
        }
        const std::uint32_t width = format->fmt.pix.width;
        const std::uint32_t height = format->fmt.pix.height;
        const std::uint32_t pixel_format = format->fmt.pix.pixelformat;
        const std::uint32_t stride = format->fmt.pix.bytesperline;

        v4l2_requestbuffers request{};
        request.count = 4;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd.get(), VIDIOC_REQBUFS, &request) == -1 || request.count < 2) {
            fail(weak, std::format("the camera {} can't stream: {}", path, errno_text()));
            return;
        }
        std::vector<Mapping> mappings(request.count);
        const auto unmap = [&] {
            for (Mapping& mapping : mappings) {
                if (mapping.data != MAP_FAILED) {
                    munmap(mapping.data, mapping.length);
                }
            }
        };
        for (std::uint32_t i = 0; i < request.count; ++i) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;
            if (xioctl(fd.get(), VIDIOC_QUERYBUF, &buffer) == -1) {
                unmap();
                fail(weak, std::format("the camera {} can't stream: {}", path, errno_text()));
                return;
            }
            mappings[i].length = buffer.length;
            mappings[i].data = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), buffer.m.offset);
            if (mappings[i].data == MAP_FAILED || xioctl(fd.get(), VIDIOC_QBUF, &buffer) == -1) {
                unmap();
                fail(weak, std::format("the camera {} can't stream: {}", path, errno_text()));
                return;
            }
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd.get(), VIDIOC_STREAMON, &type) == -1) {
            unmap();
            fail(weak, std::format("the camera {} won't start: {}", path, errno_text()));
            return;
        }
        log_info("camera: {} at {}x{} ({})", path, width, height,
                 std::string(reinterpret_cast<const char*>(&pixel_format), 4)); // the FourCC

        while (!stop.stop_requested()) {
            pollfd ready{fd.get(), POLLIN, 0};
            const int polled = poll(&ready, 1, 100); // wakes to check `stop`
            if (polled == 0 || (polled == -1 && errno == EINTR)) {
                continue;
            }
            if (polled == -1 || (ready.revents & (POLLERR | POLLHUP))) {
                fail(weak, std::format("the camera {} stopped (disconnected?)", path));
                break;
            }
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (xioctl(fd.get(), VIDIOC_DQBUF, &buffer) == -1) {
                if (errno == EAGAIN) {
                    continue;
                }
                fail(weak, std::format("the camera {} stopped: {}", path, errno_text()));
                break;
            }
            if (!(buffer.flags & V4L2_BUF_FLAG_ERROR) && buffer.index < mappings.size()) {
                const auto* data = static_cast<const std::uint8_t*>(mappings[buffer.index].data);
                deliver(weak, pixel_format, std::span(data, std::min<std::size_t>(buffer.bytesused, mappings[buffer.index].length)),
                        width, height, stride);
            }
            if (xioctl(fd.get(), VIDIOC_QBUF, &buffer) == -1) {
                fail(weak, std::format("the camera {} stopped: {}", path, errno_text()));
                break;
            }
        }
        xioctl(fd.get(), VIDIOC_STREAMOFF, &type);
        unmap();
    }

    static std::string first_camera() {
        for (const auto& node : video_nodes()) {
            FileHandle fd(open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
            if (fd && capture_capability(fd.get())) {
                return node.string();
            }
        }
        return {};
    }

    // The supported size closest to the requested one (by area, then aspect), in the best format
    // that has it; set on the device. nullopt if it has none of kFormats.
    std::optional<v4l2_format> choose_format(int fd) const {
        struct Candidate {
            std::uint32_t pixel_format = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            double score = std::numeric_limits<double>::max();
        } best;
        const double wanted_area = static_cast<double>(options_.width) * options_.height;
        const double wanted_aspect = options_.height ? static_cast<double>(options_.width) / options_.height : 16.0 / 9;
        const auto consider = [&](std::uint32_t pixel_format, std::uint32_t width, std::uint32_t height) {
            if (width == 0 || height == 0) {
                return;
            }
            const double area = static_cast<double>(width) * height;
            const double aspect = static_cast<double>(width) / height;
            // Ties in size go to the better format.
            const double score = std::abs(std::log(area / wanted_area)) + 2 * std::abs(std::log(aspect / wanted_aspect)) -
                                 0.01 * format_rank(pixel_format);
            if (score < best.score) {
                best = {pixel_format, width, height, score};
            }
        };
        v4l2_fmtdesc description{};
        description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        for (description.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &description) == 0; ++description.index) {
            if (format_rank(description.pixelformat) < 0) {
                continue;
            }
            v4l2_frmsizeenum size{};
            size.pixel_format = description.pixelformat;
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0 && size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                // Stepwise or continuous: the requested size, clamped to the range.
                consider(description.pixelformat,
                         std::clamp(options_.width, size.stepwise.min_width, size.stepwise.max_width),
                         std::clamp(options_.height, size.stepwise.min_height, size.stepwise.max_height));
                continue;
            }
            for (size.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
                consider(description.pixelformat, size.discrete.width, size.discrete.height);
            }
        }
        if (best.pixel_format == 0) {
            return std::nullopt;
        }
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = best.width;
        format.fmt.pix.height = best.height;
        format.fmt.pix.pixelformat = best.pixel_format;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(fd, VIDIOC_S_FMT, &format) == -1 || format_rank(format.fmt.pix.pixelformat) < 0) {
            return std::nullopt;
        }
        return format; // the driver may have adjusted the size
    }

    // Capture thread: one frame from the camera, packed for the renderer.
    void deliver(const std::weak_ptr<VideoSource>& weak, std::uint32_t pixel_format, std::span<const std::uint8_t> data,
                 std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::cpu;
        frame.width = width;
        frame.height = height;
        frame.matrix = VideoFrame::Matrix::bt601; // webcams
        frame.source_id = VideoSource::id;
        frame.frame_id = next_media_id();
        if (pixel_format == V4L2_PIX_FMT_MJPEG) {
            std::shared_ptr<const DecodedImage> image;
            try {
                image = decode_image(data);
            } catch (const std::exception&) {
                return; // a corrupt frame now and then is normal
            }
            auto pixels = std::make_shared<std::vector<std::uint8_t>>(image->rgba);
            for (std::size_t i = 0; i + 3 < pixels->size(); i += 4) {
                std::swap((*pixels)[i], (*pixels)[i + 2]);
            }
            frame.width = image->width;
            frame.height = image->height;
            frame.format = VideoFrame::Format::bgra;
            frame.pixels = std::move(pixels);
        } else {
            frame.format = pixel_format == V4L2_PIX_FMT_NV12   ? VideoFrame::Format::nv12
                           : pixel_format == V4L2_PIX_FMT_YUYV ? VideoFrame::Format::yuy2
                                                               : VideoFrame::Format::i420;
            const std::size_t size = packed_size(frame.format, width, height);
            const std::size_t row_bytes = frame.format == VideoFrame::Format::yuy2 ? width * 2 : width;
            if (stride == row_bytes && data.size() >= size) {
                frame.pixels = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.begin() + size);
            } else {
                auto pixels = std::make_shared<std::vector<std::uint8_t>>(size);
                if (!repack(frame.format, data, width, height, stride, *pixels)) {
                    return;
                }
                frame.pixels = std::move(pixels);
            }
        }
        const Size frame_size{static_cast<float>(frame.width), static_cast<float>(frame.height)};
        bool resized = false;
        {
            std::lock_guard lock(mutex_);
            resized = !current_ || current_->width != frame.width || current_->height != frame.height;
            current_ = std::move(frame);
            ++frame_number_;
        }
        if (resized) {
            post_ui_task([weak, frame_size] {
                if (const auto source = weak.lock()) {
                    source->size = frame_size;
                    source->ready = true;
                }
            });
        }
    }

    // Drops row padding (`stride` bytes a luma row; chroma rows are half as long in planar formats).
    static bool repack(VideoFrame::Format format, std::span<const std::uint8_t> data, std::uint32_t width,
                       std::uint32_t height, std::uint32_t stride, std::vector<std::uint8_t>& out) {
        const std::size_t chroma_w = (width + 1) / 2;
        const std::size_t chroma_h = (height + 1) / 2;
        struct Plane {
            std::size_t rows, row_bytes, stride;
        };
        std::vector<Plane> planes;
        switch (format) {
        case VideoFrame::Format::yuy2: planes = {{height, width * 2u, stride}}; break;
        case VideoFrame::Format::nv12: planes = {{height, width, stride}, {chroma_h, chroma_w * 2, stride}}; break;
        default: planes = {{height, width, stride}, {chroma_h, chroma_w, stride / 2}, {chroma_h, chroma_w, stride / 2}}; break;
        }
        std::size_t in = 0;
        std::size_t at = 0;
        for (const Plane& plane : planes) {
            // The last row may end without its padding.
            if (plane.stride < plane.row_bytes || in + (plane.rows - 1) * plane.stride + plane.row_bytes > data.size()) {
                return false;
            }
            for (std::size_t row = 0; row < plane.rows; ++row) {
                std::memcpy(out.data() + at, data.data() + in + row * plane.stride, plane.row_bytes);
                at += plane.row_bytes;
            }
            in += plane.rows * plane.stride;
        }
        return true;
    }

    // Capture thread: the error the view shows; capturing stops.
    void fail(const std::weak_ptr<VideoSource>& weak, std::string message) {
        playing_now_ = false;
        log_warning("camera: {}", message);
        post_ui_task([weak, message = std::move(message)] {
            if (const auto source = weak.lock()) {
                source->error = message;
                source->playing = false;
            }
        });
    }

    const CameraOptions options_;
    std::jthread thread_;

    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
    std::uint64_t frame_number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace

std::vector<CameraDevice> platform_camera_devices() {
    std::vector<CameraDevice> devices;
    for (const auto& node : video_nodes()) {
        FileHandle fd(open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        if (!fd) {
            continue;
        }
        if (const auto capability = capture_capability(fd.get())) {
            devices.push_back({node.string(), reinterpret_cast<const char*>(capability->card)});
        }
    }
    return devices;
}

std::shared_ptr<CameraSource> create_platform_camera(CameraOptions options) {
    return std::make_shared<V4L2Camera>(std::move(options));
}

} // namespace elg::detail
