#!/usr/bin/env python3
"""Self-check for check_protocol_freeze.py, on a tree laid out the way the repository is.

    python3 scripts/test_check_protocol_freeze.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_protocol_freeze  # noqa: E402

STACK = "firmware/protocols/adsl"


def tree():
    root = tempfile.TemporaryDirectory()
    files = {
        f"{STACK}/version.h": 'inline constexpr char kVersion[] = "1.0.0";\n',
        f"{STACK}/packet.h": '#include "protocols/common/crc.h"\n',
        f"{STACK}/test/test_packet.cpp": "// a case\n",
        "firmware/protocols/common/crc.h": '#include "protocols/common/bit_field.h"\n',
        "firmware/protocols/common/crc.cpp": "// crc\n",
        "firmware/protocols/common/bit_field.h": "// fields\n",
        "firmware/protocols/common/manchester.h": "// not included by adsl\n",
        "firmware/test/adsl/test_physical.cpp": "// a device case\n",
        "firmware/core/protocol/air.cpp": "// the receiver\n",
        f"{STACK}/version.lock": f"version 1.0.0\nroot {STACK}/\nroot firmware/test/adsl/\n"
                                 "root firmware/core/protocol/air.cpp\n",
    }
    for path, text in files.items():
        write(root.name, path, text)
    check_protocol_freeze.write_lock("adsl", root.name)
    return root


def write(root, path, text):
    full = os.path.join(root, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8") as out:
        out.write(text)


class CheckProtocolFreeze(unittest.TestCase):
    def test_the_tree_as_it_stands_matches_its_locks(self):
        for stack in check_protocol_freeze.frozen_stacks():
            self.assertEqual(check_protocol_freeze.check(stack), [])

    def test_a_sealed_tree_passes_and_seals_the_common_files_it_includes(self):
        with tree() as root:
            self.assertEqual(check_protocol_freeze.check("adsl", root), [])
            _, _, sealed = check_protocol_freeze.read_lock("adsl", root)
            self.assertIn("firmware/protocols/common/crc.h", sealed)
            self.assertIn("firmware/protocols/common/crc.cpp", sealed)
            self.assertIn("firmware/protocols/common/bit_field.h", sealed)
            self.assertNotIn("firmware/protocols/common/manchester.h", sealed)
            self.assertNotIn(f"{STACK}/version.lock", sealed)

    def test_a_changed_file_under_a_root_fails(self):
        with tree() as root:
            write(root, "firmware/test/adsl/test_physical.cpp", "// a weaker case\n")
            found = check_protocol_freeze.check("adsl", root)
            self.assertEqual(len(found), 1)
            self.assertIn("firmware/test/adsl/test_physical.cpp changed", found[0])

    def test_an_added_and_a_removed_file_fail(self):
        with tree() as root:
            write(root, f"{STACK}/uplink.h", "// new\n")
            os.remove(os.path.join(root, f"{STACK}/test/test_packet.cpp"))
            found = check_protocol_freeze.check("adsl", root)
            self.assertEqual(len(found), 2)
            self.assertIn("test_packet.cpp is sealed but gone", found[0])
            self.assertIn("uplink.h is under a frozen root but not sealed", found[1])

    def test_a_common_file_the_stack_includes_is_frozen_and_one_it_does_not_is_free(self):
        with tree() as root:
            write(root, "firmware/protocols/common/manchester.h", "// changed for ALP-TAS\n")
            self.assertEqual(check_protocol_freeze.check("adsl", root), [])
            write(root, "firmware/protocols/common/crc.cpp", "// changed\n")
            self.assertEqual(len(check_protocol_freeze.check("adsl", root)), 1)

    def test_a_version_the_lock_does_not_name_fails_until_resealed(self):
        with tree() as root:
            write(root, f"{STACK}/version.h", 'inline constexpr char kVersion[] = "1.0.1";\n')
            found = check_protocol_freeze.check("adsl", root)
            self.assertIn("version.lock says 1.0.0, version.h says 1.0.1", found[0])
            check_protocol_freeze.write_lock("adsl", root)
            self.assertEqual(check_protocol_freeze.check("adsl", root), [])

    def test_a_reseal_without_a_bump_keeps_the_version_and_the_roots(self):
        with tree() as root:
            write(root, "firmware/core/protocol/air.cpp", "// refactored\n")
            check_protocol_freeze.write_lock("adsl", root)
            version, roots, _ = check_protocol_freeze.read_lock("adsl", root)
            self.assertEqual(version, "1.0.0")
            self.assertIn("firmware/core/protocol/air.cpp", roots)
            self.assertEqual(check_protocol_freeze.check("adsl", root), [])


if __name__ == "__main__":
    unittest.main()
