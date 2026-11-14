#!/usr/bin/env python3
"""Self-check for check_en300220_clauses.py, on a listing, a suite and a standard written to disk.

    python3 scripts/test_check_en300220_clauses.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_en300220_clauses  # noqa: E402

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


class CheckEn300220Clauses(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.folder.name)
        self.suite = self.root / "suite"
        self.suite.mkdir()
        self.listing = self.suite / "clauses.txt"
        self.standard = self.root / "part1.md"
        self.write(self.listing, LISTING)
        self.write(self.suite / "test_part1.cpp", SUITE)
        self.write(self.standard, STANDARD)

    def tearDown(self):
        self.folder.cleanup()

    def write(self, path, text):
        path.write_text(text, encoding="utf-8")

    def check(self, documents=None):
        if documents is None:
            documents = {"EN 300 220-1": str(self.standard)}
        return check_en300220_clauses.main(str(self.listing), str(self.suite), documents)

    def test_the_suite_as_it_stands_reads_every_listed_clause(self):
        self.assertEqual(check_en300220_clauses.main(), 0)

    def test_a_listing_and_a_suite_that_agree_pass(self):
        self.assertEqual(self.check(), 0)

    def test_a_listed_clause_with_no_case_fails(self):
        self.write(self.listing, LISTING + "EN 300 220-2 4.6.2.3 Limits\n")
        self.assertEqual(self.check(), 1)

    def test_a_case_naming_an_unlisted_clause_fails(self):
        self.write(self.suite / "test_part2.cpp",
                   'TEST_CASE("EN 300 220-2 4.4.3.3: under 1 %") {\n    CHECK(true);\n}\n')
        self.assertEqual(self.check(), 1)

    def test_a_skip_of_no_kind_fails(self):
        self.write(self.suite / "test_part1.cpp", SUITE.replace("optional: no AFA", "no AFA"))
        self.assertEqual(self.check(), 1)

    def test_a_heading_the_fetched_standard_does_not_have_fails(self):
        self.write(self.listing, LISTING.replace("5.21.1 Description", "5.21.1 Descriptoin"))
        self.write(self.suite / "test_part1.cpp", SUITE)
        self.assertEqual(self.check(), 1)

    def test_a_standard_not_fetched_leaves_the_headings_unchecked(self):
        self.write(self.listing, LISTING.replace("5.21.1 Description", "5.21.1 Descriptoin"))
        missing = {"EN 300 220-1": os.path.join(self.folder.name, "absent.md")}
        self.assertEqual(self.check(missing), 0)


if __name__ == "__main__":
    unittest.main()
