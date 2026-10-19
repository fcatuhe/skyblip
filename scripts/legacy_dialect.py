"""The dialect units before #151 speak, translated through schemas/legacy_dialect.v1.json.

The table is the only copy: schemas/legacy_dialect.v1.json says what its sections mean.
`current()` reads any frame or NDJSON line into today's names and is safe on one already
in them. `legacy()` writes a frame back for a unit that has not updated, and `Dialect`
remembers which one a connected unit speaks.
"""
import json
import os

TABLE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "schemas", "legacy_dialect.v1.json")

with open(TABLE_PATH, encoding="utf-8") as table_file:
    TABLE = json.load(table_file)

LINK = TABLE["link"]
RECORDS = TABLE["records"]


def current(section, frame):
    out = translated(frame, section.get("keys", {}), section.get("values", {}))
    entry = entry_for(section, out.get(section["select"]))
    if entry is None:
        return out
    return translated(out, entry.get("keys", {}), entry.get("values", {}))


def legacy(section, frame):
    out = dict(frame)
    entry = entry_for(section, out.get(section["select"]))
    if entry is not None:
        out = reversed_translation(out, entry.get("keys", {}), entry.get("values", {}))
    return reversed_translation(out, section.get("keys", {}), section.get("values", {}))


def entry_for(section, selected):
    for entry in section.get("frames", ()):
        if selected in entry["when"]:
            return entry
    return None


def translated(frame, keys, values):
    out = {keys.get(key, key): value for key, value in frame.items()}
    for key, spellings in values.items():
        if isinstance(out.get(key), str):
            out[key] = spellings.get(out[key], out[key])
    return out


def reversed_translation(frame, keys, values):
    out = dict(frame)
    for key, spellings in values.items():
        if isinstance(out.get(key), str):
            back = {new: old for old, new in spellings.items()}
            out[key] = back.get(out[key], out[key])
    back_keys = {new: old for old, new in keys.items()}
    return {back_keys.get(key, key): value for key, value in out.items()}


class Dialect:
    """Which dialect one connected unit speaks, decided by the first frame it sends."""

    def __init__(self):
        self.legacy = None

    def receive(self, frame):
        if self.legacy is None:
            if any(key in frame for key in LINK["detect"]["legacy"]):
                self.legacy = True
            elif any(key in frame for key in LINK["detect"]["current"]):
                self.legacy = False
        return current(LINK, frame)

    def send(self, frame):
        if self.legacy is None:
            return {**legacy(LINK, frame), **frame}
        if self.legacy:
            return legacy(LINK, frame)
        return dict(frame)
