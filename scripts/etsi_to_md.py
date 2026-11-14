import re, sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding="utf-8").read().split("\n")

RUNNING = re.compile(r"^\s*(\d+\s+)?ETSI EN \d+ \d+(-\d+)? V\d+\.\d+\.\d+ \(\d{4}-\d{2}\)(\s+\d+)?\s*$")
LONE_ETSI = re.compile(r"^\s*ETSI\s*$")
LEADER = re.compile(r"\.{4,}")
CLAUSE = re.compile(r"^ {0,2}(\d+(?:\.\d+)*|[A-Z](?:\.\d+)+)\s{1,}([A-Z(].{0,90}?)\s*$")
ANNEX = re.compile(r"^ {0,2}(Annex [A-Z]) \((normative|informative)\):\s*(.*?)\s*$")
PLAIN_HEAD = re.compile(r"^(Intellectual Property Rights|Foreword|Modal verbs terminology|Introduction|History)\s*$")
COLUMNS = re.compile(r"\S {2,}\S")


def without_page_furniture(lines):
    return [l.replace("\f", "").rstrip() for l in lines
            if not (RUNNING.match(l.replace("\f", "")) or LONE_ETSI.match(l.replace("\f", "")) or LEADER.search(l))]


def without_repeated_blanks(lines):
    out = []
    for line in lines:
        if not line.strip() and out and not out[-1].strip():
            continue
        out.append(line)
    return out


body = without_repeated_blanks(without_page_furniture(lines))

out = []
para = []
block = []


def flush_para():
    if para:
        out.append(" ".join(w.strip() for w in para))
        out.append("")
        para.clear()


def flush_block():
    while block and not block[-1].strip():
        block.pop()
    if block:
        out.append("```")
        indent = min((len(b) - len(b.lstrip()) for b in block if b.strip()), default=0)
        out.extend(b[indent:] if b.strip() else "" for b in block)
        out.append("```")
        out.append("")
        block.clear()


def heading(i):
    line = body[i]
    m = ANNEX.match(line)
    if m and not m.group(3):
        j = i + 1
        while j < len(body) and not body[j].strip():
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


i = 0
while i < len(body):
    line = body[i]
    stripped = line.strip()

    head, used = heading(i)
    if head:
        i += used
        flush_para()
        flush_block()
        out.append(head)
        out.append("")
        continue
    i += 1

    if not stripped:
        flush_para()
        block.append("")
        if not any(b.strip() for b in block):
            block.clear()
        continue

    tabular = bool(COLUMNS.search(stripped)) or stripped.startswith(("|", "+--")) or len(line) - len(line.lstrip()) >= 8
    if tabular:
        flush_para()
        block.append(line)
    else:
        if any(b.strip() for b in block):
            nxt = body[i] if i < len(body) else ""
            if COLUMNS.search(nxt.strip()):
                block.append(line)
                continue
            flush_block()
        else:
            block.clear()
        para.append(stripped)

flush_para()
flush_block()

text = re.sub(r"\n{3,}", "\n\n", "\n".join(out))
open(dst, "w", encoding="utf-8").write(text.strip() + "\n")
