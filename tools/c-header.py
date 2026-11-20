#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Writes the C API's header, c/include/elg-c/elegance.h: valid C11, for bindings and C programs.
# It's made from the C++ that implements the API, so that the two can't drift apart:
# - the C part of include/elg/gpu_image.h, as it is;
# - the structs and callback types that cross the API (the Elg* ones in c/shim.h and c/*.cpp);
# - every function c/*.cpp exports (ELG_EXPORT, also those the style macros of c/styles.cpp define),
#   with its doc comment;
# - the error kinds elg_take_error() returns, and the ABI version.
# C++ types become C ones: a handle is a pointer to an opaque struct (an elg::View* is an ElgView*).
#
# It also writes tests/c_api_functions.c, a C file that refers to every function the header
# declares, so that a C program that links it shows they're all defined.
#
#   python3 tools/c-header.py           writes both
#   python3 tools/c-header.py --check   fails if either isn't up to date (CI)

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "c" / "include" / "elg-c" / "elegance.h"
FUNCTIONS = ROOT / "tests" / "c_api_functions.c"
LAYOUT_FIELDS = ROOT / "c" / "layout_fields.h"
LAYOUTS = ROOT / "tests" / "c_api_layouts.c"
GPU_IMAGE = ROOT / "include" / "elg" / "gpu_image.h"
SHIM = ROOT / "c" / "shim.h"
SOURCES = [ROOT / "c" / name for name in ("core.cpp", "views.cpp", "ext.cpp", "platform.cpp", "styles.cpp")]

LICENSE = """// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/."""

PRIMITIVES = {"void", "bool", "char", "unsigned char", "int", "float", "double", "size_t", "int8_t", "uint8_t",
              "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"}

# The C++ types of handles (and of closures' results), each with the opaque C type that stands for
# it. Their names are the Rust binding's, prefixed with Elg.
OPAQUE = {
    "View": "ElgView", "Text": "ElgText", "Shape": "ElgShape", "Path": "ElgPath", "Animation": "ElgAnimation",
    "Transition": "ElgTransition", "EnvironmentValues": "ElgEnvironmentValues", "WindowHandle": "ElgWindowHandle",
    "App": "ElgApp", "Binding<bool>": "ElgBindingBool", "Binding<float>": "ElgBindingF32",
    "Binding<int>": "ElgBindingI32", "Binding<std::string>": "ElgBindingString", "Binding<double>": "ElgBindingF64",
    "ScrollPosition": "ElgScrollPosition", "GraphicsContext": "ElgGraphicsContext", "LayoutCache": "ElgLayoutCache",
    "Effects": "ElgEffects", "Sound": "ElgSound", "AudioPlayer": "ElgAudioPlayer",
    "AudioStream": "ElgAudioStream", "AudioRecorder": "ElgAudioRecorder", "Camera": "ElgCamera",
    "VideoPlayer": "ElgVideoPlayer", "VideoFeed": "ElgVideoFeed", "TrayIcon": "ElgTrayIcon",
    "ModuleEntry": "ElgModuleEntry", "Timer": "ElgTimer",
    "Prop<float>": "ElgPropF32", "Prop<Color>": "ElgPropColor",
    # Closures' results, written through the C API (elg_string_assign(), ...).
    "std::string": "ElgCppString", "Font": "ElgCppFont", "ShapeStyle": "ElgCppShapeStyle",
    "std::optional<float>": "ElgOptFloat", "std::vector<TextSpan>": "ElgTextSpans",
    "std::vector<ForEachItem>": "ElgForEachItems", "std::vector<std::string>": "ElgStrings",
    "std::vector<ItemKey>": "ElgItemKeys",
}
OPAQUE_DOCS = {
    "ElgCppString": "a C++ std::string: a closure's string result, set with elg_string_assign()",
    "ElgCppFont": "an elg::Font: a closure's font result",
    "ElgCppShapeStyle": "an elg::ShapeStyle: a closure's style result",
    "ElgOptFloat": "a std::optional<float>: a closure's optional result",
}


