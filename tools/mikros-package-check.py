#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks the package of the C library for Mikros as packed, before CI attaches it to a release (ELGSPEC-0040.R28),
# with the checks that Mikros makes of the archive of a prebuilt library (MIKSPEC-0062.R19). It reads:
# - the archive's entries: each must be a regular file or a folder, named relative to the top (a leading ./ is
#   dropped) with no `..`, under include/, libraries/ or share/; the files must be exactly the library, the header,
#   the license and BUILDINFO, at their places;
# - BUILDINFO: five lines, target, elegance, run, sysroot and toolchain in that order, each `key value` with one
#   space; the three commits are 40 lowercase hexadecimal digits, and are compared with the options, if given;
# - the library's name, which must be eleganceui.<major>.dlib with the major of the project() call in the root
#   CMakeLists.txt, and its DT_SONAME, which must be that name;
# - its ABI note (name Mikros, type 1, two 32-bit values: minimum and target level, in a PT_NOTE segment): the
#   minimum must not be above the target level of the same note in the sysroot's libraries/libc.dlib;
# - its DT_NEEDED names: it walks them breadth-first from the library, resolving each by DT_SONAME among the
#   sysroot's libraries/*.dlib, and each name must be a system library (MIKSPEC-0068.R5) the sysroot has;
# - its dynamic symbol table (SHT_DYNSYM): every undefined symbol that is not weak must be defined, by name, by the
#   library or by one of the libraries that it walked to.
# It prints each problem it finds, and fails if there is one.
#
#   python3 tools/mikros-package-check.py <elegance-c-x86_64-unknown-mikros.tar.gz> <the sysroot folder>
#       [--elegance-commit <40 digits>] [--sysroot-commit <40 digits>] [--toolchain-commit <40 digits>]

import argparse
import pathlib
import re
import struct
import sys
import tarfile

# The system libraries' DT_SONAMEs (MIKSPEC-0068.R5): the C library, the C++ standard library with its ABI and
# unwinder libraries, the Vulkan loader, the Wayland client library and xkbcommon.
SYSTEM_LIBRARIES = {
    "libc.dlib",
    "libc++.dlib",
    "libc++abi.dlib",
    "libunwind.dlib",
    "libvulkan.dlib",
    "libwayland.dlib",
    "libxkbcommon.dlib",
}

PT_LOAD, PT_DYNAMIC, PT_NOTE = 1, 2, 4
DT_NULL, DT_NEEDED, DT_STRTAB, DT_SONAME = 0, 1, 5, 14
SHT_DYNSYM = 11
SHN_UNDEF = 0
STB_WEAK = 2
NT_MIKROS_ABI = 1

BUILDINFO_KEYS = ["target", "elegance", "run", "sysroot", "toolchain"]
HEX40 = re.compile(r"[0-9a-f]{40}")


