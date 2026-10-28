"""The companion link's frames: CBOR maps with numbered keys, to and from dicts keyed by name.

The numbers come from schemas/link.v1.json, read here as it stands. The codec is
the grammar the registry describes: one map of unsigned keys in ascending order,
every head in its shortest form and its argument within 32 bits, definite lengths,
arrays of unsigned integers only, text in UTF-8, no floats, no tags, nothing nested
but those arrays. Anything else is refused on the way in, so a frame this decodes is
one the unit would read, and every value is checked against its key's type on the
way out, so a frame this encodes is one the unit could have been sent.
"""
import json
import pathlib
import re

REGISTRY_PATH = pathlib.Path(__file__).resolve().parent.parent / "schemas" / "link.v1.json"
COMMAND_KEY = 0
UNSIGNED, NEGATIVE, BYTES, TEXT, ARRAY, MAP, TAG, SIMPLE = range(8)
FALSE, TRUE = 0xF4, 0xF5
ARGUMENT_CEILING = 2 ** 32 - 1
HEX = re.compile(r"(?:[0-9a-fA-F]{2})*")


class FrameError(ValueError):
    """A frame the registry or the deterministic subset does not allow."""


def load_registry(path=REGISTRY_PATH):
    with open(path, encoding="utf-8") as source:
        registry = json.load(source)
    frames = {}
    for frame in registry["frames"]:
        frames[frame["command"]] = dict(frame)
    for frame in frames.values():
        keys = frames[frame["keys_of"]]["keys"] if "keys_of" in frame else frame["keys"]
        frame["by_name"] = {key["name"]: key for key in keys}
        frame["by_number"] = {key["number"]: key for key in keys}
    return {"frames": frames, "by_code": {frame["code"]: frame for frame in frames.values()},
            "enums": registry["enums"]}


REGISTRY = load_registry()


def is_unsigned(value):
    return type(value) is int and 0 <= value <= ARGUMENT_CEILING


def is_integer(value):
    return type(value) is int and -ARGUMENT_CEILING - 1 <= value <= ARGUMENT_CEILING


def is_text(value):
    if not isinstance(value, str):
        return False
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return True


ACCEPTS = {
    "unsigned": is_unsigned,
    "integer": is_integer,
    "boolean": lambda value: type(value) is bool,
    "text": is_text,
    "bytes": lambda value: isinstance(value, (bytes, bytearray)),
    "unsigned_array": lambda value: type(value) is list and all(map(is_unsigned, value)),
    "enum": is_unsigned,
}


def encode_frame(fields, registry=REGISTRY):
    """A dict keyed by name, command first among them, as the CBOR bytes the unit reads."""
    if not isinstance(fields, dict):
        raise FrameError("a frame is an object of names, not %s" % type(fields).__name__)
    fields = dict(fields)
    command = fields.pop("command", None)
    frame = frame_named(command, registry)
    entries = {COMMAND_KEY: frame["code"]}
    for name, value in fields.items():
        key = frame["by_name"].get(name)
        if key is None:
            raise FrameError("%s has no key %r" % (frame["command"], name))
        entries[key["number"]] = value_to_wire(key, value, registry)
    return encode(entries)


def frame_named(command, registry):
    if isinstance(command, str) and command in registry["frames"]:
        return registry["frames"][command]
    if is_unsigned(command) and command in registry["by_code"]:
        return registry["by_code"][command]
    raise FrameError("no frame is commanded %r" % (command,))


def value_to_wire(key, value, registry):
    kind = key["type"]
    wire = value
    if kind == "enum" and isinstance(value, str):
        wire = enum_code(registry["enums"][key["enum"]], value)
    if kind == "bytes" and isinstance(value, str):
        wire = bytes_of_hex(key, value)
    if not ACCEPTS[kind](wire) or ("bytes" in key and len(wire) != key["bytes"]):
        width = " of %d" % key["bytes"] if "bytes" in key else ""
        raise FrameError("%s takes %s%s, not %r" % (key["name"], kind, width, value))
    return wire


def bytes_of_hex(key, text):
    if not HEX.fullmatch(text):
        raise FrameError("%s takes bytes, as hex, not %r" % (key["name"], text))
    return bytes.fromhex(text)


def enum_code(enum, name):
    for value in enum["values"]:
        if value["name"] == name:
            return value["code"]
    raise FrameError("no value named %r in %s" % (name, [value["name"] for value in enum["values"]]))


