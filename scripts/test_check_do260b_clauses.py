#!/usr/bin/env python3
"""Self-check for check_do260b_clauses.py, on a listing, two suite halves and a draft on disk.

    python3 scripts/test_check_do260b_clauses.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_do260b_clauses  # noqa: E402

LISTING = """# a comment
DO-260B 2.2.10.5 Minimum Number of Participant Track Files
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


class CheckDo260bClauses(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.folder.name)
        self.component = self.root / "component"
        self.device = self.root / "device"
        self.component.mkdir()
        self.device.mkdir()
        self.listing = self.device / "clauses.txt"
        self.draft = self.root / "draft.txt"
        self.write(self.listing, LISTING)
        self.write(self.component / "test_assembly.cpp", COMPONENT)
        self.write(self.device / "test_receiver.cpp", DEVICE)
        self.write(self.draft, DRAFT)

    def tearDown(self):
        self.folder.cleanup()

    def write(self, path, text):
        path.write_text(text, encoding="utf-8")

    def check(self, draft=None):
        return check_do260b_clauses.main(str(self.listing), (str(self.component), str(self.device)),
                                         str(draft or self.draft))

    def test_the_suite_as_it_stands_reads_every_listed_clause(self):
        self.assertEqual(check_do260b_clauses.main(), 0)

    def test_a_listing_and_two_halves_that_agree_pass(self):
        self.assertEqual(self.check(), 0)

    def test_a_listed_clause_with_no_case_fails(self):
        self.write(self.listing, LISTING + "DO-260B 2.2.10.6 Reasonableness Tests\n")
        self.assertEqual(self.check(), 1)

    def test_a_case_naming_an_unlisted_clause_fails(self):
        self.write(self.component / "test_cpr.cpp",
                   'TEST_CASE("DO-260B A.1.7.7: a pair decodes") {\n    CHECK(true);\n}\n')
        self.assertEqual(self.check(), 1)

    def test_a_skip_of_no_kind_fails(self):
        self.write(self.device / "test_receiver.cpp",
                   DEVICE.replace("not implemented: one", "one"))
        self.assertEqual(self.check(), 1)

    def test_a_heading_the_fetched_draft_does_not_have_fails(self):
        self.write(self.listing, LISTING.replace("Duplicate Address", "Duplicate Adress"))
        self.assertEqual(self.check(), 1)

    def test_a_heading_cut_by_the_running_header_still_matches(self):
        self.write(self.draft, DRAFT.replace("Files\n", "\n" + DRAFT.splitlines()[2] + "\nFiles\n"))
        self.assertEqual(self.check(), 0)

    def test_a_draft_not_fetched_leaves_the_headings_unchecked(self):
        self.write(self.listing, LISTING.replace("Duplicate Address", "Duplicate Adress"))
        self.assertEqual(self.check(os.path.join(self.folder.name, "absent.txt")), 0)


if __name__ == "__main__":
    unittest.main()
