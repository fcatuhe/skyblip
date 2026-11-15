#!/usr/bin/env python3
"""Self-check for check_bootloader_keys.py, on the keys in firmware/keys/.

    python3 scripts/test_check_bootloader_keys.py
"""
import base64
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import check_bootloader_keys  # noqa: E402

DEVELOPMENT = (ROOT / "firmware/keys/development.pub.pem").read_text(encoding="ascii")
PRODUCTION = (ROOT / "firmware/keys/production.pub.pem").read_text(encoding="ascii")


def der(pem):
    return base64.b64decode("".join(line for line in pem.splitlines() if not line.startswith("-----")))


def bootloader(*pems):
    image = bytes((i * 13 + 5) & 0xFF for i in range(4096))
    for pem in pems:
        image += bytes(37) + der(pem)
    return image + bytes(64)


class CheckBootloaderKeys(unittest.TestCase):
    def test_the_hash_of_a_key_is_the_keyhash_its_images_carry(self):
        # imgtool dumpinfo on CI's images reads KEYHASH 212602f2... for development, 88a4bef5... for production.
        self.assertTrue(check_bootloader_keys.pem_key(DEVELOPMENT).startswith("212602f2"))
        self.assertTrue(check_bootloader_keys.pem_key(PRODUCTION).startswith("88a4bef5"))

    def test_a_development_bootloader_trusts_both_keys_and_nothing_else(self):
        image = bootloader(DEVELOPMENT, PRODUCTION)
        self.assertEqual(check_bootloader_keys.problems(image, [DEVELOPMENT, PRODUCTION]), [])
        self.assertEqual(check_bootloader_keys.problems(image, [PRODUCTION]),
                         ["embeds key 212602f2, which its channel does not trust"])

    def test_a_bootloader_missing_a_key_its_channel_trusts_fails(self):
        self.assertEqual(check_bootloader_keys.problems(bootloader(DEVELOPMENT), [DEVELOPMENT, PRODUCTION]),
                         ["lacks key 88a4bef5"])
        self.assertEqual(check_bootloader_keys.problems(bootloader(), [PRODUCTION]), ["lacks key 88a4bef5"])


if __name__ == "__main__":
    unittest.main()
