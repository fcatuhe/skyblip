#!/usr/bin/env python3
"""Collect the firmware's tuning constants: the numbers a behavior is tuned by.

One parse, two readers. `check_tuning_names.py` holds the vocabulary to it and
`tuning_index.py` writes docs/TUNING.md out of it, so a constant cannot be
documented under one name and checked under another.

A tuning constant is a `constexpr` in the logic layers whose name ends in a unit:
a duration, a speed, a distance, a force or a count of samples. `hardware/`,
`boards/` and `ports/` are out of scope on purpose - a datasheet's 2.6 s busy
time is the part's figure under the part's name, and renaming it would hide
where it came from.

MECHANISMS is the closed vocabulary: the word in front of the unit, and the one
a reader takes the mechanism from. firmware/README.md carries the rules the
checker enforces.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(ROOT, "firmware")
LAYERS = ("core", "runtime", "ui", "products")

OTHER_UNITS = (
    "Mv",
    "Mpa",
    "Pa",
    "Dbm",
    "Hz",
    "Bps",
    "Bits",
    "Bytes",
    "Permille",
    "DeciCelsius",
)

UNITS = {
    "Ms": "milliseconds",
    "Us": "microseconds",
    "Sec": "seconds",
    "S": "seconds",
    "MmS": "millimetres per second",
    "Mps": "metres per second",
    "Dps": "degrees per second",
    "M": "metres",
    "Mg": "thousandths of g",
    "Samples": "samples",
    "Fixes": "fixes",
    "Records": "records",
    "Reports": "reports of the aircraft's own",
}

DIMENSIONED = ("Hold", "Settle", "Period", "Window", "MaxAge", "Stale", "Forget")

MECHANISMS = {
    "Hold": "evidence must persist this long before the state flips",
    "Settle": "wait this long after an event before trusting what follows",
    "Floor": "a lower bound, in the unit the name ends in",
    "Ceiling": "an upper bound, in the unit the name ends in",
    "Period": "fixed cadence",
    "Window": "the span a measurement or a budget is taken over",
    "MaxAge": "past this an input stops counting as evidence",
    "Stale": "past this a reading leaves the glass",
    "Forget": "past this the record itself is dropped",
    "Samples": "consecutive samples that must agree",
    "Fixes": "consecutive fixes that must agree",
}

DECLARATION = re.compile(
    r"^(?P<indent>[ \t]*)(?P<static>static\s+)?constexpr\s+[A-Za-z_][\w:<>]*\s+"
    r"(?P<name>k[A-Za-z0-9_]+)\s*=\s*(?P<value>[^;]*);",
    re.M | re.S,
)
CAMEL = re.compile(r"[A-Z][a-z0-9]*|[A-Z]+(?![a-z])")
NOTE_TAG = re.compile(r"^(?:INFO|TODO|FIXME|OPTIMIZE):\s+\w+\s+\d{2}\w{3}\d{2}\s+")
ARITHMETIC = re.compile(r"^[\d\s+\-*/()uU]+$")
HEX = re.compile(r"^0[xX][0-9a-fA-F]+$")
CONVERSION = re.compile(r"Per[A-Z]")
CONVERSIONS = ("kSecondMs", "kSecondUs", "kHalfSecondUs", "kNominalSecondUs")
SENTENCE = re.compile(r"(?<=[.:])\s+(?=[A-Z`])")
QUALIFIER = re.compile(r"\b[a-z_][a-z0-9_]*::")
IDENTIFIER = re.compile(r"\bk[A-Za-z0-9_]+\b")


class Constant:
    def __init__(self, name, value, path, line, class_scope, note):
        self.name = name
        self.value = value
        self.path = path
        self.line = line
        self.class_scope = class_scope
        self.note = note
        self.words = CAMEL.findall(name[1:])
        self.unit = self._unit()
        self.indexed = self.unit in UNITS
        self.subject_words = self.words[: -len(CAMEL.findall(self.unit))] if self.unit else []

    def _unit(self):
        for unit in sorted(list(UNITS) + list(OTHER_UNITS), key=len, reverse=True):
            if self.name.endswith(unit):
                return unit
        return ""

    @property
    def mechanism(self):
        words = self.subject_words + [self.unit]
        pairs = [a + b for a, b in zip(words, words[1:])]
        for word in reversed(pairs + words):
            if word in MECHANISMS:
                return word
        return ""

    @property
    def where(self):
        return f"{self.path}:{self.line}"

    @property
    def conversion(self):
        return self.name in CONVERSIONS or bool(CONVERSION.search(self.name))

    def scaled(self, known=None):
        expression = QUALIFIER.sub("", self.value)
        if HEX.match(expression.strip()):
            return int(expression.strip(), 16)
        for _ in range(4):
            if ARITHMETIC.match(expression.strip()):
                return int(eval(expression.replace("u", "").replace("U", "")))
            if not known:
                return None
            expanded = IDENTIFIER.sub(lambda m: str(known.get(m.group(0), m.group(0))), expression)
            if expanded == expression:
                return None
            expression = expanded
        return None

    def human(self, known=None):
        number = self.scaled(known)
        if number is None:
            return ""
        if self.unit == "Ms" and number >= 60000:
            return f"{number / 60000:g} min"
        if self.unit == "Ms" and number >= 1000:
            return f"{number / 1000:g} s"
        if self.unit == "Us" and number >= 1000:
            return f"{number / 1000:g} ms"
        if self.unit == "MmS":
            return f"{number / 1000:g} m/s"
        if self.unit == "Sec" and number >= 60:
            return f"{number / 60:g} min"
        return ""


def sources():
    for layer in LAYERS:
        for folder, _, names in sorted(os.walk(os.path.join(FIRMWARE, layer))):
            for name in sorted(names):
                if name.endswith((".h", ".cpp")):
                    yield os.path.join(folder, name)


def note_above(lines, index):
    block = []
    while index > 0 and lines[index - 1].strip().startswith("//"):
        index -= 1
        block.insert(0, lines[index].strip().lstrip("/").strip())
    if not block:
        return ""
    said = ""
    for sentence in SENTENCE.split(NOTE_TAG.sub("", " ".join(block))):
        said = f"{said} {sentence}".strip()
        if len(said) >= 40:
            break
    return said


def collect():
    found = []
    for path in sources():
        with open(path, encoding="utf-8") as source:
            text = source.read()
        lines = text.splitlines()
        for match in DECLARATION.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            found.append(
                Constant(
                    name=match.group("name"),
                    value=" ".join(match.group("value").split()),
                    path=os.path.relpath(path, ROOT),
                    line=line,
                    class_scope=bool(match.group("static") or match.group("indent")),
                    note=note_above(lines, line - 1),
                )
            )
    return found


def tuning(constants):
    return [constant for constant in constants if constant.indexed and not constant.conversion]


def resolved_values(constants):
    known = {}
    for _ in range(4):
        for constant in constants:
            number = constant.scaled(known)
            if number is not None:
                known[constant.name] = number
    return known
