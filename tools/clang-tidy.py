#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Runs clang-tidy (the checks in .clang-tidy) over Elegance's sources, and fails if it reports
# anything in them. CI runs it on every pull request.
#
#   python3 tools/clang-tidy.py build/linux-release         every file
#   python3 tools/clang-tidy.py build/linux-release src/view   only those under src/view
#
# The build folder must be configured with CMAKE_EXPORT_COMPILE_COMMANDS (the Linux presets do)
# and built, since some sources include generated headers. clang-tidy's analyzer also reports
# inside third-party headers that vcpkg installs in the build folder (Boost's coroutines, stb),
# which no header filter excludes; those are left out here.

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ("src", "include", "c", "apps", "tests")
DIAGNOSTIC = re.compile(r"^(?P<path>[^:\s][^:]*):\d+:\d+: (?:warning|error): .*\[[\w.,-]+\]$")


def find_tool(names):
    for name in names:
        if path := shutil.which(name):
            return path
    sys.exit(f"none of {', '.join(names)} found on PATH")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", help="a configured and built build folder")
    parser.add_argument("paths", nargs="*", help="only check files under these (default: all sources)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()

    build = pathlib.Path(args.build).resolve()
    if not (build / "compile_commands.json").exists():
        sys.exit(f"{build} has no compile_commands.json")
    run = find_tool(["run-clang-tidy-19", "run-clang-tidy"])
    tidy = find_tool(["clang-tidy-19", "clang-tidy"])
    replacements = find_tool(["clang-apply-replacements-19", "clang-apply-replacements"])

    roots = [ROOT / p for p in (args.paths or ["src", "c", "apps", "tests"])]
    files = "|".join(re.escape(str(r)) for r in roots)
    # A GCC build's compile commands have GCC-only warning flags, which clang doesn't know.
    command = [run, "-p", str(build), "-j", str(args.jobs), "-quiet", "-clang-tidy-binary", tidy,
               "-clang-apply-replacements-binary", replacements, "-extra-arg=-Wno-unknown-warning-option",
               f"^({files})/"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)

    # Each diagnostic and the source lines clang-tidy prints after it, for those in our sources.
    ours = []
    keep = False
    for line in result.stdout.splitlines():
        if match := DIAGNOSTIC.match(line):
            path = pathlib.Path(match["path"]).resolve()
            keep = path.is_relative_to(ROOT) and not path.is_relative_to(build) and \
                path.relative_to(ROOT).parts[0] in SOURCES
            if keep:
                ours.append(line)
        elif line.startswith(("[", "clang-tidy")) or "warnings generated" in line:
            keep = False
        elif keep:
            ours.append(line)

    if result.returncode != 0 and not any(DIAGNOSTIC.match(line) for line in result.stdout.splitlines()):
        # It failed without diagnosing anything: a broken setup, not a finding.
        sys.stderr.write(result.stdout[-4000:] + result.stderr[-4000:])
        return result.returncode
    if ours:
        print("\n".join(ours))
        print(f"\nclang-tidy: {sum(1 for line in ours if DIAGNOSTIC.match(line))} findings in Elegance's sources")
        return 1
    print("clang-tidy: no findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
