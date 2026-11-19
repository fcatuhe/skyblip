#!/usr/bin/env python3
"""Self-check for check_test_coverage.py, on a firmware tree laid out the way firmware/ is.

    python3 scripts/test_check_test_coverage.py
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_test_coverage  # noqa: E402


class Untested(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.fw = pathlib.Path(self.folder.name)
        for header in ("core/gnss/nmea.h", "core/foo/foo.h", "protocols/adsl/packet.h", "products/skyblip_go/pages/radar.h"):
            self.write(header, "")
        self.write("simulator/frontends/browser.cpp", "")
        self.write("test/core/test_nmea.cpp", '#include "core/gnss/nmea.h"\n')
        self.write("test/products/pages/test_radar.cpp", '#include "products/skyblip_go/pages/radar.h"\n')
        self.write("protocols/adsl/test/test_packet.cpp", '#include "protocols/adsl/packet.h"\n')

    def tearDown(self):
        self.folder.cleanup()

    def write(self, relative, text):
        path = self.fw / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_a_new_module_no_test_includes_is_named(self):
        self.assertEqual(check_test_coverage.untested(str(self.fw)), ["core/foo"])

    def test_a_fuzz_harness_or_a_twister_case_reaches_no_module(self):
        self.write("test/fuzz/fuzz_foo.cpp", '#include "core/foo/foo.h"\n')
        self.write("test/twister/foo/src/main.cpp", '#include "core/foo/foo.h"\n')
        self.assertEqual(check_test_coverage.untested(str(self.fw)), ["core/foo"])

    def test_a_test_in_any_folder_of_the_suite_reaches_its_module(self):
        self.write("test/core/diagnostics/test_foo.cpp", '#include "core/foo/foo.h"\n')
        self.assertEqual(check_test_coverage.untested(str(self.fw)), [])


if __name__ == "__main__":
    unittest.main()
