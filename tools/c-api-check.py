#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks every function the C API exports:
# - it keeps C++ exceptions from crossing into the caller: its body is inside ELG_TRY ... ELG_CATCH
#   (or ELG_TRY_ANY_THREAD, VIEW_TRY or MOD_TRY, which expand to the same);
# - its doc says "Callable from any thread" exactly when it starts with ELG_TRY_ANY_THREAD;
# - it guards what it consumes (Consumed, ConsumedElements) before the try block, so that the
#   guards release it however the call ends.
# Fails, naming each function, if one doesn't. CI runs it on every pull request.
#
#   python3 tools/c-api-check.py
#
# Functions defined by macros (the style handles in c/styles.cpp) are checked in the macro.

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRY = re.compile(r"\b(ELG_TRY|ELG_TRY_ANY_THREAD|VIEW_TRY|MOD_TRY)\b")
EXPORT = re.compile(r"\bELG_EXPORT\b[^{;]*\{")
GUARD = re.compile(r"(?:const Consumed consumed|ConsumedElements<[^>]*>\s+\w+)\(")
ANY_THREAD = "callable from any thread"


def without_comments(text):
    """`text` with comments blanked out (kept the same length, so offsets and lines still match)."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r"//[^\n]*|/\*.*?\*/", blank, text, flags=re.S)


def doc_before(text, offset):
    """The /// comment right above the line at `offset`, as one string."""
    lines = text[:offset].split("\n")[:-1]
    doc = []
    while lines and lines[-1].startswith("///"):
        doc.insert(0, lines.pop()[3:].strip())
    return " ".join(doc)


def main():
    problems = []
    count = 0
    for path in sorted((ROOT / "c").glob("*.cpp")):
        source = path.read_text(encoding="utf-8")
        text = without_comments(source)
        for match in EXPORT.finditer(text):
            name = re.search(r"\b(elg_\w+|elg_##\w+|\w+##\w+)\s*\(", match.group(0))
            name = name.group(1) if name else "?"
            i, depth = match.end(), 1
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            count += 1
            where = f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}: {name}"
            found = TRY.search(text[match.end():i])
            if not found:
                problems.append(f"{where} has no ELG_TRY: an exception would cross the C boundary")
                continue
            any_thread = found.group(1) == "ELG_TRY_ANY_THREAD"
            documented = ANY_THREAD in doc_before(source, match.start()).lower()
            if any_thread and not documented:
                problems.append(f"{where} uses ELG_TRY_ANY_THREAD, but its doc doesn't say it's callable from "
                                "any thread")
            elif documented and not any_thread:
                problems.append(f"{where}'s doc says it's callable from any thread: start it with "
                                "ELG_TRY_ANY_THREAD")
            after = text[match.end():i - 1][found.end():]
            if any(after[:g.start()].count("{") == after[:g.start()].count("}") for g in GUARD.finditer(after)):
                problems.append(f"{where} guards what it consumes inside its try block: declare the guard before "
                                f"{found.group(1)}, so that it releases them however the call ends")
    if problems:
        print("\n".join(problems))
        print(f"\nc-api-check: {len(problems)} problems in {count} exported functions")
        return 1
    print(f"c-api-check: all {count} exported functions catch exceptions, guard what they consume before their "
          "try block, and ELG_TRY_ANY_THREAD marks those documented as callable from any thread")
    return 0


if __name__ == "__main__":
    sys.exit(main())
