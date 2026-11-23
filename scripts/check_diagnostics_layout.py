#!/usr/bin/env python3
"""Fail if schemas/diagnostics_layout.v1.json, the files generated from it or the decoded view disagree.

    python3 scripts/check_diagnostics_layout.py

The layout must load: unique type and enum codes, enum codes from 0 with no hole, every field
inside the payload, no two fields on one byte or bit, a type's flags on bits 2 to 7. Every
generated file must be byte for byte what scripts/diagnostics_layout.py writes. The decoded view,
schemas/diagnostics_log.v1.schema.json, must declare every field the decoders write and no other,
list the types in code order, list each enum's names in code order, and cap a code8 field at the
highest code of its enum.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import diagnostics_layout  # noqa: E402

SCHEMA = diagnostics_layout.ROOT / "schemas" / "diagnostics_log.v1.schema.json"
ENVELOPE = ("type", "at_s", "into_ms", "phase_valid", "utc_dated")


def main():
    try:
        layout = diagnostics_layout.load()
    except ValueError as error:
        print("schemas/diagnostics_layout.v1.json: %s" % error, file=sys.stderr)
        return 1
    expected = diagnostics_layout.generated(layout)
    broken = ["%s: differs from schemas/diagnostics_layout.v1.json, run "
              "scripts/diagnostics_layout.py" % relative
              for relative in diagnostics_layout.stale(expected, diagnostics_layout.committed)]
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    broken += ["schemas/diagnostics_log.v1.schema.json: %s" % why
               for why in schema_failures(layout, schema)]
    for why in broken:
        print(why, file=sys.stderr)
    if broken:
        return 1
    print("OK: the diagnostics layout holds together, every file generated from it is current, "
          "and the decoded view names what the decoders write.", file=sys.stderr)
    return 0


def schema_failures(layout, schema):
    properties = schema["properties"]
    names = [kind["name"] for kind in layout["types"]]
    if properties["type"].get("enum") != names:
        yield "type lists %s, the layout's types in code order are %s" % (
            properties["type"].get("enum"), names)
    decoded = set()
    through_names = set()
    for kind in layout["types"]:
        for field in kind["fields"]:
            if field["kind"] == "vouch":
                continue
            decoded.add(field["name"])
            spec = properties.get(field["name"])
            if spec is None:
                yield "%s.%s is decoded and not declared" % (kind["name"], field["name"])
                continue
            values = [value["name"] for value in layout["enums"].get(field.get("enum"), {})
                      .get("values", [])]
            if field["kind"] == "enum8":
                through_names.add(field["name"])
                if spec.get("enum") != values:
                    yield "%s lists %s, the %s enum is %s" % (
                        field["name"], spec.get("enum"), field["enum"], values)
            if field["kind"] == "code8" and spec.get("maximum") != len(values) - 1:
                yield "%s caps at %s, the %s enum's highest code is %d" % (
                    field["name"], spec.get("maximum"), field["enum"], len(values) - 1)
    for name, spec in properties.items():
        if name in ENVELOPE:
            continue
        if name not in decoded:
            yield "%s is declared and no type decodes it" % name
        elif "enum" in spec and name not in through_names:
            yield "%s lists names and the layout decodes it as a number" % name


if __name__ == "__main__":
    sys.exit(main())
