// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Bugs of Elegance's that it carries on after, such as WebGPU validation errors: logged at error
// level in every build, and failing the test they happen in (ELGSPEC-0025.R2, R3).

#include "core/log.h"
#include "gpu/device.h"
#include "gpu/instance.h"

#include <catch2/catch_test_macros.hpp>

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_mutex;
std::vector<std::string> g_seen;

void collect(std::string_view message) noexcept {
    try {
        const std::lock_guard lock(g_mutex);
        g_seen.emplace_back(message);
    } catch (...) {
        // Swallowed: an observer is noexcept; the test then finds the message missing.
    }
}

} // namespace

// Run by the ctest test internal_error_fails_its_test, which expects this run to fail: no
// assertion here fails, but the error does.
TEST_CASE("an internal error fails the test it happens in", "[.internal-error]") {
    elg::detail::log_internal_error("an internal error, on purpose");
    CHECK(true);
}

TEST_CASE("a WebGPU validation error is logged at error level and the run goes on", "[.gpu]") {
    using namespace elg::detail;
    const gpu::Instance instance;
    const gpu::Device device(instance);
    {
        const std::lock_guard lock(g_mutex);
        g_seen.clear();
    }
    // This test's own observer, so that the error it makes on purpose doesn't fail it.
    const InternalErrorObserver tests = set_internal_error_observer(&collect);
    wgpu::BufferDescriptor invalid; // mapped both for reading and for writing: not allowed
    invalid.size = 4;
    invalid.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::MapWrite;
    const wgpu::Buffer buffer = device.handle().CreateBuffer(&invalid);
    instance.handle().ProcessEvents();
    set_internal_error_observer(tests);

    std::vector<std::string> seen;
    {
        const std::lock_guard lock(g_mutex);
        seen = g_seen;
    }
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].starts_with("webgpu: "));
    const std::vector<std::string> lines = recent_log_lines();
    CHECK(std::ranges::any_of(lines, [&](const std::string& line) { return line == "[elg:error] " + seen[0]; }));
}
