#!/usr/bin/env python3
"""Fail if a markdown conversion in docs/reference/ lost, added, cut or misplaced text of its PDF.

    python3 scripts/check_spec_markdown.py

Each `.md` beside a reference PDF is generated from `pdftotext -layout` (docs/reference/README.md),
and the firmware and its conformance suites are written against the markdown. This check reads
the PDF again and holds the markdown to it:

- Every page of `pdftotext -raw`, the PDF's own reading order, must hold the same characters as
  the same page of `pdftotext -layout`, so the layout extraction lost nothing.
- The layout extraction and the markdown must hold the same words in the same order. Every run
  of words in one and not the other is reported with the markdown clause it falls in.
- A line that wraps onto the next, on the same page or over a page break, must stay in one
  markdown block, and so must a table that runs on over a page break.
- A footnote must appear whole, as a paragraph closing the clause in which its page ends. It is
  the one thing allowed to move.

Before comparing, the check takes out what is not text: the running header and the footer found
at the top and bottom edge of each page, the footnote separator, and the table of contents. On
the markdown side it ignores heading marks and code fences. On both, it reads every dash as `-`,
and joins a word hyphenated over a line break, as `ADS-` and `L.4.SRD860.E.3`, into one.

The two ADS-L issues are committed and always checked. The ETSI standards are not committed, so
they are checked only once scripts/fetch_etsi.py has put them here, and skipped otherwise.
Needs pdftotext (poppler-utils).
"""
import collections
import difflib
import os
import re
import subprocess
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFERENCE = os.path.join(ROOT, "docs", "reference")
COMMITTED = ("ads-l_4_srd860_issue_1", "ads-l_4_srd860_issue_2")
FETCHED = ("etsi_en_300_220-1_v3.1.1", "etsi_en_300_220-2_v3.3.1")

HEADERS = (
    re.compile(r"^ADS-L 4 S[DR]{2}-860 Issue \d+\s"),
    re.compile(r"^ED Decision 2022/024/R$"),
    re.compile(r"^(\d+\s+)?ETSI EN \d+ \d+(-\d+)? V\d+\.\d+\.\d+ \(\d{4}-\d{2}\)(\s+\d+)?$"),
)
FOOTERS = (
    re.compile(r"^(ED Decision 2022/024/R\s+)?Page \d+ of \d+$"),
    re.compile(r"^ED Decision 2022/024/R$"),
    re.compile(r"^ETSI$"),
)
SEPARATOR = re.compile(r"^_{5,}$")
FOOTNOTE = re.compile(r"^\d+(\s|$)")
LEADER = re.compile(r"\.{4,}\s*\d+$")
TOC_TAIL = re.compile(r"^\S.*\s\d+$")
TOC_GAP = 4
HYPHENATED = re.compile(r"\w-$")
SENTENCE_END = re.compile(r"[.:;!?]$")
CONTINUES = re.compile(r"^[a-z(]")
FORMULA = re.compile(r"[=<>]")
COLUMNS = re.compile(r"\S {2,}\S")
BULLET = re.compile(r"^[\u2022\uf0b7-]\s+")
WRAPPED = 0.75
HEADING = re.compile(r"^#+\s+(.*)$")
FENCE = re.compile(r"^```")
DASHES = str.maketrans({"\u2013": "-", "\u2014": "-", "\u2212": "-"})


def main():
    failed = False
    for name in COMMITTED + FETCHED:
        pdf = os.path.join(REFERENCE, name + ".pdf")
        md = os.path.join(REFERENCE, name + ".md")
        if name in FETCHED and not (os.path.exists(pdf) and os.path.exists(md)):
            report(f"{name}: not fetched, skipped (python3 scripts/fetch_etsi.py fetches it)")
            continue
        with open(md, encoding="utf-8") as source:
            markdown = source.read()
        findings = check(pdftotext(pdf, "-layout"), pdftotext(pdf, "-raw"), markdown)
        for finding in findings:
            report(f"{name}: {finding}")
        report(f"{name}: {len(findings)} differences")
        failed = failed or bool(findings)
    return 1 if failed else 0


