#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Checks that every function the C API exports keeps C++ exceptions from crossing into the caller:
# its body is inside ELG_TRY ... ELG_CATCH (or VIEW_TRY / MOD_TRY, which expand to them). Fails,
# naming each function, if one isn't. CI runs it on every pull request.
#
#   python3 tools/c-api-check.py
#
# Functions defined by macros (the style handles in c/styles.cpp) are checked in the macro.

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRY = re.compile(r"\b(ELG_TRY|VIEW_TRY|MOD_TRY)\b")
EXPORT = re.compile(r"\bELG_EXPORT\b[^{;]*\{")


def without_comments(text):
    """`text` with comments blanked out (kept the same length, so offsets and lines still match)."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r"//[^\n]*|/\*.*?\*/", blank, text, flags=re.S)


def main():
    problems = []
    count = 0
    for path in sorted((ROOT / "c").glob("*.cpp")):
        text = without_comments(path.read_text(encoding="utf-8"))
        for match in EXPORT.finditer(text):
            name = re.search(r"\b(elg_\w+|elg_##\w+|\w+##\w+)\s*\(", match.group(0))
            i, depth = match.end(), 1
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            count += 1
            if not TRY.search(text[match.end():i]):
                line = text.count("\n", 0, match.start()) + 1
                problems.append(f"{path.relative_to(ROOT)}:{line}: {name.group(1) if name else '?'} "
                                "has no ELG_TRY: an exception would cross the C boundary")
    if problems:
        print("\n".join(problems))
        print(f"\nc-api-check: {len(problems)} of {count} exported functions don't catch exceptions")
        return 1
    print(f"c-api-check: all {count} exported functions catch exceptions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
