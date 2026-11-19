#!/usr/bin/env python3
"""Self-check for behavior_index.py, on a run small enough to read whole.

    python3 scripts/test_behavior_index.py
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import behavior_index  # noqa: E402

BUTTON = "test/core/test_button.cpp"
TONE = "test/core/diagnostics/test_tone.cpp"
SLOT = "protocols/adsl/test/conformance/test_physical.cpp"
CHIME = "test/core/test_chime.cpp"
MARKER = behavior_index.MARKER

RUN = f"""<?xml version="1.0" encoding="UTF-8"?>
<doctest binary="skyblip_tests">
  <Options order_by="file"/>
  <TestSuite>
    <TestCase name="button: a bounce emits nothing" filename="{BUTTON}" line="12" description="">
      <OverallResultsAsserts successes="1" failures="0" test_case_success="true"/>
    </TestCase>
  </TestSuite>
  <TestSuite name="main">
    <TestCase name="button: a stable press emits exactly one event" filename="{BUTTON}" line="30" description="">
      <SubCase name="held" filename="{BUTTON}" line="33"/>
      <OverallResultsAsserts successes="2" failures="0" test_case_success="true"/>
    </TestCase>
  </TestSuite>
  <TestSuite>
    <TestCase name="button: a count in [0, 256) wraps once" filename="{BUTTON}" line="48" description=""/>
    <TestCase name="tone: a fault reads as two beeps" filename="{TONE}" line="7" description=""/>
    <TestCase name="ADS-L.4.SRD860.C.2.5: the two channels are 868.2 and 868.4 MHz" filename="{SLOT}" line="20" description=""/>
    <TestCase name="tone: a click is a fault" filename="{TONE}" line="20" skipped="true"/>
  </TestSuite>
  <TestSuite name="main">
    <TestCase name="tone: a hum is a fault" filename="{TONE}" line="15" skipped="true"/>
  </TestSuite>
  <OverallResultsAsserts successes="5" failures="0"/>
