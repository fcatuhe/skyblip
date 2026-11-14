#!/usr/bin/env python3
"""Self-check for check_spec_markdown.py, on a three-page layout extraction written out below.

    python3 scripts/test_check_spec_markdown.py

Runs no pdftotext: the fixture stands in for its layout output, and serves as its raw output too.
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_spec_markdown  # noqa: E402
import spec_to_md  # noqa: E402
from layout_to_md import convert  # noqa: E402

LAYOUT = "\n".join([
    "                ADS-L 4 SDR-860 Issue 2              TABLE OF CONTENTS",
    "",
    "                     TABLE OF CONTENTS",
    "SUBPART A - GENERAL ........................................... 2",
    "    ADS-L.4.SRD860.A.1        Introduction .................... 2",
    "",
    "Note: EASA would appreciate stakeholders' voluntary feedback.",
    "",
    "ED Decision 2022/024/R                                   Page 1 of 3",
    "\f                ADS-L 4 SDR-860 Issue 2              SUBPART A - GENERAL",
    "",
    "SUBPART A - GENERAL",
    "ADS-L.4.SRD860.A.1        Introduction",
    "ED Decision 2022/024/R",
    "The ADS-L system shall carry every word of its specification over",
    "the page break, the hyphenated word and the footnote, and the ADS-",
    "L system shall do so for the reader of the markdown, so that the",
    "reader of the PDF and the reader of the markdown read the same and",
    "_____________________",
    "4",
    "    Payload definition is outside the scope of this document.",
    "",
    "ED Decision 2022/024/R                                   Page 2 of 3",
    "\f                ADS-L 4 SDR-860 Issue 2              SUBPART A - GENERAL",
    "",
    "nobody is left with half a requirement in the field Traffic4.",
    "",
    "    Value        Payload",
    "    0            Reserved",
    "",
    "ED Decision 2022/024/R                                   Page 3 of 3",
    "\f                ADS-L 4 SDR-860 Issue 2              SUBPART A - GENERAL",
    "",
    "    1            Traffic",
    "    2            Status",
    "",
    "ED Decision 2022/024/R                                   Page 4 of 3",
])


def findings(markdown, raw=LAYOUT):
    return check_spec_markdown.check(LAYOUT, raw, markdown)


class CheckSpecMarkdown(unittest.TestCase):
    def setUp(self):
        self.markdown = convert(LAYOUT, spec_to_md.HEADER, spec_to_md.FOOTER, spec_to_md.heading)

    def test_the_converter_carries_every_word_and_the_check_agrees(self):
        self.assertEqual(findings(self.markdown), [])
        self.assertIn("the ADS-L system shall do so", self.markdown)
        self.assertIn("read the same and nobody is left", self.markdown)
        self.assertIn("ED Decision 2022/024/R", self.markdown)
        self.assertTrue(self.markdown.endswith("Payload definition is outside the scope of this document.\n"))

    def test_a_dropped_word_is_reported_with_its_clause(self):
        dropped = self.markdown.replace("half a requirement", "a requirement")
        self.assertEqual(findings(dropped), ['missing from ADS-L.4.SRD860.A.1 Introduction (page 3): "half"'])

    def test_a_line_dropped_as_furniture_inside_a_page_is_reported(self):
        dropped = self.markdown.replace("ED Decision 2022/024/R The ADS-L", "The ADS-L")
        self.assertEqual(findings(dropped), ['missing from ADS-L.4.SRD860.A.1 Introduction (page 2): "ED Decision 2022/024/R"'])

    def test_text_after_the_table_of_contents_is_text(self):
        dropped = self.markdown.replace("Note: EASA would appreciate stakeholders' voluntary feedback.\n", "")
        self.assertTrue(any("missing" in f and "voluntary feedback." in f for f in findings(dropped)))

    def test_a_sentence_cut_at_a_page_break_is_reported(self):
        cut = self.markdown.replace("read the same and nobody", "read the same and\n\nnobody")
        self.assertEqual(len(findings(cut)), 1)
        self.assertTrue(findings(cut)[0].startswith("sentence cut in ADS-L.4.SRD860.A.1 Introduction (page 2)"))

    def test_a_word_split_at_its_hyphen_is_reported(self):
        split = self.markdown.replace("ADS-L system shall do", "ADS- L system shall do")
        self.assertIn('missing from ADS-L.4.SRD860.A.1 Introduction (page 2): "ADS-L"', findings(split))

    def test_a_footnote_left_where_it_fell_is_reported(self):
        note = "4 Payload definition is outside the scope of this document.\n"
        misplaced = self.markdown.replace(note, "").replace("nobody is left", note + "\nnobody is left")
        self.assertTrue(any("footnote of page 2" in f for f in findings(misplaced)))

    def test_a_lost_footnote_is_reported(self):
        lost = self.markdown.replace("4 Payload definition is outside the scope of this document.\n", "")
        self.assertIn('footnote of page 2 missing: "4 Payload definition is outside the scope of this document."', findings(lost))

    def test_a_table_cut_at_a_page_break_is_reported(self):
        cut = self.markdown.replace("0            Reserved\n", "0            Reserved\n```\n\n```\n")
        self.assertEqual(len(findings(cut)), 1)
        self.assertTrue(findings(cut)[0].startswith("table cut in ADS-L.4.SRD860.A.1 Introduction (page 3)"))

    def test_text_the_layout_extraction_lost_is_reported_from_the_raw_one(self):
        raw = LAYOUT.replace("Traffic4.", "Traffic4. Lost")
        self.assertEqual(findings(self.markdown, raw),
                         ["page 3 reads differently in layout and raw order: layout lacks 'Lost' and has '' more"])


if __name__ == "__main__":
    unittest.main()
