#!/usr/bin/env python3
"""Fail unless a signed image pins the BHI260AP image it was built against.

    python3 scripts/check_imu_image.py <image.signed.bin> <BHI260AP.fw> full|slim

The update page reads two protected TLVs to pick the file a device needs:
0xa0, the SHA-256 of the hub's image, and 0xa1, 1 when the image carries it.
"""
import hashlib
import struct
import sys

IMAGE_MAGIC = 0x96F3B83D
PROTECTED_TLV_MAGIC = 0x6908
TLV_PINNED = 0xA0
TLV_CARRIED = 0xA1


def protected_tlvs(image):
    if len(image) < 32 or struct.unpack_from("<I", image, 0)[0] != IMAGE_MAGIC:
        raise ValueError("not an MCUboot image")
    header_size, = struct.unpack_from("<H", image, 8)
    body_size, = struct.unpack_from("<I", image, 12)
    at = header_size + body_size
    if at + 4 > len(image):
        raise ValueError("the image ends before its TLVs")
    magic, total = struct.unpack_from("<HH", image, at)
    if magic != PROTECTED_TLV_MAGIC:
        raise ValueError("no protected TLVs")
    end = at + total
    at += 4
    found = {}
    while at + 4 <= end:
        kind, length = struct.unpack_from("<HH", image, at)
        found[kind] = image[at + 4:at + 4 + length]
        at += 4 + length
    return found


def problems(image, hub_image, variant):
    try:
        tlvs = protected_tlvs(image)
    except ValueError as error:
        return [str(error)]
    found = []
    pinned = tlvs.get(TLV_PINNED)
    if pinned != hashlib.sha256(hub_image).digest():
        found.append("TLV 0xa0 is not the SHA-256 of the hub's image")
    carried = tlvs.get(TLV_CARRIED)
    if carried != bytes([1 if variant == "full" else 0]):
        found.append(f"TLV 0xa1 does not say {variant}")
    return found


def main(argv):
    if len(argv) != 4 or argv[3] not in ("full", "slim"):
        print(__doc__.strip().splitlines()[2])
        return 2
    with open(argv[1], "rb") as image, open(argv[2], "rb") as hub_image:
        found = problems(image.read(), hub_image.read(), argv[3])
    for problem in found:
        print(f"FAIL: {argv[1]}: {problem}")
    if not found:
        print(f"{argv[1]}: pins the hub's image, {argv[3]}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
