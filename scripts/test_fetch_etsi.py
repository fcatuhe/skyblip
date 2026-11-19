#!/usr/bin/env python3
"""Self-check for fetch_etsi.py: the checksum refusal.

    python3 scripts/test_fetch_etsi.py

Never touches the network.
"""
import hashlib
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


if __name__ == "__main__":
    unittest.main()
