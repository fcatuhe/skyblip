# Captures

A pilot who helps measure a unit arms a diagnostics capture from its device page, lets the unit record on its own, then fetches the capture over Bluetooth and sends it from the same page (`app/javascript/skyblip/README.md`). The server keeps it as a row of `captures`, in a SQLite database of its own, `storage/production_captures.sqlite3`, so a capture's megabyte never sits in the app's database or in the events log. The body is the gzipped NDJSON `blip.py fetch --log diagnostics` writes, so the file an admin downloads is the file the bench tools read.

A capture is not an event. Its records hold the unit's track and every aircraft it heard, addresses included, which the events log never holds (`app/models/event/README.md`, Privacy). Receiving one is an event, `capture.received`, with the device, the profile, the counts and the firmware, so the story of a pilot who helped still lines up by support code. Each download is `capture.downloaded`, with the admin who made it, from the admin area or through `get_capture` on the admin MCP server, which hands an agent the same file as a base64 blob.

## What is kept, and how long

`Capture::Contents` reads the first lines of the body: the session note gives the session and the record count, and the boot record the firmware and the profile, which must match the one the page sent. A body that is not gzip, does not open on a session note whose session and record count are unsigned 32-bit integers, or is past `Capture::MAX_BYTES` is refused, and so is an unknown profile or a body sent as anything but a file. The firmware is kept only when the boot record's four version numbers fit the widths `schemas/diagnostics_layout.v1.json` gives them, since admins and their agents read it in the list and in `capture.received`. 4 MB is several times a full ring gzipped: 28,900 records of 24 bytes come to about 7 MB of NDJSON and under 1 MB once compressed.

The two periods are the ones the rest of the app keeps to. A full capture holds positions and other aircraft, so it is kept `config.x.tracker_lifetime`, 13 months. A power run or a flight run records the boot, the settings, the cell and the duty and nothing else, so it is kept `config.x.retention`, 25 months, long enough to compare one season's units with the last. The device page says both before the pilot sends. `Capture.prune` deletes what is past its `kept_until` at 2:45am (`config/recurring.yml`).

A device posts at most ten captures an hour from one address.
