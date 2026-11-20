// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// tracy-inspect: lists what a Tracy capture holds, for tools/tracy-check.py (ELGSPEC-0042.R12).
// One line each, its fields separated by tabs:
//
//   zone    <name> <count>
//   frames  <name> <count>    "Frames" is the default frame set
//   thread  <name>
//   plot    <name> <count>
//   samples <count>           call stacks sampled (none when the OS doesn't allow sampling)
//
// Built against Tracy's server, at the version of the client Elegance links (CMakeLists.txt here).

#include "TracyFileRead.hpp"
#include "TracyWorker.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: tracy-inspect <capture.tracy>\n");
        return 2;
    }
    const std::unique_ptr<tracy::FileRead> file(tracy::FileRead::Open(argv[1]));
    if (!file) {
        std::fprintf(stderr, "tracy-inspect: can't open %s\n", argv[1]);
        return 1;
    }
    tracy::Worker worker(*file);
    while (!worker.AreSourceLocationZonesReady()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (const auto& [id, zones] : worker.GetSourceLocationZones()) {
        if (zones.zones.empty()) {
            continue;
        }
        const tracy::SourceLocation& location = worker.GetSourceLocation(id);
        const char* name = worker.GetString(location.name.active ? location.name : location.function);
        std::printf("zone\t%s\t%zu\n", name, static_cast<std::size_t>(zones.zones.size()));
    }
    for (const tracy::FrameData* frames : worker.GetFrames()) {
        const char* name = frames->name == 0 ? "Frames" : worker.GetString(frames->name);
        std::printf("frames\t%s\t%zu\n", name, static_cast<std::size_t>(frames->frames.size()));
    }
    for (const tracy::ThreadData* thread : worker.GetThreadData()) {
        std::printf("thread\t%s\n", worker.GetThreadName(thread->id));
    }
    for (const tracy::PlotData* plot : worker.GetPlots()) {
        std::printf("plot\t%s\t%zu\n", worker.GetString(plot->name), static_cast<std::size_t>(plot->data.size()));
    }
    std::printf("samples\t%llu\n", static_cast<unsigned long long>(worker.GetCallstackSampleCount()));
    return 0;
}
