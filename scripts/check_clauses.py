#!/usr/bin/env python3
"""Fail if a conformance suite and the clauses of its standard disagree.

    python3 scripts/check_clauses.py

STANDARDS holds one entry per suite. ADS-L's clauses are the `ADS-L.4.SRD860.x.y` headings of the
committed specification. ETSI and RTCA do not let EN 300 220 and DO-260B be committed
(docs/reference/README.md), so a `clauses.txt` beside the suite stands in, one line per clause,
`<clause> <heading>`, and each heading is checked against the fetched document when it is present.

Every clause must have a case, every case is named `<clause>: what holds`, and a clause the firmware
does not meet still gets one, marked doctest::skip(). A skipped case fails with a message that opens
on its kind: "not implemented:" for a clause the firmware should meet and does not, "prose:" for
nothing to execute, "paperwork:" for a manufacturer's duty, "optional:" for a clause a device may
leave out, and "unmeasured:" for a radio figure no bench has measured yet.
"""
import dataclasses
import functools
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFERENCE = os.path.join(ROOT, "docs", "reference")
FIRMWARE = os.path.join(ROOT, "firmware")

KINDS = ("not implemented", "prose", "paperwork", "optional", "unmeasured")
SKIPPED = re.compile(r"doctest::skip\(\)\)\s*\{(.*?)\n\}", re.S)
FAILED = re.compile(r'FAIL\(\s*((?:"[^"]*"\s*)+)\)', re.S)
NAMED = re.compile(r'TEST_CASE\(\s*\n?\s*"([^"]*)')
PLAIN = str.maketrans({"\u201c": '"', "\u201d": '"', "\u2015": '"', "\u2016": '"', "\u2019": "'",
                       "\u2013": "-", "\u2014": "-"})
RUNNING_HEADER = re.compile(r"^.*Working Paper 1090-WP30-18.*$", re.M)


def markdown_heading(text, number, heading):
    return re.search(rf"^(?:#+\s+)?{re.escape(number)}\s+{re.escape(heading)}\s*$", text, re.M)


def draft_heading(text, number, heading):
    return f" {number} {plain(heading)}" in flattened(text)


@dataclasses.dataclass
class Standard:
    title: str
    clause: str
    suites: tuple
    spec: str = None
    listing: str = None
    documents: dict = dataclasses.field(default_factory=dict)
    has_heading: object = None
    fetcher: str = None
    unnumbered: tuple = ()


STANDARDS = (
    Standard(
        title="ADS-L 4 SRD-860 issue 2",
        clause=r"ADS-L\.4\.SRD860\.[A-Z](?:\.\d+)+",
        suites=(os.path.join(FIRMWARE, "protocols", "adsl", "test", "conformance"),
                os.path.join(FIRMWARE, "test", "adsl")),
        spec=os.path.join(REFERENCE, "ads-l_4_srd860_issue_2.md"),
        unnumbered=("ADS-L.4.SRD860.APPENDIX",),
    ),
    Standard(
        title="EN 300 220",
        clause=r"EN 300 220-[12] \d+(?:\.\d+)+",
        suites=(os.path.join(FIRMWARE, "test", "en300220"),),
        listing=os.path.join(FIRMWARE, "test", "en300220", "clauses.txt"),
        documents={"EN 300 220-1": os.path.join(REFERENCE, "etsi_en_300_220-1_v3.1.1.md"),
                   "EN 300 220-2": os.path.join(REFERENCE, "etsi_en_300_220-2_v3.3.1.md")},
        has_heading=markdown_heading,
        fetcher="scripts/fetch_etsi.py",
    ),
    Standard(
        title="DO-260B",
        clause=r"DO-260B (?:\d+|A)(?:\.\d+)+",
        suites=(os.path.join(FIRMWARE, "protocols", "adsb", "test", "conformance"),
                os.path.join(FIRMWARE, "test", "adsb")),
        listing=os.path.join(FIRMWARE, "test", "adsb", "clauses.txt"),
        documents={"DO-260B": os.path.join(REFERENCE, "rtca_do-260b_draft_v4.2.txt")},
        has_heading=draft_heading,
        fetcher="scripts/fetch_rtca.py",
    ),
)


def main():
    return max(check(standard) for standard in STANDARDS)


