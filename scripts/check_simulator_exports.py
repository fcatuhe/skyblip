#!/usr/bin/env python3
"""Fail if the browser simulator's JavaScript binds a C export that does not exist.

    python3 scripts/check_simulator_exports.py

firmware/simulator/web/embed.js reaches the firmware through cwrap() by C name,
and the names are only resolved when a page loads the module: a C export renamed
in firmware/simulator/frontends/browser.cpp and not in embed.js builds, ships,
and breaks the simulator in the browser of every visitor. This gate is the build
noticing instead.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BROWSER = os.path.join("firmware", "simulator", "frontends", "browser.cpp")
EMBED = os.path.join("firmware", "simulator", "web", "embed.js")

EXPORT = re.compile(r"KEEPALIVE\b[^(;{]*?\b(simulator_\w+)\s*\(")
BINDING = re.compile(r"\bcall\(\s*'(simulator_\w+)'")


def unbound(root=ROOT):
    exports = set(EXPORT.findall(read(root, BROWSER)))
    return sorted(set(BINDING.findall(read(root, EMBED))) - exports)


def read(root, relative):
    with open(os.path.join(root, relative), encoding="utf-8") as source:
        return source.read()


def main():
    missing = unbound()
    for name in missing:
        report(f"{EMBED}: binds {name}, which {BROWSER} does not export")
    if missing:
        return 1
    report("OK: every C name embed.js binds is exported by browser.cpp.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
