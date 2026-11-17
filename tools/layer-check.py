#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks the library's layers (ELGSPEC-0001): the folders under src/ (R1), which layers may include
# which (R2), what the display list may include (R3), the backend interfaces (R4), where code may
# test the operating system (R5), which layers may include which third-party headers (R6), what
# the public headers may include and name (R7), and what the C API may include (R8). Each finding names the
# file, the line, what it includes or names, and the requirement it breaks. Exits with 1 if there
# is any.
#
#   python3 tools/layer-check.py
#
# Tests (tests/), the example apps (apps/) and the build files aren't library code, so only src/,
# include/elg/ and c/ are checked.

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# --- Layers (ELGSPEC-0001.R1) ---

# Folder under src/ → layer.
LAYER_FOLDERS = {
    "core": "Core",
    "geometry": "Geometry",
    "reactive": "Reactive",
    "animation": "Animation",
    "text": "Text",
    "media": "Media",
    "net": "Net",
    "display": "Display list",
    "gpu": "GPU",
    "render": "Render",
    "platform": "Platform interface",  # top-level headers only; its subfolders are below
    "view": "View",
    "app": "App",
    "hot_reload": "Hot reload",
}
OS_FOLDERS = {"linux", "macos", "windows"}
PLATFORM_IMPLEMENTATION_FOLDERS = OS_FOLDERS | {"sdl"}
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".c", ".mm", ".m", ".inl"}


def layer_of(path):
    """The layer of a file under src/, or None if it isn't in one."""
    parts = path.relative_to(ROOT / "src").parts
    if len(parts) < 2 or parts[0] not in LAYER_FOLDERS:
        return None
    if parts[0] == "platform" and len(parts) > 2:
        return "Platform implementation" if parts[1] in PLATFORM_IMPLEMENTATION_FOLDERS else None
    return LAYER_FOLDERS[parts[0]]


def os_code_allowed(path):
    """Whether `path` is in a folder where code may test the operating system (R5)."""
    if not path.is_relative_to(ROOT / "src"):
        return False
    parts = path.relative_to(ROOT / "src").parts
    if len(parts) < 3:
        return False
    if parts[0] == "platform":
        return parts[1] in PLATFORM_IMPLEMENTATION_FOLDERS
    return parts[0] == "gpu" and parts[1] in OS_FOLDERS


# --- Reading sources ---


def without_comments(text, strings=False):
    """`text` with comments blanked out, and with `strings` string literals too (offsets and lines
    still match). Includes are read with the strings kept."""
    def blank(match):
        token = match.group(0)
        if token.startswith('"') or token.startswith("'"):
            if not strings:
                return token
            return token[0] + re.sub(r"[^\n]", " ", token[1:-1]) + token[-1]
        return re.sub(r"[^\n]", " ", token)
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', blank, text, flags=re.S)


INCLUDE = re.compile(r'^[ \t]*#[ \t]*(?:include|import)[ \t]*([<"])([^>"]+)[>"]', re.M)  # #import: Objective-C


def includes(path, text):
    """(line, kind, header) for each #include in `text`: kind is '<' or '"'."""
    found = []
    for match in INCLUDE.finditer(text):
        found.append((text.count("\n", 0, match.start()) + 1, match.group(1), match.group(2).strip()))
    return found


