import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layout_to_md import COLUMNS, convert  # noqa: E402

HEADER = (re.compile(r"^(\d+\s+)?ETSI EN \d+ \d+(-\d+)? V\d+\.\d+\.\d+ \(\d{4}-\d{2}\)(\s+\d+)?$"),)
FOOTER = (re.compile(r"^ETSI$"),)
CLAUSE = re.compile(r"^ {0,2}(\d+(?:\.\d+)*|[A-Z](?:\.\d+)+)\s{1,}([A-Z(].{0,90}?)\s*$")
ANNEX = re.compile(r"^ {0,2}(Annex [A-Z]) \((normative|informative)\):\s*(.*?)\s*$")
PLAIN_HEAD = re.compile(r"^(Intellectual Property Rights|Foreword|Modal verbs terminology|Introduction|History)\s*$")


def heading(body, i):
    line = body[i]
    if not isinstance(line, str):
        return None, 0
    m = ANNEX.match(line)
    if m and not m.group(3):
        j = i + 1
        while j < len(body) and not (isinstance(body[j], str) and body[j].strip()):
            j += 1
        title = body[j].strip() if j < len(body) else ""
        used = j - i + 1
        return f"## {m.group(1)} ({m.group(2)}): {title}", used
    m = PLAIN_HEAD.match(line)
    if m:
        return "## " + m.group(1), 1
    m = CLAUSE.match(line)
    if m and not COLUMNS.search(m.group(2)) and not m.group(2).endswith((".", ",", ";", ":")):
        number = m.group(1)
        depth = number.count(".") + 1
        return "#" * min(depth + 1, 6) + f" {number} {m.group(2)}", 1
    return None, 0


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as source:
        text = source.read()
    with open(dst, "w", encoding="utf-8") as target:
        target.write(convert(text, HEADER, FOOTER, heading))
