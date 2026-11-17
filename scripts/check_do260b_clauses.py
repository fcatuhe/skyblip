#!/usr/bin/env python3
"""Fail if the DO-260B suite and the list of clauses it reads disagree.

    python3 scripts/check_do260b_clauses.py

RTCA does not let DO-260B be committed (docs/reference/README.md), so CI cannot read its
headings the way check_adsl_clauses.py reads ADS-L's. firmware/test/adsb/clauses.txt stands in:
one line per clause the suite reads, `DO-260B <number> <heading>`, every receive-side heading of
the draft and none of the transmitter's. Every listed clause must have a case in one of the two
halves, firmware/protocols/adsb/test/conformance/ and firmware/test/adsb/, and every case must
name a listed clause, `DO-260B 2.2.10.3.1: what holds`. A skipped case opens its message on one
of the ADS-L suite's kinds. When scripts/fetch_rtca.py has fetched the draft into docs/reference/,
each listed heading is also checked against it, so a typo in the list cannot stand in for a clause.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from check_adsl_clauses import KINDS, kind_of, skip_messages  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITES = (
    os.path.join(ROOT, "firmware", "protocols", "adsb", "test", "conformance"),
    os.path.join(ROOT, "firmware", "test", "adsb"),
)
LISTING = os.path.join(ROOT, "firmware", "test", "adsb", "clauses.txt")
DOCUMENT = os.path.join(ROOT, "docs", "reference", "rtca_do-260b_draft_v4.2.txt")

CLAUSE = r"DO-260B (?:\d+|A)(?:\.\d+)+"
LISTED = re.compile(rf"^({CLAUSE}) (\S.*?)\s*$")
SUBJECT = re.compile(rf'TEST_CASE\(\s*(?:"[^"]*"\s*)*?"({CLAUSE}):')
NAMED = re.compile(r'TEST_CASE\(\s*\n?\s*"([^"]*)')
PLAIN = str.maketrans({"\u201c": '"', "\u201d": '"', "\u2015": '"', "\u2016": '"', "\u2019": "'",
                       "\u2013": "-", "\u2014": "-"})
RUNNING_HEADER = re.compile(r"^.*Working Paper 1090-WP30-18.*$", re.M)


def listed(path=LISTING):
    clauses, malformed = {}, []
    with open(path, encoding="utf-8") as listing:
        for number, line in enumerate(listing, 1):
            if not line.strip() or line.startswith("#"):
                continue
            match = LISTED.match(line)
            if match:
                clauses[match.group(1)] = match.group(2)
            else:
                malformed.append(f"{os.path.relpath(path, ROOT)}:{number}: {line.rstrip()}")
    return clauses, malformed


def suite_clauses(suites=SUITES):
    covered, named, skipped = set(), [], []
    for suite in suites:
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


def plain(text):
    return " ".join(RUNNING_HEADER.sub("", text.translate(PLAIN)).split())


def unheaded(clauses, document=DOCUMENT):
    """The listed clauses the fetched draft has no such heading for, and whether it was fetched."""
    if not os.path.exists(document):
        return [], False
    with open(document, encoding="utf-8") as source:
        text = " " + plain(source.read())
    wrong = []
    for clause, heading in sorted(clauses.items()):
        number = clause.split(" ", 1)[1]
        if f" {number} {plain(heading)}" not in text:
            wrong.append(f"{clause}: the draft has no heading {number} {heading}")
    return wrong, True


def main(listing=LISTING, suites=SUITES, document=DOCUMENT):
    clauses, malformed = listed(listing)
    covered, named, skipped = suite_clauses(suites)
    wrong, fetched = unheaded(clauses, document)

    missing = sorted(set(clauses) - covered)
    unknown = sorted(covered - set(clauses))
    stray = [(path, case) for path, case in named if not re.match(rf"{CLAUSE}: ", case)]
    kindless = [(path, message) for path, message in skipped if kind_of(message) is None]

    for line in malformed:
        report(f"not a clause line: {line}")
    for clause in missing:
        report(f"no case reads {clause}")
    for clause in unknown:
        report(f"{clause} is not in {os.path.relpath(listing, ROOT)}")
    for path, case in stray:
        report(f"a case in {path} is not named after a clause: {case}")
    for path, message in kindless:
        report(f"a skipped case in {path} opens on none of {', '.join(KINDS)}: {message!r}")
    for problem in wrong:
        report(problem)

    if malformed or missing or unknown or stray or kindless or wrong:
        return 1
    counts = ", ".join(f"{sum(kind_of(m) == kind for _, m in skipped)} {kind}" for kind in KINDS)
    report(f"OK: all {len(clauses)} listed clauses of DO-260B are read by the suite.")
    report(f"{len(skipped)} cases are skipped: {counts}.")
    if not fetched:
        report("The draft is not fetched, so its headings were not checked (scripts/fetch_rtca.py).")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
