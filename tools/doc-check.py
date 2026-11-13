#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks that Elegance's public API is documented, and fails if any of it isn't. CI runs it on
# every pull request.
#
#   python3 tools/doc-check.py build/linux-release                      everything
#   python3 tools/doc-check.py build/linux-release include/elg/views.h  only these files
#
# The public API is what include/elg declares in namespace elg (not elg::detail, nothing private),
# and the C API: the elg_* functions c/*.cpp exports and the Elg* types it uses. Each of these
# needs a /// comment. A function's comment needs an @param for every named parameter and, unless
# it returns void, an @return; a class template's needs an @tparam per template parameter.
# Constructors, destructors, conversion operators, defaulted and deleted members, copying and
# moving are exempt from what doesn't apply to them. It also reports clang's -Wdocumentation
# warnings in those files: an @param that names no parameter, an unknown command, ...
#
# A C API function a macro defines (its name built with ##) can't have a doc comment, since a
# comment can't go inside a macro; document it above the #define instead. This check skips them.
#
# It parses with libclang 19 (Debian and Ubuntu: python3-clang-19) and the compile flags of the
# build folder's compile_commands.json, so the folder only needs to be configured.

import argparse
import json
import pathlib
import re
import shlex
import sys

import clang.cindex as ci

ROOT = pathlib.Path(__file__).resolve().parent.parent
K = ci.CursorKind

FUNCTIONS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.FUNCTION_TEMPLATE, K.CONVERSION_FUNCTION}
TYPES = {K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.UNION_DECL, K.ENUM_DECL, K.TYPE_ALIAS_DECL,
         K.TYPE_ALIAS_TEMPLATE_DECL, K.TYPEDEF_DECL, K.CONCEPT_DECL}
RECORDS = {K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.UNION_DECL}
TEMPLATE_PARAMETERS = {K.TEMPLATE_TYPE_PARAMETER, K.TEMPLATE_NON_TYPE_PARAMETER, K.TEMPLATE_TEMPLATE_PARAMETER}
PARAM = re.compile(r"[@\\]param(?:\[[a-z,]+\])?\s+(\w+)")
TPARAM = re.compile(r"[@\\]tparam\s+(\w+)")
RETURN = re.compile(r"[@\\]returns?\b")
DOC_WARNINGS = ["-Wdocumentation", "-Wdocumentation-unknown-command", "-Wno-unknown-warning-option"]


def load_libclang():
    try:
        ci.Index.create()
    except ci.LibclangError:
        for candidate in ("/usr/lib/llvm-19/lib/libclang-19.so.1", "/usr/lib/llvm-19/lib/libclang.so.1"):
            if pathlib.Path(candidate).exists():
                ci.Config.set_library_file(candidate)
                break
    return ci.Index.create()


def compile_commands(build):
    """Each source's compiler arguments without the compiler, the source, and the output."""
    commands = {}
    for entry in json.loads((build / "compile_commands.json").read_text()):
        source = pathlib.Path(entry["directory"], entry["file"]).resolve()
        args = entry.get("arguments") or shlex.split(entry["command"])
        kept, skip = [], False
        for arg in args[1:]:
            if skip:
                skip = False
            elif arg in ("-o", "-c", "-MF", "-MT", "-MQ"):
                skip = arg != "-c"
            elif pathlib.Path(entry["directory"], arg).resolve() != source:
                kept.append(arg)
        commands[source] = (kept, entry["directory"])
    return commands


def hidden(cursor):
    """Whether the cursor is in a detail or anonymous namespace, or inside a private class."""
    parent = cursor.semantic_parent
    while parent is not None and parent.kind != K.TRANSLATION_UNIT:
        if parent.kind == K.NAMESPACE and parent.spelling in ("detail", ""):
            return True
        if parent.kind in RECORDS and parent.access_specifier == ci.AccessSpecifier.PRIVATE:
            return True
        parent = parent.semantic_parent
    return False


def exempt(cursor):
    if cursor.kind in (K.CXX_METHOD, K.CONSTRUCTOR, K.CONVERSION_FUNCTION) and \
            (cursor.is_deleted_method() or cursor.is_default_method()):
        return True
    if cursor.kind == K.CONSTRUCTOR and (cursor.is_copy_constructor() or cursor.is_move_constructor()):
        return True
    return cursor.kind == K.CXX_METHOD and \
        (cursor.is_copy_assignment_operator_method() or cursor.is_move_assignment_operator_method())


def function_problems(cursor, doc):
    if not doc:
        return ["no /// comment"]
    problems = []
    named = set(PARAM.findall(doc))
    for child in cursor.get_children():
        if child.kind == K.PARM_DECL and child.spelling and child.spelling not in named:
            problems.append(f"no @param {child.spelling}")
    constructor = cursor.kind == K.CONSTRUCTOR or \
        (cursor.kind == K.FUNCTION_TEMPLATE and cursor.spelling == cursor.semantic_parent.spelling)
    if not constructor and cursor.kind != K.CONVERSION_FUNCTION and \
            cursor.result_type.spelling != "void" and not RETURN.search(doc):
        problems.append("no @return")
    return problems


