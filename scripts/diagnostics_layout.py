#!/usr/bin/env python3
"""Write the files generated from schemas/diagnostics_layout.v1.json, the diagnostics record layout.

    python3 scripts/diagnostics_layout.py
"""
import json
import pathlib
import sys

from link_registry import (CLANG_FORMAT_OFF, committed, constant, enum_class, exhaustive_switch,
                           pascal, refuse_duplicates, stale, write)

ROOT = pathlib.Path(__file__).resolve().parent.parent
LAYOUT = ROOT / "schemas" / "diagnostics_layout.v1.json"
FIRMWARE_HEADER = "firmware/core/diagnostics/layout.h"
MIRRORS_TEST = "firmware/test/core/diagnostics/test_diagnostics_mirrors.cpp"
PYTHON_MODULE = "scripts/blip_diagnostics_layout.py"
JAVASCRIPT_MODULE = "server/app/javascript/skyblip/diagnostics_layout.js"

PAYLOAD_BYTES = 16
TYPE_FLAG_BITS = range(2, 8)
WIDTHS = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u24": 3, "u32": 4, "i32": 4, "enum8": 1,
          "code8": 1, "bits": 1}
FLAG_KINDS = ("flag", "vouch")
KINDS = tuple(WIDTHS) + FLAG_KINDS
LINE_WIDTH = 100


def load(path=LAYOUT):
    with open(path, encoding="utf-8") as source:
        layout = json.load(source)
    check(layout)
    return layout


def check(layout):
    enums = layout["enums"]
    for name, enum in enums.items():
        check_enum(name, enum)
    refuse_duplicates("type", [kind["name"] for kind in layout["types"]])
    codes = [kind["code"] for kind in layout["types"]]
    if codes != sorted(set(codes)) or codes[0] < 1:
        raise ValueError("type codes %s are not unique, ascending and above 0" % codes)
    for kind in layout["types"]:
        check_fields(kind["name"], kind["fields"], enums)


def check_enum(name, enum):
    values = enum["values"]
    refuse_duplicates("%s value" % name, [value["name"] for value in values])
    codes = [value["code"] for value in values]
    if codes != list(range(len(values))):
        raise ValueError("%s: codes %s are not 0 up to one less than the count, in order"
                         % (name, codes))
    if enum.get("declared") and "mirrors" in enum:
        raise ValueError("%s: an enum is declared here or mirrors a C++ one, not both" % name)
    if "mirrors" in enum and not all("mirrors" in value for value in values):
        raise ValueError("%s: a mirrored enum names the C++ member of every value" % name)


def check_fields(type_name, fields, enums):
    refuse_duplicates("%s field" % type_name, [field["name"] for field in fields])
    by_name = {field["name"]: field for field in fields}
    taken = {}
    for field in fields:
        where = "%s.%s" % (type_name, field["name"])
        kind = field["kind"]
        if kind not in KINDS:
            raise ValueError("%s: kind %s is not one of %s" % (where, kind, KINDS))
        if kind in ("enum8", "code8") and field.get("enum") not in enums:
            raise ValueError("%s: no enum %s" % (where, field.get("enum")))
        for slot in slots(where, field):
            if slot in taken:
                raise ValueError("%s: %s overlaps %s" % (where, slot_text(slot), taken[slot]))
            taken[slot] = where
        if "valid_when" in field:
            check_valid_when(where, field, by_name)


def slots(where, field):
    if field["kind"] in FLAG_KINDS:
        if field["bit"] not in TYPE_FLAG_BITS:
            raise ValueError("%s: flag bit %d is not one of a type's bits 2 to 7"
                             % (where, field["bit"]))
        return [("bit", field["bit"])]
    at = field["at"]
    if at < 0 or at + WIDTHS[field["kind"]] > PAYLOAD_BYTES:
        raise ValueError("%s: bytes from %d run past the %d-byte payload"
                         % (where, at, PAYLOAD_BYTES))
    if field["kind"] == "bits":
        if field["shift"] < 0 or field["width"] < 1 or field["shift"] + field["width"] > 8:
            raise ValueError("%s: bits %d up from %d leave the byte"
                             % (where, field["width"], field["shift"]))
        return [("byte bit", at, bit) for bit in range(field["shift"],
                                                       field["shift"] + field["width"])]
    return [("byte bit", byte, bit) for byte in range(at, at + WIDTHS[field["kind"]])
            for bit in range(8)]


