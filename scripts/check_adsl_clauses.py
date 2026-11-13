#!/usr/bin/env python3
"""Fail if a clause of ADS-L 4 SRD-860 issue 2 has no case in the conformance suite.

The clause list is the specification's own: every `ADS-L.4.SRD860.x.y` heading in
docs/reference/ads-l_4_srd860_issue_2.md. The suite's list is the subject of every
TEST_CASE in its two halves, which reads `ADS-L.4.SRD860.x.y: what holds`: the
component's cases in firmware/protocols/adsl/test/conformance/, and the device's in
firmware/test/adsl/.

The two must match exactly. A clause the firmware does not implement still gets a
case, written as the clause requires and marked doctest::skip(): the gap is then
stated in the suite rather than missing from it. A subject that names no clause is
a typo, and a clause with no case is a hole in the reading.

A skipped case fails with a message that opens on its kind: "not implemented:" for
a clause the firmware should meet and does not, "prose:" for a clause with nothing
to execute, "paperwork:" for a manufacturer's duty, and "optional:" for a clause the
specification lets a device leave out. A skip of no kind is refused.
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

KINDS = ("not implemented", "prose", "paperwork", "optional")
SKIPPED = re.compile(r"doctest::skip\(\)\)\s*\{(.*?)\n\}", re.S)
FAILED = re.compile(r'FAIL\(\s*((?:"[^"]*"\s*)+)\)', re.S)


def spec_clauses():
    with open(SPEC, encoding="utf-8") as spec:
        return {match.group(1) for match in HEADING.finditer(spec.read())}


def skip_messages(text):
    messages = []
    for body in SKIPPED.finditer(text):
        failed = FAILED.search(body.group(1))
        messages.append("".join(re.findall(r'"([^"]*)"', failed.group(1))) if failed else "")
    return messages


def kind_of(message):
    kind = message.split(":", 1)[0]
    return kind if ":" in message and kind in KINDS else None


def suite_clauses():
    covered, named, skipped = set(), [], []
    for suite in SUITES:
        for name in sorted(os.listdir(suite)):
            if not name.endswith(".cpp"):
                continue
            path = os.path.join(suite, name)
            with open(path, encoding="utf-8") as source:
                text = source.read()
            covered |= {match.group(1) for match in SUBJECT.finditer(text)}
            named += [(os.path.relpath(path, ROOT), case) for case in NAMED.findall(text)]
            skipped += [(os.path.relpath(path, ROOT), message) for message in skip_messages(text)]
    return covered, named, skipped


def main():
    clauses = spec_clauses()
    covered, named, skipped = suite_clauses()

    missing = sorted(clauses - covered)
    unknown = sorted(covered - clauses - {APPENDIX})
    stray = [(path, case) for path, case in named if not case.startswith("ADS-L.4.SRD860.")]
    kindless = [(path, message) for path, message in skipped if kind_of(message) is None]

    for clause in missing:
        report(f"no case reads {clause}")
    for clause in unknown:
        report(f"{clause} is not a clause of issue 2")
    for path, case in stray:
        report(f"a case in {path} is not named after a clause: {case}")
    for path, message in kindless:
        report(f"a skipped case in {path} opens on none of {', '.join(KINDS)}: {message!r}")

    if missing or unknown or stray or kindless:
        return 1
    counts = ", ".join(f"{sum(kind_of(m) == kind for _, m in skipped)} {kind}" for kind in KINDS)
    report(f"OK: all {len(clauses)} clauses of ADS-L 4 SRD-860 issue 2 are read by the suite.")
    report(f"{len(skipped)} cases are skipped: {counts}.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