def check(standard):
    clauses, malformed = listed(standard)
    covered, named, skipped = suite_clauses(standard)
    wrong, absent = unheaded(standard, clauses)
    subject = subject_pattern(standard)

    missing = sorted(set(clauses) - covered)
    unknown = sorted(covered - set(clauses) - set(standard.unnumbered))
    stray = [(path, case) for path, case in named if not re.match(rf"{subject}: ", case)]
    kindless = [(path, message) for path, message in skipped if kind_of(message) is None]

    for line in malformed:
        report(f"not a clause line: {line}")
    for clause in missing:
        report(f"no case reads {clause}")
    for clause in unknown:
        report(f"{clause} is not in {os.path.relpath(standard.spec or standard.listing, ROOT)}")
    for path, case in stray:
        report(f"a case in {path} is not named after a clause: {case}")
    for path, message in kindless:
        report(f"a skipped case in {path} opens on none of {', '.join(KINDS)}: {message!r}")
    for problem in wrong:
        report(problem)

    if malformed or missing or unknown or stray or kindless or wrong:
        return 1
    counts = ", ".join(f"{sum(kind_of(m) == kind for _, m in skipped)} {kind}" for kind in KINDS)
    report(f"OK: all {len(clauses)} clauses of {standard.title} are read by the suite.")
    report(f"{len(skipped)} cases are skipped: {counts}.")
    for part in absent:
        report(f"{part} is not fetched, so its headings were not checked ({standard.fetcher}).")
    return 0


def subject_pattern(standard):
    return "|".join([standard.clause] + [re.escape(name) for name in standard.unnumbered])


def listed(standard):
    if standard.spec:
        with open(standard.spec, encoding="utf-8") as spec:
            heading = re.compile(rf"^#+\s+({standard.clause})\b", re.M)
            return {match.group(1): None for match in heading.finditer(spec.read())}, []
    clauses, malformed = {}, []
    line_of = re.compile(rf"^({standard.clause}) (\S.*?)\s*$")
    with open(standard.listing, encoding="utf-8") as listing:
        for number, line in enumerate(listing, 1):
            if not line.strip() or line.startswith("#"):
                continue
            match = line_of.match(line)
            if match:
                clauses[match.group(1)] = match.group(2)
            else:
                malformed.append(f"{os.path.relpath(standard.listing, ROOT)}:{number}: {line.rstrip()}")
    return clauses, malformed


def suite_clauses(standard):
    subject = re.compile(rf'TEST_CASE\(\s*(?:"[^"]*"\s*)*?"({subject_pattern(standard)}):')
    covered, named, skipped = set(), [], []
    for suite in standard.suites:
        for name in sorted(os.listdir(suite)):
            if not name.endswith(".cpp"):
                continue
            path = os.path.join(suite, name)
            with open(path, encoding="utf-8") as source:
                text = source.read()
            covered |= {match.group(1) for match in subject.finditer(text)}
            named += [(os.path.relpath(path, ROOT), case) for case in NAMED.findall(text)]
            skipped += [(os.path.relpath(path, ROOT), message) for message in skip_messages(text)]
    return covered, named, skipped


def unheaded(standard, clauses):
    """The listed clauses a fetched document has no such heading for, and the parts not fetched."""
    texts, absent = {}, []
    for part, path in standard.documents.items():
        if os.path.exists(path):
            with open(path, encoding="utf-8") as document:
                texts[part] = document.read()
        else:
            absent.append(part)
    wrong = []
    for clause, heading in sorted(clauses.items()):
        part, _, number = clause.rpartition(" ")
        if part in texts and not standard.has_heading(texts[part], number, heading):
            wrong.append(f"{clause}: {part} has no heading {number} {heading}")
    return wrong, absent


def skip_messages(text):
    messages = []
    for body in SKIPPED.finditer(text):
        failed = FAILED.search(body.group(1))
        messages.append("".join(re.findall(r'"([^"]*)"', failed.group(1))) if failed else "")
    return messages


def kind_of(message):
    kind = message.split(":", 1)[0]
    return kind if ":" in message and kind in KINDS else None


def plain(text):
    return " ".join(RUNNING_HEADER.sub("", text.translate(PLAIN)).split())


@functools.lru_cache(maxsize=1)
def flattened(text):
    return " " + plain(text)


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
