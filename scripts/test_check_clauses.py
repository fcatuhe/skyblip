#!/usr/bin/env python3
"""Self-check for check_clauses.py, on specifications, listings, suites and fetched documents written to disk.

    python3 scripts/test_check_clauses.py
"""
import dataclasses
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_clauses  # noqa: E402

ADSL, EN300220, DO260B = check_clauses.STANDARDS

CASES = '''
TEST_CASE("ADS-L.4.SRD860.E.4.1: a signature carries the UTC second" * doctest::skip()) {
    FAIL("optional: no secure signature is produced");
}

TEST_CASE("ADS-L.4.SRD860.D.3: a burst waits on a carrier sense" *
          doctest::skip()) {
    FAIL(
        "not implemented: no listen-before-talk, so neither the 3000 ms force nor the "
        "silence after it");
}

TEST_CASE("ADS-L.4.SRD860.B.3: an integer rides the air least significant byte first") {
    CHECK(true);
}

TEST_CASE("ADS-L.4.SRD860.G.5: a Remote Identification payload" * doctest::skip()) {
    FAIL("the payload is not implemented: type 6 is dropped");
}
'''

SPEC = """## ADS-L.4.SRD860.B.3 Byte order

### ADS-L.4.SRD860.D.3 Channel access
"""

ADSL_SUITE = '''
TEST_CASE("ADS-L.4.SRD860.B.3: an integer rides the air least significant byte first") {
    CHECK(true);
}

TEST_CASE("ADS-L.4.SRD860.APPENDIX: the worked example decodes") {
    CHECK(true);
}
'''

LISTING = """# a comment
EN 300 220-1 5.21.1 Description
EN 300 220-1 5.21.4.1 Description
"""

SUITE = '''
TEST_CASE("EN 300 220-1 5.21.1: a burst goes out below the threshold") {
    CHECK(true);
}

TEST_CASE("EN 300 220-1 5.21.4.1: a busy channel is left for another" *
          doctest::skip()) {
    FAIL("optional: no AFA");
}
'''

STANDARD = """#### 5.21.1 Description

Before transmitting, a polite device senses the channel.

##### 5.21.4.1 Description
"""

DRAFT_LISTING = """DO-260B 2.2.10.5 Minimum Number of Participant Track Files
DO-260B 2.2.10.7 Duplicate Address Processing
"""

COMPONENT = '''
TEST_CASE("DO-260B 2.2.10.5: a class A0 receiver keeps 100 track files") {
    CHECK(true);
}
'''

DEVICE = '''
TEST_CASE("DO-260B 2.2.10.7: two aircraft sending one address are told apart" *
          doctest::skip()) {
    FAIL("not implemented: one record per address");
}
'''

DRAFT = """2.2.10.5       Minimum Number of Participant Track Files
               In the absence of an applied interference environment
                     Working Paper 1090-WP30-18 as DRAFT Version 4.2 of DO-282B
2.2.10.7     Duplicate Address Processing
"""


class SkippedCases(unittest.TestCase):
    def test_a_skipped_case_is_read_for_its_message_across_lines(self):
        self.assertEqual(check_clauses.skip_messages(CASES), [
            "optional: no secure signature is produced",
            "not implemented: no listen-before-talk, so neither the 3000 ms force nor the "
            "silence after it",
            "the payload is not implemented: type 6 is dropped",
        ])

    def test_a_message_opens_on_one_of_the_kinds_or_on_none(self):
        self.assertEqual(check_clauses.kind_of("optional: no signature"), "optional")
        self.assertEqual(check_clauses.kind_of("not implemented: no LBT"), "not implemented")
        self.assertEqual(check_clauses.kind_of("prose: a block diagram"), "prose")
        self.assertEqual(check_clauses.kind_of("paperwork: no manual"), "paperwork")
        self.assertIsNone(check_clauses.kind_of("the payload is not implemented: type 6"))
        self.assertIsNone(check_clauses.kind_of("no colon at all"))
        self.assertIsNone(check_clauses.kind_of(""))