def pdftotext(pdf, *options):
    return subprocess.run(["pdftotext", *options, pdf, "-"], check=True, capture_output=True, text=True).stdout


def check(layout, raw, markdown):
    """Every difference between a PDF's two extractions and its markdown, as one line each."""
    pdf = Pdf(layout)
    md = Markdown(markdown)
    findings = reading_findings(layout, raw)
    findings += footnote_findings(pdf, md)
    findings += word_findings(pdf, md)
    findings += cut_findings(pdf, md)
    return findings


class Pdf:
    """The words of a `pdftotext -layout` extraction, without page furniture and contents."""

    def __init__(self, text):
        self.footnotes = []
        lines = []
        for page, content in enumerate(text.split("\f"), 1):
            body, footnotes = split_page(content.split("\n"))
            self.footnotes += [(page, note) for note in footnotes]
            lines += [(page, line) for line in body]
        toc = contents(lines)
        self.lines = [line for index, line in enumerate(lines) if index not in toc]
        self.words, self.pages, self.first_word = [], [], []
        joinable = False
        for page, line in self.lines:
            self.first_word.append(len(self.words))
            for number, word in enumerate(words(line)):
                if number == 0 and joinable and word[0].isalnum():
                    self.words[-1] += word
                else:
                    self.words.append(word)
                    self.pages.append(page)
            joinable = bool(line.strip()) and bool(HYPHENATED.search(self.words[-1]))
        self.width = prose_width([line for _, line in self.lines])

    def adjacent_lines(self):
        """Each pair of lines that touch: one under the other, or the last and first of two pages."""
        last = None
        for index, (_, line) in enumerate(self.lines):
            if not line.strip():
                last = None
                continue
            if last is not None:
                yield last, index
            last = index


def split_page(lines):
    """The body lines of a page, edges stripped, and the footnotes under its separator."""
    lines = [line.rstrip() for line in lines]
    top, bottom = edges(lines, HEADERS, FOOTERS)
    body = lines[top:bottom]
    footnotes = []
    separators = [i for i, line in enumerate(body) if SEPARATOR.match(line.strip())]
    if separators:
        start = separators[-1]
        for line in body[start + 1:]:
            if FOOTNOTE.match(line.strip()) or not footnotes:
                footnotes.append([])
            footnotes[-1] += words(line)
        body = body[:start]
    while body and not body[-1].strip():
        body.pop()
    return body, [note for note in footnotes if note]

def edges(lines, header, footer):
    """Where a page's body starts and ends: past its blank lines and one header line at the top, one footer line at the bottom."""
    top, bottom = 0, len(lines)
    top = past_blanks(lines, top, bottom, 1)
    if top < bottom and any(p.match(lines[top].strip()) for p in header):
        top = past_blanks(lines, top + 1, bottom, 1)
    bottom = past_blanks(lines, bottom - 1, top - 1, -1) + 1
    if bottom > top and any(p.match(lines[bottom - 1].strip()) for p in footer):
        bottom = past_blanks(lines, bottom - 2, top - 1, -1) + 1
    return top, bottom


def past_blanks(lines, index, stop, step):
    while index != stop and not lines[index].strip():
        index += step
    return index



def contents(lines):
    """Indexes of the lines that make up the table of contents: its dotted entries, the lines between them and its undotted tail."""
    leaders = [i for i, (_, line) in enumerate(lines) if LEADER.search(line)]
    if not leaders:
        return set()
    first = last = leaders[0]
    for index in leaders[1:]:
        if sum(1 for _, line in lines[last + 1:index] if line.strip()) > TOC_GAP:
            break
        last = index
    while last + 1 < len(lines) and TOC_TAIL.match(lines[last + 1][1].strip()):
        last += 1
    return set(range(first, last + 1))


def prose_width(lines):
    lengths = sorted(len(line.strip()) for line in lines if line.strip())
    return lengths[int(len(lengths) * 0.95)] if lengths else 0


