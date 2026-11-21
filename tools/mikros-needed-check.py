#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks the shared library that a build for Mikros made (ELGSPEC-0040.R2):
# - its file name is eleganceui.<major>.dlib, and its DT_SONAME is that name;
# - every library it needs (DT_NEEDED) is a system library of MIKSPEC-0068.R5, under its DT_SONAME;
# - it needs the system's C++ standard library and its C library.
# Fails, saying what, if one doesn't hold. Run it on the library after building for Mikros.
#
#   python3 tools/mikros-needed-check.py <path of eleganceui.<major>.dlib>

import pathlib
import re
import struct
import sys

# The system libraries' DT_SONAMEs, as the sysroot's libraries carry them (MIKSPEC-0068.R5): the C library,
# the C++ standard library with its ABI and unwinder libraries, the Vulkan loader, the Wayland client library
# and xkbcommon.
SYSTEM_LIBRARIES = {
    "libc.dlib",
    "libc++.dlib",
    "libc++abi.dlib",
    "libunwind.dlib",
    "libvulkan.dlib",
    "libwayland.dlib",
    "libxkbcommon.dlib",
}
REQUIRED = {"libc.dlib", "libc++.dlib"}

PT_DYNAMIC = 2
DT_NULL, DT_NEEDED, DT_STRTAB, DT_SONAME = 0, 1, 5, 14


def dynamic_names(data):
    """The DT_SONAME and the DT_NEEDED names of a 64-bit little-endian ELF file."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    segments = []
    dynamic = None
    for index in range(phnum):
        base = phoff + index * phentsize
        kind, _flags, offset, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from("<IIQQQQQQ", data, base)
        if kind == 1:  # PT_LOAD
            segments.append((vaddr, offset, filesz))
        elif kind == PT_DYNAMIC:
            dynamic = (offset, filesz)
    if dynamic is None:
        raise ValueError("no dynamic section")

    def file_offset(address):
        for vaddr, offset, filesz in segments:
            if vaddr <= address < vaddr + filesz:
                return offset + address - vaddr
        raise ValueError(f"address {address:#x} is in no segment")

    entries = []
    offset, size = dynamic
    for position in range(offset, offset + size, 16):
        tag, value = struct.unpack_from("<qQ", data, position)
        if tag == DT_NULL:
            break
        entries.append((tag, value))
    strtab = next((value for tag, value in entries if tag == DT_STRTAB), None)
    if strtab is None:
        raise ValueError("no string table")
    strings = file_offset(strtab)

    def name(index):
        start = strings + index
        return data[start:data.index(b"\0", start)].decode()

    soname = next((name(value) for tag, value in entries if tag == DT_SONAME), None)
    needed = [name(value) for tag, value in entries if tag == DT_NEEDED]
    return soname, needed


def main():
    if len(sys.argv) != 2:
        print("usage: python3 tools/mikros-needed-check.py <path of eleganceui.<major>.dlib>")
        return 2
    path = pathlib.Path(sys.argv[1])
    problems = []
    if not re.fullmatch(r"eleganceui\.\d+\.dlib", path.name):
        problems.append(f"the file's name is {path.name}, not eleganceui.<major>.dlib")
    try:
        soname, needed = dynamic_names(path.read_bytes())
    except (OSError, ValueError, IndexError, struct.error) as error:
        print(f"{path}: {error}")
        return 1
    if soname != path.name:
        problems.append(f"its DT_SONAME is {soname}, not {path.name}")
    for library in needed:
        if library not in SYSTEM_LIBRARIES:
            problems.append(f"it needs {library}, which isn't a system library")
    for library in sorted(REQUIRED - set(needed)):
        problems.append(f"it doesn't need {library}")
    for problem in problems:
        print(f"{path}: {problem}")
    if not problems:
        print(f"{path}: needs only system libraries ({', '.join(needed)})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
