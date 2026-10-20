#!/usr/bin/env python3
"""Fail if the status frame the firmware writes, the link registry and the schema disagree.

The frame is written by one function, ConfigService::format_status in
firmware/core/comms/config_reports.cpp, one `writer.add_<type>(link::status_key::k<Name>, ...)`
per key. schemas/link.v1.json numbers the keys and schemas/status.v1.schema.json names
them. Five things must match:

  keys      the schema declares every key the firmware writes, and nothing more
  order     in the order the firmware writes them, which is ascending key number
  required  a key written under an `if` may be absent, every other one is required
  types     each add_<type> writes the registry type it is named after, which the schema
            must declare as its JSON type once a client has named it
  values    an enumerated key lists in the schema exactly the names the registry gives it
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "firmware", "core", "comms", "config_reports.cpp")
SCHEMA = os.path.join(ROOT, "schemas", "status.v1.schema.json")
REGISTRY = os.path.join(ROOT, "schemas", "link.v1.json")

BODY = re.compile(r"int ConfigService::format_status\([^)]*\) \{\n(.*?)\n\}", re.S)
FIELD = re.compile(r"^\s*(if \(.*\)\s+)?writer\.add_(\w+)\(link::(?:status_key::k(\w+)|kCommandKey)",
                   re.M)
REGISTRY_TYPE = {"code": "enum", "boolean": "boolean", "unsigned": "unsigned",
                 "integer": "integer", "text": "text"}
SCHEMA_TYPE = {"enum": "string", "boolean": "boolean", "unsigned": "integer",
               "integer": "integer", "text": "string"}


def pascal(name):
    return "".join(part.capitalize() for part in name.split("_"))


def written_fields(status_keys):
    with open(SOURCE, encoding="utf-8") as source:
        body = BODY.search(source.read())
    if body is None:
        raise SystemExit(f"{SOURCE}: format_status not found")
    by_constant = {pascal(key["name"]): key for key in status_keys}
    fields = []
    for guard, method, constant in FIELD.findall(body.group(1)):
        key = by_constant.get(constant) if constant else {"name": "command", "number": 0,
                                                          "type": "enum", "enum": "command"}
        if key is None:
            raise SystemExit(f"{SOURCE}: status_key::k{constant} is not in {REGISTRY}")
        fields.append((key, method, not guard))
    return fields


def failures(fields, schema, enums):
    properties = schema["properties"]
    written = [key["name"] for key, _, _ in fields]
    if written != list(properties):
        yield f"firmware writes {written}, schema lists {list(properties)}"
    numbers = [key["number"] for key, _, _ in fields]
    if numbers != sorted(numbers):
        yield f"firmware writes key numbers {numbers}, which are not ascending"
    always = [key["name"] for key, _, unconditional in fields if unconditional]
    if always != schema["required"]:
        yield f"firmware always writes {always}, schema requires {schema['required']}"
    for key, method, _ in fields:
        if REGISTRY_TYPE.get(method) != key["type"]:
            yield f"{key['name']}: firmware calls add_{method}, the registry says {key['type']}"
        declared = properties.get(key["name"], {})
        if declared.get("type", SCHEMA_TYPE[key["type"]]) != SCHEMA_TYPE[key["type"]]:
            yield f"{key['name']}: the registry says {key['type']}, schema says {declared['type']}"
        if key["type"] == "enum" and key["enum"] != "command":
            names = [value["name"] for value in enums[key["enum"]]["values"]]
            if declared.get("enum") != names:
                yield f"{key['name']}: schema lists {declared.get('enum')}, registry names {names}"
    if schema.get("additionalProperties") is not False:
        yield "additionalProperties must be false: the frame has no key the schema does not name"


def main():
    with open(REGISTRY, encoding="utf-8") as registry_file:
        registry = json.load(registry_file)
    status = next(frame for frame in registry["frames"] if frame["command"] == "status")
    fields = written_fields(status["keys"])
    with open(SCHEMA, encoding="utf-8") as schema_file:
        schema = json.load(schema_file)
    broken = list(failures(fields, schema, registry["enums"]))
    for why in broken:
        report(f"{SCHEMA}: {why}")
    if broken:
        return 1
    report(f"OK: the {len(fields)} status keys match the registry and the schema, in order.")
    return 0


def report(message):
    print(message, file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
