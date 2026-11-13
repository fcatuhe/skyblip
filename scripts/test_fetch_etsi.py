#!/usr/bin/env python3
"""Self-check for fetch_etsi.py: the checksum refusal and the file naming.

    python3 scripts/test_fetch_etsi.py

Never touches the network.
"""
import hashlib
import pathlib
import unittest

import fetch_etsi


class FetchEtsiTest(unittest.TestCase):
    def test_verify_accepts_a_pdf_with_the_recorded_checksum(self):
        data = b"%PDF-1.7 body"
        fetch_etsi.verify(data, hashlib.sha256(data).hexdigest(), "u")

    def test_verify_refuses_a_different_checksum(self):
        with self.assertRaisesRegex(ValueError, "expected"):
            fetch_etsi.verify(b"%PDF-1.7 body", "0" * 64, "u")

    def test_verify_refuses_a_challenge_page(self):
        page = b"<!DOCTYPE html>"
        with self.assertRaisesRegex(ValueError, "did not return a PDF"):
            fetch_etsi.verify(page, hashlib.sha256(page).hexdigest(), "u")

    def test_files_land_in_docs_reference_under_the_standard_names(self):
        names = [fetch_etsi.pdf_path(d).name for d in fetch_etsi.DOCUMENTS]
        self.assertEqual(names, ["etsi_en_300_220-1_v3.1.1.pdf", "etsi_en_300_220-2_v3.3.1.pdf"])
        self.assertEqual(fetch_etsi.md_path(fetch_etsi.DOCUMENTS[0]).name, "etsi_en_300_220-1_v3.1.1.md")
        self.assertEqual(fetch_etsi.pdf_path(fetch_etsi.DOCUMENTS[0]).parent.parts[-2:], ("docs", "reference"))


if __name__ == "__main__":
    unittest.main()
