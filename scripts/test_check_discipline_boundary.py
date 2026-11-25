#!/usr/bin/env python3
"""Self-check for check_discipline_boundary.py, on packs laid out the way firmware/disciplines/ is.

    python3 scripts/test_check_discipline_boundary.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_discipline_boundary  # noqa: E402


def tree(files):
    root = tempfile.TemporaryDirectory()
    disciplines = os.path.join(root.name, "disciplines")
    for path, text in files.items():
        full = os.path.join(disciplines, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as out:
            out.write(text)
    return root, disciplines


class CheckDisciplineBoundary(unittest.TestCase):
    def test_a_pack_may_read_own_ship_the_traffic_and_the_flight_arithmetic(self):
        root, disciplines = tree({
            "soaring/soaring.cpp": '#include "disciplines/soaring/soaring.h"\n'
                                   '#include "core/flight/arc.h"\n#include "core/traffic/alarm.h"\n'
                                   '#include "protocols/common/geometry.h"\n#include <cstdint>\n',
        })
        with root:
            self.assertEqual(check_discipline_boundary.violations(disciplines), [])

    def test_a_pack_reaches_no_radio_setting_service_or_other_pack(self):
        root, disciplines = tree({
            "soaring/soaring.cpp": '#include "core/radio/schedule.h"\n#include "core/settings/address.h"\n'
                                   '#include "protocols/adsl/packet.h"\n#include "products/common/alarm.h"\n'
                                   '#include "disciplines/free_flight/free_flight.h"\n',
        })
        with root:
            found = check_discipline_boundary.violations(disciplines)
        self.assertEqual(len(found), 5)
        self.assertIn('disciplines/soaring/soaring.cpp includes "core/radio/schedule.h"', found[0])

    def test_the_dispatcher_may_include_every_pack(self):
        root, disciplines = tree({
            "discipline.cpp": '#include "disciplines/discipline.h"\n#include "disciplines/soaring/soaring.h"\n',
        })
        with root:
            self.assertEqual(check_discipline_boundary.violations(disciplines), [])

    def test_an_angle_include_must_be_the_standard_library(self):
        root, disciplines = tree({"soaring/soaring.cpp": "#include <zephyr/kernel.h>\n#include <algorithm>\n"})
        with root:
            found = check_discipline_boundary.violations(disciplines)
        self.assertEqual(found, ["disciplines/soaring/soaring.cpp includes <zephyr/kernel.h>, "
                                 "which is not the C++ standard library"])


if __name__ == "__main__":
    unittest.main()
