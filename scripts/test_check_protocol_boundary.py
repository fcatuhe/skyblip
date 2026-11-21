#!/usr/bin/env python3
"""Self-check for check_protocol_boundary.py, on stacks laid out the way firmware/protocols/ is.

    python3 scripts/test_check_protocol_boundary.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_protocol_boundary  # noqa: E402


def tree(files):
    root = tempfile.TemporaryDirectory()
    protocols = os.path.join(root.name, "protocols")
    for path, text in files.items():
        full = os.path.join(protocols, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as out:
            out.write(text)
    return root, protocols


class CheckProtocolBoundary(unittest.TestCase):
    def test_a_stack_may_include_the_standard_library_and_any_quoted_header(self):
        root, protocols = tree({
            "adsl/packet.cpp": '#include "protocols/adsl/packet.h"\n#include "protocols/common/crc.h"\n'
                               "#include <cstdint>\n#  include <algorithm>\n",
        })
        with root:
            self.assertEqual(check_protocol_boundary.violations(protocols), [])

    def test_an_angle_include_must_be_the_standard_library(self):
        root, protocols = tree({
            "adsl/transmit.cpp": "#include <zephyr/kernel.h>\n#include <unistd.h>\n#include <algorithm>\n",
        })
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 2)
        self.assertIn("protocols/adsl/transmit.cpp includes <zephyr/kernel.h>", found[0])
        self.assertIn("<unistd.h>", found[1])

    def test_every_kind_of_file_is_read(self):
        root, protocols = tree({
            "adsl/table.inc": "#include <unistd.h>\n",
            "adsb/decode.hpp": "#include <zephyr/kernel.h>\n",
            "alptas/frame.c": "#include <monocypher.h>\n",
        })
        with root:
            self.assertEqual(len(check_protocol_boundary.violations(protocols)), 3)


if __name__ == "__main__":
    unittest.main()
