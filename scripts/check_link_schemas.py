#!/usr/bin/env python3
"""Fail if a frame's JSON Schema and schemas/link.v1.json disagree on the keys it carries.

    python3 scripts/check_link_schemas.py

The registry numbers every key a frame may carry and the frame's JSON Schema names and
describes them. For every frame that has a schema, both must hold the same keys, the
schema must declare each with the JSON type a client decodes it to, and an enumerated
key must list exactly the names the registry gives its codes. check_status_schema.py
holds the status frame to the firmware's writer besides.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import link_registry  # noqa: E402

SCHEMAS = link_registry.ROOT / "schemas"
FRAMES = {
    "status": [("status.v1.schema.json", "#")],
    "config": [("config.v1.schema.json", "#")],
    "set": [("config.v1.schema.json", "#")],
    "defaults": [("config.v1.schema.json", "#")],
    "diagnostics": [("diagnostics.v1.schema.json", "#")],
    "radio": [("diagnostics.v1.schema.json", "#")],
    "challenge": [("activation.v1.schema.json", "#/definitions/challenge"),
                  ("activation.v1.schema.json", "#/definitions/challenge_reply")],
    "register": [("activation.v1.schema.json", "#/definitions/register")],
    "registration": [("activation.v1.schema.json", "#/definitions/registration")],
    "unlock": [("activation.v1.schema.json", "#/definitions/unlock")],
}
JSON_TYPE = {"unsigned": "integer", "integer": "integer", "boolean": "boolean", "text": "string",
             "bytes": "string", "enum": "string", "unsigned_array": "array"}


def main():
    registry = link_registry.load()
    broken = [why for frame in registry["frames"] if frame["command"] in FRAMES
              for why in failures(frame, FRAMES[frame["command"]], registry["enums"])]
    unknown = sorted(set(FRAMES) - {frame["command"] for frame in registry["frames"]})
    broken += ["%s: no such frame in the registry" % command for command in unknown]
    for why in broken:
        print(why, file=sys.stderr)
    if broken:
        return 1
    print("OK: the %d frames with a JSON Schema carry the keys the registry numbers."
          % len(FRAMES), file=sys.stderr)
    return 0


def failures(frame, places, enums):
    named = {}
    for file_name, pointer in places:
        schema = json.loads((SCHEMAS / file_name).read_text(encoding="utf-8"))
        for name, declared in resolved(schema, at(schema, pointer))["properties"].items():
            named[name] = resolved(schema, declared)
    where = "%s (%s)" % (frame["command"], ", ".join(name + pointer for name, pointer in places))
    command = named.pop("command", {})
    if command and frame["command"] not in command.get("enum", [command.get("const")]):
        yield "%s: the schema's command does not name %s" % (where, frame["command"])
    keys = {key["name"]: key for key in frame["keys"] if key["name"] != "command"}
    for name in sorted(set(keys) - set(named)):
        yield "%s: the registry numbers %s, the schema does not name it" % (where, name)
    for name in sorted(set(named) - set(keys)):
        yield "%s: the schema names %s, the registry does not number it" % (where, name)
    for name in sorted(set(keys) & set(named)):
        key, declared = keys[name], named[name]
        if "type" in declared and declared["type"] != JSON_TYPE[key["type"]]:
            yield "%s.%s: the registry says %s, the schema says %s" % (
                where, name, key["type"], declared["type"])
        if key["type"] == "enum":
            names = [value["name"] for value in enums[key["enum"]]["values"]]
            if declared.get("enum") != names:
                yield "%s.%s: the schema lists %s, the registry names %s" % (
                    where, name, declared.get("enum"), names)


def resolved(schema, declared):
    while "$ref" in declared:
        declared = at(schema, declared["$ref"])
    return declared


def at(schema, pointer):
    node = schema
    for part in pointer.lstrip("#").strip("/").split("/"):
        if part:
            node = node[part]
    return node


if __name__ == "__main__":
    sys.exit(main())