class Failure(Exception):
    pass


def blank_comments(text):
    """`text` with its comments (and string literals) blanked out, keeping every offset."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', blank, text, flags=re.S)


def doc_above(text, offset):
    """The /// lines right above the line at `offset`."""
    lines = text[:offset].split("\n")[:-1]
    doc = []
    while lines and lines[-1].lstrip().startswith("///"):
        doc.insert(0, lines.pop().strip())
    return doc


def split_top(text, separator=","):
    """`text` split at `separator`s outside of <>, () and []."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth -= 1
        elif c == separator and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return [p.strip() for p in parts if p.strip()]


class Types:
    """Maps the C++ types of the API's signatures and fields to C ones."""

    def __init__(self):
        self.defined = set()  # Elg* structs and typedefs
        self.fields = {}  # each struct's field names, in order
        self.used_opaque = {}  # C name -> the C++ type it stands for

    def base(self, cpp, where):
        if cpp in PRIMITIVES:
            return cpp
        if cpp.startswith("Elg") and cpp in self.defined:
            return cpp
        if cpp in OPAQUE:
            self.used_opaque[OPAQUE[cpp]] = cpp
            return OPAQUE[cpp]
        if match := re.fullmatch(r"std::shared_ptr<const (\w+Style)>", cpp):
            name = "Elg" + match[1]
            self.used_opaque[name] = cpp
            return name
        if match := re.fullmatch(r"(\w+Configuration)", cpp):
            name = "Elg" + match[1]
            self.used_opaque[name] = cpp
            return name
        raise Failure(f"{where}: no C type for the C++ type {cpp!r}: add it to OPAQUE in tools/c-header.py")

    def c(self, cpp, where):
        """The C spelling of the C++ type `cpp` (a parameter's, field's or result's)."""
        t = " ".join(cpp.replace("*", " * ").split())
        t = re.sub(r"\b(?:elg|detail|elg_shim)::", "", t)
        t = re.sub(r"\s*([<>,])\s*", r"\1", t).replace("const ", "const ") if "<" in t else t
        stars = []
        while t.endswith("*") or t.endswith(" const"):
            if t.endswith("*"):
                stars.insert(0, "*")
                t = t[:-1].rstrip()
            else:
                stars.insert(0, " const")
                t = t[:-len(" const")].rstrip()
        lead = ""
        if t.startswith("const "):
            lead, t = "const ", t[len("const "):]
        t = re.sub(r"<const(\w)", r"<const \1", t)
        return lead + self.base(t, where) + "".join(stars).replace(" const", " const")


def parse_param(param, types, where):
    """A parameter `type name` in C."""
    match = re.fullmatch(r"(.+?)\s*\b(\w+)", param)
    if not match or match[1].endswith("("):
        raise Failure(f"{where}: can't read the parameter {param!r}")
    ctype = types.c(match[1], where)
    return f"{ctype} {match[2]}"


def c_params(params, types, where):
    params = " ".join(params.split())
    if not params or params == "void":
        return "void"
    return ", ".join(parse_param(p, types, where) for p in split_top(params))


def c_result(result, types, where):
    return types.c(result, where) + " "


def align_comments(lines):
    """`lines` (a struct's fields) with their trailing ///< comments lined up."""
    split = [re.match(r"^(.*?;)\s*(///<.*)?$", line) if not line.lstrip().startswith("//") else None for line in lines]
    width = max((len(m[1]) for m in split if m and m[2]), default=0)
    return [line if not m or not m[2] else f"{m[1]:<{width}} {m[2]}" for line, m in zip(lines, split)]


# --- Types: structs and callbacks ---