def source_files(*folders):
    for folder in folders:
        for path in sorted((ROOT / folder).rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


# --- Standard and third-party headers (ELGSPEC-0001.R6) ---

STANDARD = set("""
algorithm any array atomic barrier bit bitset cassert cctype cerrno cfenv cfloat charconv chrono cinttypes
climits clocale cmath codecvt compare complex concepts condition_variable coroutine csetjmp csignal cstdarg
cstddef cstdint cstdio cstdlib cstring ctime cuchar cwchar cwctype deque exception execution expected
filesystem flat_map flat_set format forward_list fstream functional future generator initializer_list
iomanip ios iosfwd iostream istream iterator latch limits list locale map mdspan memory memory_resource
mutex new numbers numeric optional ostream print queue random ranges ratio regex scoped_allocator
semaphore set shared_mutex source_location span spanstream sstream stack stacktrace stdexcept stdfloat
stop_token streambuf string string_view strstream syncstream system_error thread tuple type_traits
typeindex typeinfo unordered_map unordered_set utility valarray variant vector version
assert.h ctype.h errno.h float.h inttypes.h limits.h locale.h math.h setjmp.h signal.h stdarg.h
stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h time.h uchar.h wchar.h wctype.h
cxxabi.h
""".split())  # cxxabi.h: the C++ runtime's (GCC's and Clang's), used under a compiler test

# Third-party header prefixes → the layers that may include them. "Render (sources)" is Render in
# source files only. Operating-system headers go with the platform implementation, and the
# graphics APIs' also with the GPU layer's operating-system folders (R5).
PLATFORM = {"Platform implementation"}
GPU_INTEROP = {"Platform implementation", "GPU (OS)"}
THIRD_PARTY = [
    (re.compile(r"^SDL3/"), "SDL3", PLATFORM),
    (re.compile(r"^(webgpu/|dawn/)"), "WebGPU and Dawn", {"GPU", "Render (sources)"}),
    (re.compile(r"^(ft2build\.h|freetype/|hb\.h|hb-[\w-]+\.h|harfbuzz/|msdfgen)"),
     "FreeType, HarfBuzz, msdfgen", {"Text"}),
    (re.compile(r"^(boost/|openssl/)"), "Boost, OpenSSL", {"Net"}),
    (re.compile(r"^(miniaudio\.h|stb_[\w]+\.h|stb/)"), "miniaudio, stb", {"Media", "Platform implementation"}),
    (re.compile(r"^(accesskit\.h|accesskit/)"), "AccessKit", PLATFORM),
    (re.compile(r"^catch2/"), "Catch2", set()),
    (re.compile(r"^(d3d\w*\.h|dxgi\w*\.h|dcomp\w*\.h|wrl/|wrl\.h|Metal/|IOSurface/|CoreVideo/|QuartzCore/)"),
     "graphics APIs (Direct3D, DXGI, DirectComposition, Metal)", GPU_INTEROP),
    # What those graphics APIs build on, which their interop code in src/gpu/<os>/ (R5) uses too.
    (re.compile(r"^(windows\.h|Foundation/|CoreFoundation/|objc/)"),
     "the operating system's base headers", GPU_INTEROP),
    (re.compile(r"^(windows\.h|win\w+\.h|shl\w*\.h|shobjidl\w*\.h|objbase\.h|combaseapi\.h|ole\w*\.h|"
                r"mf\w*\.h|propvarutil\.h|wincrypt\.h|winhttp\.h|dwmapi\.h|uxtheme\.h|tlhelp32\.h|"
                r"knownfolders\.h|psapi\.h|dbghelp\.h|dshow\.h|commctrl\.h|imm\.h|shellapi\.h|process\.h|"
                r"io\.h|direct\.h|objidl\.h|unknwn\.h|"
                r"gst/|glib\.h|glib/|gio/|gobject/|"
                r"[A-Z]\w*/[A-Z]\w*\.h$|objc/|"  # Apple's frameworks: Name/Name.h
                r"mach/|mach-o/|dlfcn\.h|unistd\.h|sys/|pthread\.h|fcntl\.h|spawn\.h|signal\.h|poll\.h|"
                r"execinfo\.h|linux/|asm/|"
                r"fontconfig/|X11/|wayland-)"),
     "operating-system libraries", PLATFORM),
]


def third_party(header):
    """(library name, allowed layers) for a third-party header, or None for a standard one."""
    if header in STANDARD or header.startswith("elg/"):
        return None
    for pattern, name, layers in THIRD_PARTY:
        if pattern.match(header):
            return name, layers
    return "an unknown library", set()


def effective_layer(path):
    """The layer, refined as THIRD_PARTY names it: GPU (OS) and Render (sources)."""
    layer = layer_of(path)
    parts = path.relative_to(ROOT / "src").parts if path.is_relative_to(ROOT / "src") else ()
    if layer == "GPU" and len(parts) > 2 and parts[1] in OS_FOLDERS:
        return {"GPU", "GPU (OS)"}
    if layer == "Render":
        return {"Render", "Render (sources)"} if path.suffix != ".h" else {"Render"}
    return {layer} if layer else set()


# --- Allowed dependencies (ELGSPEC-0001.R2) ---

ALLOWED = {
    "Core": set(),
    "Geometry": {"Core"},
    "Reactive": {"Core"},
    "Animation": {"Core", "Geometry", "Reactive"},
    "Text": {"Core", "Geometry", "Reactive"},
    "Media": {"Core", "Geometry", "Reactive"},
    "Net": {"Core"},
    "Display list": {"Core", "Geometry"},  # and Text's and Media's data types (R3), below
    "GPU": {"Core", "Geometry"},
    "Render": {"Core", "Geometry", "Display list", "GPU", "Text", "Media"},
    "Platform interface": {"Core", "Geometry"},
    "View": {"Core", "Geometry", "Reactive", "Animation", "Text", "Media", "Net", "Display list",
             "Platform interface"},
    "Platform implementation": {"Core", "Geometry", "Platform interface"},  # and backend interfaces (R4)
    "Hot reload": {"Core", "Reactive", "View", "Platform interface"},
}
# App may include every layer but the platform implementation.
ALLOWED["App"] = set(ALLOWED) - {"Platform implementation"}


def header_layer(header):
    """The layer of a quoted include ("core/log.h"), or None if it isn't in src/."""
    path = ROOT / "src" / header
    return layer_of(path) if path.is_file() else None


def is_backend(header):
    """Whether a quoted include names a backend interface (R4): <layer>/<name>_backend.h."""
    return header.endswith("_backend.h") and header_layer(header) not in (None, "Platform implementation")


# The public headers of plain values (points, colors, fills, paths), which a header of Text's or
# Media's data types may include (R3): Geometry's, in the public API.
DATA_PUBLIC_HEADERS = {"elg/geometry.h", "elg/color.h", "elg/gradient.h", "elg/path.h", "elg/export.h"}


def data_type_header(header, seen=None):
    """Whether a Text or Media header declares only data types (R3): it includes Core, Geometry,
    the standard library, public headers and other such headers of Text and Media, and nothing else."""
    seen = set() if seen is None else seen
    if header in seen:
        return True
    seen.add(header)
    path = ROOT / "src" / header
    for _, kind, included in includes(path, without_comments(path.read_text(encoding="utf-8"))):
        if kind == "<":
            # Of the public headers, only the plain value types', which are Geometry's.
            if not (included in STANDARD or included in DATA_PUBLIC_HEADERS):
                return False
            continue
        layer = header_layer(included)
        if layer in ("Core", "Geometry"):
            continue
        if layer in ("Text", "Media") and data_type_header(included, seen):
            continue
        return False
    return True


def check_dependencies(path, found):
    layer = layer_of(path) if path.is_relative_to(ROOT / "src") else None
    if layer is None:
        return
    for line, kind, header in found:
        if kind != '"':
            continue
        target = header_layer(header)
        if target is None or target == layer:
            continue
        if is_backend(header):
            # A backend interface: its own layer and the platform implementation, which implements
            # it, include it (R4). Other layers may, where R2 lets them include its layer.
            if layer == "Platform implementation" or target in ALLOWED[layer]:
                continue
            report(path, line, f"includes {header}, {target}'s backend interface", "ELGSPEC-0001.R4")
            continue
        if layer == "Display list" and target in ("Text", "Media"):
            if not data_type_header(header):
                report(path, line, f"includes {header}, which declares more than data types of {target}",
                       "ELGSPEC-0001.R3")
            continue
        if target not in ALLOWED[layer]:
            report(path, line, f"includes {header}: {layer} may not depend on {target}", "ELGSPEC-0001.R2")


def check_backend_interface(path, found):
    """A backend interface includes only Core, Geometry, its own layer and the standard library (R4)."""
    if not path.name.endswith("_backend.h") or not path.is_relative_to(ROOT / "src"):
        return
    layer = layer_of(path)
    for line, kind, header in found:
        if kind == "<":
            if not (header in STANDARD or header.startswith("elg/")):
                report(path, line, f"the backend interface includes {header}", "ELGSPEC-0001.R4")
        elif header_layer(header) not in ("Core", "Geometry", layer):
            report(path, line, f"the backend interface includes {header}, outside Core, Geometry and {layer}",
                   "ELGSPEC-0001.R4")


# --- Findings ---

problems = []


def report(path, line, what, requirement):
    problems.append(f"{path.relative_to(ROOT)}:{line}: {what} ({requirement})")


# --- R1: folders ---


def check_folders():
    src = ROOT / "src"
    for entry in sorted(src.iterdir()):
        if entry.is_file() and entry.suffix in SOURCE_SUFFIXES:
            report(entry, 1, "a source file directly in src/", "ELGSPEC-0001.R1")
        elif entry.is_dir() and entry.name not in LAYER_FOLDERS:
            report(entry, 1, f"src/{entry.name}/ is not a layer's folder", "ELGSPEC-0001.R1")
    platform = src / "platform"
    if platform.is_dir():
        for entry in sorted(platform.iterdir()):
            if entry.is_dir() and entry.name not in PLATFORM_IMPLEMENTATION_FOLDERS:
                report(entry, 1, f"src/platform/{entry.name}/ is neither sdl/ nor an operating system's folder",
                       "ELGSPEC-0001.R1")
            elif entry.is_file() and entry.suffix in SOURCE_SUFFIXES and entry.suffix != ".h":
                report(entry, 1, "a source file in the platform interface, which has headers only",
                       "ELGSPEC-0001.R1")
    gpu = src / "gpu"
    if gpu.is_dir():
        for entry in sorted(gpu.iterdir()):
            if entry.is_dir() and entry.name not in OS_FOLDERS:
                report(entry, 1, f"src/gpu/{entry.name}/ is not an operating system's folder", "ELGSPEC-0001.R1")


# --- R5: operating-system code ---

OS_MACRO = re.compile(r"\b(_WIN32|_WIN64|WIN32|__WIN32__|__APPLE__|__MACH__|TARGET_OS_\w+|__linux__|__linux|"
                      r"__unix__|__unix|__ANDROID__|__FreeBSD__|__OpenBSD__|__NetBSD__|SDL_PLATFORM_\w+)\b")
CONDITIONAL = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)")


