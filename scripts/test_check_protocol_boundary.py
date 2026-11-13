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
    def test_the_tree_as_it_stands_keeps_the_boundary(self):
        self.assertEqual(check_protocol_boundary.violations(), [])

    def test_a_stack_may_include_itself_common_and_the_standard_library(self):
        root, protocols = tree({
            "adsl/packet.cpp": '#include "protocols/adsl/packet.h"\n#include "protocols/common/crc.h"\n'
                               "#include <cstdint>\n",
            "adsl/test/test_packet.cpp": '#include "doctest/doctest.h"\n#include "protocols/common/test/channel.h"\n',
        })
        with root:
            self.assertEqual(check_protocol_boundary.violations(protocols), [])

    def test_a_skyblip_header_is_refused_wherever_it_is_included(self):
        root, protocols = tree({
            "adsl/packet.h": '#include "core/model/aircraft.h"\n',
            "adsb/test/test_message.cpp": '#include "test/support/adsb_frames.h"\n',
        })
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 2)
        self.assertIn('protocols/adsb/test/test_message.cpp includes "test/support/adsb_frames.h"', found[0])
        self.assertIn('protocols/adsl/packet.h includes "core/model/aircraft.h"', found[1])

    def test_one_stack_may_not_include_another(self):
        root, protocols = tree({"alptas/frame.cpp": '#include "protocols/adsl/packet.h"\n'})
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 1)
        self.assertIn("outside protocols/alptas/ and protocols/common/", found[0])

    def test_doctest_is_for_tests_only(self):
        root, protocols = tree({"adsb/message.cpp": '#include "doctest/doctest.h"\n'})
        with root:
            self.assertEqual(len(check_protocol_boundary.violations(protocols)), 1)

    def test_an_angle_include_must_be_the_standard_library(self):
        root, protocols = tree({
            "adsl/transmit.cpp": "#include <zephyr/kernel.h>\n#include <monocypher.h>\n#include <algorithm>\n",
        })
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 2)
        self.assertIn("<zephyr/kernel.h>", found[0])
        self.assertIn("<monocypher.h>", found[1])

    def test_a_path_that_climbs_out_of_the_stack_is_refused(self):
        root, protocols = tree({"adsl/packet.cpp": '#include "protocols/adsl/../../core/model/aircraft.h"\n'})
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 1)
        self.assertIn("not written plainly", found[0])

    def test_an_include_that_names_no_quoted_or_angled_header_is_refused(self):
        root, protocols = tree({"adsl/packet.cpp": "#include HEADER_OF(packet)\n#  include <cstdint>\n"})
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 1)
        self.assertIn("names no header", found[0])

    def test_every_kind_of_file_is_read(self):
        root, protocols = tree({
            "adsl/table.inc": '#include "core/settings/address.h"\n',
            "adsb/decode.hpp": "#include <zephyr/kernel.h>\n",
            "alptas/frame.c": '#include "hardware/parts/sx1262/sx1262.h"\n',
        })
        with root:
            self.assertEqual(len(check_protocol_boundary.violations(protocols)), 3)

    def test_a_file_directly_under_protocols_may_include_only_common(self):
        root, protocols = tree({
            "all.h": '#include "protocols/common/crc.h"\n#include "protocols/adsl/packet.h"\n',
        })
        with root:
            found = check_protocol_boundary.violations(protocols)
        self.assertEqual(len(found), 1)
        self.assertIn("protocols/all.h", found[0])

    def test_test_is_read_off_the_path_inside_protocols_not_the_checkout(self):
        root = tempfile.TemporaryDirectory()
        with root:
            protocols = os.path.join(root.name, "test", "protocols")
            os.makedirs(os.path.join(protocols, "adsl"))
            with open(os.path.join(protocols, "adsl", "packet.cpp"), "w", encoding="utf-8") as out:
                out.write('#include "doctest/doctest.h"\n')
            self.assertEqual(len(check_protocol_boundary.violations(protocols)), 1)


if __name__ == "__main__":
    unittest.main()