STRUCT = re.compile(r"^(?P<typedef>typedef )?struct (?P<name>Elg\w+) \{\n(?P<body>.*?)^\}(?: (?P=name))?;", re.M | re.S)
CALLBACK = re.compile(r"^typedef (?P<result>[^;(]+?)\s*\(\*(?P<name>Elg\w+)\)\((?P<params>[^;]*)\);|"
                      r"^using (?P<uname>Elg\w+) = (?P<uresult>[^;(]+?)\s*\(\*\)\((?P<uparams>[^;]*)\);", re.M)
FUNCTION_FIELD = re.compile(r"^\s*(?P<result>[^;(]+?)\s*\(\*(?P<name>\w+)\)\((?P<params>[^;]*)\);(?P<rest>.*)$")
FIELD = re.compile(r"^(?P<indent>\s*)(?P<type>.+?)\s*\b(?P<names>\w+(?:\[\w+\])?(?:\s*,\s*\w+(?:\[\w+\])?)*)\s*;"
                   r"(?P<rest>.*)$")


def field_names(body):
    """The names of the fields of a struct whose body is `body`, in order."""
    names = []
    for line in blank_comments(body).split("\n"):
        if not line.strip():
            continue
        if pointer := FUNCTION_FIELD.match(line):
            names.append(pointer["name"])
        elif field := FIELD.match(line):
            names += [re.sub(r"\[.*\]", "", n).strip() for n in field["names"].split(",")]
    return names


def collect_types(path, types):
    """The structs and callback typedefs `path` defines, in order, as C."""
    text = path.read_text(encoding="utf-8")
    plain = blank_comments(text)
    found = []
    for match in STRUCT.finditer(plain):
        found.append((match.start(), "struct", match))
    for match in CALLBACK.finditer(plain):
        found.append((match.start(), "callback", match))
    out = []
    for offset, kind, match in sorted(found, key=lambda f: f[0]):
        where = f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, offset) + 1}"
        doc = doc_above(text, offset)
        if kind == "struct":
            name = match["name"]
            body = text[match.start("body"):match.end("body")]
            types.defined.add(name)  # a field may point to its own struct
            fields = []
            types.fields[name] = field_names(body)
            for line in body.split("\n"):
                code = line.split("///")[0].split("//")[0].rstrip()
                if not code.strip():
                    if line.strip():
                        fields.append(line.rstrip())  # a comment line
                    continue
                if pointer := FUNCTION_FIELD.match(line):
                    fields.append(f"    {c_result(pointer['result'], types, where)}(*{pointer['name']})("
                                  f"{c_params(pointer['params'], types, where)});{pointer['rest']}".rstrip())
                    continue
                field = FIELD.match(line)
                if not field:
                    raise Failure(f"{where}: can't read the field {line.strip()!r} of {name}")
                # Inside its own definition, a struct's typedef name isn't declared yet.
                ctype = re.sub(rf"\b{name}\b", f"struct {name}", types.c(field["type"], where))
                fields.append(f"    {ctype} {field['names']};{field['rest']}".rstrip())
            out.append("\n".join(doc + [f"typedef struct {name} {{"] + align_comments(fields) + [f"}} {name};"]))
            types.defined.add(name)
        else:
            name = match["name"] or match["uname"]
            result = match["result"] or match["uresult"]
            params = match["params"] if match["name"] else match["uparams"]
            out.append("\n".join(doc + [f"typedef {c_result(result, types, where)}(*{name})("
                                        f"{c_params(params, types, where)});"]))
            types.defined.add(name)
    return out


# --- Functions ---

EXPORT = re.compile(r"\bELG_EXPORT\b(?P<sig>[^{;]*)\{")
SIGNATURE = re.compile(r"\s*(?P<result>.*?)\b(?P<name>elg_\w+)\s*\((?P<params>.*)\)\s*$", re.S)
DEFINE = re.compile(r"^#define (?P<name>\w+)\((?P<params>[^)]*)\)[ \t]*\\\n(?P<body>(?:.*\\\n)*.*)$", re.M)
SECTION = re.compile(r"^// --- (?P<title>.+) ---$", re.M)


