#!/usr/bin/env python3
"""Write the files generated from schemas/link.v1.json, the companion link's registry.

    python3 scripts/link_registry.py

Stdlib only. scripts/check_link_registry.py runs the same generation and fails
on any file that differs from what this would write.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "schemas" / "link.v1.json"
FIRMWARE_HEADER = "firmware/core/comms/link_registry.h"
NAMES_HEADER = "firmware/simulator/world/link_names.h"
MIRRORS_TEST = "firmware/test/core/test_link_mirrors.cpp"
JAVASCRIPT_MODULE = "server/app/javascript/skyblip/link_registry.js"

COMMAND_KEY = {"name": "command", "number": 0, "type": "enum", "enum": "command"}
TYPES = ("unsigned", "integer", "boolean", "text", "bytes", "unsigned_array", "enum")
ENDPOINTS = ("config", "log")
SENDERS = ("host", "unit", "both")
CPP_TYPES = {"unsigned": "Unsigned", "integer": "Integer", "boolean": "Boolean", "text": "Text",
             "bytes": "Bytes", "unsigned_array": "UnsignedArray", "enum": "Enum"}


def load(path=REGISTRY):
    with open(path, encoding="utf-8") as source:
        return normalised(json.load(source))


def normalised(registry):
    """The registry with every frame's key table resolved and the command enum spelled out."""
    frames = registry["frames"]
    enums = dict(registry["enums"])
    refuse_duplicates("command", [frame["command"] for frame in frames])
    refuse_duplicates("command code", [frame["code"] for frame in frames])
    by_name = {frame["command"]: frame for frame in frames}
    resolved = []
    for frame in frames:
        if frame["endpoint"] not in ENDPOINTS or frame["sender"] not in SENDERS:
            raise ValueError("%s: endpoint or sender is not one of %s, %s"
                             % (frame["command"], ENDPOINTS, SENDERS))
        owner = by_name[frame["keys_of"]] if "keys_of" in frame else frame
        if "keys_of" in owner and owner is not frame:
            raise ValueError("%s: keys_of names %s, which shares a table itself"
                             % (frame["command"], owner["command"]))
        keys = [COMMAND_KEY] + owner["keys"]
        check_keys(frame["command"], keys, enums)
        entry = {"command": frame["command"], "code": frame["code"], "endpoint": frame["endpoint"],
                 "sender": frame["sender"], "keys": keys}
        if "keys_of" in frame:
            entry["keys_of"] = frame["keys_of"]
        if "description" in frame:
            entry["description"] = frame["description"]
        resolved.append(entry)
    enums["command"] = {"values": [{"name": frame["command"], "code": frame["code"]}
                                   for frame in frames]}
    for name, enum in enums.items():
        refuse_duplicates("%s value" % name, [value["name"] for value in enum["values"]])
        refuse_duplicates("%s code" % name, [value["code"] for value in enum["values"]])
    return {"title": registry["title"], "description": registry["description"],
            "frames": resolved, "enums": enums}


def check_keys(command, keys, enums):
    refuse_duplicates("%s key" % command, [key["name"] for key in keys])
    numbers = [key["number"] for key in keys]
    if numbers != sorted(set(numbers)):
        raise ValueError("%s: key numbers %s are not unique and ascending" % (command, numbers))
    for key in keys:
        if key["type"] not in TYPES:
            raise ValueError("%s.%s: type %s is not one of %s"
                             % (command, key["name"], key["type"], TYPES))
        if key["type"] == "enum" and key["enum"] != "command" and key["enum"] not in enums:
            raise ValueError("%s.%s: no enum %s" % (command, key["name"], key["enum"]))


def refuse_duplicates(what, items):
    seen = set()
    for item in items:
        if item in seen:
            raise ValueError("%s %r appears twice" % (what, item))
        seen.add(item)


def pascal(name):
    return "".join(part.capitalize() for part in name.split("_"))


def constant(name):
    return "k" + pascal(name)