def decode_frame(data, registry=REGISTRY):
    """CBOR bytes from the unit as a dict keyed by name; a key or code it does not know stays a number."""
    entries = decode(data)
    if not is_unsigned(entries.get(COMMAND_KEY)):
        raise FrameError("key 0 names the frame, and %r does not" % (entries.get(COMMAND_KEY),))
    frame = registry["by_code"].get(entries[COMMAND_KEY])
    out = {}
    for number, value in entries.items():
        if number == COMMAND_KEY:
            out["command"] = frame["command"] if frame else value
            continue
        key = frame["by_number"].get(number) if frame else None
        if key is None:
            out[str(number)] = value
        elif key["type"] == "enum":
            out[key["name"]] = enum_name(registry["enums"][key["enum"]], value)
        else:
            out[key["name"]] = value
    return out


def enum_name(enum, code):
    for value in enum["values"]:
        if value["code"] == code:
            return value["name"]
    return code


def more_follows(frame):
    """A multi-frame reply says so in every frame but its last."""
    return frame.get("more") is True


def printable(frame):
    """A decoded frame as JSON text, byte strings in hex, for a person to read."""
    return json.dumps({name: value.hex() if isinstance(value, bytes) else value
                       for name, value in frame.items()}, separators=(",", ":"))


def encode(value):
    out = bytearray()
    put(out, value)
    return bytes(out)


def put(out, value):
    if isinstance(value, bool):
        out.append(TRUE if value else FALSE)
    elif isinstance(value, int):
        if value >= 0:
            head(out, UNSIGNED, value)
        else:
            head(out, NEGATIVE, -1 - value)
    elif isinstance(value, str):
        data = value.encode("utf-8")
        head(out, TEXT, len(data))
        out += data
    elif isinstance(value, (bytes, bytearray)):
        head(out, BYTES, len(value))
        out += value
    elif isinstance(value, list):
        head(out, ARRAY, len(value))
        for item in value:
            put(out, item)
    elif isinstance(value, dict):
        head(out, MAP, len(value))
        for key in sorted(value):
            put(out, key)
            put(out, value[key])
    else:
        raise FrameError("cannot encode %s" % type(value).__name__)


def head(out, major, argument):
    if argument > ARGUMENT_CEILING:
        raise FrameError("%d does not fit 32 bits" % argument)
    for width, info in ((0, None), (1, 24), (2, 25), (4, 26)):
        if width == 0 and argument < 24:
            out.append(major << 5 | argument)
            return
        if width and argument < 2 ** (8 * width):
            out.append(major << 5 | info)
            out += argument.to_bytes(width, "big")
            return


def decode(data):
    reader = Reader(bytes(data))
    value = reader.frame()
    if reader.at != len(reader.data):
        raise FrameError("%d bytes after the frame" % (len(reader.data) - reader.at))
    return value


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def take(self, count):
        if self.at + count > len(self.data):
            raise FrameError("truncated")
        piece = self.data[self.at:self.at + count]
        self.at += count
        return piece

    def head(self):
        initial = self.take(1)[0]
        major, info = initial >> 5, initial & 0x1F
        if info < 24:
            return major, info, info
        if info > 26:
            raise FrameError("an argument past 32 bits, or an indefinite or reserved length")
        argument = int.from_bytes(self.take(1 << (info - 24)), "big")
        if argument < (24 if info == 24 else 1 << (8 << (info - 25))):
            raise FrameError("%d is not in its shortest form" % argument)
        return major, info, argument

    def frame(self):
        major, _, count = self.head()
        if major != MAP:
            raise FrameError("a frame is a map, not major type %d" % major)
        if count > len(self.data) - self.at:
            raise FrameError("truncated")
        out = {}
        last = -1
        for _ in range(count):
            major, _, key = self.head()
            if major != UNSIGNED or key <= last:
                raise FrameError("keys are unsigned and ascending, %r is not" % key)
            out[key] = self.value()
            last = key
        return out

    def value(self):
        major, info, argument = self.head()
        if major == UNSIGNED:
            return argument
        if major == NEGATIVE:
            return -1 - argument
        if major == BYTES:
            return self.take(argument)
        if major == TEXT:
            return self.text(argument)
        if major == ARRAY:
            return [self.unsigned_item() for _ in range(argument)]
        if major == SIMPLE and info in (20, 21):
            return info == 21
        raise FrameError("major type %d, simple %d is outside the link's grammar" % (major, info))

    def text(self, count):
        try:
            return self.take(count).decode("utf-8")
        except UnicodeDecodeError as error:
            raise FrameError("text that is not UTF-8: %s" % error) from None

    def unsigned_item(self):
        major, _, argument = self.head()
        if major != UNSIGNED:
            raise FrameError("an array holds unsigned integers, not major type %d" % major)
        return argument