def check_os_code(path, text):
    if os_code_allowed(path):
        return
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        match = CONDITIONAL.match(lines[i])
        if match and match.group(1) in ("if", "ifdef", "ifndef", "elif") and OS_MACRO.search(match.group(2)):
            # The block from here to its #endif: allowed when it only defines macros (export,
            # visibility or calling-convention macros).
            depth, j, only_defines = 1, i + 1, True
            while j < len(lines) and depth:
                directive = DIRECTIVE.match(lines[j])
                if directive:
                    name = directive.group(1)
                    if name in ("if", "ifdef", "ifndef"):
                        depth += 1
                        only_defines = False
                    elif name == "endif":
                        depth -= 1
                    elif name not in ("define", "undef", "elif", "else"):
                        only_defines = False
                elif lines[j].strip():
                    only_defines = False
                j += 1
            if not only_defines:
                report(path, i + 1, f"tests the operating system ({OS_MACRO.search(match.group(2)).group(1)})",
                       "ELGSPEC-0001.R5")
            i = j if only_defines else i + 1
            continue
        i += 1


def check_platform_includes(path, found):
    if path.is_relative_to(ROOT / "src" / "platform"):
        return
    for line, kind, header in found:
        parts = header.split("/")
        if kind == '"' and len(parts) > 2 and parts[0] == "platform" and parts[1] in PLATFORM_IMPLEMENTATION_FOLDERS:
            report(path, line, f"includes {header}, a header of the platform implementation", "ELGSPEC-0001.R5")