def generated(registry):
    """Every generated file, relative to the repository root, and its text."""
    return {
        FIRMWARE_HEADER: firmware_header(registry),
        NAMES_HEADER: names_header(registry),
        MIRRORS_TEST: mirrors_test(registry),
        JAVASCRIPT_MODULE: javascript_module(registry),
    }


def firmware_header(registry):
    lines = ["#ifndef SKYBLIP_CORE_COMMS_LINK_REGISTRY_H",
             "#define SKYBLIP_CORE_COMMS_LINK_REGISTRY_H", "", "#include <cstddef>",
             "#include <cstdint>", "",
             "namespace skyblip::link {", "",
             "constexpr uint32_t kCommandKey = %d;" % COMMAND_KEY["number"], ""]
    for name in ("command", "reason"):
        lines += enum_class(pascal(name), registry["enums"][name]["values"])
    for frame in registry["frames"]:
        if "keys_of" in frame or len(frame["keys"]) == 1:
            continue
        lines += ["namespace %s_key {" % frame["command"], "enum : uint32_t {"]
        for key in frame["keys"][1:]:
            lines.append("    %s = %d," % (constant(key["name"]), key["number"]))
        lines += ["};", "}", ""]
        widths = [key for key in frame["keys"] if "bytes" in key]
        if widths:
            lines.append("namespace %s_bytes {" % frame["command"])
            lines += ["constexpr size_t %s = %d;" % (constant(key["name"]), key["bytes"])
                      for key in widths]
            lines += ["}", ""]
    for frame in registry["frames"]:
        if "keys_of" in frame:
            lines += ["namespace %s_key = %s_key;" % (frame["command"], frame["keys_of"]), ""]
    lines += ["}", "", "#endif"]
    return "\n".join(lines) + "\n"


def enum_class(name, values):
    lines = ["enum class %s : uint8_t {" % name]
    for value in values:
        lines.append("    %s = %d," % (pascal(value["name"]), value["code"]))
    return lines + ["};", ""]


def names_header(registry):
    lines = ["#ifndef SKYBLIP_SIMULATOR_WORLD_LINK_NAMES_H",
             "#define SKYBLIP_SIMULATOR_WORLD_LINK_NAMES_H", "", "#include <cstdint>", "",
             "#include \"core/events/link.h\"", "", "namespace skyblip::simulator::link_names {", "",
             "enum class Type : uint8_t { %s };" % ", ".join(CPP_TYPES[kind] for kind in TYPES), "",
             "struct Value {", "    const char* name;", "    uint32_t code;", "};", "",
             "struct Enumeration {", "    const Value* values;", "    int count;", "};", "",
             "struct Key {", "    const char* name;", "    uint32_t number;", "    Type type;",
             "    const Enumeration* enumeration;", "    int bytes;", "};", "",
             "struct Frame {", "    const char* command;", "    uint32_t code;",
             "    events::Endpoint endpoint;", "    const Key* keys;", "    int count;", "};", ""]
    for name, enum in registry["enums"].items():
        lines.append("inline constexpr Value k%sValues[] = {" % pascal(name))
        for value in enum["values"]:
            lines.append("    {\"%s\", %d}," % (value["name"], value["code"]))
        lines += ["};", "inline constexpr Enumeration k%s{k%sValues, %d};"
                  % (pascal(name), pascal(name), len(enum["values"])), ""]
    for frame in registry["frames"]:
        if "keys_of" in frame:
            continue
        lines.append("inline constexpr Key k%sKeys[] = {" % pascal(frame["command"]))
        for key in frame["keys"]:
            enumeration = "&k%s" % pascal(key["enum"]) if key["type"] == "enum" else "nullptr"
            lines.append("    {\"%s\", %d, Type::%s, %s, %d}," % (
                key["name"], key["number"], CPP_TYPES[key["type"]], enumeration,
                key.get("bytes", 0)))
        lines += ["};", ""]
    lines.append("inline constexpr Frame kFrames[] = {")
    for frame in registry["frames"]:
        table = pascal(frame.get("keys_of", frame["command"]))
        lines.append("    {\"%s\", %d, events::Endpoint::%s, k%sKeys, %d}," % (
            frame["command"], frame["code"], pascal(frame["endpoint"]), table,
            len(frame["keys"])))
    lines += ["};", "", "}", "", "#endif"]
    return "\n".join(lines) + "\n"