</doctest>
"""

PROSE = {
    BUTTON: ("The one button, debounced into events.",
             {"button: a stable press emits exactly one event": "A held press is still one."}),
    TONE: ("What a fault sounds like.", {}),
    CHIME: ("", {}),
    SLOT: ("ADS-L's physical layer, as the component reads it.", {}),
}


def cases():
    return behavior_index.parse_run(RUN)[0]


def skipped():
    return behavior_index.parse_run(RUN)[1]


class Run(unittest.TestCase):
    # doctest writes no name attribute for the unnamed suite, and reports a skipped case where it would run.
    def test_a_case_takes_its_tier_from_the_test_suite_around_it(self):
        main = [case.name for case in cases() if behavior_index.is_main(case)]
        self.assertEqual(main, ["button: a stable press emits exactly one event"])
        self.assertEqual(len(cases()), 5)
        self.assertEqual(cases()[1].line, 30)

    def test_a_skipped_case_is_set_apart_with_its_tier(self):
        self.assertEqual([(case.name, case.suite) for case in skipped()],
                         [("tone: a click is a fault", ""), ("tone: a hum is a fault", "main")])


class Problems(unittest.TestCase):
    def test_a_clean_run_has_none(self):
        self.assertEqual(behavior_index.problems(cases(), skipped()[:1], PROSE), [])

    def test_each_rule_refuses_its_case(self):
        bad = cases() + [
            behavior_index.Case(BUTTON, 60, "button presses", ""),
            behavior_index.Case(BUTTON, 62, ": no subject", ""),
            behavior_index.Case(BUTTON, 64, "button: ", ""),
            behavior_index.Case(BUTTON, 70, "button: a press is loud", "edge"),
            behavior_index.Case("test/test_loose.cpp", 3, "loose: a case", ""),
        ]
        prose = dict(PROSE, **{"test/test_loose.cpp": ("", {})})
        messages = behavior_index.problems(bad, skipped(), prose)
        self.assertEqual(len(messages), 7)
        self.assertIn("name must read `subject: what holds`: button presses", messages)
        self.assertIn("name must read `subject: what holds`: : no subject", messages)
        self.assertIn("name must read `subject: what holds`: button: ", messages)
        self.assertIn('test suite must be "main" or none, not "edge": button: a press is loud',
                      messages)
        self.assertIn(f"a skipped case claims nothing, so it cannot be main: {TONE}: "
                      "tone: a hum is a fault", messages)
        self.assertIn("no opening comment saying what it pins down: test/test_loose.cpp", messages)
        self.assertIn("in no area: test/test_loose.cpp: name its area in AREAS", messages)

    def test_a_file_with_only_skipped_cases_still_needs_its_opening_comment(self):
        silent = [behavior_index.Case(CHIME, 9, "chime: a tone is a chime", "")]
        self.assertEqual(behavior_index.problems(cases(), silent, PROSE),
                         [f"no opening comment saying what it pins down: {CHIME}"])


class Pages(unittest.TestCase):
    def test_one_page_per_area_with_subdirectories_folded_in(self):
        pages = behavior_index.render_pages(cases(), PROSE)
        self.assertEqual(sorted(pages), ["core.md", "protocols-adsl-conformance.md"])
        self.assertIn("**tone**\n\n- a fault reads as two beeps", pages["core.md"])

    def test_main_claims_come_first_and_the_rest_fold_below(self):
        page = behavior_index.render_pages(cases(), PROSE)["core.md"]
        main = page.index("## Main claims")
        details = page.index("<details><summary>Edge cases and details (3)</summary>")
        fold_end = page.index("</details>")
        files = page.index("## Test files")
        self.assertLess(main, details)
        self.assertLess(details, fold_end)
        self.assertLess(fold_end, files)
        self.assertEqual(page.count("a stable press emits exactly one event"), 1)
        self.assertIn("- a stable press emits exactly one event "
                      f"([test_button.cpp:30](../../firmware/{BUTTON}#L30))\n"
                      "  > A held press is still one.", page[main:details])
        for claim in ("a bounce emits nothing", "a count in [0, 256) wraps once",
                      "a fault reads as two beeps"):
            self.assertEqual(page.count(claim), 1)
            self.assertIn(claim, page[details:fold_end])
        self.assertIn("- a count in [0, 256) wraps once "
                      f"([test_button.cpp:48](../../firmware/{BUTTON}#L48))", page)

    def test_a_page_with_no_main_case_hides_nothing(self):
        page = behavior_index.render_pages(cases(), PROSE)["protocols-adsl-conformance.md"]
        self.assertNotIn("<details>", page)
        self.assertIn("## Claims\n\n**ADS-L.4.SRD860.C.2.5**\n\n- the two channels", page)
        self.assertIn(f"### [{SLOT}](../../firmware/{SLOT})\n\n"
                      "ADS-L's physical layer, as the component reads it.", page)

    def test_the_go_pages_and_bravo_are_areas_apart_from_products(self):
        self.assertEqual(behavior_index.area_of("test/products/pages/test_radar.cpp"), "pages")
        self.assertEqual(behavior_index.area_of("test/products/skyblip_bravo/test_fold.cpp"), "bravo")
        self.assertEqual(behavior_index.area_of("test/products/test_pipeline.cpp"), "products")
        self.assertEqual(behavior_index.area_of("test/core/diagnostics/test_tone.cpp"), "core")


class Index(unittest.TestCase):
    def test_the_index_counts_each_area_and_lists_its_main_claims(self):
        index = behavior_index.render_index(cases(), PROSE)
        self.assertIn("5 host test cases over 3 subjects in 3 files", index)
        self.assertIn("| [core - the logic the radio and the screen are built on]"
                      "(behavior/core.md) | 4 | 1 |", index)
        self.assertIn("(behavior/protocols-adsl-conformance.md) | 1 | 0 |", index)
        self.assertIn("**button**\n\n- a stable press emits exactly one event "
                      f"([test_button.cpp:30](../firmware/{BUTTON}#L30))", index)
        self.assertNotIn("a bounce emits nothing", index)

    def test_with_no_main_case_the_index_says_so(self):
        plain = [case._replace(suite="") for case in cases()]
        self.assertIn("No case is marked main yet.", behavior_index.render_index(plain, PROSE))


class Writing(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.output = os.path.join(self.directory.name, "BEHAVIOR.md")
        self.pages = os.path.join(self.directory.name, "behavior")
        os.makedirs(self.pages)

    def tearDown(self):
        self.directory.cleanup()

    def page(self, name, text):
        with open(os.path.join(self.pages, name), "w", encoding="utf-8") as out:
            out.write(text)

    def generate(self):
        return behavior_index.write_docs(f"{MARKER}\n# Behavior\n", {
            "core.md": f"{MARKER}\n# core\n"}, self.output, self.pages)

    def test_a_stale_page_goes_and_an_authored_draft_stays(self):
        self.page("ui.md", f"{MARKER}\n# ui\n")
        self.page("draft.md", "A page someone is writing.")
        self.page("quoting.md", f"# Notes\n\nThe pages open on `{MARKER}`.\n{MARKER}\n"
                                "Generated by `scripts/behavior_index.py` from the run itself.\n")
        self.assertEqual(self.generate(), [])
        self.assertEqual(sorted(os.listdir(self.pages)), ["core.md", "draft.md", "quoting.md"])

    def test_an_authored_file_at_a_generated_path_is_refused_and_kept(self):
        self.page("core.md", "A page someone is writing.")
        self.assertEqual(len(self.generate()), 1)
        with open(os.path.join(self.pages, "core.md"), encoding="utf-8") as kept:
            self.assertEqual(kept.read(), "A page someone is writing.")
        self.assertFalse(os.path.exists(self.output))


class Subjects(unittest.TestCase):
    # adsl_conformance.py renders the stack's own tests through it, so its output must not move.
    def test_render_subjects_keeps_its_output(self):
        self.assertEqual(
            behavior_index.render_subjects(
                ["b: two", "a: one", "b: three"], {"b: two": "a note"}),
            ["**a**\n", "- one", "", "**b**\n", "- three", "- two", "  > a note", ""])


if __name__ == "__main__":
    unittest.main()
