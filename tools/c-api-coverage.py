#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks tools/c-api-coverage.txt, the list of what in the C API offers each public declaration of
# the C++ API (ELGSPEC-0007.R20), against the public headers and the C API's header.
#
#   python3 tools/c-api-coverage.py build/linux-release              check (CI)
#   python3 tools/c-api-coverage.py build/linux-release --summary    also count the entries by kind
#   python3 tools/c-api-coverage.py build/linux-release --generate   add the declarations the list
#                                                                    doesn't name, with a guess
#
# The public C++ API is what include/elg declares in namespace elg, as tools/doc-check.py walks it:
# not elg::detail, nothing private, and without destructors, friend declarations, and defaulted,
# deleted, copying and moving members (a type's C entry covers those). Enum values count, one by one.
#
# Each line of the list is a declaration, " => ", and what offers it in the C API:
#   elg_name, elg_other   C API functions (one or more, separated by ", ")
#   ElgType               a C type: an opaque handle, or a struct or value that crosses the C API
#   ElgType.field         a field of a C struct (also for an enum value, the field that takes it)
#   exempt: <reason>      type-erasure, template or macro: belongs to the C++ language
#   missing               nothing yet; the check fails while any entry says so
# Lines that start with # are comments. The check fails when a public declaration isn't in the
# list, when the list names one that no longer exists, when it names C API that the header doesn't
# declare, and when an entry is missing.
#
# --generate guesses an entry for each declaration the list doesn't name: from the C functions whose
# doc comments say "Wraps `X`", C structs named after the C++ ones with fields of the same name, the
# C types of handles, and the C API that names an enum's type in its comments; otherwise "missing".
# The guesses go at the end of the list, to be checked by hand.
#
# It parses with libclang 19 (Debian and Ubuntu: python3-clang-19), as tools/doc-check.py does.

import argparse
import collections
import importlib.util
import pathlib
import re
import sys

sys.dont_write_bytecode = True  # importing tools/doc-check.py leaves no __pycache__

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIST = ROOT / "tools" / "c-api-coverage.txt"
C_HEADER = ROOT / "c" / "include" / "elg-c" / "elegance.h"
EXEMPTIONS = {"type-erasure", "template", "macro"}
SEPARATOR = " => "


