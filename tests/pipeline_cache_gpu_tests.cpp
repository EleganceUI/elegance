// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The pipeline cache with the WebGPU implementation (ELGSPEC-0036.R12): a device made twice on one folder,
// which needs a GPU, so it is hidden from a run that has none.

#include "gpu/device.h"
#include "gpu/instance.h"
#include "gpu/pipeline_cache.h"
#include "gpu/quad_renderer.h"
#include "gpu/texture_cache.h"
#include "gpu/webgpu.h"

#include <catch2/catch_test_macros.hpp>

#include <webgpu/webgpu_cpp.h>

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>

using namespace elg::detail::gpu;
namespace fs = std::filesystem;

namespace {

// A folder that is removed, with what is in it, at the end of the test.
class Folder {
public:
    Folder() {
        std::random_device random;
        path_ = fs::temp_directory_path() / ("elegance-pipeline-cache-gpu-" + std::to_string(random()));
    }
    ~Folder() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// A run: a cache on `folder`, a device that uses it, and the shaders and pipelines that the renderer makes
// first. What the cache did is returned once everything it was offered is written.
PipelineCacheStats run(const fs::path& folder) {
    PipelineCacheSettings settings;
    settings.directory = folder;
    const auto cache = std::make_shared<PipelineCache>(settings);
    {
        const Instance instance;
        const Device device(instance, cache);
        const auto layout = create_image_layout(device.handle());
        const TextureCache textures(device, layout);
        const QuadRenderer quads(device, layout);
        device.wait_idle();
    }
    cache->flush();
    return cache->stats();
}

} // namespace

TEST_CASE("a second run finds what the first stored, and stores nothing", "[.gpu][pipeline-cache]") {
    const Folder folder;

    const PipelineCacheStats first = run(folder.path());
    // The implementation asked for what it needed, and nothing was there yet.
    CHECK(first.asked > 0);
    CHECK(first.found == 0);
    CHECK(first.missing == first.asked);
    CHECK(first.stored > 0);
    CHECK(first.write_failures == 0);

    const PipelineCacheStats second = run(folder.path());
    CHECK(second.asked > 0);
    CHECK(second.found > 0);
    CHECK(second.missing == 0);
    CHECK(second.stored == 0);
    CHECK(second.damaged == 0);
}
