"""The companion link's frames: CBOR maps with numbered keys, to and from dicts keyed by name.

The numbers come from schemas/link.v1.json, read here as it stands. The codec is
the deterministic subset the registry describes: shortest forms, definite
lengths, unsigned keys in ascending order, no floats and no tags. Anything else
is refused on the way in, so a frame this decodes is one the unit would read.
"""
import json
import pathlib

REGISTRY_PATH = pathlib.Path(__file__).resolve().parent.parent / "schemas" / "link.v1.json"
COMMAND_KEY = 0
UNSIGNED, NEGATIVE, BYTES, TEXT, ARRAY, MAP, TAG, SIMPLE = range(8)
FALSE, TRUE = 0xF4, 0xF5


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


def encode_frame(fields, registry=REGISTRY):
    """A dict keyed by name, command first among them, as the CBOR bytes the unit reads."""
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
    if isinstance(command, int) and command in registry["by_code"]:
        return registry["by_code"][command]
    raise FrameError("no frame is commanded %r" % (command,))


def value_to_wire(key, value, registry):
    kind = key["type"]
    if kind == "enum" and isinstance(value, str):
        return enum_code(registry["enums"][key["enum"]], value)
    if kind == "bytes" and isinstance(value, str):
        value = bytes.fromhex(value)
    if kind == "bytes" and "bytes" in key and len(value) != key["bytes"]:
        raise FrameError("%s is %d bytes, not %d" % (key["name"], key["bytes"], len(value)))
    return value


def enum_code(enum, name):
    for value in enum["values"]:
        if value["name"] == name:
            return value["code"]
    raise FrameError("no value named %r in %s" % (name, [value["name"] for value in enum["values"]]))


def decode_frame(data, registry=REGISTRY):
    """CBOR bytes from the unit as a dict keyed by name; a key or code it does not know stays a number."""
    entries = decode(data)
    if not isinstance(entries, dict):
        raise FrameError("a frame is a map, not %s" % type(entries).__name__)
    frame = registry["by_code"].get(entries.get(COMMAND_KEY))
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
    if argument >= 2 ** 64:
        raise FrameError("%d does not fit 64 bits" % argument)
    for width, info in ((0, None), (1, 24), (2, 25), (4, 26), (8, 27)):
        if width == 0 and argument < 24:
            out.append(major << 5 | argument)
            return
        if width and argument < 2 ** (8 * width):
            out.append(major << 5 | info)
            out += argument.to_bytes(width, "big")
            return


def decode(data):
    reader = Reader(bytes(data))
    value = reader.item(top=True)
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
        if info > 27:
            raise FrameError("indefinite or reserved length")
        argument = int.from_bytes(self.take(1 << (info - 24)), "big")
        if argument < (24 if info == 24 else 1 << (8 << (info - 25))):
            raise FrameError("%d is not in its shortest form" % argument)
        return major, info, argument

    def item(self, top=False):
        major, info, argument = self.head()
        if major == UNSIGNED:
            return argument
        if major == NEGATIVE:
            return -1 - argument
        if major == BYTES:
            return self.take(argument)
        if major == TEXT:
            return self.take(argument).decode("utf-8")
        if major == ARRAY:
            return [self.item() for _ in range(argument)]
        if major == MAP and top:
            return self.entries(argument)
        if major == SIMPLE and info in (20, 21):
            return info == 21
        raise FrameError("major type %d, simple %d is outside the subset" % (major, info))

    def entries(self, count):
        out = {}
        last = -1
        for _ in range(count):
            major, _, key = self.head()
            if major != UNSIGNED or key <= last:
                raise FrameError("keys are unsigned and ascending, %r is not" % key)
            out[key] = self.item()
            last = key
        return out
