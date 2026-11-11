#!/usr/bin/env python3
"""Self-check for check_image_product.py, on images laid out the way imgtool writes them.

    python3 scripts/test_check_image_product.py
"""
import pathlib
import re
import struct
import sys
import unittest
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_image_product  # noqa: E402

VENDOR = uuid.uuid5(uuid.NAMESPACE_DNS, "skyblip.eu").bytes


def signed(tlvs):
    header_size, body = 32, bytes(100)
    entries = b"".join(struct.pack("<HH", kind, len(value)) + value for kind, value in tlvs)
    protected = struct.pack("<HH", 0x6908, 4 + len(entries)) + entries
    header = struct.pack("<IIHHI", 0x96F3B83D, 0, header_size, len(protected), len(body))
    header += bytes(header_size - len(header))
    hashed = struct.pack("<HH", 0x6907, 4 + 36) + struct.pack("<HH", 0x10, 32) + bytes(32)
    return header + body + protected + hashed


def named(product):
    return signed([(0x74, VENDOR), (0x75, check_image_product.image_class(product)),
                   (0xA0, bytes(32)), (0xA1, b"\x01")])


class CheckImageProduct(unittest.TestCase):
    def test_the_class_is_what_imgtool_derives_from_the_names(self):
        # imgtool's parse_uuid: uuid5(NAMESPACE_DNS, --vid), then uuid5(that, --cid).
        expected = uuid.uuid5(uuid.UUID(bytes=VENDOR), "skyblip_go").bytes
        self.assertEqual(check_image_product.image_class("skyblip_go"), expected)
        self.assertNotEqual(check_image_product.image_class("skyblip_bravo"), expected)

    def test_an_image_naming_its_product_passes(self):
        self.assertEqual(check_image_product.problems(named("skyblip_go"), "skyblip_go"), [])

    def test_an_image_for_another_product_fails(self):
        self.assertEqual(check_image_product.problems(named("skyblip_bravo"), "skyblip_go"),
                         ["TLV 0x75 is not the image class of skyblip_go"])

    def test_an_image_naming_no_product_fails_on_both_tlvs(self):
        self.assertEqual(len(check_image_product.problems(signed([(0xA1, b"\x00")]), "skyblip_go")), 2)

    def test_the_width_is_the_one_the_unit_holds(self):
        header = (pathlib.Path(__file__).resolve().parent.parent
                  / "firmware/core/dfu/product_check.h").read_text()
        held = re.search(r"kProtectedAreaBytes = (\d+);", header)
        self.assertEqual(int(held.group(1)), check_image_product.PROTECTED_AREA_BYTES)

    def test_a_protected_area_wider_than_the_unit_holds_fails(self):
        # 4 of area header, 20 each for the vendor and the class, and 4 more for the third TLV's head.
        def padded(value_bytes):
            return signed([(0x74, VENDOR), (0x75, check_image_product.image_class("skyblip_go")),
                           (0xA2, bytes(value_bytes))])
        self.assertEqual(check_image_product.problems(padded(80), "skyblip_go"), [])
        self.assertEqual(check_image_product.problems(padded(81), "skyblip_go"),
                         ["the protected TLVs are 129 bytes, wider than the 128 of "
                          "ProductCheck::kProtectedAreaBytes: every unit would refuse it"])

    def test_a_file_that_is_not_an_image_fails(self):
        self.assertEqual(check_image_product.problems(bytes(64), "skyblip_go"), ["not an MCUboot image"])


if __name__ == "__main__":
    unittest.main()
