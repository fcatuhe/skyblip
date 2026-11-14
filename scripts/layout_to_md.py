"""Turn a `pdftotext -layout` extraction into markdown, for spec_to_md.py and etsi_to_md.py.

Each page loses its running header and footer, matched only at its top and bottom edges, so a
line of text that reads like them stays. The pages then run on into each other: a line the width
of the page cut goes on over the page break, and a table goes on as one block. Footnotes leave the
foot of their page and close the clause in which that page ends, as paragraphs. The table of
contents is dropped, from its first dotted entry to its last. Wrapped lines are joined into
paragraphs, a word hyphenated over the line break back into one word, and every column-aligned
block is fenced verbatim.
"""
import re

COLUMNS = re.compile(r"\S {2,}\S")
SEPARATOR = re.compile(r"^\s*_{5,}\s*$")
FOOTNOTE = re.compile(r"^\s*\d+(\s|$)")
LEADER = re.compile(r"\.{4,}\s*\d+\s*$")
TOC_TAIL = re.compile(r"^\S.*\s\d+$")
TOC_GAP = 4
SENTENCE_END = re.compile(r"[.:;!?]$")
CONTINUATION = re.compile(r"^[a-z(]")
FORMULA = re.compile(r"[=<>]")
HYPHENATED = re.compile(r"\w-$")
WRAPPED = 0.75


class Footnote:
    def __init__(self, text):
        self.text = text


def convert(text, header, footer, heading):
    """The markdown of a layout extraction.

    `header` and `footer` are patterns a stripped line at a page's top or bottom edge is dropped
    for. `heading(lines, i)` returns the markdown heading line `i` opens, or None, and how many
    lines it used.
    """
    bodies = [page_body(page.split("\n"), header, footer) for page in text.split("\f")]
    width = prose_width(line for body, _ in bodies for line in body if not LEADER.search(line))
    lines = without_repeated_blanks(without_contents(run_on(bodies, width)))
    return Writer(heading, width).write(lines)


def prose_width(lines):
    lengths = sorted(len(line.strip()) for line in lines if line.strip())
    return lengths[int(len(lengths) * 0.95)] if lengths else 0


def run_on(bodies, width):
    """The pages' body lines as one list, each page's footnotes after its lines."""
    lines = []
    for body, footnotes in bodies:
        if not body and not footnotes:
            continue
        if lines and not runs_on(last_line(lines), body[0] if body else "", width):
            lines.append("")
        lines += body
        lines += [Footnote(note) for note in footnotes]
    return lines


def page_body(lines, header, footer):
    lines = [line.rstrip() for line in lines]
    lines = ["" if not line.strip() else line for line in lines]
    top, bottom = edges(lines, header, footer)
    body = lines[top:bottom]
    footnotes = []
    separators = [i for i, line in enumerate(body) if SEPARATOR.match(line)]
    if separators:
        start = separators[-1]
        for line in body[start + 1:]:
            if FOOTNOTE.match(line) or not footnotes:
                footnotes.append([])
            footnotes[-1] += line.split()
        body = body[:start]
    while body and not body[-1].strip():
        body.pop()
    return body, [" ".join(note) for note in footnotes if note]

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



def last_line(lines):
    return next((line for line in reversed(lines) if isinstance(line, str) and line.strip()), "")


def runs_on(last, first, width):
    """Whether a page's last line wraps onto the next page's first: a table goes on over a blank line anyway."""
    if not last.strip() or not first.strip() or any(tabular(line) for line in (last, first)):
        return False
    return wraps(last.strip(), first.strip(), width)


def tabular(line):
    stripped = line.strip()
    return bool(COLUMNS.search(stripped)) or stripped.startswith(("|", "+--")) or len(line) - len(line.lstrip()) >= 8


def without_contents(lines):
    """The lines minus the table of contents: its dotted entries, the lines between them and its undotted tail."""
    text = [line if isinstance(line, str) else "" for line in lines]
    leaders = [i for i, line in enumerate(text) if LEADER.search(line)]
    if not leaders:
        return lines
    first = last = leaders[0]
    for index in leaders[1:]:
        if sum(1 for line in text[last + 1:index] if line.strip()) > TOC_GAP:
            break
        last = index
    while last + 1 < len(text) and TOC_TAIL.match(text[last + 1].strip()):
        last += 1
    kept = [line for line in lines[first:last + 1] if not isinstance(line, str)]
    return lines[:first] + kept + lines[last + 1:]


def without_repeated_blanks(lines):
    out = []
    for line in lines:
        if line == "" and out and out[-1] == "":
            continue
        out.append(line)
    return out


class Writer:
    def __init__(self, heading, width):
        self.heading, self.width = heading, width
        self.out, self.para, self.block, self.footnotes = [], [], [], []

    def write(self, body):
        i = 0
        while i < len(body):
            line = body[i]
            if isinstance(line, Footnote):
                self.footnotes.append(line.text)
                i += 1
                continue
            head, used = self.heading(body, i)
            if head:
                i += used
                self.flush_clause()
                self.out.append(head)
                self.out.append("")
                continue
            i += 1
            self.add(line, next_text(body, i))
        self.flush_clause()
        text = re.sub(r"\n{3,}", "\n\n", "\n".join(self.out))
        return text.strip() + "\n"

    def add(self, line, following):
        stripped = line.strip()
        if not stripped:
            self.flush_para()
            self.block.append("")
            if not any(b.strip() for b in self.block):
                self.block.clear()
            return
        columns = bool(COLUMNS.search(stripped)) or stripped.startswith(("|", "+--"))
        indented = len(line) - len(line.lstrip()) >= 8
        if self.para and wraps(self.para[-1], stripped, self.width) and not (indented and columns):
            self.para.append(stripped)
            return
        if columns or indented:
            self.flush_para()
            self.block.append(line)
            return
        if any(b.strip() for b in self.block):
            if COLUMNS.search(following.strip()) or (self.block[-1].strip() and continues(self.block[-1].strip(), stripped)):
                self.block.append(line)
                return
            self.flush_block()
        else:
            self.block.clear()
        self.para.append(stripped)

    def flush_clause(self):
        self.flush_para()
        self.flush_block()
        for note in self.footnotes:
            self.out.append(note)
            self.out.append("")
        self.footnotes.clear()

    def flush_para(self):
        if self.para:
            text = self.para[0]
            for line in self.para[1:]:
                text += line if HYPHENATED.search(text) and line[0].isalnum() else " " + line
            self.out.append(text)
            self.out.append("")
            self.para.clear()

    def flush_block(self):
        while self.block and not self.block[-1].strip():
            self.block.pop()
        if self.block:
            self.out.append("```")
            indent = min((len(b) - len(b.lstrip()) for b in self.block if b.strip()), default=0)
            self.out.extend(b[indent:] if b.strip() else "" for b in self.block)
            self.out.append("```")
            self.out.append("")
            self.block.clear()


def wraps(previous, line, width):
    """Whether a line goes on with a word the line above hyphenated, or a sentence the width of the page cut."""
    if HYPHENATED.search(previous) and line[0].isalnum():
        return True
    return len(previous) >= WRAPPED * width and continues(previous, line)


def continues(previous, line):
    """Whether a line goes on with the sentence the line above left open."""
    return not SENTENCE_END.search(previous) and bool(CONTINUATION.match(line)) and not FORMULA.search(line)


def next_text(body, i):
    while i < len(body) and not isinstance(body[i], str):
        i += 1
    return body[i] if i < len(body) else ""
