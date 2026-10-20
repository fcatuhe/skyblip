#!/usr/bin/env python3
"""Fail if a file generated from schemas/link.v1.json differs from what the registry produces.

    python3 scripts/check_link_registry.py

The C++ files are compared without their whitespace, because scripts/link_registry.py
writes them through clang-format and the formatting gate owns the layout.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import link_registry  # noqa: E402


def main():
    stale = []
    for relative, expected in link_registry.generated(link_registry.load()).items():
        path = link_registry.ROOT / relative
        written = path.read_text(encoding="utf-8") if path.exists() else ""
        if not link_registry.same_tokens(written, expected):
            stale.append(relative)
    for relative in stale:
        print("%s: differs from schemas/link.v1.json, run scripts/link_registry.py" % relative,
              file=sys.stderr)
    if stale:
        return 1
    print("OK: every file generated from the link registry is current.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
