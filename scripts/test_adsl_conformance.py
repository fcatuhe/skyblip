#!/usr/bin/env python3
"""Self-check for adsl_conformance.py, on a specification and a run small enough to read whole.

    python3 scripts/test_adsl_conformance.py
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import adsl_conformance  # noqa: E402

SPEC = """## TABLE OF CONTENTS

## SUBPART C - PHYSICAL LAYER

### ADS-L.4.SRD860.C.1 Introduction

#### ADS-L.4.SRD860.C.2.5 Frequency Diversity

## SUBPART G - APPLICATION LAYER

#### ADS-L.4.SRD860.G.1.16 Velocity Accuracy

#### ADS-L.4.SRD860.G.1.16 Transmit Rate and Timing

## SUBPART H - CREDITS

## APPENDIX \u2013 PERFORMANCE INFORMATION

## APPENDIX \u2013 DECLARATION OF CONFORMITY
"""

EN300220 = """# a comment line
EN 300 220-1 5.21.1 Description
"""

VERSION = 'inline constexpr char kVersion[] = "1.0.0";\n' \
          'inline constexpr char kSpecification[] = "ADS-L 4 SRD-860 Issue 2";\n'

COMPONENT = "protocols/adsl/test/conformance/test_physical.cpp"
DEVICE = "test/adsl/test_physical.cpp"
UNIT = "protocols/adsl/test/test_packet.cpp"
PART1 = "test/en300220/test_part1.cpp"

PASSED = [
    (DEVICE, "ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency"),
    (COMPONENT, "ADS-L.4.SRD860.C.2.5: the two channels are 868.2 and 868.4 MHz"),
    (DEVICE, "ADS-L.4.SRD860.G.1.16: Traffic goes out at 1 Hz airborne"),
    (DEVICE, "ADS-L.4.SRD860.APPENDIX: the transmitter sits inside the nominal power"),
    (PART1, "EN 300 220-1 5.21.1: a burst goes out below the CCA threshold"),
    (UNIT, "adsl: a frame survives one flipped bit"),
]
SKIPPED = [
    (COMPONENT, "ADS-L.4.SRD860.C.1: ADS-L runs on two bands",
     "prose: an introduction to the bands"),
]
PROSE = {
    DEVICE: {"ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency":
             "skyBlip's reading: every transmission the schedule makes alternates."},
    COMPONENT: {},
    UNIT: {},
    PART1: {},
}


def rendered():
    return adsl_conformance.render(PASSED, SKIPPED, PROSE, SPEC, EN300220, VERSION)


class AdslConformance(unittest.TestCase):
    def test_the_header_counts_the_clauses_and_the_clause_cases_alone(self):
        self.assertIn("skyBlip's ADS-L implementation 1.0.0, read against ADS-L 4 SRD-860 Issue 2",
                      rendered())
        self.assertIn("Each of the specification's 3 clause headings is below", rendered())
        self.assertIn("4 pass and 1 are skipped (1 prose)", rendered())

    def test_sections_follow_the_specification_and_drop_the_empty_ones(self):
        headings = [line for line in rendered().splitlines() if line.startswith("## ")]
        self.assertEqual(headings, [
            "## SUBPART C - PHYSICAL LAYER",
            "## SUBPART G - APPLICATION LAYER",
            "## APPENDIX - PERFORMANCE INFORMATION",
            "## EN 300 220, the standard D.3 names",
            "## The stack's own tests",
        ])

    def test_a_clause_lists_the_component_before_the_device_with_each_file(self):
        text = rendered()
        component = text.index("- the two channels are 868.2 and 868.4 MHz (`" + COMPONENT)
        device = text.index("- consecutive Traffic transmissions change frequency (`" + DEVICE)
        self.assertLess(component, device)
        self.assertIn("  > skyBlip's reading: every transmission the schedule makes alternates.",
                      text)

    def test_a_skipped_case_says_why_under_its_clause(self):
        self.assertIn("### ADS-L.4.SRD860.C.1 Introduction\n\n"
                      "- ADS-L runs on two bands (`" + COMPONENT + "`)\n"
                      "  > Skipped, prose: an introduction to the bands", rendered())

    def test_two_clauses_with_one_number_share_one_heading(self):
        text = rendered()
        self.assertEqual(text.count("### ADS-L.4.SRD860.G.1.16"), 1)
        self.assertIn("Issue 2 gives the same number to Transmit Rate and Timing", text)

    def test_the_appendix_the_unit_tests_and_en_300_220_have_their_cases(self):
        text = rendered()
        self.assertIn("### ADS-L.4.SRD860.APPENDIX Performance information\n\n"
                      "- the transmitter sits inside the nominal power", text)
        self.assertIn("### EN 300 220-1 5.21.1 Description\n\n"
                      "- a burst goes out below the CCA threshold", text)
        self.assertIn("### " + UNIT, text)
        self.assertIn("- a frame survives one flipped bit", text)


if __name__ == "__main__":
    unittest.main()