def slot_text(slot):
    return "flag bit %d" % slot[1] if slot[0] == "bit" else "payload byte %d" % slot[1]


def check_valid_when(where, field, by_name):
    flag = by_name.get(field["valid_when"])
    if flag is None or flag["kind"] not in FLAG_KINDS:
        raise ValueError("%s: valid_when names %s, which is no flag of the type"
                         % (where, field["valid_when"]))
    if "valid_when" in flag and "valid_when" in by_name[flag["valid_when"]]:
        raise ValueError("%s: valid_when reaches through more than one flag" % where)


def flag_constant(type_name, field_name):
    return "k%sFlag%s" % (pascal(type_name), pascal(field_name))


def generated(layout):
    return {
        FIRMWARE_HEADER: firmware_header(layout),
        MIRRORS_TEST: mirrors_test(layout),
        PYTHON_MODULE: python_module(layout),
        JAVASCRIPT_MODULE: javascript_module(layout),
    }


def firmware_header(layout):
    lines = [CLANG_FORMAT_OFF, "#ifndef SKYBLIP_CORE_DIAGNOSTICS_LAYOUT_H",
             "#define SKYBLIP_CORE_DIAGNOSTICS_LAYOUT_H", "", "#include <cstdint>", "",
             "namespace skyblip::diagnostics {", ""]
    types = [{"name": "none", "code": 0}] + layout["types"]
    lines += enum_class("Type", types)
    lines += ["constexpr uint8_t kHighestType = %d;" % layout["types"][-1]["code"], ""]
    for name, enum in layout["enums"].items():
        if enum.get("declared"):
            lines += enum_class(pascal(name), enum["values"])
    for kind in layout["types"]:
        lines += type_constants(kind)
    lines += ["}", "", "#endif"]
    return "\n".join(lines) + "\n"


def type_constants(kind):
    live = [field for field in kind["fields"] if not field.get("retired")]
    payload = [field for field in live if field["kind"] not in FLAG_KINDS]
    flags = sorted((field for field in live if field["kind"] in FLAG_KINDS),
                   key=lambda field: field["bit"])
    lines = []
    if payload:
        lines += ["namespace %s_at {" % kind["name"], "enum : uint32_t {"]
        lines += ["    %s = %d," % (constant(field["name"]), field["at"]) for field in payload]
        lines += ["};", "}", ""]
    if flags:
        lines += ["constexpr uint8_t %s = 1u << %d;" % (flag_constant(kind["name"], field["name"]),
                                                       field["bit"]) for field in flags]
        lines.append("")
    bits = [field for field in payload if field["kind"] == "bits"]
    if bits:
        lines.append("namespace %s_bits {" % kind["name"])
        for field in bits:
            lines += ["constexpr uint8_t %sShift = %d;" % (constant(field["name"]), field["shift"]),
                      "constexpr uint8_t %sMask = 0x%02X;" % (constant(field["name"]),
                                                              (1 << field["width"]) - 1)]
        lines += ["}", ""]
    scaled = [field for field in payload if "scale" in field]
    if scaled:
        lines += ["namespace %s_scale {" % kind["name"], "enum : int32_t {"]
        lines += ["    %s = %d," % (constant(field["name"]), field["scale"]) for field in scaled]
        lines += ["};", "}", ""]
    return lines


