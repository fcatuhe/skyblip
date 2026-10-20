#!/usr/bin/env python3
"""Fail if a file generated from schemas/link.v1.json differs from what the registry produces.

    python3 scripts/check_link_registry.py

Every file is compared byte for byte: the JavaScript module as generated, the C++ files
as generated and then put through clang-format (CLANG_FORMAT names it), which is how
scripts/link_registry.py writes them.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import link_registry  # noqa: E402


def main():
    expected = link_registry.written(link_registry.load(), link_registry.formatter())
    stale = link_registry.stale(expected, link_registry.committed)
    for relative in stale:
        print("%s: differs from schemas/link.v1.json, run scripts/link_registry.py" % relative,
              file=sys.stderr)
    if stale:
        return 1
    print("OK: every file generated from the link registry is current.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
