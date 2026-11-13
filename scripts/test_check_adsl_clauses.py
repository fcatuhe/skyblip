#!/usr/bin/env python3
"""Self-check for check_adsl_clauses.py, on skipped cases written the way the suite writes them.

    python3 scripts/test_check_adsl_clauses.py
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_adsl_clauses  # noqa: E402

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


class CheckAdslClauses(unittest.TestCase):
    def test_the_suite_as_it_stands_names_every_clause_and_every_skip(self):
        self.assertEqual(check_adsl_clauses.main(), 0)

    def test_a_skipped_case_is_read_for_its_message_across_lines(self):
        self.assertEqual(check_adsl_clauses.skip_messages(CASES), [
            "optional: no secure signature is produced",
            "not implemented: no listen-before-talk, so neither the 3000 ms force nor the "
            "silence after it",
            "the payload is not implemented: type 6 is dropped",
        ])

    def test_a_message_opens_on_one_of_four_kinds_or_on_none(self):
        self.assertEqual(check_adsl_clauses.kind_of("optional: no signature"), "optional")
        self.assertEqual(check_adsl_clauses.kind_of("not implemented: no LBT"), "not implemented")
        self.assertEqual(check_adsl_clauses.kind_of("prose: a block diagram"), "prose")
        self.assertEqual(check_adsl_clauses.kind_of("paperwork: no manual"), "paperwork")
        self.assertIsNone(check_adsl_clauses.kind_of("the payload is not implemented: type 6"))
        self.assertIsNone(check_adsl_clauses.kind_of("no colon at all"))
        self.assertIsNone(check_adsl_clauses.kind_of(""))


if __name__ == "__main__":
    unittest.main()
