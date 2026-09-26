#!/usr/bin/env python3
"""Fail if a workflow or a script builds or ships a Nordic DFU package.

    python3 scripts/check_no_dfu_package.py

The factory Adafruit bootloader starts the application only while
bootloader_app_is_valid() holds (Adafruit_nRF52_Bootloader
lib/sdk11/components/libraries/bootloader_dfu/bootloader.c:175-223). A UF2
install stores bank_0_crc as 0, which turns the check off for good. A Nordic DFU
install over BLE or serial stores a real CRC of the image as delivered, and the
first MCUboot swap rewrites those bytes: the bootloader then refuses the
application and the device sits in DFU mode until someone drops a .uf2 on it.

So the one install artifact is mkuf2.py's .uf2, and this gate fails the pull
request that adds nrfutil, adafruit-nrfutil, genpkg or a .zip to a workflow or a
script. The fix is never to make a .zip work: that needs the bootloader's
settings page to know about MCUboot, and the bootloader is factory-programmed.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCANNED = (os.path.join(".github", "workflows"), "scripts")
EXEMPT = {
    os.path.join("scripts", "check_no_dfu_package.py"),
    os.path.join("scripts", "test_mkuf2.py"),
}

FORBIDDEN = (
    (re.compile(r"nrfutil", re.I), "invokes nrfutil"),
    (re.compile(r"genpkg", re.I), "builds a Nordic DFU package (genpkg)"),
    (re.compile(r"\.zip\b", re.I), "stages a .zip firmware package"),
)


def findings(root=ROOT):
    found = []
    for relative in scanned_files(root):
        with open(os.path.join(root, relative), encoding="utf-8", errors="replace") as source:
            for number, line in enumerate(source, 1):
                for pattern, reason in FORBIDDEN:
                    if pattern.search(line):
                        found.append((relative, number, reason))
    return found


def scanned_files(root):
    for top in SCANNED:
        for dirpath, dirnames, files in os.walk(os.path.join(root, top)):
            dirnames[:] = sorted(d for d in dirnames if d != "__pycache__")
            for name in sorted(files):
                relative = os.path.relpath(os.path.join(dirpath, name), root)
                if relative not in EXEMPT:
                    yield relative


def main():
    found = findings()
    for path, number, reason in found:
        report(f"{path}:{number}: {reason}, which strands the MCUboot chain on the first swap")
    if found:
        return 1
    report("OK: no workflow or script builds or ships a Nordic DFU package.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
