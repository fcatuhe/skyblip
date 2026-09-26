#!/usr/bin/env python3
"""Fail if the status frame the firmware writes and schemas/status.v1.schema.json disagree.

The frame is written by one function, ConfigService::format_status in
firmware/core/comms/config_reports.cpp, one `w.kv_<type>("key", ...)` per key. The
schema lists the same keys as its properties. Four things must match:

  keys      the schema declares every key the firmware writes, and nothing more
  order     in the order the firmware writes them, which is the order on the wire
  required  a key written under an `if` may be absent, every other one is required
  types     kv_int is an integer, kv_bool a boolean, kv_str a string
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "firmware", "core", "comms", "config_reports.cpp")
SCHEMA = os.path.join(ROOT, "schemas", "status.v1.schema.json")

BODY = re.compile(r"int ConfigService::format_status\([^)]*\) \{\n(.*?)\n\}", re.S)
FIELD = re.compile(r'^\s*(if \(.*\) )?w\.kv_(int|bool|str)\("(\w+)"', re.M)
TYPES = {"int": "integer", "bool": "boolean", "str": "string"}


def written_fields():
    with open(SOURCE, encoding="utf-8") as source:
        body = BODY.search(source.read())
    if body is None:
        raise SystemExit(f"{SOURCE}: format_status not found")
    return [(key, TYPES[kind], not guard) for guard, kind, key in FIELD.findall(body.group(1))]


def failures(fields, schema):
    properties = schema["properties"]
    written = [key for key, _, _ in fields]
    if written != list(properties):
        yield f"firmware writes {written}, schema lists {list(properties)}"
    always = [key for key, _, unconditional in fields if unconditional]
    if always != schema["required"]:
        yield f"firmware always writes {always}, schema requires {schema['required']}"
    for key, kind, _ in fields:
        declared = properties.get(key, {}).get("type")
        if declared is not None and declared != kind:
            yield f"{key}: firmware writes {kind}, schema says {declared}"
    if schema.get("additionalProperties") is not False:
        yield "additionalProperties must be false: the frame has no key the schema does not name"


def main():
    fields = written_fields()
    with open(SCHEMA, encoding="utf-8") as schema_file:
        schema = json.load(schema_file)
    broken = list(failures(fields, schema))
    for why in broken:
        report(f"{SCHEMA}: {why}")
    if broken:
        return 1
    report(f"OK: the {len(fields)} status keys match the schema, in order.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
