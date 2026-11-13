#!/usr/bin/env python3
"""Fail if a file under firmware/protocols/ includes anything but its own stack, protocols/common and the standard library.

    python3 scripts/check_protocol_boundary.py

Each radio protocol stack in firmware/protocols/ is copied into other projects with its tests, so
it must build with nothing from skyBlip (firmware/protocols/README.md). Every file is read,
whatever its extension. A quoted include names a path from firmware/, written plainly with no
`..`: a file in protocols/<stack>/ may include protocols/<stack>/ and protocols/common/, a file
directly under protocols/ only protocols/common/, and a test file may also include the vendored
doctest. An angle include must be a C++ standard library header, so no Zephyr, no platform and no
vendor header gets in that way. An include that names no header in <> or "", such as one built
from a macro, is refused, because nothing can say where it leads.
"""
import os
import posixpath
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTOCOLS = os.path.join(ROOT, "firmware", "protocols")
DIRECTIVE = re.compile(r"^\s*#\s*include\b(.*)$", re.M)
HEADER = re.compile(r'^\s*([<"])([^>"]+)[>"]')
TEST_ONLY = {"doctest/doctest.h"}

STANDARD = {
    "algorithm", "any", "array", "atomic", "bit", "bitset", "cassert", "cctype", "cerrno",
    "cfloat", "charconv", "chrono", "cinttypes", "climits", "cmath", "compare", "complex",
    "concepts", "cstdarg", "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "ctime",
    "cwchar", "deque", "functional", "initializer_list", "iomanip", "ios", "iosfwd", "iostream",
    "istream", "iterator", "limits", "list", "map", "memory", "new", "numeric", "optional",
    "ostream", "queue", "random", "ratio", "set", "sstream", "stack", "string", "string_view",
    "tuple", "type_traits", "unordered_map", "unordered_set", "utility", "variant", "vector",
}


def violations(protocols=PROTOCOLS):
    found = []
    for folder, _, names in sorted(os.walk(protocols)):
        for name in sorted(names):
            path = os.path.join(folder, name)
            relative = os.path.relpath(path, os.path.dirname(protocols)).replace(os.sep, "/")
            parts = relative.split("/")
            stack = parts[1] if len(parts) > 2 else None
            allowed = ("protocols/common/",) if stack is None else (f"protocols/{stack}/", "protocols/common/")
            is_test = "test" in parts[2:-1]
            try:
                with open(path, encoding="utf-8") as source:
                    text = source.read()
            except UnicodeDecodeError:
                continue
            for directive in DIRECTIVE.findall(text):
                found += judge(relative, directive, allowed, is_test)
    return found


def judge(relative, directive, allowed, is_test):
    match = HEADER.match(directive)
    if not match:
        return [f"{relative} includes{directive}, which names no header in <> or \"\""]
    kind, header = match.groups()
    if kind == "<":
        if header in STANDARD:
            return []
        return [f"{relative} includes <{header}>, which is not the C++ standard library"]
    if is_test and header in TEST_ONLY:
        return []
    normal = posixpath.normpath(header)
    if ".." in header.split("/") or normal != header:
        return [f'{relative} includes "{header}", a path that is not written plainly from firmware/']
    if not header.startswith(allowed):
        return [f'{relative} includes "{header}", outside {" and ".join(allowed)}']
    return []


def main():
    found = violations()
    for violation in found:
        print(violation, file=sys.stderr)
    if found:
        print(f"FAIL: {len(found)} includes cross the protocols/ boundary (firmware/protocols/README.md)",
              file=sys.stderr)
        return 1
    print("OK: every file under firmware/protocols/ includes only its stack, protocols/common and the standard library.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