class Elf:
    """What is read from a 64-bit little-endian ELF file."""

    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
            raise ValueError("not a 64-bit little-endian ELF file")
        self.data = data
        phoff, = struct.unpack_from("<Q", data, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
        self.segments = []
        for index in range(phnum):
            base = phoff + index * phentsize
            kind, _flags, offset, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from("<IIQQQQQQ", data, base)
            self.segments.append((kind, offset, vaddr, filesz))

    def file_offset(self, address):
        for kind, offset, vaddr, filesz in self.segments:
            if kind == PT_LOAD and vaddr <= address < vaddr + filesz:
                return offset + address - vaddr
        raise ValueError(f"address {address:#x} is in no segment")

    def dynamic(self):
        """The DT_SONAME (or None) and the DT_NEEDED names."""
        dynamic = next(((offset, filesz) for kind, offset, _vaddr, filesz in self.segments if kind == PT_DYNAMIC), None)
        if dynamic is None:
            raise ValueError("no dynamic section")
        entries = []
        offset, size = dynamic
        for position in range(offset, offset + size, 16):
            tag, value = struct.unpack_from("<qQ", self.data, position)
            if tag == DT_NULL:
                break
            entries.append((tag, value))
        strtab = next((value for tag, value in entries if tag == DT_STRTAB), None)
        if strtab is None:
            raise ValueError("no string table")
        strings = self.file_offset(strtab)

        def name(index):
            start = strings + index
            return self.data[start:self.data.index(b"\0", start)].decode()

        soname = next((name(value) for tag, value in entries if tag == DT_SONAME), None)
        return soname, [name(value) for tag, value in entries if tag == DT_NEEDED]

    def abi_levels(self):
        """(minimum, target) of the Mikros ABI note in a PT_NOTE segment, or None when there is none."""
        for kind, offset, _vaddr, filesz in self.segments:
            if kind != PT_NOTE:
                continue
            position, end = offset, offset + filesz
            while position + 12 <= end:
                namesz, descsz, note_type = struct.unpack_from("<III", self.data, position)
                name_at = position + 12
                desc_at = name_at + (namesz + 3) // 4 * 4
                if note_type == NT_MIKROS_ABI and self.data[name_at:name_at + namesz] == b"Mikros\0":
                    if descsz != 8:
                        raise ValueError(f"its ABI note holds {descsz} bytes, not 8")
                    minimum, target = struct.unpack_from("<II", self.data, desc_at)
                    if target < minimum:
                        raise ValueError(f"its ABI note has a target level {target} below its minimum {minimum}")
                    return (minimum, target)
                position = desc_at + (descsz + 3) // 4 * 4
        return None

    def dynamic_symbols(self):
        """The (defined, undefined) names of the dynamic symbol table; weak undefined symbols are left out."""
        data = self.data
        shoff, = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
        sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + index * shentsize) for index in range(shnum)]
        defined, undefined = set(), set()
        if not any(section[1] == SHT_DYNSYM for section in sections):
            raise ValueError("it has no dynamic symbol table (SHT_DYNSYM)")
        for _name, kind, _flags, _addr, offset, size, link, _info, _align, entsize in sections:
            if kind != SHT_DYNSYM:
                continue
            strings = sections[link][4]
            for position in range(offset, offset + size, entsize or 24):
                st_name, st_info, _other, st_shndx, _value, _size = struct.unpack_from("<IBBHQQ", data, position)
                if st_name == 0:
                    continue
                start = strings + st_name
                symbol = data[start:data.index(b"\0", start)].decode()
                if st_shndx != SHN_UNDEF:
                    defined.add(symbol)
                elif st_info >> 4 != STB_WEAK:
                    undefined.add(symbol)
        return defined, undefined


def entry_problems(archive):
    """The package's files, by name, and what is wrong with its entries."""
    problems = []
    files = {}
    folders = set()
    seen = set()
    for member in archive.getmembers():
        name = member.name
        while name.startswith("./"):
            name = name[2:]
        name = name.rstrip("/")
        if not (member.isreg() or member.isdir()):
            problems.append(f"the entry {member.name} is neither a regular file nor a folder")
            continue
        if name in ("", "."):
            continue
        if name in seen:
            problems.append(f"the entry {name} is in the archive twice")
        seen.add(name)
        parts = name.split("/")
        if name.startswith("/") or ".." in parts:
            problems.append(f"the entry {member.name} is not relative to the top of the archive")
        elif parts[0] not in ("include", "libraries", "share"):
            problems.append(f"the entry {member.name} is outside include/, libraries/ and share/")
        elif member.isdir():
            folders.add(name)
        else:
            files[name] = member
    return files, folders, problems


def buildinfo_problems(text, elegance, sysroot, toolchain):
    problems = []
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    pairs = []
    for line in lines:
        key, separator, value = line.partition(" ")
        if not separator or not value or " " in value or line != f"{key} {value}":
            problems.append(f"BUILDINFO has the line '{line}', which is not a key and a value split by one space")
        else:
            pairs.append((key, value))
    if [key for key, _value in pairs] != BUILDINFO_KEYS and not problems:
        problems.append(f"BUILDINFO has the keys {[key for key, _value in pairs]}, not {BUILDINFO_KEYS}")
    values = dict(pairs)
    if values.get("target") not in (None, "x86_64-unknown-mikros"):
        problems.append(f"BUILDINFO names the target {values['target']}")
    for key, expected in (("elegance", elegance), ("sysroot", sysroot), ("toolchain", toolchain)):
        value = values.get(key)
        if value is not None and not HEX40.fullmatch(value):
            problems.append(f"BUILDINFO's {key} is {value}, not a commit of 40 hexadecimal digits")
        elif value is not None and expected and value != expected:
            problems.append(f"BUILDINFO's {key} is {value}, not {expected}")
    run = values.get("run")
    if run is not None and not re.fullmatch(r"https://\S+", run):
        problems.append(f"BUILDINFO's run is {run}, not an address")
    return problems


