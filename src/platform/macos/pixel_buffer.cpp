// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/macos/pixel_buffer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

namespace elg::detail::macos {

namespace {

std::atomic<std::uint64_t> g_next_frame{1};

VideoFrame::Matrix matrix_of(CVPixelBufferRef buffer) {
    VideoFrame::Matrix result = VideoFrame::Matrix::bt709;
    if (CFTypeRef matrix = CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr)) {
        if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) {
            result = VideoFrame::Matrix::bt601;
        }
        CFRelease(matrix);
    }
    return result;
}

} // namespace

std::optional<VideoFrame> pack_pixel_buffer(CVPixelBufferRef buffer, std::uint64_t source_id) {
    const OSType format = CVPixelBufferGetPixelFormatType(buffer);
    const bool full_range = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    const bool nv12 = full_range || format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    const bool bgra = format == kCVPixelFormatType_32BGRA;
    if ((!nv12 && !bgra) || (nv12 && CVPixelBufferGetPlaneCount(buffer) != 2) ||
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return std::nullopt;
    }
    const auto width = static_cast<std::uint32_t>(CVPixelBufferGetWidth(buffer));
    const auto height = static_cast<std::uint32_t>(CVPixelBufferGetHeight(buffer));
    const VideoFrame::Format packed = nv12 ? VideoFrame::Format::nv12 : VideoFrame::Format::bgra;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(packed_size(packed, width, height));
    std::uint8_t* out = pixels->data();
    const auto copy_rows = [&](const void* base, std::size_t stride, std::size_t rows, std::size_t row_bytes) {
        const auto* in = static_cast<const std::uint8_t*>(base);
        for (std::size_t row = 0; row < rows; ++row) {
            std::memcpy(out + row * row_bytes, in + row * stride, std::min(row_bytes, stride));
        }
        out += rows * row_bytes;
    };
    if (nv12) {
        const std::size_t chroma_rows = (height + 1) / 2;
        const std::size_t chroma_row_bytes = 2 * static_cast<std::size_t>((width + 1) / 2);
        const struct {
            std::size_t rows;
            std::size_t row_bytes;
        } planes[2] = {{height, width}, {chroma_rows, chroma_row_bytes}};
        for (std::size_t plane = 0; plane < 2; ++plane) {
            const std::size_t rows = std::min(planes[plane].rows, CVPixelBufferGetHeightOfPlane(buffer, plane));
            copy_rows(CVPixelBufferGetBaseAddressOfPlane(buffer, plane), CVPixelBufferGetBytesPerRowOfPlane(buffer, plane),
                      rows, planes[plane].row_bytes);
            out += (planes[plane].rows - rows) * planes[plane].row_bytes;
        }
    } else {
        copy_rows(CVPixelBufferGetBaseAddress(buffer), CVPixelBufferGetBytesPerRow(buffer), height,
                  static_cast<std::size_t>(width) * 4);
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

    VideoFrame frame;
    frame.kind = VideoFrame::Kind::cpu;
    frame.width = width;
    frame.height = height;
    frame.format = packed;
    frame.full_range = full_range;
    frame.matrix = matrix_of(buffer);
    frame.pixels = std::move(pixels);
    frame.source_id = source_id;
    frame.frame_id = g_next_frame.fetch_add(1);
    return frame;
}

} // namespace elg::detail::macos