class Fixture(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return str(path)


class ClausesOfACommittedSpecification(Fixture):
    def check(self, spec=SPEC):
        self.write("suite/test_conformance.cpp", ADSL_SUITE)
        standard = dataclasses.replace(ADSL, spec=self.write("spec.md", spec), suites=(str(self.root / "suite"),))
        return check_clauses.check(standard)

    def test_every_heading_read_and_the_appendix_named_pass(self):
        self.assertEqual(self.check(SPEC.replace("### ADS-L.4.SRD860.D.3 Channel access\n", "")), 0)

    def test_a_heading_with_no_case_fails(self):
        self.assertEqual(self.check(), 1)


class ClausesOfAListing(Fixture):
    def setUp(self):
        super().setUp()
        self.write("suite/test_part1.cpp", SUITE)
        self.listing = self.write("suite/clauses.txt", LISTING)
        self.standard = dataclasses.replace(
            EN300220, listing=self.listing, suites=(str(self.root / "suite"),),
            documents={"EN 300 220-1": self.write("part1.md", STANDARD)})

    def check(self, **changes):
        return check_clauses.check(dataclasses.replace(self.standard, **changes))

    def test_a_listing_and_a_suite_that_agree_pass(self):
        self.assertEqual(self.check(), 0)

    def test_a_listed_clause_with_no_case_fails(self):
        self.write("suite/clauses.txt", LISTING + "EN 300 220-2 4.6.2.3 Limits\n")
        self.assertEqual(self.check(), 1)

    def test_a_line_that_names_no_clause_fails(self):
        self.write("suite/clauses.txt", LISTING + "5.21.5 Description\n")
        self.assertEqual(self.check(), 1)

    def test_a_case_naming_an_unlisted_clause_fails(self):
        self.write("suite/test_part2.cpp", 'TEST_CASE("EN 300 220-2 4.4.3.3: under 1 %") {\n    CHECK(true);\n}\n')
        self.assertEqual(self.check(), 1)

    def test_a_case_named_after_no_clause_fails(self):
        self.write("suite/test_part2.cpp", 'TEST_CASE("a burst is short") {\n    CHECK(true);\n}\n')
        self.assertEqual(self.check(), 1)

    def test_a_skip_of_no_kind_fails(self):
        self.write("suite/test_part1.cpp", SUITE.replace("optional: no AFA", "no AFA"))
        self.assertEqual(self.check(), 1)

    def test_a_heading_the_fetched_standard_does_not_have_fails(self):
        self.write("suite/clauses.txt", LISTING.replace("5.21.1 Description", "5.21.1 Descriptoin"))
        self.assertEqual(self.check(), 1)

    def test_a_standard_not_fetched_leaves_the_headings_unchecked(self):
        self.write("suite/clauses.txt", LISTING.replace("5.21.1 Description", "5.21.1 Descriptoin"))
        self.assertEqual(self.check(documents={"EN 300 220-1": str(self.root / "absent.md")}), 0)


class ClausesOfADraftInTwoHalves(Fixture):
    def setUp(self):
        super().setUp()
        self.write("component/test_assembly.cpp", COMPONENT)
        self.write("device/test_receiver.cpp", DEVICE)
        self.standard = dataclasses.replace(
            DO260B, listing=self.write("device/clauses.txt", DRAFT_LISTING),
            suites=(str(self.root / "component"), str(self.root / "device")))

    def check(self, draft):
        documents = {"DO-260B": self.write("draft.txt", draft)}
        return check_clauses.check(dataclasses.replace(self.standard, documents=documents))

    def test_a_listing_and_two_halves_that_agree_pass(self):
        self.assertEqual(self.check(DRAFT), 0)

    def test_a_heading_the_fetched_draft_does_not_have_fails(self):
        self.assertEqual(self.check(DRAFT.replace("Duplicate Address", "Duplicate Adress")), 1)

    def test_a_heading_cut_by_the_running_header_still_matches(self):
        self.assertEqual(self.check(DRAFT.replace("Files\n", "\n" + DRAFT.splitlines()[2] + "\nFiles\n")), 0)


if __name__ == "__main__":
    unittest.main()
