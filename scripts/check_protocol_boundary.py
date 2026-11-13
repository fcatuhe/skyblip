#!/usr/bin/env python3
"""Fail if a file under firmware/protocols/ includes anything but its own stack, protocols/common and the standard library.

    python3 scripts/check_protocol_boundary.py

Each radio protocol stack in firmware/protocols/ is copied into other projects with its tests, so
it must build with nothing from skyBlip (firmware/protocols/README.md). A quoted include names a
path from firmware/: a file in protocols/<stack>/ may include protocols/<stack>/ and
protocols/common/, and a test file may also include the vendored doctest. An angle include must be
a C++ standard library header, so no Zephyr, no platform and no vendor header gets in that way.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTOCOLS = os.path.join(ROOT, "firmware", "protocols")
SOURCES = (".h", ".cpp")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
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
    for stack in sorted(os.listdir(protocols)):
        directory = os.path.join(protocols, stack)
        if not os.path.isdir(directory):
            continue
        allowed = (f"protocols/{stack}/", "protocols/common/")
        for folder, _, names in sorted(os.walk(directory)):
            for name in sorted(names):
                if not name.endswith(SOURCES):
                    continue
                path = os.path.join(folder, name)
                relative = os.path.relpath(path, os.path.dirname(protocols))
                is_test = f"{os.sep}test{os.sep}" in path
                with open(path, encoding="utf-8") as source:
                    text = source.read()
                for kind, header in INCLUDE.findall(text):
                    if kind == "<":
                        if header not in STANDARD:
                            found.append(f"{relative} includes <{header}>, which is not the C++ standard library")
                    elif not header.startswith(allowed) and not (is_test and header in TEST_ONLY):
                        found.append(f'{relative} includes "{header}", outside {allowed[0]} and {allowed[1]}')
    return found


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
