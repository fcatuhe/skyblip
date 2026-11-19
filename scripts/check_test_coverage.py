#!/usr/bin/env python3
"""Fail if a logic module or a part has no host test that reaches it.

Structural gate, not line coverage. Tests live in firmware/test/, one folder per
layer, and never beside the code they test: every directory with a header under
protocols/, core/, ui/, products/, runtime/ and simulator/ must be included by
some test the host suite compiles, and every part with a driver must have
test/hardware/test_<part>.cpp that includes both the driver and its model. A part
with no test is a datasheet transcription nothing checks. A radio protocol stack
under protocols/ is the one exception: it travels with its tests, so they live in
its own test/ folder.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")

MODULE_LAYERS = ("protocols", "core", "ui", "products", "runtime", "simulator")
NOT_HOST_TESTS = ("test/fuzz", "test/twister")
PART_TEST_DIR = "test/hardware"


def modules(fw=FW):
    found = []
    for layer in MODULE_LAYERS:
        for dirpath, dirs, files in os.walk(os.path.join(fw, layer)):
            dirs[:] = sorted(d for d in dirs if d != "test")
            if any(f.endswith(".h") for f in files):
                found.append(os.path.relpath(dirpath, fw))
    return found


def test_blob(fw=FW):
    tops = [os.path.join(fw, "test")] + [os.path.join(fw, "protocols", stack, "test")
                                         for stack in sorted(os.listdir(os.path.join(fw, "protocols")))]
    blob = []
    for top in tops:
        for dirpath, dirs, files in os.walk(top):
            dirs[:] = sorted(d for d in dirs if os.path.relpath(os.path.join(dirpath, d), fw) not in NOT_HOST_TESTS)
            for f in sorted(files):
                if f.endswith(".cpp"):
                    with open(os.path.join(dirpath, f)) as fh:
                        blob.append(fh.read())
    return "\n".join(blob)


def parts():
    d = os.path.join(FW, "hardware/parts")
    return sorted(p for p in os.listdir(d) if os.path.isdir(os.path.join(d, p)))


def has_driver(part):
    files = os.listdir(os.path.join(FW, "hardware/parts", part))
    return f"{part}.cpp" in files or f"{part}.h" in files


def strays():
    """Test files that crept back in beside the code they test."""
    found = []
    for layer in ("protocols", "core", "ui", "ports", "hardware", "boards", "runtime", "products",
                  "simulator"):
        for dirpath, _, files in os.walk(os.path.join(FW, layer)):
            if layer == "protocols" and f"{os.sep}test" in os.path.relpath(dirpath, FW):
                continue
            for f in files:
                if f.startswith("test_") and f.endswith(".cpp"):
                    found.append(os.path.relpath(os.path.join(dirpath, f), FW))
    return sorted(found)


def untested(fw=FW):
    blob = test_blob(fw)
    return [mod for mod in modules(fw) if f'#include "{mod}/' not in blob]


def main():
    missing = untested()

    for part in parts():
        if not has_driver(part):
            continue
        test = os.path.join(FW, PART_TEST_DIR, f"test_{part}.cpp")
        if not os.path.isfile(test):
            missing.append(f"hardware/parts/{part} (no {PART_TEST_DIR}/test_{part}.cpp)")
            continue
        with open(test) as fh:
            body = fh.read()
        # The point of a part test is the driver running against its own model,
        # so a file that includes neither is not one.
        for header in (f"hardware/parts/{part}/{part}.h", f"hardware/parts/{part}/model.h"):
            if f'#include "{header}"' not in body:
                missing.append(f"{PART_TEST_DIR}/test_{part}.cpp (does not include {header})")

    misplaced = strays()

    if missing or misplaced:
        if missing:
            print("FAIL: no host test coverage for:")
            for m in missing:
                print("  -", m)
        if misplaced:
            print(f"FAIL: tests live in firmware/test/, not beside the code they test:")
            for m in misplaced:
                print("  -", m)
        return 1
    print(f"OK: all {len(modules())} modules and every part are reached by a host test.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