# --- R6: third-party headers ---

def gpu_header_reaches_webgpu(header, seen=None):
    """Whether the GPU header `header` ("gpu/x.h") includes WebGPU or Dawn, itself or through
    other GPU headers."""
    seen = set() if seen is None else seen
    if header in seen:
        return False
    seen.add(header)
    path = ROOT / "src" / header
    if not path.is_file():
        return False
    for _, kind, included in includes(path, without_comments(path.read_text(encoding="utf-8"))):
        if kind == "<" and re.match(r"^(webgpu/|dawn/)", included):
            return True
        if kind == '"' and included.startswith("gpu/") and gpu_header_reaches_webgpu(included, seen):
            return True
    return False


def check_third_party(path, found):
    layers = effective_layer(path) if path.is_relative_to(ROOT / "src") else set()
    for line, kind, header in found:
        if kind == "<":
            library = third_party(header)
            if library and not (layers & library[1]):
                where = " or ".join(sorted(layers)) if layers else str(path.relative_to(ROOT).parts[0]) + "/"
                report(path, line, f"includes {header} ({library[0]}), which {where} may not include",
                       "ELGSPEC-0001.R6")
        elif header.startswith("gpu/") and "GPU" not in layers and gpu_header_reaches_webgpu(header):
            report(path, line, f"includes {header}, a GPU header that includes WebGPU or Dawn", "ELGSPEC-0001.R6")


