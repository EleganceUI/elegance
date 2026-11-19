#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks the C library's two packages, as c/CMakeLists.txt installs them (ELGSPEC-0007.R17):
# - the shared library exports only the C API's functions (elg_*);
# - Linux: the shared library needs no C++ standard library of the system's; the static package's
#   only global definitions are the C API's, and it refers to no symbol of a C++ standard library,
#   since it holds the one it was built with;
# - macOS: the static package links the system's libc++ (elegance-link.txt) and refers to it;
# - Windows: the static package holds no object code of the C++ standard library's static library
#   (libcpmt): its objects ask for the DLL's (msvcprt), and a program linked with it imports the
#   system's C++ standard library (msvcp140.dll);
# - c/link_check.cpp, linked with the static package, runs: built with the C++ compiler (CXX, or
#   c++), whose C++ standard library is another copy of the package's or another one, and on Linux
#   also with libc++, by LIBCXX_CXX (default clang++) where it's installed.
# Fails, saying what, if one doesn't hold. CI runs it after building the packages.
#
#   python3 tools/c-package-check.py <shared package folder> <static package folder>

import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Symbols that only a C++ standard library defines: a package that refers to one needs it. The C
# library (glibc) defines __cxa_atexit, __cxa_finalize and __cxa_thread_atexit_impl.
CXX_RUNTIME = re.compile(r"^_?(_ZN?K?St|_ZSt|__cxa_(?!atexit$|finalize$|thread_atexit_impl$)|__gxx_personality|_Znw|_Zna|"
                         r"_ZdlPv|_ZdaPv)")

problems = []


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, check=True, **kwargs).stdout


def symbols(command):
    """The names in `nm` output: each line's last word, but for an archive's "member.o:" lines. A
    line is "[address] <type> <name>", or on macOS, with -u, the name alone."""
    return [line.split()[-1] for line in run(command).splitlines() if line.strip() and not line.endswith(":")]


class PortableExecutable:
    """A Windows DLL or program, read for the names it exports and the DLLs it imports."""

    def __init__(self, path):
        self.data = path.read_bytes()
        header = struct.unpack_from("<I", self.data, 0x3C)[0]
        sections, optional_size = struct.unpack_from("<H12xH", self.data, header + 6)
        optional = header + 24
        pe32_plus = struct.unpack_from("<H", self.data, optional)[0] == 0x20B
        self.directories = optional + (112 if pe32_plus else 96)
        self.sections = [struct.unpack_from("<8xIIII", self.data, optional + optional_size + 40 * i)
                         for i in range(sections)]

    def offset(self, rva):
        for virtual_size, address, raw_size, raw in self.sections:
            if address <= rva < address + max(virtual_size, raw_size):
                return rva - address + raw
        raise ValueError(f"RVA {rva:#x} is in no section")

    def string(self, rva):
        start = self.offset(rva)
        return self.data[start:self.data.index(b"\0", start)].decode("ascii")

    def directory(self, index):
        return struct.unpack_from("<II", self.data, self.directories + 8 * index)

    def exports(self):
        rva, _ = self.directory(0)
        if not rva:
            return []
        count, names = struct.unpack_from("<24xI4xI", self.data, self.offset(rva))
        return [self.string(struct.unpack_from("<I", self.data, self.offset(names) + 4 * i)[0])
                for i in range(count)]

    def imports(self):
        rva, _ = self.directory(1)
        dlls = []
        at = self.offset(rva) if rva else None
        while at is not None:
            name = struct.unpack_from("<12xI", self.data, at)[0]
            if not name:
                break
            dlls.append(self.string(name))
            at += 20
        return dlls


def check_exports(shared):
    if sys.platform == "win32":
        library = shared / "lib" / "elegance_c.dll"
        names = PortableExecutable(library).exports()
        others = sorted(n for n in names if not n.startswith("elg_"))
        if others:
            problems.append(f"{library.name} exports {len(others)} symbols besides the C API's: "
                            f"{', '.join(others[:10])}")
        return len(names)
    if sys.platform == "darwin":
        library = shared / "lib" / "libelegance_c.dylib"
        names = [n[1:] for n in symbols(["nm", "-gU", str(library)])]
    else:
        library = shared / "lib" / "libelegance_c.so"
        names = symbols(["nm", "-D", "--defined-only", str(library)])
    others = sorted(n for n in names if not n.startswith("elg_"))
    if others:
        problems.append(f"{library.name} exports {len(others)} symbols besides the C API's: {', '.join(others[:10])}")
    if sys.platform != "darwin":
        needed = re.findall(r"\(NEEDED\).*\[(.+)\]", run(["readelf", "-d", str(library)]))
        cxx = [n for n in needed if re.match(r"lib(stdc\+\+|c\+\+|c\+\+abi)\.so", n)]
        if cxx:
            problems.append(f"{library.name} needs the system's C++ standard library: {', '.join(cxx)}")
    return len(names)


