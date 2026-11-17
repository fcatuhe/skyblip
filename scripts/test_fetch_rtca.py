#!/usr/bin/env python3
"""Self-check for fetch_rtca.py: where the draft lands and which checksum it is held to.

    python3 scripts/test_fetch_rtca.py

Never touches the network.
"""
import unittest

import fetch_rtca


class FetchRtcaTest(unittest.TestCase):
    def test_the_draft_lands_in_docs_reference_under_its_name(self):
        self.assertEqual(fetch_rtca.pdf_path().name, "rtca_do-260b_draft_v4.2.pdf")
        self.assertEqual(fetch_rtca.text_path().name, "rtca_do-260b_draft_v4.2.txt")
        self.assertEqual(fetch_rtca.pdf_path().parent.parts[-2:], ("docs", "reference"))

    def test_the_draft_is_held_to_a_full_sha256(self):
        self.assertRegex(fetch_rtca.DOCUMENT["sha256"], r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
