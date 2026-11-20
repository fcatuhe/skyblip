#!/usr/bin/env python3
"""Build a board's drag-and-drop .uf2 install image, and refuse a dangerous one.

    mkuf2.py <board> <out.uf2> <in1.hex> [in2.hex ...]

It merges MCUboot and the signed, confirmed application. Sysbuild's own merge
takes the unconfirmed image, which a bootloader-written slot0 would revert on
the second boot. The .uf2 stays the only install artifact (check_no_dfu_package.py).

The factory bootloader writes any address from 0x001000 to 0x0EA000 without
complaint (Adafruit_nRF52_Bootloader src/usb/uf2/uf2cfg.h), so the image is
refused against the board's WINDOWS entry when a block falls:

  * below the SoftDevice's end, where it would erase the factory MBR and SoftDevice
  * at or above USER_FLASH_END, which the bootloader silently drops, truncating the image
  * at or above the bootloader's start, where it would remove the recovery path

The .uf2 also writes 0xFF over the bytes of slot0's last sector the image leaves
out, so a stale swap status from a Bluetooth update cannot hang MCUboot.
"""
import os
import pathlib
import subprocess
import sys
from typing import NamedTuple

UF2_FAMILY_NRF52840 = "0xADA52840"


class Window(NamedTuple):
    softdevice_end: int
    user_flash_end: int
    bootloader_start: int
    softdevice: str


# INFO: fc 15nov26 one nRF52840 page at slot0's end holds MCUboot's trailer and swap status: test_mkuf2.py
TRAILER_SECTOR_BYTES = 0x1000
MCUBOOT_MAGIC = bytes.fromhex("77c295f360d2ef7f3552500f2cb67980")
# INFO: fc 15nov26 imgtool --confirm sets image_ok, the 8-byte field just below the magic at BOOT_MAX_ALIGN 8
IMAGE_OK_FROM_END = 24
IMAGE_OK_SET = 0x01

WINDOWS = {
    "t_echo_plus": Window(0x00026000, 0x000EA000, 0x000F4000, "S140 6.1.1"),
    # INFO: fc 12nov26 the X1's factory files confirm 0x27000 and 0xEA000, 0xF4000 is from source only: meshtracker_x1/factory/README.md
    "meshtracker_x1": Window(0x00027000, 0x000EA000, 0x000F4000, "S140 7.3.0"),
}


def read_hex(path, into):
    offset = 0
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rectype = int(line[7:9], 16)
        payload = bytes.fromhex(line[9:9 + 2 * count])
        if rectype == 0x04:
            offset = int(line[9:9 + 2 * count], 16) << 16
        elif rectype == 0x02:
            offset = int(line[9:9 + 2 * count], 16) << 4
        elif rectype == 0x00:
            for i, byte in enumerate(payload):
                a = offset + addr + i
                if a in into and into[a] != byte:
                    sys.exit(f"ERROR: images disagree at 0x{a:08X} ({path})")
                into[a] = byte


def ranges_of(byte_map):
    if not byte_map:
        return []
    ranges = []
    ordered = sorted(byte_map)
    start = prev = ordered[0]
    for a in ordered[1:]:
        if a != prev + 1:
            ranges.append((start, prev + 1))
            start = a
        prev = a
    ranges.append((start, prev + 1))
    return ranges


def write_hex(byte_map, path):
    lines = []
    upper = None
    for start, end in ranges_of(byte_map):
        for chunk in range(start, end, 16):
            data = bytes(byte_map[a] for a in range(chunk, min(chunk + 16, end)))
            if (chunk >> 16) != upper:
                upper = chunk >> 16
                rec = bytes([2, 0, 0, 4, (upper >> 8) & 0xFF, upper & 0xFF])
                lines.append(":%s%02X" % (rec.hex().upper(), (-sum(rec)) & 0xFF))
            rec = bytes([len(data), (chunk >> 8) & 0xFF, chunk & 0xFF, 0]) + data
            lines.append(":%s%02X" % (rec.hex().upper(), (-sum(rec)) & 0xFF))
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n")


