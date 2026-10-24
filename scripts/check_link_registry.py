#!/usr/bin/env python3
"""Fail if schemas/link.v1.json, the files generated from it or the status writer disagree.

    python3 scripts/check_link_registry.py

Every generated file is compared byte for byte: the JavaScript module as generated, the
C++ files as generated and then put through clang-format (CLANG_FORMAT names it), which
is how scripts/link_registry.py writes them. Every frame and every key must carry a
description. The status frame, the one the unit pushes unasked, is written by one
function, ConfigService::format_status in firmware/core/comms/config_reports.cpp, one
`writer.add_<type>(link::status_key::k<Name>, ...)` per key, and must match the registry:

  keys      it writes every key the registry gives the frame, and nothing more
  order     in ascending key number
  required  a key written under an `if` may be absent, every other one is required
  types     each add_<type> writes the registry type it is named after
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import link_registry  # noqa: E402

STATUS_WRITER = link_registry.ROOT / "firmware" / "core" / "comms" / "config_reports.cpp"
BODY = re.compile(r"int ConfigService::format_status\([^)]*\) \{\n(.*?)\n\}", re.S)
FIELD = re.compile(r"^\s*(if \(.*\)\s+)?writer\.add_(\w+)\(link::(?:status_key::k(\w+)|kCommandKey)",
                   re.M)
WRITER_TYPE = {"code": "enum", "boolean": "boolean", "unsigned": "unsigned",
               "integer": "integer", "text": "text"}


def main():
    registry = link_registry.load()
    expected = link_registry.written(registry, link_registry.formatter())
    broken = ["%s: differs from schemas/link.v1.json, run scripts/link_registry.py" % relative
              for relative in link_registry.stale(expected, link_registry.committed)]
    broken += ["schemas/link.v1.json: %s has no description" % name
               for name in undescribed(registry)]
    broken += ["%s: %s" % (STATUS_WRITER.relative_to(link_registry.ROOT), why)
               for why in status_writer_failures(registry,
                                                 STATUS_WRITER.read_text(encoding="utf-8"))]
    for why in broken:
        print(why, file=sys.stderr)
    if broken:
        return 1
    print("OK: the link registry describes every frame and key, every file generated from it "
          "is current, and the status writer matches it.", file=sys.stderr)
    return 0


def undescribed(registry):
    """Every frame and key, by name, that the registry leaves without a description."""
    missing = []
    for frame in registry["frames"]:
        if not frame.get("description"):
            missing.append(frame["command"])
        if "keys_of" in frame:
            continue
        missing += ["%s.%s" % (frame["command"], key["name"]) for key in frame["keys"]
                    if key is not link_registry.COMMAND_KEY and not key.get("description")]
    return missing


def status_writer_failures(registry, source):
    status = next(frame for frame in registry["frames"] if frame["command"] == "status")
    body = BODY.search(source)
    if body is None:
        yield "format_status not found"
        return
    by_constant = {link_registry.constant(key["name"]): key for key in status["keys"]}
    fields = []
    for guard, method, constant in FIELD.findall(body.group(1)):
        key = by_constant.get("k" + constant) if constant else link_registry.COMMAND_KEY
        if key is None:
            yield "status_key::k%s is not a status key of the registry" % constant
            return
        fields.append((key, method, not guard))
    written = [key["name"] for key, _, _ in fields]
    numbered = [key["name"] for key in status["keys"]]
    if written != numbered:
        yield "format_status writes %s, the registry numbers %s" % (written, numbered)
    always = [key["name"] for key, _, unconditional in fields
              if unconditional and key is not link_registry.COMMAND_KEY]
    required = [key["name"] for key in status["keys"] if key.get("required")]
    if always != required:
        yield "format_status always writes %s, the registry requires %s" % (always, required)
    for key, method, _ in fields:
        if WRITER_TYPE.get(method) != key["type"]:
            yield "%s: format_status calls add_%s, the registry says %s" % (
                key["name"], method, key["type"])


if __name__ == "__main__":
    sys.exit(main())