class Markdown:
    """The words of a markdown conversion, each with the heading it falls under, cut in blocks."""

    def __init__(self, text):
        self.words, self.clauses, self.blocks = [], [], []
        self.footnotes = set()
        self.diff = None
        self.joinable = False
        clause = "(before the first heading)"
        open_block = None
        for line in text.split("\n"):
            fence = FENCE.match(line)
            if open_block == "fence":
                if fence:
                    open_block = None
                else:
                    self.add(line, clause, joins=True)
                continue
            heading = HEADING.match(line)
            if heading or fence or not line.strip():
                open_block = None
            if fence:
                open_block = "fence"
                self.blocks.append(["fence", len(self.words), len(self.words)])
                continue
            if heading:
                clause = heading.group(1).strip()
                line = clause
                self.blocks.append(["heading", len(self.words), len(self.words)])
            elif line.strip() and open_block is None:
                open_block = "paragraph"
                self.blocks.append(["paragraph", len(self.words), len(self.words)])
            self.add(line, clause, joins=False)

    def add(self, line, clause, joins):
        """Append a line's words. In a fence, a word hyphenated over the line break joins its end."""
        for number, word in enumerate(words(line)):
            if joins and number == 0 and self.joinable and word[0].isalnum():
                self.words[-1] += word
            else:
                self.words.append(word)
                self.clauses.append(clause)
        self.joinable = joins and bool(line.strip()) and bool(self.words) and bool(HYPHENATED.search(self.words[-1]))
        if self.blocks:
            self.blocks[-1][2] = len(self.words)

    def paragraphs(self):
        return [(start, end) for kind, start, end in self.blocks if kind == "paragraph"]

    def closes_clause(self, span):
        """Whether only footnotes follow this paragraph before the next heading."""
        for kind, start, end in self.blocks:
            if start < span[1] or end == start:
                continue
            if kind == "heading":
                return True
            if (start, end) not in self.footnotes:
                return False
        return True

    def block_of(self):
        owner = [None] * len(self.words)
        for block, (_, start, end) in enumerate(self.blocks):
            for index in range(start, end):
                owner[index] = block
        return owner

    def clause_at(self, index):
        if not self.clauses:
            return "(empty)"
        return self.clauses[min(max(index, 0), len(self.clauses) - 1)]


def words(text):
    return text.translate(DASHES).split()


def reading_findings(layout, raw):
    """Characters the raw reading order finds on a page and the layout extraction does not, or the reverse."""
    layout_pages, raw_pages = layout.split("\f"), raw.split("\f")
    findings = []
    if len(layout_pages) != len(raw_pages):
        findings.append(f"the layout extraction has {len(layout_pages)} pages, the raw one {len(raw_pages)}")
    for page, (laid, read) in enumerate(zip(layout_pages, raw_pages), 1):
        laid_chars, read_chars = characters(laid), characters(read)
        if laid_chars != read_chars:
            lost = "".join(sorted((read_chars - laid_chars).elements()))
            extra = "".join(sorted((laid_chars - read_chars).elements()))
            findings.append(f"page {page} reads differently in layout and raw order: layout lacks {lost!r} and has {extra!r} more")
    return findings


def characters(text):
    return collections.Counter(c for c in text if not c.isspace() and unicodedata.category(c) != "Cf")


def footnote_findings(pdf, md):
    """Find each PDF footnote as a paragraph of the markdown, closing the clause its page ends in."""
    findings = []
    placed = []
    for page, note in pdf.footnotes:
        span = next((s for s in md.paragraphs() if md.words[s[0]:s[1]] == note and s not in md.footnotes), None)
        if span is None:
            found = find(md.words, note)
            where = f"inside a block of {md.clause_at(found)}" if found is not None else "missing"
            findings.append(f"footnote of page {page} {where}: {quote(note)}")
            continue
        md.footnotes.add(span)
        placed.append((page, note, span))
    aligned = alignment(pdf, md) if placed else {}
    for page, note, span in placed:
        if not md.closes_clause(span):
            findings.append(f"footnote of page {page} does not close {md.clause_at(span[0])}: {quote(note)}")
            continue
        page_end = page_end_clause(pdf, md, aligned, page)
        if page_end is not None and md.clause_at(span[0]) != page_end:
            findings.append(f"footnote of page {page} closes {md.clause_at(span[0])}, not {page_end} where its page ends: {quote(note)}")
    return findings