def prepare_drop(byte_map, window):
    end = window.user_flash_end
    magic = bytes(byte_map.get(a, 0xFF) for a in range(end - len(MCUBOOT_MAGIC), end))
    if magic != MCUBOOT_MAGIC or byte_map.get(end - IMAGE_OK_FROM_END) != IMAGE_OK_SET:
        return (
            f"no confirmed MCUboot trailer ending at 0x{end:06X}. The input must be "
            "zephyr.signed.confirmed.hex, padded to a slot that ends at USER_FLASH_END."
        )
    blank_trailer_sector(byte_map, window)
    return check_window(ranges_of(byte_map), window)


def blank_trailer_sector(byte_map, window):
    for address in range(window.user_flash_end - TRAILER_SECTOR_BYTES, window.user_flash_end):
        byte_map.setdefault(address, 0xFF)
    return byte_map


def zephyr_uf2conv():
    # INFO: fc 20nov26 the fallbacks cover both west layouts: topdir at the repo root, or the repo inside the topdir
    candidates = []
    if os.environ.get("ZEPHYR_BASE"):
        candidates.append(pathlib.Path(os.environ["ZEPHYR_BASE"]))
    here = pathlib.Path(__file__).resolve()
    candidates += [p / "zephyr" for p in here.parents[1:4]]
    for base in candidates:
        tool = base / "scripts" / "build" / "uf2conv.py"
        if tool.is_file():
            return tool
    sys.exit(
        "ERROR: cannot find Zephyr's scripts/build/uf2conv.py. "
        "Set ZEPHYR_BASE or run from inside the west workspace. Tried: "
        + ", ".join(str(c) for c in candidates)
    )


def check_window(ranges, window):
    low = min(start for start, _ in ranges)
    high = max(end for _, end in ranges)
    if low < window.softdevice_end:
        return (
            f"image starts at 0x{low:06X}, below 0x{window.softdevice_end:06X}. "
            f"This would overwrite the factory MBR/SoftDevice ({window.softdevice}). Check "
            "that CONFIG_USE_DT_CODE_PARTITION=y and that boot_partition starts at "
            f"0x{window.softdevice_end:X}."
        )
    if high > window.bootloader_start:
        return (
            f"image ends at 0x{high:06X}, inside the factory UF2 bootloader at "
            f"0x{window.bootloader_start:06X}. That bootloader is the only way to "
            "install anything without a programmer; it is never a target."
        )
    if high > window.user_flash_end:
        return (
            f"image ends at 0x{high:06X}, past USER_FLASH_END 0x{window.user_flash_end:06X}. "
            "The UF2 bootloader silently drops writes above that, so the device "
            "would be flashed with a truncated image."
        )
    return None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    board = sys.argv[1]
    if board not in WINDOWS:
        return fail(f"no write window for board {board!r}, only for {', '.join(sorted(WINDOWS))}")
    out = pathlib.Path(sys.argv[2])
    inputs = [pathlib.Path(p) for p in sys.argv[3:]]

    byte_map = {}
    for path in inputs:
        if not path.is_file():
            return fail(f"no such hex: {path}")
        read_hex(path, byte_map)
        print(f"merged {path}")

    if not byte_map:
        return fail("inputs contain no data records")
    problem = prepare_drop(byte_map, WINDOWS[board])
    if problem:
        return fail(problem)

    print("combined image covers:")
    for start, end in ranges_of(byte_map):
        print(f"  0x{start:08X} - 0x{end - 1:08X}  ({end - start} bytes)")

    merged = out.with_suffix(".merged.hex")
    write_hex(byte_map, merged)
    subprocess.check_call([
        sys.executable, str(zephyr_uf2conv()),
        str(merged), "-c", "-f", UF2_FAMILY_NRF52840, "-o", str(out),
    ])
    print(f"OK: {out} ({out.stat().st_size} bytes), family {UF2_FAMILY_NRF52840}")
    return 0


def fail(msg):
    print(f"FAIL: {msg}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