def mirrors_test(layout):
    mirrored = {name: enum for name, enum in layout["enums"].items() if "mirrors" in enum}
    headers = sorted({enum["mirrors"]["header"] for enum in mirrored.values()})
    lines = [CLANG_FORMAT_OFF, "#include <cstdint>", ""]
    lines += ["#include \"%s\"" % header for header in headers]
    lines += ["", "#pragma GCC diagnostic error \"-Wswitch\"", "", "namespace skyblip {",
              "namespace {", "", "template <class Enumeration>",
              "constexpr uint32_t code(Enumeration value) {",
              "    return static_cast<uint32_t>(value);", "}", ""]
    for enum in mirrored.values():
        lines += exhaustive_switch(enum)
    for name, enum in mirrored.items():
        cpp = enum["mirrors"]["type"]
        for value in enum["values"]:
            lines.append("static_assert(code(%s::%s) == %d, \"%s %s\");" % (
                cpp, value["mirrors"], value["code"], name, value["name"]))
        lines += ["static_assert(on_the_wire(%s::%s), \"%s\");" % (
            cpp, enum["values"][0]["mirrors"], name), ""]
    lines += ["}", "}"]
    return "\n".join(lines) + "\n"


def decoder_tables(layout):
    enums = {}
    types = []
    for kind in layout["types"]:
        by_name = {field["name"]: field for field in kind["fields"]}
        fields = [decoder_field(field, by_name) for field in kind["fields"]
                  if field["kind"] != "vouch"]
        for field in fields:
            if field["kind"] == "enum8":
                enums[field["enum"]] = [value["name"]
                                        for value in layout["enums"][field["enum"]]["values"]]
        types.append((kind["code"], kind["name"], fields))
    ordered = {name: enums[name] for name in layout["enums"] if name in enums}
    return ordered, types


def decoder_field(field, by_name):
    out = {"name": field["name"], "kind": "u8" if field["kind"] == "code8" else field["kind"]}
    if field["kind"] in FLAG_KINDS:
        out["bit"] = field["bit"]
    else:
        out["at"] = field["at"]
    if field["kind"] == "enum8":
        out["enum"] = field["enum"]
    if field["kind"] == "bits":
        out["shift"] = field["shift"]
        out["mask"] = (1 << field["width"]) - 1
    if "scale" in field:
        out["scale"] = field["scale"]
    if "valid_when" in field:
        flag = by_name[field["valid_when"]]
        out["valid_bit"] = flag["bit"]
        if "valid_when" in flag:
            out["vouch_bit"] = by_name[flag["valid_when"]]["bit"]
    return out


def wrapped(opening, items, closing, indent):
    lines = []
    line = opening
    for at, item in enumerate(items):
        text = item + ("," if at < len(items) - 1 else closing)
        if len(line) + 1 + len(text) > LINE_WIDTH and line.strip() != opening.strip():
            lines.append(line)
            line = indent + text
        else:
            line += ("" if line.endswith(("(", "[")) else " ") + text
    return lines + [line]


def python_module(layout):
    enums, types = decoder_tables(layout)
    lines = ["ENUMS = {"]
    for name, names in enums.items():
        opening = "    %s: (" % json.dumps(name)
        lines += wrapped(opening, [json.dumps(item) for item in names], "),",
                         " " * len(opening))
    lines += ["}", "", "TYPES = ("]
    for code, name, fields in types:
        lines.append("    (%d, %s, (" % (code, json.dumps(name)))
        lines += ["        %s," % json.dumps(field) for field in fields]
        lines.append("    )),")
    return "\n".join(lines + [")"]) + "\n"


def javascript_module(layout):
    enums, types = decoder_tables(layout)
    lines = ["export const DIAGNOSTICS_LAYOUT = Object.freeze({", "  enums: {"]
    for name, names in enums.items():
        opening = "    %s: [" % name
        lines += wrapped(opening, [json.dumps(item) for item in names], "],", " " * len(opening))
    lines += ["  },", "  types: ["]
    for code, name, fields in types:
        lines.append("    [%d, %s, [" % (code, json.dumps(name)))
        lines += ["      %s," % json.dumps(field) for field in fields]
        lines.append("    ]],")
    return "\n".join(lines + ["  ],", "});"]) + "\n"


def main():
    return write(generated(load()))


if __name__ == "__main__":
    sys.exit(main())
