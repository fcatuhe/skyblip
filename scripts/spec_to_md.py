import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layout_to_md import convert  # noqa: E402

HEADER = (re.compile(r"^ADS-L 4 S[DR]{2}-860 Issue \d+\s"), re.compile(r"^ED Decision 2022/024/R$"))
FOOTER = (re.compile(r"^(ED Decision 2022/024/R\s+)?Page \d+ of \d+$"), re.compile(r"^ED Decision 2022/024/R$"))
SUBPART = re.compile(r"^\s*(SUBPART [A-Z].*?)\s*$")
CLAUSE = re.compile(r"^\s*(ADS-L\.4\.SRD860\.[A-Z](?:\.\d+)*)\s{1,}(\S.*?)\s*$")
PLAIN_HEAD = re.compile(r"^\s*(PREAMBLE|DEFINITIONS AND ABBREVIATIONS|TABLE OF CONTENTS|APPENDIX.*)\s*$")


def heading(lines, i):
    line = lines[i]
    if not isinstance(line, str):
        return None, 0
    m = SUBPART.match(line)
    if m and "..." not in line:
        return "## " + m.group(1).replace("\u2014", "-").replace("\u2013", "-"), 1
    m = PLAIN_HEAD.match(line)
    if m:
        return "## " + m.group(1), 1
    m = CLAUSE.match(line)
    if m:
        depth = m.group(1).count(".") - 4
        return "#" * min(3 + depth, 6) + f" {m.group(1)} {m.group(2)}", 1
    return None, 0


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as source:
        text = source.read()
    with open(dst, "w", encoding="utf-8") as target:
        target.write(convert(text, HEADER, FOOTER, heading))
