---
name: help-a-pilot
description: Find out what happened to a pilot who asks for help with the briefing app or a skyBlip device, from their support code or their device's address, and say what to tell them. Use when a pilot writes in, when someone gives a support code like ABCD-EFGH or a device like go-00012a, or when the insights list a stranded browser.
---

# Help a pilot

The briefing app keeps a journal of everything a browser does with a device, and posts it to skyBlip, so nobody has to ask the pilot to copy a report or describe their screen. `server/app/models/event/README.md`, "Answering a pilot", says what each event tells.

## Find their story

Ask for the support code, under Settings in the app, or the address on the device's glass. Then, from skyBlip's MCP server:

1. `list_events` with `support_code` (or `device`) and a `limit` of 500: the whole story, oldest first.
2. `get_unit` with the device, for a Go: its serial, when it was activated, the keys it registered, the firmware it signed and the firmware browsers reported.
3. `list_errors`, if the story holds a `browser.failed` or the server answered an error at the same time: its `fingerprint` leads to every pilot it hit.

## Read it

Line the events up by time and find the last step that went wrong, then what came before it:

- `browser.opened` with `bluetooth` false: their browser cannot reach a device. Tell them which browsers can, the words of `briefing.devices.no_bluetooth`.
- `bluetooth.chooser_closed` again and again: the chooser did not show the device. Its `detail` says whether Bluetooth was off. Otherwise the unit was asleep, out of range or connected to another phone.
- `device.noticed`: what the page told them, by its key. Read the key's words in `server/config/locales/briefing.en.yml` to know what they saw, and check whether they did what it said.
- `device.task_ended` with an outcome other than `granted`: what refused their install, save or capture, and whether the glass asked them (`confirmed`).
- `device.install_reported`: why an install did not take, with the unit's report. Its `cause` has words under `briefing.notices.install_report`.
- `device.read`: the firmware, image state, settings fallback and battery they had.

## Answer

Say in one or two sentences what happened and what they can do now, in their language, without the event names. If it is a bug, say so, and open an issue with the support code, the events that show it and the fix you expect, never their name or address. If the insights listed them as stranded and they have not written in, an admin decides whether to reach out.