def check_static(static):
    if sys.platform == "win32":
        # Each object names the C++ standard library it was built for: the DLL's (/MD) or the
        # static library (/MT), whose object code the linker would then put in the program.
        archive = static / "lib" / "elegance_c.lib"
        content = archive.read_bytes().lower()
        if b"/defaultlib:libcpmt" in content or b'/defaultlib:"libcpmt' in content:
            problems.append(f"{archive.name} holds objects built for the static C++ standard library (libcpmt)")
        if b"msvcprt" not in content:
            problems.append(f"{archive.name} doesn't ask for the C++ standard library's DLL (msvcprt)")
        return
    archive = static / "lib" / "libelegance_c.a"
    manifest = (static / "elegance-link.txt").read_text(encoding="utf-8")
    if sys.platform == "darwin":
        undefined = [n[1:] for n in symbols(["nm", "-gu", str(archive)])]
        if "lib c++" not in manifest.splitlines() or not any(CXX_RUNTIME.match(n) for n in undefined):
            problems.append(f"{archive.name} doesn't link the system's libc++")
        return
    defined = symbols(["nm", "-g", "--defined-only", str(archive)])
    others = sorted({n for n in defined if not n.startswith("elg_")})
    if others:
        problems.append(f"{archive.name} defines {len(others)} global symbols besides the C API's: "
                        f"{', '.join(others[:10])}")
    needs = sorted({n for n in symbols(["nm", "-u", str(archive)]) if CXX_RUNTIME.match(n)})
    if needs:
        problems.append(f"{archive.name} needs a C++ standard library: {', '.join(needs[:10])}")


def link_arguments(static):
    """What linking the static package takes, from its elegance-link.txt."""
    arguments = []
    for line in (static / "elegance-link.txt").read_text(encoding="utf-8").splitlines():
        kind, _, value = line.partition(" ")
        if kind == "static":
            arguments.append(str(static / value))
        elif kind == "lib" and value.startswith("-framework "):
            arguments += value.split()
        elif kind == "lib" and value.startswith("lib/"):
            arguments.append(str(static / value))
        elif kind == "lib" and sys.platform == "win32":
            # A system library, by its name (dwmapi) or its file's (user32.lib).
            arguments.append(value if value.lower().endswith(".lib") else f"{value}.lib")
        elif kind == "lib":
            arguments.append(f"-l{value}")
    return arguments


def check_link_windows(static):
    """link_check.cpp, linked with the static package by MSVC (the DLL runtime, /MD), runs, and
    imports the system's C++ standard library."""
    with tempfile.TemporaryDirectory() as folder:
        program = pathlib.Path(folder) / "link_check.exe"
        command = ["cl", "/nologo", "/std:c++17", "/EHsc", "/MD", "/utf-8", str(ROOT / "c" / "link_check.cpp"),
                   "/I", str(static / "include"), f"/Fo{folder}\\", f"/Fe{program}", "/link",
                   *link_arguments(static)]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode != 0:
            problems.append(f"link_check.cpp doesn't build with MSVC:\n{(built.stdout + built.stderr)[-3000:]}")
            return
        imports = [name.lower() for name in PortableExecutable(program).imports()]
        if not any(re.match(r"msvcp\d+\.dll$", name) for name in imports):
            problems.append(f"a program linked with the static package doesn't import msvcp*.dll: {', '.join(imports)}")
        environment = dict(os.environ)
        environment["PATH"] = str(static / "lib") + os.pathsep + environment.get("PATH", "")
        ran = subprocess.run([str(program)], capture_output=True, text=True, env=environment)
        if ran.returncode != 0:
            problems.append(f"link_check.cpp built with MSVC exits with {ran.returncode}:\n{ran.stderr[-3000:]}")
        else:
            print(f"c-package-check: linked with MSVC: {ran.stdout.strip()}")


def check_link(static, compiler, flags, what):
    with tempfile.TemporaryDirectory() as folder:
        program = pathlib.Path(folder) / "link_check"
        # Linux: as Ubuntu's toolchain links, so that a library nothing refers to isn't needed.
        # macOS: for the package's architecture, which CI builds for x86_64 on an arm64 runner too.
        target = []
        if sys.platform.startswith("linux"):
            target = ["-Wl,--as-needed"]
        elif sys.platform == "darwin":
            archive = static / "lib" / "libelegance_c.a"
            for arch in run(["lipo", "-archs", str(archive)]).split():
                target += ["-arch", arch]
        command = [compiler, *flags, *target, "-std=c++17", str(ROOT / "c" / "link_check.cpp"), "-I",
                   str(static / "include"), *link_arguments(static), "-o", str(program)]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode != 0:
            problems.append(f"link_check.cpp doesn't build with {what}:\n{built.stderr[-3000:]}")
            return
        environment = dict(os.environ)
        for variable in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            environment[variable] = str(static / "lib")
        ran = subprocess.run([str(program)], capture_output=True, text=True, env=environment)
        if ran.returncode != 0:
            problems.append(f"link_check.cpp built with {what} exits with {ran.returncode}:\n{ran.stderr[-3000:]}")
        else:
            print(f"c-package-check: linked with {what}: {ran.stdout.strip()}")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: c-package-check.py <shared package folder> <static package folder>")
    shared, static = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    exported = check_exports(shared)
    check_static(static)
    if sys.platform == "win32":
        check_link_windows(static)
        return report(exported)
    compiler = os.environ.get("CXX", "c++")
    check_link(static, compiler, [], f"{compiler} and its C++ standard library")
    libcxx = os.environ.get("LIBCXX_CXX", "clang++")  # a Clang whose libc++ is installed
    if sys.platform.startswith("linux") and shutil.which(libcxx):
        check_link(static, libcxx, ["-stdlib=libc++"], f"{libcxx} and libc++")
    return report(exported)


def report(exported):
    if problems:
        print("\n".join(problems))
        print(f"\nc-package-check: {len(problems)} problem(s)")
        return 1
    print(f"c-package-check: the shared library exports the C API's {exported} functions alone, and the static "
          "package keeps its C++ standard library to itself")
    return 0


if __name__ == "__main__":
    sys.exit(main())