def check(tu, files, c_api, report):
    seen = set()

    def macro_defined(cursor):
        """Whether a macro builds the function's name: its source is a macro call without the name."""
        return all(token.spelling != cursor.spelling for token in cursor.get_tokens())

    def visit(parent):
        for cursor in parent.get_children():
            if cursor.location.file is None:
                continue
            path = pathlib.Path(cursor.location.file.name).resolve()
            if path not in files:
                continue
            kind = cursor.kind
            if kind == K.NAMESPACE:
                if cursor.spelling not in ("detail", ""):
                    visit(cursor)
                continue
            if kind in (K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
                visit(cursor)
                continue
            if cursor.access_specifier == ci.AccessSpecifier.PRIVATE or hidden(cursor) or \
                    kind in (K.FRIEND_DECL, K.DESTRUCTOR) or (kind in FUNCTIONS and cursor.canonical != cursor):
                continue
            key = (path, cursor.location.line, cursor.spelling, kind)
            if key in seen:
                continue
            seen.add(key)
            doc = cursor.raw_comment or ""
            where = f"{path.relative_to(ROOT)}:{cursor.location.line}: {cursor.displayname or cursor.spelling}"
            if kind in FUNCTIONS:
                if c_api and not (cursor.spelling.startswith("elg_") and cursor.is_definition() and
                                  cursor.linkage == ci.LinkageKind.EXTERNAL) or \
                        c_api and macro_defined(cursor):
                    continue
                if not exempt(cursor):
                    report.extend(f"{where}: {problem}" for problem in function_problems(cursor, doc))
            elif kind in TYPES:
                if not cursor.spelling or (c_api and not cursor.spelling.startswith("Elg")):
                    continue
                if kind in RECORDS | {K.ENUM_DECL} and not cursor.is_definition():
                    continue
                if not doc:
                    report.append(f"{where}: no /// comment")
                elif kind == K.CLASS_TEMPLATE:
                    named = set(TPARAM.findall(doc))
                    report.extend(f"{where}: no @tparam {t.spelling}" for t in cursor.get_children()
                               if t.kind in TEMPLATE_PARAMETERS and t.spelling and t.spelling not in named)
                if kind in RECORDS:
                    visit(cursor)
            elif kind == K.FIELD_DECL or (kind == K.VAR_DECL and not c_api):
                if not doc:
                    report.append(f"{where}: no /// comment")

    visit(tu.cursor)
    for diagnostic in tu.diagnostics:
        if diagnostic.location.file is None or diagnostic.severity < ci.Diagnostic.Warning:
            continue
        path = pathlib.Path(diagnostic.location.file.name).resolve()
        if path in files and ("documentation" in diagnostic.option or diagnostic.severity >= ci.Diagnostic.Error):
            report.append(f"{path.relative_to(ROOT)}:{diagnostic.location.line}: {diagnostic.spelling}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", help="a configured build folder")
    parser.add_argument("files", nargs="*", help="only check these files (default: the whole public API)")
    args = parser.parse_args()

    build = pathlib.Path(args.build).resolve()
    if not (build / "compile_commands.json").exists():
        sys.exit(f"{build} has no compile_commands.json")
    commands = compile_commands(build)
    headers = sorted(path.resolve() for path in (ROOT / "include/elg").rglob("*.h"))
    c_sources = sorted(path for path in commands if path.parent == ROOT / "c")
    c_headers = {(ROOT / "c/shim.h").resolve()}
    if args.files:
        wanted = {(ROOT / name).resolve() for name in args.files}
        headers = [path for path in headers if path in wanted]
        c_sources = [path for path in c_sources if path in wanted] or \
            (c_sources[:1] if c_headers & wanted else [])
        c_headers &= wanted

    index = load_libclang()
    report = []
    if headers:
        # One translation unit that includes every public header, with the flags Elegance's own
        # sources compile with.
        base = commands.get((ROOT / "c/core.cpp").resolve()) or \
            next(command for path, command in commands.items() if path.is_relative_to(ROOT / "src"))
        source = "public_headers.cpp"
        tu = index.parse(source, args=["-x", "c++", *base[0], *DOC_WARNINGS, f"-working-directory={base[1]}"],
                         unsaved_files=[(source, "".join(f'#include "{h}"\n' for h in headers))],
                         options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
        check(tu, set(headers), False, report)
    for source in c_sources:
        flags, directory = commands[source]
        # Not skipping function bodies: a function whose body is skipped isn't a definition.
        tu = index.parse(str(source), args=[*flags, *DOC_WARNINGS, f"-working-directory={directory}"])
        check(tu, {source} | c_headers, True, report)
        c_headers = set()

    if report:
        print("\n".join(report))
        print(f"\ndoc-check: {len(report)} undocumented or misdocumented declarations in the public API")
        return 1
    print("doc-check: the public API is documented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
