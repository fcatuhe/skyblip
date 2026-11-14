#!/usr/bin/env python3
"""Fail if the EN 300 220 suite and the list of clauses it reads disagree.

    python3 scripts/check_en300220_clauses.py

ETSI does not let the standards be committed (docs/reference/README.md), so CI cannot read their
headings the way check_adsl_clauses.py reads ADS-L's. firmware/test/en300220/clauses.txt stands
in: one line per clause the suite reads, `EN 300 220-<part> <number> <heading>`. Every listed
clause must have a case in firmware/test/en300220/, and every case must name a listed clause,
`EN 300 220-1 5.21.1: what holds`. A skipped case opens its message on one of the ADS-L suite's
kinds. When scripts/fetch_etsi.py has fetched the standards into docs/reference/, each listed
heading is also checked against the document, so a typo in the list cannot stand in for a clause.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from check_adsl_clauses import KINDS, kind_of, skip_messages  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, "firmware", "test", "en300220")
LISTING = os.path.join(SUITE, "clauses.txt")
DOCUMENTS = {
    "EN 300 220-1": os.path.join(ROOT, "docs", "reference", "etsi_en_300_220-1_v3.1.1.md"),
    "EN 300 220-2": os.path.join(ROOT, "docs", "reference", "etsi_en_300_220-2_v3.3.1.md"),
}

CLAUSE = r"EN 300 220-[12] \d+(?:\.\d+)+"
LISTED = re.compile(rf"^({CLAUSE}) (\S.*?)\s*$")
SUBJECT = re.compile(rf'TEST_CASE\(\s*(?:"[^"]*"\s*)*?"({CLAUSE}):')
NAMED = re.compile(r'TEST_CASE\(\s*\n?\s*"([^"]*)')


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


def suite_clauses(suite=SUITE):
    covered, named, skipped = set(), [], []
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


def unheaded(clauses, documents=DOCUMENTS):
    """The listed clauses a fetched standard has no such heading for, and the parts not fetched."""
    wrong, absent = [], []
    texts = {}
    for part, path in documents.items():
        if os.path.exists(path):
            with open(path, encoding="utf-8") as document:
                texts[part] = document.read()
        else:
            absent.append(part)
    for clause, heading in sorted(clauses.items()):
        part, number = clause.rsplit(" ", 1)
        if part not in texts:
            continue
        line = re.compile(rf"^(?:#+\s+)?{re.escape(number)}\s+{re.escape(heading)}\s*$", re.M)
        if not line.search(texts[part]):
            wrong.append(f"{clause}: {part} has no heading {number} {heading}")
    return wrong, absent


def main(listing=LISTING, suite=SUITE, documents=DOCUMENTS):
    clauses, malformed = listed(listing)
    covered, named, skipped = suite_clauses(suite)
    wrong, absent = unheaded(clauses, documents)

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
    report(f"OK: all {len(clauses)} listed clauses of EN 300 220 are read by the suite, "
           f"{len(skipped)} of them skipped.")
    for part in absent:
        report(f"{part} is not fetched, so its headings were not checked (scripts/fetch_etsi.py).")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
