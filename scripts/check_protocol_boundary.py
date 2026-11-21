#!/usr/bin/env python3
"""Fail if a file under firmware/protocols/ includes an angle header outside the C++ standard library.

    python3 scripts/check_protocol_boundary.py

make test-protocols builds each stack from a copy holding nothing of skyBlip's, so a quoted
include from outside the stack fails there. A host header such as <unistd.h> still compiles
in that copy, and would not build in another project's firmware.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTOCOLS = os.path.join(ROOT, "firmware", "protocols")
ANGLE = re.compile(r"^\s*#\s*include\s*<([^>]+)>", re.M)

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
            try:
                with open(path, encoding="utf-8") as source:
                    text = source.read()
            except UnicodeDecodeError:
                continue
            found += [f"{relative} includes <{header}>, which is not the C++ standard library"
                      for header in ANGLE.findall(text) if header not in STANDARD]
    return found


def main():
    found = violations()
    for violation in found:
        print(violation, file=sys.stderr)
    if found:
        print(f"FAIL: {len(found)} includes reach past the C++ standard library (firmware/protocols/README.md)",
              file=sys.stderr)
        return 1
    print("OK: every angle include under firmware/protocols/ is the C++ standard library.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
