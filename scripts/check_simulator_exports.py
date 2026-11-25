#!/usr/bin/env python3
"""Fail if the browser simulator's JavaScript and the firmware's exports disagree.

    python3 scripts/check_simulator_exports.py

firmware/simulator/web/embed.js binds firmware exports through cwrap() by C name, and
the names resolve only when a page loads the module, so a rename in
firmware/simulator/frontends/browser.cpp would otherwise break only in the browser.
An export embed.js never binds is code no page can reach, so it fails too.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BROWSER = os.path.join("firmware", "simulator", "frontends", "browser.cpp")
EMBED = os.path.join("firmware", "simulator", "web", "embed.js")

EXPORT = re.compile(r"KEEPALIVE\b[^(;{]*?\b(simulator_\w+)\s*\(")
BINDING = re.compile(r"\bcall\(\s*'(simulator_\w+)'")


def mismatches(root=ROOT):
    exports = set(EXPORT.findall(read(root, BROWSER)))
    bindings = set(BINDING.findall(read(root, EMBED)))
    return sorted(bindings - exports), sorted(exports - bindings)


def read(root, relative):
    with open(os.path.join(root, relative), encoding="utf-8") as source:
        return source.read()


def main():
    missing, unbound = mismatches()
    for name in missing:
        report(f"{EMBED}: binds {name}, which {BROWSER} does not export")
    for name in unbound:
        report(f"{BROWSER}: exports {name}, which {EMBED} never binds")
    if missing or unbound:
        return 1
    report("OK: embed.js binds every C name browser.cpp exports, and no other.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
