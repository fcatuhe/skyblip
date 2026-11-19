#!/usr/bin/env python3
"""Check both conformance reports on fixtures small enough to read whole."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import conformance  # noqa: E402

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
    (UNIT, "adsl: a frame survives two flipped bits", "optional: no second repair pass"),
]
PROSE = {
    DEVICE: {"ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency":
             "skyBlip's reading: every transmission the schedule makes alternates."},
    COMPONENT: {},
    UNIT: {},
    PART1: {},
}


def rendered():
    return conformance.render_adsl(PASSED, SKIPPED, PROSE, SPEC, EN300220, VERSION)


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
        self.assertIn(f"- a frame survives two flipped bits (`{UNIT}`)\n"
                      "  > Skipped, optional: no second repair pass", text)

    def test_an_en_300_220_case_without_a_clause_stops_the_report(self):
        stray = PASSED + [(PART1, "EN 300 220-9 1.1: a clause the list does not hold")]
        with self.assertRaisesRegex(ValueError, r"no clause.*EN 300 220-9 1\.1"):
            conformance.render_adsl(stray, SKIPPED, PROSE, SPEC, EN300220, VERSION)

    def test_a_case_outside_the_report_paths_stops_the_report(self):
        stray = PASSED + [("test/core/test_other.cpp", "adsl: an unrelated check")]
        with self.assertRaisesRegex(ValueError, r"no place.*test/core/test_other\.cpp"):
            conformance.render_adsl(stray, SKIPPED, PROSE, SPEC, EN300220, VERSION)


ADSB_CLAUSES = """# Receive-side clauses in the order listed
DO-260B A.1.7.9.2 Emitter Centered Local Decoding
DO-260B 2.2.4.3.4 Receiver Signal Processing
DO-260B 2.2.4.3.4.7 Message Reception
"""
ADSB_VERSION = 'inline constexpr char kVersion[] = "1.0.0";\n'
ADSB_COMPONENT = "protocols/adsb/test/conformance/test_reception.cpp"
ADSB_CPR = "protocols/adsb/test/conformance/test_cpr.cpp"
ADSB_DEVICE = "test/adsb/test_receiver.cpp"
ADSB_PASSED = [
    (ADSB_DEVICE, "DO-260B 2.2.4.3.4.7: a framed burst reaches the decoder"),
    (ADSB_COMPONENT, "DO-260B 2.2.4.3.4.7: a frame with valid parity is accepted"),
    (ADSB_CPR, "DO-260B A.1.7.9.2: the previous fix anchors local decoding"),
]
ADSB_SKIPPED = [
    (ADSB_DEVICE, "DO-260B 2.2.4.3.4: the receiver meets its sensitivity target",
     "unmeasured: no bench measurement yet"),
]
ADSB_PROSE = {
    ADSB_COMPONENT: {},
    ADSB_CPR: {},
    ADSB_DEVICE: {ADSB_PASSED[0][1]: "The driver hands the whole message up."},
}


class AdsbConformance(unittest.TestCase):
    def setUp(self):
        with tempfile.TemporaryDirectory() as directory:
            listing = pathlib.Path(directory, "clauses.txt")
            listing.write_text(ADSB_CLAUSES, encoding="utf-8")
            self.clauses = conformance.do260b_clauses(listing)

    def rendered(self, passed=ADSB_PASSED, skipped=ADSB_SKIPPED):
        return conformance.render_adsb(passed, skipped, ADSB_PROSE, self.clauses, ADSB_VERSION)

    def test_the_header_limits_the_report_to_the_receive_side_and_counts_the_cases(self):
        text = self.rendered()
        self.assertIn("ADS-B implementation 1.0.0", text)
        self.assertIn("RTCA DO-260B draft 4.2 (1090-WP30-18)", text)
        self.assertIn("3 receive-side clauses", text)
        self.assertIn("passed 3 cases and skipped 1 (1 unmeasured)", text)
        self.assertNotIn("EN 300 220", text)

    def test_headings_follow_the_clause_listing(self):
        self.assertEqual([line for line in self.rendered().splitlines() if line.startswith("##")], [
            "## Appendix A",
            "### DO-260B A.1.7.9.2 Emitter Centered Local Decoding",
            "## Section 2",
            "### DO-260B 2.2.4.3.4 Receiver Signal Processing",
            "### DO-260B 2.2.4.3.4.7 Message Reception",
        ])

    def test_a_clause_lists_the_component_before_the_device_with_each_file(self):
        text = self.rendered()
        self.assertIn("### DO-260B 2.2.4.3.4.7 Message Reception\n\n"
                      f"- a frame with valid parity is accepted (`{ADSB_COMPONENT}`)\n"
                      f"- a framed burst reaches the decoder (`{ADSB_DEVICE}`)\n"
                      "  > The driver hands the whole message up.", text)

    def test_a_skipped_case_says_why_under_its_clause(self):
        self.assertIn("### DO-260B 2.2.4.3.4 Receiver Signal Processing\n\n"
                      f"- the receiver meets its sensitivity target (`{ADSB_DEVICE}`)\n"
                      "  > Skipped, unmeasured: no bench measurement yet", self.rendered())

    def test_a_skipped_case_cannot_disappear_by_naming_an_unlisted_clause(self):
        skipped = [(ADSB_DEVICE, "DO-260B 2.2.4.99: the receiver meets its sensitivity target",
                    "unmeasured: no bench measurement yet")]
        with self.assertRaisesRegex(ValueError, r"no clause.*DO-260B 2\.2\.4\.99"):
            self.rendered(skipped=skipped)

    def test_a_listed_clause_must_have_a_case_in_the_run(self):
        with self.assertRaisesRegex(ValueError, r"no case reads DO-260B A\.1\.7\.9\.2"):
            self.rendered(passed=ADSB_PASSED[:2])

    def test_a_malformed_listing_names_the_bad_line(self):
        with tempfile.TemporaryDirectory() as directory:
            listing = pathlib.Path(directory, "clauses.txt")
            listing.write_text(ADSB_CLAUSES + "DO-260B 2.2.4\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, r"clauses\.txt:5: DO-260B 2\.2\.4"):
                conformance.do260b_clauses(listing)


class JunitConformance(unittest.TestCase):
    def test_a_skipped_case_reports_its_reason_after_a_failing_check(self):
        xml = """<testsuites><testsuite>
          <testcase classname="test/adsb/test_receiver.cpp" name="DO-260B 2.2.4: the target is met">
            <failure message="true" type="CHECK_FALSE"/>
            <failure message="unmeasured: no bench measurement yet" type="FAIL"/>
          </testcase>
        </testsuite></testsuites>"""
        self.assertEqual(conformance.junit_cases(xml), [
            (ADSB_DEVICE, "DO-260B 2.2.4: the target is met", "unmeasured: no bench measurement yet"),
        ])


if __name__ == "__main__":
    unittest.main()
