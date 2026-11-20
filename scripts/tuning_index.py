#!/usr/bin/env python3
"""Generate docs/TUNING.md: every number the firmware's behavior is tuned by.

Each constant's reason is the comment above it in the source. A folder's README that names
the constant is linked, not quoted.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tuning_constants import (  # noqa: E402
    MECHANISMS,
    ROOT,
    UNITS,
    collect,
    resolved_values,
    tuning,
)

OUTPUT = os.path.join(ROOT, "docs", "TUNING.md")

PREAMBLE = """# Tuning

Every constant the firmware's behavior is tuned by: what the device claims about
itself, what the glass says, what goes on the air, and how often. Generated from
the sources by `scripts/tuning_index.py`, so a value that changes without its
note changing is a red build.

The names read `k<Subject><Mechanism><Unit>`, and
[`firmware/README.md`](../firmware/README.md) is where the mechanisms are
defined: a hold, a settle, a floor and a period are four different things that
all read as "a delay" in conversation.

Out of scope, deliberately: `firmware/hardware/`, `firmware/boards/` and
`firmware/ports/`. Those figures come from datasheets, under the datasheet's
name. Anything a pilot can change is a setting rather than a tuning, and lives
in `firmware/products/skyblip_go/settings.h`.
"""


def folder_of(constant):
    return os.path.dirname(constant.path)


def readme_link(folder):
    if readme_of(folder) is None:
        return f"`{folder}`"
    return f"[`{folder}`](../{folder}/README.md)"


def readme_of(folder):
    path = os.path.join(ROOT, folder, "README.md")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as readme:
        return readme.read()


def why(constant):
    if constant.note:
        return constant.note
    readme = readme_of(folder_of(constant))
    if readme and constant.name in readme:
        return f"[README](../{folder_of(constant)}/README.md) argues it"
    return "-"


def value_of(constant, known):
    number = constant.scaled(known)
    written = constant.value
    human = constant.human(known)
    if number is not None and written != str(number):
        written = f"`{written}` = {number}"
    else:
        written = str(number) if number is not None else f"`{written}`"
    return f"{written} ({human})" if human else written


def mechanism_of(constant):
    return MECHANISMS.get(constant.mechanism, "") and constant.mechanism or "-"


def rows(constants, known):
    for constant in constants:
        yield (
            f"`{constant.name}`",
            value_of(constant, known),
            UNITS[constant.unit],
            mechanism_of(constant),
            why(constant),
        )


def table(constants, known):
    lines = ["| Constant | Value | Unit | Mechanism | Why |", "|---|---|---|---|---|"]
    for row in rows(constants, known):
        lines.append("| " + " | ".join(row) + " |")
    return lines


def document(constants, known):
    lines = [PREAMBLE]
    folders = sorted({folder_of(constant) for constant in constants})
    for folder in folders:
        inside = [c for c in constants if folder_of(c) == folder]
        inside.sort(key=lambda c: (c.path, c.line))
        lines.append(f"## {readme_link(folder)}\n")
        lines += table(inside, known)
        lines.append("")
    lines.append(f"{len(constants)} constants over {len(folders)} folders.")
    return "\n".join(lines) + "\n"


def main():
    every = collect()
    constants = tuning(every)
    known = resolved_values(every)
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(document(constants, known))
    print(f"wrote {os.path.relpath(OUTPUT, ROOT)}: {len(constants)} constants", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