def page_end_clause(pdf, md, aligned, page):
    last = max((i for i, p in enumerate(pdf.pages) if p <= page), default=-1)
    while last >= 0 and last not in aligned:
        last -= 1
    return md.clause_at(aligned[last]) if last >= 0 else None


def word_findings(pdf, md):
    body, positions, opcodes = diff(pdf, md)
    findings = []
    for tag, i1, i2, j1, j2 in opcodes:
        if tag == "equal":
            continue
        at = positions[j1] if j1 < len(positions) else len(md.words)
        clause = md.clause_at(at - 1 if tag == "delete" else at)
        if i2 > i1:
            findings.append(f"missing from {clause} (page {pdf.pages[i1]}): {quote(pdf.words[i1:i2])}")
        if j2 > j1:
            findings.append(f"added to {clause}: {quote(body[j1:j2])}")
    return findings


def cut_findings(pdf, md):
    """A line that wraps, or a table that runs on over a page break, must stay one markdown block."""
    aligned = alignment(pdf, md)
    block_of = md.block_of()
    findings = []
    for upper, lower in pdf.adjacent_lines():
        start = pdf.first_word[lower]
        end = start - 1
        before, after = aligned.get(end), aligned.get(start)
        if before is None or after is None or block_of[before] == block_of[after]:
            continue
        kinds = (md.blocks[block_of[before]][0], md.blocks[block_of[after]][0])
        new_page = pdf.lines[upper][0] != pdf.lines[lower][0]
        if kinds == ("fence", "fence") and new_page:
            what = "table"
        elif "heading" not in kinds and wraps(pdf, pdf.lines[upper][1], pdf.lines[lower][1]):
            what = "sentence"
        else:
            continue
        findings.append(f"{what} cut in {md.clause_at(before)} (page {pdf.lines[upper][0]}): "
                        f"{quote(pdf.words[max(0, end - 5):end + 1])} / {quote(pdf.words[start:start + 6])}")
    return findings


def wraps(pdf, upper, lower):
    """Whether a line of prose runs on into the next: it fills the width, ends mid-sentence, and the next goes on in lower case."""
    upper, lower = BULLET.sub("", upper.strip()), lower.strip()
    return (len(upper) >= WRAPPED * pdf.width and not COLUMNS.search(upper) and not SENTENCE_END.search(upper)
            and bool(CONTINUES.match(lower)) and not FORMULA.search(lower))


def alignment(pdf, md):
    """Map each PDF word index to the markdown word it matched, in the word diff."""
    _, positions, opcodes = diff(pdf, md)
    aligned = {}
    for tag, i1, i2, j1, j2 in opcodes:
        if tag == "equal":
            for offset in range(i2 - i1):
                aligned[i1 + offset] = positions[j1 + offset]
    return aligned


def diff(pdf, md):
    """The word diff of the PDF and the markdown's body, footnotes taken out of both."""
    if md.diff is None:
        skipped = set()
        for start, end in md.footnotes:
            skipped |= set(range(start, end))
        positions = [i for i in range(len(md.words)) if i not in skipped]
        body = [md.words[i] for i in positions]
        md.diff = body, positions, difflib.SequenceMatcher(None, pdf.words, body, autojunk=False).get_opcodes()
    return md.diff


def find(haystack, needle):
    for start in range(len(haystack) - len(needle) + 1):
        if haystack[start:start + len(needle)] == needle:
            return start
    return None


def quote(sequence):
    text = " ".join(sequence)
    return '"' + (text if len(text) <= 300 else text[:297] + "...") + '"'


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