def mirrors_test(registry):
    mirrored = {name: enum for name, enum in registry["enums"].items() if "mirrors" in enum}
    headers = sorted({enum["mirrors"]["header"] for enum in mirrored.values()})
    lines = ["#include <cstdint>", ""] + ["#include \"%s\"" % header for header in headers]
    lines += ["", "#pragma GCC diagnostic error \"-Wswitch\"", "", "namespace skyblip {", "",
              "template <class Enumeration>", "constexpr uint32_t code(Enumeration value) {",
              "    return static_cast<uint32_t>(value);", "}", "", "namespace {", ""]
    for enum in mirrored.values():
        lines += exhaustive_switch(enum)
    lines += ["}", ""]
    for name, enum in mirrored.items():
        cpp = enum["mirrors"]["type"]
        for value in enum["values"]:
            lines.append("static_assert(code(%s::%s) == %d, \"%s %s\");" % (
                cpp, value["mirrors"], value["code"], name, value["name"]))
        first = "%s::%s" % (cpp, enum["values"][0]["mirrors"])
        lines += ["static_assert(on_the_wire(%s), \"%s\");" % (first, name), ""]
    lines += ["}"]
    return "\n".join(lines) + "\n"


def exhaustive_switch(enum):
    """A switch -Wswitch refuses to compile once the C++ enum holds a value the registry lacks."""
    cpp = enum["mirrors"]["type"]
    lines = ["constexpr bool on_the_wire(%s value) {" % cpp, "    switch (value) {"]
    lines += ["        case %s::%s:" % (cpp, value["mirrors"]) for value in enum["values"]]
    lines.append("            return true;")
    off = enum["mirrors"].get("off_the_wire", [])
    lines += ["        case %s::%s:" % (cpp, enumerator) for enumerator in off]
    if off:
        lines.append("            return false;")
    return lines + ["    }", "    return false;", "}", ""]


def javascript_module(registry):
    body = json.dumps(undocumented(registry), indent=2, ensure_ascii=False)
    return "export const LINK_REGISTRY = Object.freeze(%s);\n" % body


def undocumented(node):
    """The registry without its descriptions, which no page reads."""
    if isinstance(node, dict):
        return {name: undocumented(value) for name, value in node.items() if name != "description"}
    if isinstance(node, list):
        return [undocumented(item) for item in node]
    return node


def written(registry, clang_format):
    """Every generated file and its text as committed: the C++ ones through clang-format."""
    return {relative: formatted(relative, text, clang_format) if is_cpp(relative) else text
            for relative, text in generated(registry).items()}


def is_cpp(relative):
    return pathlib.PurePath(relative).suffix in (".h", ".cpp")


def formatted(relative, text, clang_format):
    result = subprocess.run([clang_format, "--style=file", "--assume-filename=%s" % (ROOT / relative)],
                            input=text, capture_output=True, text=True, check=True)
    return result.stdout


def stale(expected, read):
    """The generated files whose committed text is not, byte for byte, what the registry writes."""
    return [relative for relative, text in expected.items() if read(relative) != text]


def committed(relative):
    path = ROOT / relative
    return path.read_text(encoding="utf-8") if path.exists() else None


def formatter():
    found = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if found is None:
        raise SystemExit("no clang-format: set CLANG_FORMAT, the C++ files are compared formatted")
    return found


def main():
    for relative, text in written(load(), formatter()).items():
        path = ROOT / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print("wrote %s" % relative)
    return 0


if __name__ == "__main__":
    sys.exit(main())
