#!/usr/bin/env python3
"""Fail unless a signed image names the product it was built for.

    python3 scripts/check_image_product.py <image.signed.bin> <product>

Every product signs with the same key, so the image names its product in its protected
TLVs as MCUboot's image class: 0x74 is the uuid5 of skyblip.eu, and 0x75 the uuid5 of the
product directory in that namespace. A unit refuses an upload of another class, and every
upload while it runs an image without one (firmware/core/dfu/README.md). It also
refuses protected TLVs wider than PROTECTED_AREA_BYTES, and so does this check.
"""
import pathlib
import struct
import sys
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from check_imu_image import protected_tlvs  # noqa: E402

TLV_VENDOR = 0x74
TLV_CLASS = 0x75
VENDOR = uuid.uuid5(uuid.NAMESPACE_DNS, "skyblip.eu")
# INFO: fc 20nov26 ProductCheck::kProtectedAreaBytes on the device, and test_check_image_product.py holds the two equal
PROTECTED_AREA_BYTES = 128


def image_class(product):
    return uuid.uuid5(VENDOR, product).bytes


def problems(image, product):
    try:
        tlvs = protected_tlvs(image)
    except ValueError as error:
        return [str(error)]
    found = []
    protected_bytes, = struct.unpack_from("<H", image, 10)
    if protected_bytes > PROTECTED_AREA_BYTES:
        found.append(f"the protected TLVs are {protected_bytes} bytes, wider than the "
                     f"{PROTECTED_AREA_BYTES} of ProductCheck::kProtectedAreaBytes: every unit would refuse it")
    if tlvs.get(TLV_VENDOR) != VENDOR.bytes:
        found.append("TLV 0x74 is not the vendor uuid5 of skyblip.eu")
    if tlvs.get(TLV_CLASS) != image_class(product):
        found.append(f"TLV 0x75 is not the image class of {product}")
    return found


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    with open(argv[1], "rb") as image:
        found = problems(image.read(), argv[2])
    for problem in found:
        print(f"FAIL: {argv[1]}: {problem}")
    if not found:
        print(f"{argv[1]}: names {argv[2]}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