def elegance_major(root):
    """The major of Elegance's version, from the project() call of CMakeLists.txt, or None."""
    try:
        text = (root / "CMakeLists.txt").read_text()
    except OSError:
        return None
    found = re.search(r"project\(\s*\w+[^)]*?\bVERSION\s+(\d+)", text)
    return int(found.group(1)) if found else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("sysroot")
    parser.add_argument("--elegance-commit")
    parser.add_argument("--sysroot-commit")
    parser.add_argument("--toolchain-commit")
    args = parser.parse_args()
    problems = []

    try:
        archive = tarfile.open(args.archive, "r:gz")
    except (OSError, tarfile.TarError) as error:
        print(f"{args.archive}: it is not a gzip-compressed tar archive: {error}")
        return 1
    with archive:
        files, folders, problems = entry_problems(archive)
        libraries = [name for name in files if re.fullmatch(r"libraries/eleganceui\.\d+\.dlib", name)]
        expected = {"include/elg-c/elegance.h", "share/doc/elegance/LICENSE-elegance", "share/doc/elegance/BUILDINFO"}
        if len(libraries) != 1:
            problems.append("the package must hold exactly one libraries/eleganceui.<major>.dlib")
            library_name = None
        else:
            library_name = libraries[0]
            expected.add(library_name)
        for name in sorted(set(files) - expected):
            if name not in libraries:
                problems.append(f"the package holds {name}, which it must not")
        for name in sorted(expected - set(files)):
            problems.append(f"the package lacks {name}")
        if "share/doc/elegance/BUILDINFO" in files:
            try:
                text = archive.extractfile(files["share/doc/elegance/BUILDINFO"]).read().decode()
            except UnicodeDecodeError:
                problems.append("BUILDINFO is not UTF-8 text")
            else:
                problems += buildinfo_problems(text, args.elegance_commit, args.sysroot_commit, args.toolchain_commit)
        library_data = archive.extractfile(files[library_name]).read() if library_name else None

    root = pathlib.Path(__file__).resolve().parent.parent
    if library_name:
        file_name = library_name.split("/")[-1]
        major = elegance_major(root)
        if major is None:
            problems.append("the major of Elegance's version can't be read from CMakeLists.txt: the name is unchecked")
        elif file_name != f"eleganceui.{major}.dlib":
            problems.append(f"the library is {file_name}, not eleganceui.{major}.dlib")

    if library_data is not None:
        sysroot_libraries = {}
        for path in sorted((pathlib.Path(args.sysroot) / "libraries").glob("*.dlib")):
            try:
                soname, _needed = Elf(path.read_bytes()).dynamic()
            except (OSError, ValueError, IndexError, struct.error):
                continue
            sysroot_libraries[soname or path.name] = path
        try:
            library = Elf(library_data)
            soname, needed = library.dynamic()
            if soname != file_name:
                problems.append(f"its DT_SONAME is {soname}, not {file_name}")
            levels = library.abi_levels()
            libc = sysroot_libraries.get("libc.dlib")
            level = Elf(libc.read_bytes()).abi_levels() if libc else None
            if levels is None:
                problems.append("it has no ABI note")
            elif level is None:
                problems.append("the sysroot's C library has no ABI note, so its level is unknown")
            elif levels[0] > level[1]:
                problems.append(f"its ABI note's minimum level {levels[0]} is above the sysroot's level {level[1]}")

            closure = {}
            seen = set()
            queue = list(needed)
            while queue:
                name = queue.pop(0)
                if name in seen:
                    continue
                seen.add(name)
                if name not in SYSTEM_LIBRARIES:
                    problems.append(f"it needs {name}, which isn't a system library")
                if name not in sysroot_libraries:
                    problems.append(f"it needs {name}, which the sysroot doesn't have")
                    continue
                dependency = Elf(sysroot_libraries[name].read_bytes())
                closure[name] = dependency
                queue.extend(dependency.dynamic()[1])
            defined, undefined = library.dynamic_symbols()
            for dependency in closure.values():
                defined |= dependency.dynamic_symbols()[0]
            missing = sorted(undefined - defined)
            if missing:
                shown = ", ".join(missing[:10]) + (f" and {len(missing) - 10} more" if len(missing) > 10 else "")
                problems.append(f"{len(missing)} of its undefined symbols are defined by no library it needs: {shown}")
        except (OSError, ValueError, IndexError, struct.error) as error:
            problems.append(f"the library can't be read: {error}")

    for problem in problems:
        print(f"{args.archive}: {problem}")
    if not problems:
        print(f"{args.archive}: the package holds the four files, and its library has the ABI note, needs only system "
              f"libraries of the sysroot, and has all its symbols")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