def doc_check():
    """tools/doc-check.py, as a module: its libclang setup and how it walks the public API."""
    spec = importlib.util.spec_from_file_location("doc_check", ROOT / "tools" / "doc-check.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def qualified(cursor, ci):
    """`elg::Type::member`, with a function's parameter types."""
    parts = []
    node = cursor
    while node is not None and node.kind != ci.CursorKind.TRANSLATION_UNIT:
        if node.spelling:
            name = node.displayname if node is cursor and node.kind in FUNCTION_KINDS(ci) else node.spelling
            parts.insert(0, name)
        node = node.semantic_parent
    return "::".join(parts)


def FUNCTION_KINDS(ci):
    K = ci.CursorKind
    return {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.FUNCTION_TEMPLATE, K.CONVERSION_FUNCTION}


def public_declarations(dc, build):
    """Each public declaration's key (qualified name) -> its kind ('type', 'function', 'field',
    'value', 'variable') and where it is."""
    ci = dc.ci
    K = ci.CursorKind
    commands = dc.compile_commands(build)
    headers = sorted(path.resolve() for path in (ROOT / "include/elg").rglob("*.h"))
    base = commands.get((ROOT / "c/core.cpp").resolve()) or \
        next(command for path, command in commands.items() if path.is_relative_to(ROOT / "src"))
    source = "public_headers.cpp"
    tu = dc.load_libclang().parse(source, args=["-x", "c++", *base[0], f"-working-directory={base[1]}"],
                                  unsaved_files=[(source, "".join(f'#include "{h}"\n' for h in headers))],
                                  options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    files = set(headers)
    found = {}

    def add(cursor, kind):
        key = qualified(cursor, ci)
        if key.startswith("elg::"):
            path = pathlib.Path(cursor.location.file.name).resolve()
            found.setdefault(key, (kind, f"{path.relative_to(ROOT)}:{cursor.location.line}"))

    def visit(parent):
        for cursor in parent.get_children():
            if cursor.location.file is None or pathlib.Path(cursor.location.file.name).resolve() not in files:
                continue
            kind = cursor.kind
            if kind == K.NAMESPACE:
                if cursor.spelling not in ("detail", ""):
                    visit(cursor)
                continue
            if kind in (K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
                visit(cursor)
                continue
            if cursor.access_specifier == ci.AccessSpecifier.PRIVATE or dc.hidden(cursor) or \
                    kind in (K.FRIEND_DECL, K.DESTRUCTOR):
                continue
            if kind in dc.FUNCTIONS:
                if cursor.canonical == cursor and not dc.exempt(cursor):
                    add(cursor, "function")
            elif kind in dc.TYPES:
                if not cursor.spelling or (kind in dc.RECORDS | {K.ENUM_DECL} and not cursor.is_definition()):
                    continue
                add(cursor, "type")
                if kind in dc.RECORDS:
                    visit(cursor)
                elif kind == K.ENUM_DECL:
                    for value in cursor.get_children():
                        if value.kind == K.ENUM_CONSTANT_DECL:
                            add(value, "value")
            elif kind == K.FIELD_DECL:
                add(cursor, "field")
            elif kind == K.VAR_DECL:
                add(cursor, "variable")

    visit(tu.cursor)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        sys.exit("c-api-coverage: the public headers don't parse:\n" + "\n".join(str(d) for d in errors[:10]))
    return found


def c_api():
    """The C API's functions, types, and each struct's fields, from its header."""
    text = C_HEADER.read_text(encoding="utf-8")
    plain = re.sub(r"//[^\n]*", "", text)
    functions = set(re.findall(r"\b(elg_\w+)\s*\(", plain))
    types = set(re.findall(r"\b(Elg\w+)\b", plain))
    fields = {}
    for match in re.finditer(r"typedef struct (Elg\w+) \{(.*?)\} \1;", plain, re.S):
        names = set()
        for declaration in match[2].split(";"):
            declaration = declaration.strip()
            if not declaration:
                continue
            for part in declaration.split(","):
                name = re.search(r"(\w+)\s*(?:\[[^\]]*\])?\s*$", part.strip())
                if name:
                    names.add(name[1])
        fields[match[1]] = names
    return functions, types, fields, text


def read_list():
    entries = {}
    problems = []
    if not LIST.exists():
        return entries, problems
    for number, line in enumerate(LIST.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        if SEPARATOR not in line:
            problems.append(f"{LIST.relative_to(ROOT)}:{number}: no '{SEPARATOR.strip()}'")
            continue
        key, value = (part.strip() for part in line.split(SEPARATOR, 1))
        if key in entries:
            problems.append(f"{LIST.relative_to(ROOT)}:{number}: {key} is listed twice")
        entries[key] = (value, number)
    return entries, problems


def check_value(value, functions, types, fields):
    """What's wrong with what an entry names, or None."""
    if value == "missing":
        return "missing: no C API offers it yet"
    if value.startswith("exempt:"):
        reason = value.removeprefix("exempt:").strip()
        return None if reason in EXEMPTIONS else f"unknown exemption {reason!r}"
    for name in (part.strip() for part in value.split(",")):
        if name.startswith("elg_"):
            if name not in functions:
                return f"the C API declares no {name}()"
        elif "." in name:
            struct, field = name.split(".", 1)
            if field not in fields.get(struct, set()):
                return f"the C API's {struct} has no field {field}"
        elif name not in types:
            return f"the C API declares no type {name}"
    return None


# --- Guesses for --generate ---

def wrapped(text):
    """C++ names that C API functions say they wrap ("Wraps `View::on_appear()`") -> those functions."""
    result = collections.defaultdict(list)
    for source in sorted((ROOT / "c").glob("*.cpp")):
        lines = source.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            match = re.search(r"Wraps `([^`]+)`", line)
            if not match or not line.lstrip().startswith("///"):
                continue
            for following in lines[index + 1:index + 40]:
                export = re.search(r"ELG_EXPORT\b.*?\b(elg_\w+)\s*\(", following)
                if export:
                    name = re.sub(r"\(.*", "", match[1]).strip()
                    result[name.removeprefix("elg::")].append(export[1])
                    break
                if not following.lstrip().startswith("///"):
                    break
    return result


# Class templates whose role a binding takes with its own types, and type-erasure helpers.
TEMPLATES = {"Prop", "Binding", "State", "Computed", "Signal", "Effect", "Environment"}
TYPE_ERASURE = {"MoveOnlyFunction"}


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def by_name(parts, functions):
    """C API functions named after the declaration: View::x -> elg_mod_x, Type::x -> elg_type_x,
    x -> elg_x."""
    if len(parts) == 1:
        candidates = [f"elg_{parts[0]}"]
    else:
        owner, member = parts[-2], parts[-1]
        candidates = [f"elg_{snake(owner)}_{member}"]
        if owner == "View":
            candidates.insert(0, f"elg_mod_{member}")
    return [name for name in candidates if name in functions]


def taking(type_name, entries_by_key):
    """C API functions offering a declared function that takes `type_name` as a parameter."""
    pattern = re.compile(rf"[(,]\s*(?:const\s+)?(?:elg::)?{re.escape(type_name)}\s*[&*]?\s*[,)]")
    found = []
    for key, value in entries_by_key.items():
        if "(" in key and pattern.search(key[key.index("("):]) and value.startswith("elg_"):
            found.extend(part.strip() for part in value.split(","))
    return list(dict.fromkeys(found))


def guess_from_docs(key, kind, wraps, functions, types, fields, header_text, handles):
    """A guess from the C API's doc comments and types, or None."""
    name = re.sub(r"\(.*", "", key).removeprefix("elg::")
    parts = name.split("::")
    if kind == "function":
        if name in wraps:
            return ", ".join(dict.fromkeys(wraps[name]))
        short = parts[-1]
        if short.startswith("operator") and len(parts) > 1 and f"Elg{parts[-2]}" in types:
            return f"Elg{parts[-2]}"
        if len(parts) > 1 and parts[-1] == parts[-2] and f"Elg{parts[-1]}" in types:
            return f"Elg{parts[-1]}"  # a value type's constructor: its C struct
    if kind == "type":
        if parts[-1] in handles:
            return handles[parts[-1]]
        if f"Elg{parts[-1]}" in types:
            return f"Elg{parts[-1]}"
    if kind == "field" and len(parts) > 1:
        struct = f"Elg{parts[-2]}"
        if parts[-1] in fields.get(struct, set()):
            return f"{struct}.{parts[-1]}"
    if kind == "value" and len(parts) > 1:
        enum = re.escape(parts[-2])
        mention = re.compile(rf"(?:elg::|`|\b[Aa]n? ){enum}\b")
        lines = header_text.splitlines()
        for index, line in enumerate(lines):
            if not mention.search(line):
                continue
            # A field with a trailing comment that names the enum: the field on that line.
            trailing = re.match(r"\s+[\w\s\*]+?\b(\w+);\s*///<", line)
            if trailing:
                owner = next((re.match(r"typedef struct (Elg\w+)", l)[1] for l in reversed(lines[:index])
                              if l.startswith("typedef struct ")), None)
                if owner and trailing[1] in fields.get(owner, set()):
                    return f"{owner}.{trailing[1]}"
            # A leading comment: the field or function it documents, after the comment's last line.
            if line.lstrip().startswith("///") and "///<" not in line:
                following = index + 1
                while following < len(lines) and lines[following].lstrip().startswith("///"):
                    following += 1
                if following < len(lines):
                    declaration = lines[following]
                    function = re.search(r"\b(elg_\w+)\s*\(", declaration)
                    if function and function[1] in functions:
                        return function[1]
                    field = re.match(r"\s+[\w\s\*]+?\b(\w+);", declaration)
                    owner = next((re.match(r"typedef struct (Elg\w+)", l)[1] for l in reversed(lines[:index])
                                  if l.startswith("typedef struct ")), None)
                    if field and owner and field[1] in fields.get(owner, set()):
                        return f"{owner}.{field[1]}"
    return None


def guess(key, kind, wraps, functions, types, fields, header_text, handles, known):
    """What in the C API offers the declaration, as best the names and docs tell, or "missing"."""
    name = re.sub(r"\(.*", "", key).removeprefix("elg::")
    parts = name.split("::")
    if parts[0] in TEMPLATES:
        return "exempt: template"
    if parts[0] in TYPE_ERASURE:
        return "exempt: type-erasure"
    from_docs = guess_from_docs(key, kind, wraps, functions, types, fields, header_text, handles)
    if from_docs:
        return from_docs
    if kind in ("function", "variable"):
        named = by_name(parts, functions)
        if named:
            return ", ".join(named)
    if parts[0].endswith("Modifier") and len(parts[0]) > len("Modifier"):
        view_method = f"elg_mod_{snake(parts[0].removesuffix('Modifier'))}"
        if view_method in functions:
            return view_method
    if kind in ("field", "value") and len(parts) > 1:
        offered = taking(parts[-2], known)
        if offered:
            return ", ".join(offered)
    return "missing"


def handle_types():
    """C++ type -> its C handle type, as tools/c-header.py maps them."""
    text = (ROOT / "tools" / "c-header.py").read_text(encoding="utf-8")
    block = re.search(r"OPAQUE = \{(.*?)\n\}", text, re.S)[1]
    return {cpp: c for cpp, c in re.findall(r'"([\w:<>]+)":\s*"(Elg\w+)"', block)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", help="a configured build folder")
    parser.add_argument("--summary", action="store_true", help="also count the entries by kind")
    parser.add_argument("--generate", action="store_true", help="add the declarations the list doesn't name")
    args = parser.parse_args()

    build = pathlib.Path(args.build).resolve()
    if not (build / "compile_commands.json").exists():
        sys.exit(f"{build} has no compile_commands.json")
    dc = doc_check()
    declarations = public_declarations(dc, build)
    functions, types, fields, header_text = c_api()
    entries, problems = read_list()

    if args.generate:
        wraps = wrapped(header_text)
        handles = handle_types()
        known = {key: value for key, (value, _) in entries.items() if value != "missing"}
        new = [(key, kind) for key, (kind, _) in declarations.items() if key not in entries]
        # Functions first, so that fields and enum values can follow the functions that take them.
        new.sort(key=lambda item: (item[1] not in ("function", "type"), item[0]))
        lines = []
        for key, kind in new:
            value = guess(key, kind, wraps, functions, types, fields, header_text, handles, known)
            known[key] = value
            lines.append(f"{key}{SEPARATOR}{value}")
        lines.sort()
        if lines:
            existing = LIST.read_text(encoding="utf-8") if LIST.exists() else ""
            LIST.write_text(existing + "\n".join(lines) + "\n", encoding="utf-8")
        print(f"c-api-coverage: added {len(lines)} entries to {LIST.relative_to(ROOT)}")
        entries, problems = read_list()

    for key, (kind, where) in sorted(declarations.items(), key=lambda item: item[1][1]):
        if key not in entries:
            problems.append(f"{where}: {key} isn't in {LIST.relative_to(ROOT)}")
    missing = 0
    for key, (value, number) in entries.items():
        where = f"{LIST.relative_to(ROOT)}:{number}"
        if key not in declarations:
            problems.append(f"{where}: {key} isn't a public declaration (any more)")
            continue
        problem = check_value(value, functions, types, fields)
        if problem:
            missing += value == "missing"
            problems.append(f"{where}: {key}: {problem}")

    if args.summary:
        kinds = collections.Counter(kind for kind, _ in declarations.values())
        values = collections.Counter("missing" if v == "missing" else "exempt" if v.startswith("exempt:") else "offered"
                                     for key, (v, _) in entries.items() if key in declarations)
        print(f"c-api-coverage: {len(declarations)} public declarations ({dict(kinds)}); "
              f"list: {dict(values)}")
    if problems:
        print("\n".join(problems))
        print(f"\nc-api-coverage: {len(problems)} problem(s), {missing} of them missing C API")
        return 1
    print(f"c-api-coverage: every one of the {len(declarations)} public declarations has C API or an exemption")
    return 0


if __name__ == "__main__":
    sys.exit(main())
