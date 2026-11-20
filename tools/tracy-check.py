#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

"""Captures the sandbox of a profiling build with Tracy and checks the capture (ELGSPEC-0042.R12):
the zones of ELGSPEC-0042.R13 that the sandbox reaches on its own, the frame sets of
ELGSPEC-0042.R6 and the thread names of ELGSPEC-0042.R7.

    python3 tools/tracy-check.py build/linux-tracy

The sandbox needs a display: on a machine without one, run it under `xvfb-run -a`. The check
needs Tracy's tracy-capture and tools/tracy-inspect, built at the version of the client Elegance
links, on PATH or given with --capture and --inspect (design.md §16).
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

# Every zone of ELGSPEC-0042.R13, in its order.
ZONES = [
    "Events", "Effects", "Timers", "UI tasks", "Popups", "Lifecycle actions", "Idle", "Window frame",
    "Layout", "Paint", "Accessibility", "Post frame", "Wait for render", "Render batch", "Atlas upload",
    "Frame sources", "Render window", "Damage", "Plan", "Record", "Acquire", "Submit", "Present",
    "Wait for GPU", "Shape text", "Lay out text", "Prepare glyph", "Generate glyph", "Glyph cache file",
    "Decode image", "Import video frame", "Convert video frame",
]

# The zones the sandbox doesn't reach without input, and why. The capture must hold all others.
UNREACHED = {
    "Idle": "the sandbox's loader window animates without end, so the event loop never waits",
    "Timers": "the sandbox's timer starts when its Load button is clicked",
    "Popups": "menus, tooltips and popups open on clicks and hovers",
    "Import video frame": "the sandbox's video plays when its play button is clicked",
    "Convert video frame": "the sandbox's video plays when its play button is clicked",
}

FRAME_SETS = ["Frames", "Render"]  # the default frame set, and the render thread's

# Threads that must have named themselves: each one the sandbox certainly runs. Every other
# thread with data must be named "Elegance ..." too, except the app's own main thread.
THREADS = ["Elegance render", "Elegance glyphs 1", "Elegance image decoder"]
MAIN_THREAD = "Main thread"  # Tracy's name for the thread that started the process


def parse_listing(text):
    """tracy-inspect's output: {kind: {name: count}}; thread counts are how many have the name."""
    listing = {"zone": {}, "frames": {}, "thread": {}, "plot": {}, "samples": {}}
    for line in text.splitlines():
        fields = line.split("\t")
        kind = fields[0]
        if kind == "thread":
            listing[kind][fields[1]] = listing[kind].get(fields[1], 0) + 1
        elif kind == "samples":
            listing[kind]["samples"] = int(fields[1])
        elif kind in listing:
            listing[kind][fields[1]] = listing[kind].get(fields[1], 0) + int(fields[2])
    return listing


def capture(args, trace, glyph_cache):
    """Runs the sandbox and captures it; returns tracy-capture's output."""
    sandbox = pathlib.Path(args.build) / "bin" / ("sandbox.exe" if os.name == "nt" else "sandbox")
    if not sandbox.exists():
        sys.exit(f"tracy-check: no sandbox in {sandbox.parent}: build the profiling preset first")
    # The capture waits for the sandbox, so that it holds the sandbox's start too.
    recorder = subprocess.Popen([args.capture, "-o", str(trace), "-f", "-a", "127.0.0.1", "-p", str(args.port),
                                 "-s", str(args.seconds)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)
    # A new glyph cache, so that glyphs are generated and their files written.
    env = dict(os.environ, TRACY_PORT=str(args.port), ELG_GLYPH_CACHE=str(glyph_cache))
    log = trace.with_suffix(".log")
    with log.open("w") as errors:
        app = subprocess.Popen([str(sandbox), *args.sandbox_args], env=env, stdout=subprocess.DEVNULL, stderr=errors)
        try:
            output, _ = recorder.communicate(timeout=args.seconds + 60)
        except subprocess.TimeoutExpired:
            recorder.kill()
            output, _ = recorder.communicate()
        finally:
            if app.poll() is None:
                app.terminate()
            try:
                app.wait(timeout=20)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
    if recorder.returncode != 0 or not trace.exists():
        print(output, file=sys.stderr)
        print(log.read_text(errors="replace"), file=sys.stderr)
        sys.exit("tracy-check: tracy-capture didn't save a capture")
    return output


def check(listing):
    """The problems with the capture's listing, as messages."""
    problems = []
    for zone in ZONES:
        if zone not in UNREACHED and not listing["zone"].get(zone):
            problems.append(f"no zone {zone!r} (ELGSPEC-0042.R13)")
    for frames in FRAME_SETS:
        if not listing["frames"].get(frames):
            problems.append(f"no frames in the frame set {frames!r} (ELGSPEC-0042.R6)")
    for thread in THREADS:
        if thread not in listing["thread"]:
            problems.append(f"no thread named {thread!r} (ELGSPEC-0042.R7)")
    for thread in listing["thread"]:
        if thread != MAIN_THREAD and not thread.startswith("Elegance"):
            problems.append(f"a thread named {thread!r}, not \"Elegance ...\" (ELGSPEC-0042.R7)")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", help="the profiling build's folder, e.g. build/linux-tracy")
    parser.add_argument("--seconds", type=int, default=5, help="how long to capture (default: 5)")
    parser.add_argument("--port", type=int, default=8086, help="the port the capture connects to (default: 8086)")
    parser.add_argument("--capture", default="tracy-capture", help="Tracy's capture tool")
    parser.add_argument("--inspect", default="tracy-inspect", help="tools/tracy-inspect, built")
    parser.add_argument("--keep", help="also save the capture here, to open in Tracy's profiler")
    parser.add_argument("sandbox_args", nargs="*", help="arguments for the sandbox (after --)")
    args = parser.parse_args()
    for tool in ("capture", "inspect"):
        if not shutil.which(getattr(args, tool)):
            sys.exit(f"tracy-check: can't find {getattr(args, tool)} (--{tool})")

    with tempfile.TemporaryDirectory() as folder:
        trace = pathlib.Path(folder) / "sandbox.tracy"
        glyph_cache = pathlib.Path(folder) / "glyphs"
        summary = capture(args, trace, glyph_cache)
        if args.keep:
            shutil.copyfile(trace, args.keep)
        inspected = subprocess.run([args.inspect, str(trace)], capture_output=True, text=True)
        if inspected.returncode != 0:
            sys.exit(f"tracy-check: tracy-inspect failed: {inspected.stderr.strip()}")
    listing = parse_listing(inspected.stdout)

    frames = ", ".join(f"{name} {count}" for name, count in listing["frames"].items())
    print(f"tracy-check: {len(listing['zone'])} zones, {len(listing['thread'])} thread names, "
          f"{len(listing['plot'])} plots; frames: {frames}; samples: {listing['samples'].get('samples', 0)}")
    for zone, reason in UNREACHED.items():
        if not listing["zone"].get(zone):
            print(f"  not checked: {zone} ({reason})")
    problems = check(listing)
    sys.stdout.flush()
    for problem in problems:
        print(f"tracy-check: {problem}", file=sys.stderr)
    if problems:
        print(summary.strip(), file=sys.stderr)
        return 1
    print("tracy-check: the capture holds the zones, frame sets and thread names of ELGSPEC-0042")
    return 0


if __name__ == "__main__":
    sys.exit(main())
