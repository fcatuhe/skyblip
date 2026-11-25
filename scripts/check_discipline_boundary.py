#!/usr/bin/env python3
"""Fail if a file under firmware/disciplines/ includes what a discipline may not read.

    python3 scripts/check_discipline_boundary.py

A pack reads own-ship, the traffic and the flight arithmetic, and answers a level. It reaches
no radio, protocol stack other than the shared arithmetic, setting, service or part, so it
cannot change what goes on air (firmware/disciplines/README.md). The dispatcher, the files
directly under firmware/disciplines/, may also include the packs.
"""
import os
import posixpath
import re
import sys

from check_protocol_boundary import STANDARD

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISCIPLINES = os.path.join(ROOT, "firmware", "disciplines")
ANGLE = re.compile(r"^\s*#\s*include\s*<([^>]+)>", re.M)
QUOTED = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

READABLE = ("core/model/", "core/flight/", "core/traffic/", "core/units/", "core/util/",
            "protocols/common/")


def violations(disciplines=DISCIPLINES):
    found = []
    for folder, _, names in sorted(os.walk(disciplines)):
        own = os.path.relpath(folder, os.path.dirname(disciplines)).replace(os.sep, "/") + "/"
        readable = READABLE + (own,)
        for name in sorted(names):
            path = os.path.join(folder, name)
            relative = own + name
            try:
                with open(path, encoding="utf-8") as source:
                    text = source.read()
            except UnicodeDecodeError:
                continue
            found += [f"{relative} includes <{header}>, which is not the C++ standard library"
                      for header in ANGLE.findall(text) if header not in STANDARD]
            found += [f'{relative} includes "{header}", which a discipline may not read'
                      for header in QUOTED.findall(text)
                      if not posixpath.normpath(header).startswith(readable)]
    return found


def main():
    found = violations()
    for violation in found:
        print(violation, file=sys.stderr)
    if found:
        print(f"FAIL: {len(found)} includes reach past what a discipline reads (firmware/disciplines/README.md)",
              file=sys.stderr)
        return 1
    print("OK: every include under firmware/disciplines/ is one a discipline may read.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