def macro_docs(text, offset):
    """The functions a macro's comment describes: `elg_<x>_y` template -> its description."""
    lines = text[:offset].split("\n")[:-1]
    block = []
    while lines and lines[-1].startswith("//"):
        block.insert(0, lines.pop())
    docs, current = {}, None
    for line in block:
        if entry := re.match(r"^//   (elg_\S+?)\([^)]*\): (.*)$", line):
            current = entry[1]
            docs[current] = entry[2]
        elif current and (more := re.match(r"^//     (.*)$", line)):
            docs[current] += " " + more[1]
        else:
            current = None
    return docs


def wrap_doc(text, width=100):
    """`text` as /// lines."""
    text = text[:1].upper() + text[1:]
    lines, line = [], "///"
    for word in text.split():
        if len(line) + 1 + len(word) > width:
            lines.append(line)
            line = "///"
        line += " " + word
    lines.append(line)
    return lines


def declaration(sig, types, where):
    match = SIGNATURE.fullmatch(sig)
    if not match:
        raise Failure(f"{where}: can't read the signature {sig.strip()!r}")
    return match["name"], f"{c_result(match['result'], types, where)}{match['name']}(" \
                          f"{c_params(match['params'], types, where)});"


def collect_functions(path, types):
    """The declarations of the functions `path` exports, in order, with their docs and sections."""
    text = path.read_text(encoding="utf-8")
    plain = blank_comments(text)
    macros = {}
    spans = []
    for match in DEFINE.finditer(plain):
        spans.append((match.start(), match.end()))
        body = match["body"].replace("\\\n", "\n")
        exports = [m["sig"] for m in EXPORT.finditer(body)]
        if exports:
            macros[match["name"]] = ([p.strip() for p in match["params"].split(",")], exports,
                                     macro_docs(text, match.start()))
    items = []
    for match in EXPORT.finditer(plain):
        if any(start <= match.start() < end for start, end in spans):
            continue
        items.append((match.start(), "export", match))
    for match in SECTION.finditer(text):
        items.append((match.start(), "section", match))
    if macros:
        invocation = re.compile(rf"^(?P<macro>{'|'.join(macros)})\((?P<args>[^)]*)\)$", re.M)
        for match in invocation.finditer(plain):
            items.append((match.start(), "macro", match))
    out, names = [], []
    for offset, kind, match in sorted(items, key=lambda i: i[0]):
        where = f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, offset) + 1}"
        if kind == "section":
            out.append(f"// --- {match['title']} ---")
        elif kind == "export":
            name, decl = declaration(match["sig"], types, where)
            doc = doc_above(text, offset)
            if not doc:
                raise Failure(f"{where}: {name} has no doc comment")
            out.append("\n".join(doc + [decl]))
            names.append(name)
        else:
            params, exports, docs = macros[match["macro"]]
            args = [a.strip() for a in match["args"].split(",")]
            for sig in exports:
                template = re.search(r"elg_\S*?(?=\s*\()", sig)[0]
                expanded = sig
                for param, arg in zip(params, args):
                    template = template.replace(f"##{param}##", f"<{param}>").replace(f"##{param}", f"<{param}>")
                    expanded = re.sub(rf"##\s*\b{param}\b\s*##|##\s*\b{param}\b|\b{param}\b\s*##|\b{param}\b", arg,
                                      expanded)
                name, decl = declaration(expanded, types, where)
                if template not in docs:
                    raise Failure(f"{where}: the comment above {match['macro']} doesn't describe {template}")
                doc = docs[template]
                for param, arg in zip(params, args):
                    doc = doc.replace(f"<{param}>", arg)
                out.append("\n".join(wrap_doc(doc) + [decl]))
                names.append(name)
    return out, names


# --- The header ---

