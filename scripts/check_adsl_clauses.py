#!/usr/bin/env python3
"""Fail if a clause of ADS-L 4 SRD-860 issue 2 has no case in the conformance suite.

The clause list is the specification's own: every `ADS-L.4.SRD860.x.y` heading in
docs/reference/ads-l_4_srd860_issue_2.md. The suite's list is the subject of every
TEST_CASE in its two halves, which reads `ADS-L.4.SRD860.x.y: what holds`: the
component's cases in firmware/protocols/adsl/test/conformance/, and the device's in
firmware/test/adsl/.

The two must match exactly. A clause the firmware does not implement still gets a
case, written as the clause requires and marked doctest::skip() over a TODO: the
gap is then stated in the suite rather than missing from it. A subject that names
no clause is a typo, and a clause with no case is a hole in the reading.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "docs", "reference", "ads-l_4_srd860_issue_2.md")
SUITES = (
    os.path.join(ROOT, "firmware", "protocols", "adsl", "test", "conformance"),
    os.path.join(ROOT, "firmware", "test", "adsl"),
)

CLAUSE = r"ADS-L\.4\.SRD860\.[A-Z](?:\.\d+)+"
HEADING = re.compile(rf"^#+\s+({CLAUSE})\b", re.M)
SUBJECT = re.compile(rf'TEST_CASE\(\s*(?:"[^"]*"\s*)*?"({CLAUSE}|ADS-L\.4\.SRD860\.APPENDIX):')
NAMED = re.compile(r'TEST_CASE\(\s*\n?\s*"([^"]*)')

APPENDIX = "ADS-L.4.SRD860.APPENDIX"


def spec_clauses():
    with open(SPEC, encoding="utf-8") as spec:
        return {match.group(1) for match in HEADING.finditer(spec.read())}


def suite_clauses():
    covered, named = set(), []
    for suite in SUITES:
        for name in sorted(os.listdir(suite)):
            if not name.endswith(".cpp"):
                continue
            path = os.path.join(suite, name)
            with open(path, encoding="utf-8") as source:
                text = source.read()
            covered |= {match.group(1) for match in SUBJECT.finditer(text)}
            named += [(os.path.relpath(path, ROOT), case) for case in NAMED.findall(text)]
    return covered, named


def main():
    clauses = spec_clauses()
    covered, named = suite_clauses()

    missing = sorted(clauses - covered)
    unknown = sorted(covered - clauses - {APPENDIX})
    stray = [(path, case) for path, case in named if not case.startswith("ADS-L.4.SRD860.")]

    for clause in missing:
        report(f"no case reads {clause}")
    for clause in unknown:
        report(f"{clause} is not a clause of issue 2")
    for path, case in stray:
        report(f"a case in {path} is not named after a clause: {case}")

    if missing or unknown or stray:
        return 1
    report(f"OK: all {len(clauses)} clauses of ADS-L 4 SRD-860 issue 2 are read by the suite.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