# --- R7: public headers ---

DECLARATION_KEYWORDS = re.compile(r"\b(namespace|class|struct|union|enum)\b")


def check_public_header(path, text, found):
    for line, kind, header in found:
        if kind == '"' or not (header in STANDARD or header.startswith("elg/")):
            report(path, line, f"includes {header}, which is neither standard nor public", "ELGSPEC-0001.R7")
    # `detail` types in declarations outside namespace detail, at namespace scope or in the
    # public part of a class: function signatures, constructors and data members.
    scopes = []  # (kind, access) for each open brace: kind is namespace/detail/class/body
    statement_start = 0
    parens = 0  # open parentheses in the statement: braces inside them (T{}, lambdas) aren't scopes
    i = 0
    while i < len(text):
        ch = text[i]
        if ch in "()":
            parens = max(0, parens + (1 if ch == "(" else -1))
            i += 1
            continue
        if ch in "{}" and parens:
            i += 1
            continue
        if ch == "#":  # preprocessor line: skip it, continuations included
            end = i
            while True:
                end = text.find("\n", end)
                if end < 0 or text[end - 1] != "\\":
                    break
                end += 1
            i = len(text) if end < 0 else end
            statement_start = i
            continue
        in_body = any(kind == "body" for kind, _ in scopes)
        if ch == "{":
            head = text[statement_start:i]
            if not in_body:
                check_statement(path, text, statement_start, head, scopes)
            keyword = None if in_body else last_keyword(head)
            if keyword == "namespace":
                scopes.append(("detail" if re.search(r"\bdetail\b", head) else "namespace", "public"))
            elif keyword in ("class", "struct", "union"):
                scopes.append(("class", "private" if keyword == "class" else "public"))
            elif keyword == "enum":
                scopes.append(("body", None))
            else:
                scopes.append(("body", None))
            statement_start = i + 1
        elif ch == "}":
            if scopes:
                scopes.pop()
            statement_start = i + 1
        elif ch == ";" and not in_body:
            check_statement(path, text, statement_start, text[statement_start:i], scopes)
            statement_start = i + 1
        elif ch == ":" and not in_body and scopes and scopes[-1][0] == "class":
            label = re.search(r"\b(public|private|protected)\s*$", text[statement_start:i])
            if label and text[i + 1:i + 2] != ":":
                scopes[-1] = ("class", label.group(1))
                statement_start = i + 1
        i += 1