def gpu_image_part():
    """The C part of include/elg/gpu_image.h, between its extern "C" braces, without ELG_C_FUNCTION
    (which exports them from the C library, ELG_API from a shared Elegance)."""
    text = GPU_IMAGE.read_text(encoding="utf-8")
    match = re.search(r'#ifdef __cplusplus\nextern "C" \{\n#endif\n(.*?)\n#ifdef __cplusplus\n\} // extern "C"', text,
                      re.S)
    if not match:
        raise Failure(f"{GPU_IMAGE.relative_to(ROOT)}: can't find its C part")
    part = match[1].strip("\n")
    names = re.findall(r"^ELG_C_FUNCTION [^(]*?\b(elg_\w+)\(", part, re.M)
    return part.replace("ELG_C_FUNCTION ", ""), names


def constants():
    core = (ROOT / "c" / "core.cpp").read_text(encoding="utf-8")
    kinds = re.search(r"enum ErrorKind : int32_t \{(.*?)\};", core, re.S)
    abi = re.search(r"constexpr std::uint32_t kAbiVersion = (\d+);", core)
    if not kinds or not abi:
        raise Failure("c/core.cpp: can't find ErrorKind or kAbiVersion")
    lines = ["/// The ABI version this header declares: what elg_abi_version() returns for a library that",
             "/// matches it. A binding refuses to run with a library whose version differs.",
             f"#define ELG_ABI_VERSION {abi[1]}u",
             "",
             "/// The kinds of error elg_take_error() returns."]
    for name, value in re.findall(r"(\w+) = (\d+),", kinds[1]):
        lines.append(f"#define ELG_{name.upper()} {value}")
    return "\n".join(lines)


def generate():
    types = Types()
    gpu, gpu_names = gpu_image_part()
    types.defined.update(re.findall(r"\b(Elg\w+)\b", gpu))
    for match in STRUCT.finditer(blank_comments(gpu)):
        types.fields[match["name"]] = field_names(match["body"])
    type_parts = collect_types(SHIM, types)
    for path in SOURCES:
        type_parts += collect_types(path, types)
    function_parts, names = [], list(gpu_names)
    for path in SOURCES:
        parts, found = collect_functions(path, types)
        function_parts += parts
        names += found
    duplicates = sorted({n for n in names if names.count(n) > 1})
    if duplicates:
        raise Failure(f"declared more than once: {', '.join(duplicates)}")
    opaque = []
    for name, cpp in sorted(types.used_opaque.items()):
        what = OPAQUE_DOCS.get(name, f"a handle to an elg::{cpp}" if "<" not in cpp else f"a handle to a {cpp}")
        opaque.append(f"/// {what[:1].upper() + what[1:]}.\ntypedef struct {name} {name};")
    header = f"""{LICENSE}

// Elegance's C API, for bindings and C programs. Generated by tools/c-header.py from the C++ that
// implements it (c/*.cpp, c/shim.h, include/elg/gpu_image.h); don't edit.
//
// Errors: a function that fails records an error for the calling thread (elg_take_error()) and
// returns the fallback its doc names (null for a pointer). Strings are ElgStr: UTF-8, not
// NUL-terminated. Closures are ElgClosure: a null `call` is no closure, and Elegance calls a non-null
// `drop` exactly once. Ownership: a pointer argument named take_* (and each handle of an array) is
// consumed, also when the call fails; other pointer arguments are borrowed; returned handles are
// the caller's, freed with the function named after their type (elg_view_free(), ...). Threads: a
// function is for the UI thread unless its doc says "Callable from any thread"; called on another
// thread while an App runs, it fails with a usage error.

#ifndef ELG_C_ELEGANCE_H
#define ELG_C_ELEGANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

{constants()}

// --- Handles ---

{chr(10).join(opaque)}

// --- GPU images (elg/gpu_image.h) ---

{gpu}

// --- Structs and callbacks ---

{(chr(10) * 2).join(type_parts)}

{(chr(10) * 2).join(function_parts)}

#ifdef __cplusplus
}} // extern "C"
#endif

#endif // ELG_C_ELEGANCE_H
"""
    header = re.sub(r"(// --- .+ ---)\n\n(// --- .+ ---)", r"\2", header)  # sections without functions
    functions = f"""{LICENSE}

// Generated by tools/c-header.py; don't edit. Refers to every function c/include/elg-c/elegance.h
// declares: a C program that links this shows that each is defined.

#include "c_api_reference.h"

const ElgAnyFunction elg_c_api_functions[] = {{
{chr(10).join(f"    (ElgAnyFunction)&{name}," for name in names)}
}};
const size_t elg_c_api_function_count = sizeof(elg_c_api_functions) / sizeof(elg_c_api_functions[0]);
"""
    check_reported(types.fields)
    return {HEADER: header, FUNCTIONS: functions, LAYOUT_FIELDS: layout_fields(types.fields),
            LAYOUTS: layouts(types.fields)}, len(names)


