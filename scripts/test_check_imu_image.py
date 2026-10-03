#!/usr/bin/env python3
"""Self-check for check_imu_image.py, on images laid out the way imgtool writes them.

    python3 scripts/test_check_imu_image.py
"""
import hashlib
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_imu_image  # noqa: E402

HUB_IMAGE = bytes([0x2B, 0x66, 0x00, 0x00]) + bytes(range(60))


def signed(tlvs, protected_magic=0x6908):
    header_size, body = 32, bytes(100)
    entries = b"".join(struct.pack("<HH", kind, len(value)) + value for kind, value in tlvs)
    protected = struct.pack("<HH", protected_magic, 4 + len(entries)) + entries
    header = struct.pack("<IIHHI", 0x96F3B83D, 0, header_size, len(protected), len(body))
    header += bytes(header_size - len(header))
    hashed = struct.pack("<HH", 0x6907, 4 + 36) + struct.pack("<HH", 0x10, 32) + bytes(32)
    return header + body + protected + hashed


class CheckImuImage(unittest.TestCase):
    def test_a_slim_image_pinned_to_the_hub_image_passes(self):
        image = signed([(0xA0, hashlib.sha256(HUB_IMAGE).digest()), (0xA1, b"\x00")])
        self.assertEqual(check_imu_image.problems(image, HUB_IMAGE, "slim"), [])

    def test_a_full_image_says_it_carries_the_hub_image(self):
        image = signed([(0xA0, hashlib.sha256(HUB_IMAGE).digest()), (0xA1, b"\x01")])
        self.assertEqual(check_imu_image.problems(image, HUB_IMAGE, "full"), [])
        self.assertEqual(len(check_imu_image.problems(image, HUB_IMAGE, "slim")), 1)

    def test_a_pin_to_another_hub_image_fails(self):
        image = signed([(0xA0, hashlib.sha256(b"older").digest()), (0xA1, b"\x00")])
        self.assertIn("TLV 0xa0 is not the SHA-256 of the hub's image",
                      check_imu_image.problems(image, HUB_IMAGE, "slim"))

    def test_an_image_without_protected_tlvs_fails(self):
        image = signed([], protected_magic=0x6907)
        self.assertEqual(check_imu_image.problems(image, HUB_IMAGE, "slim"), ["no protected TLVs"])

    def test_a_file_that_is_not_an_image_fails(self):
        self.assertEqual(check_imu_image.problems(bytes(64), HUB_IMAGE, "full"),
                         ["not an MCUboot image"])


if __name__ == "__main__":
    unittest.main()
