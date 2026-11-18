#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Rebinds the GNU unique symbols (STB_GNU_UNIQUE) of an ELF relocatable object as global ones, in
# place. GCC binds inline variables, their guards and the static members of templates that way, in
# Elegance, its dependencies and libstdc++.a alike. package-static.cmake runs it on the static
# package's object before `objcopy --keep-global-symbol`, which makes the global symbols local but
# leaves unique ones as they are. Unique symbols already come after the local ones in the symbol
# table, as global ones must, so the table needs no other change.
#
#   python3 c/unique-to-global.py <object>

import mmap
import struct
import sys

STB_GLOBAL = 1
STB_GNU_UNIQUE = 10
SHT_SYMTAB = 2


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: unique-to-global.py <object>")
    path = sys.argv[1]
    with open(path, "r+b") as file, mmap.mmap(file.fileno(), 0) as data:
        if data[:4] != b"\x7fELF":
            sys.exit(f"unique-to-global: {path} isn't an ELF file")
        is64 = data[4] == 2
        order = "<" if data[5] == 1 else ">"

        def read(fmt, at):
            return struct.unpack_from(order + fmt, data, at)

        if is64:
            (shoff,) = read("Q", 0x28)
            shentsize, shnum = read("HH", 0x3A)
        else:
            (shoff,) = read("I", 0x20)
            shentsize, shnum = read("HH", 0x2E)
        if shnum == 0:
            # 65,536 sections or more: the count is the first section header's size.
            (shnum,) = read("Q", shoff + 0x20) if is64 else read("I", shoff + 0x14)
        changed = 0
        for index in range(shnum):
            header = shoff + index * shentsize
            (kind,) = read("I", header + 4)
            if kind != SHT_SYMTAB:
                continue
            if is64:
                offset, size = read("QQ", header + 0x18)
                (entsize,) = read("Q", header + 0x38)
                info = 4  # st_info in Elf64_Sym
            else:
                offset, size = read("II", header + 0x10)
                (entsize,) = read("I", header + 0x24)
                info = 12  # st_info in Elf32_Sym
            for symbol in range(offset, offset + size, entsize):
                value = data[symbol + info]
                if value >> 4 == STB_GNU_UNIQUE:
                    data[symbol + info] = (STB_GLOBAL << 4) | (value & 0xF)
                    changed += 1
        data.flush()
    print(f"unique-to-global: {changed} symbols of {path} are global instead of unique")


if __name__ == "__main__":
    main()