def check_reported(fields):
    """Fails unless every struct is reported by an elg_layouts_* function (ELG_LAYOUT), and only once."""
    reported = []
    for path in SOURCES:
        reported += re.findall(r"\bELG_LAYOUT\((\w+)\)", blank_comments(path.read_text(encoding="utf-8")))
    missing = sorted(set(fields) - set(reported))
    unknown = sorted(set(reported) - set(fields))
    twice = sorted({name for name in reported if reported.count(name) > 1})
    if missing or unknown or twice:
        raise Failure("the elg_layouts_* functions must report each struct once (ELG_LAYOUT): "
                      + "; ".join(f"{what}: {', '.join(names)}" for what, names in
                                  (("missing", missing), ("not a struct", unknown), ("twice", twice)) if names))


def layout_fields(fields):
    """c/layout_fields.h: each struct's fields, for ELG_LAYOUT."""
    lines = [f"#define ELG_FIELDS_{name}(X) " + " ".join(f"X({name}, {field})" for field in names)
             for name, names in fields.items()]
    return f"""{LICENSE}

// Generated by tools/c-header.py; don't edit. For each struct that crosses the C API,
// ELG_FIELDS_<struct>(X) is X(<struct>, <field>) for each of its fields, in order: what ELG_LAYOUT
// (shim.h) reports of it.

#ifndef ELG_C_LAYOUT_FIELDS_H
#define ELG_C_LAYOUT_FIELDS_H

{chr(10).join(lines)}

#endif // ELG_C_LAYOUT_FIELDS_H
"""


def layouts(fields):
    """tests/c_api_layouts.c: each struct's layout, as a C compiler sees the header's declaration."""
    tables = [f"static const ElgFieldReference fields_{name}[] = {{"
              + ", ".join(f"{{\"{field}\", offsetof({name}, {field})}}" for field in names) + "};"
              for name, names in fields.items()]
    entries = [f"    {{\"{name}\", sizeof({name}), _Alignof({name}), fields_{name}, {len(names)}}},"
               for name, names in fields.items()]
    return f"""{LICENSE}

// Generated by tools/c-header.py; don't edit. The layout of every struct c/include/elg-c/elegance.h
// declares, as the C compiler sees it: the C tests compare it with what the library reports.

#include "c_api_reference.h"

#include <stddef.h>

{chr(10).join(tables)}

const ElgLayoutReference elg_c_api_layouts[] = {{
{chr(10).join(entries)}
}};
const size_t elg_c_api_layout_count = sizeof(elg_c_api_layouts) / sizeof(elg_c_api_layouts[0]);
"""


def main():
    check = "--check" in sys.argv[1:]
    try:
        outputs, count = generate()
    except Failure as failure:
        print(f"c-header: {failure}")
        return 1
    stale = []
    for path, text in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != text:
            if check:
                stale.append(str(path.relative_to(ROOT)))
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
    if stale:
        print(f"c-header: {', '.join(stale)} isn't up to date with c/: run python3 tools/c-header.py")
        return 1
    print(f"c-header: {HEADER.relative_to(ROOT)} declares {count} functions"
          + (" and is up to date" if check else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