def last_keyword(head):
    """The namespace/class/struct/union/enum keyword that opens the brace after `head`, if any:
    the last one outside parentheses and angle brackets (not that of a parameter's type)."""
    flat = re.sub(r"\([^()]*\)", "()", head)
    flat = re.sub(r"<[^<>]*>", "<>", re.sub(r"<[^<>]*>", "<>", flat))
    if "(" in flat.split("{")[0] and not re.search(r"\b(class|struct|union)\b[^()]*$", flat):
        return None  # a function's body
    keywords = DECLARATION_KEYWORDS.findall(flat)
    return keywords[-1] if keywords else None


def signature(statement):
    """`statement` without what follows a constructor's parameter list: its member initializers
    (") : a(...), b(...)"), which are its definition, not its signature."""
    depth = 0
    for i, ch in enumerate(statement):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0 and re.match(r"^\s*(noexcept\s*(\([^()]*\))?)?\s*(requires\b(?:[^:;{]|::)*)?:(?!:)",
                                       statement[i + 1:]):
                return statement[:i + 1]
    return statement


def check_statement(path, text, start, statement, scopes):
    statement = signature(statement)
    if "detail::" not in statement:
        return
    if any(kind == "detail" for kind, _ in scopes):
        return
    # Macros without a semicolon (ELG_PRIVATE_MEMBERS_END) run into the next declaration.
    stripped = re.sub(r"^(?:[A-Z][A-Z0-9_]+\s+)+", "", statement.strip())
    if re.match(r"^(friend|using|static_assert|template\s*<[^>]*>\s*friend)\b", stripped):
        return
    if scopes and scopes[-1][0] == "class" and scopes[-1][1] != "public":
        # Private: only a data member that holds a detail type by value breaks R7 (it must be
        # behind an opaque pointer); functions and pointers are fine.
        if "(" in stripped:
            return
        for match in re.finditer(r"detail::[\w:]+", stripped):
            before, after = stripped[:match.start()], stripped[match.end():]
            wrapped = re.search(r"\b(shared_ptr|unique_ptr|weak_ptr)\s*<\s*(const\s+)?$", before)
            if not wrapped and not re.match(r"^\s*(<[^;]*>)?\s*(const\s*)?[*&]", after):
                offset = start + statement.find(match.group(0))
                report(path, text.count("\n", 0, offset) + 1,
                       f"holds {match.group(0)} by value instead of behind an opaque pointer", "ELGSPEC-0001.R7")
                return
        return
    offset = start + statement.find("detail::")
    name = re.search(r"detail::[\w:]+", statement).group(0)
    report(path, text.count("\n", 0, offset) + 1, f"names {name} in a public declaration", "ELGSPEC-0001.R7")


# --- R8: C API ---


def check_c_api(path, found):
    for line, kind, header in found:
        if kind == '"' and (ROOT / "c" / header).is_file():
            continue
        if kind == "<" and (header in STANDARD or header.startswith("elg/")):
            continue
        report(path, line, f"includes {header}, which is neither public nor the C API's own", "ELGSPEC-0001.R8")


def main():
    check_folders()
    for path in source_files("src", "include/elg", "c"):
        raw = path.read_text(encoding="utf-8")
        text = without_comments(raw)
        found = includes(path, text)
        check_os_code(path, text)
        check_platform_includes(path, found)
        check_third_party(path, found)
        check_dependencies(path, found)
        check_backend_interface(path, found)
        if path.is_relative_to(ROOT / "include" / "elg"):
            check_public_header(path, without_comments(raw, strings=True), found)
        if path.is_relative_to(ROOT / "c"):
            check_c_api(path, found)
    for problem in problems:
        print(problem)
    if problems:
        print(f"layer-check: {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print("layer-check: the layers follow ELGSPEC-0001.R1 to R8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
