---
name: improve-skyblip
description: Find what to improve in the briefing app, skyblip.eu or the firmware from how pilots use them, turn it into a change with a number to move, and check after the deploy that it moved. Use for a weekly review, when asked what to improve, why pilots get stuck, what breaks, or whether a shipped change helped.
---

# Improve skyBlip

skyBlip records every request, job, error, activation and every step a browser takes with a device as an event, with no words a pilot typed in it. This skill reads them, picks the change worth making, and comes back after the deploy to see whether it worked. `server/app/models/event/README.md` says what is recorded and how.

## Read the numbers

From skyBlip's MCP server, `https://skyblip.eu/mcp`, which an admin connects as `server/app/models/oauth/README.md` says. Every tool is for admins only:

- `get_insights` with `days` 7, 30 or 90: this period against the one before, the numbers of the Insights page.
- `list_errors` with `days`: what breaks, grouped into problems, newest first.
- `list_events`: the catalog first, then everybody's events as JSON lines. Narrow with `name` (`device.`, `browser.failed`), `device`, `support_code` or an error's `fingerprint`.
- `get_unit` with `device`: an activated unit's record.

When the MCP server is unavailable, from a laptop that deploys skyBlip, in `server/`:

```
mise run kamal app exec "bin/rails events:review DAYS=30"
mise run kamal app exec "bin/rails events:tail NAME=browser.failed LAST=200"
```

## Look for, in this order

1. **Pilots stranded.** `stranded` in the insights: browsers whose last step was a failure, by reason. Each reason with more than one browser is a place pilots give up. Read two or three of their journals before believing it.
2. **What breaks.** New problems in `list_errors`, on the server and in browsers, and `requests.server_errors` and `jobs.failed`. A problem new since a revision is that revision's.
3. **Installs.** `installs.failed_by_cause` against `installs.installed`, and `tasks.install.refused`. A cause from the install report is a firmware fix, `firmware/core/dfu/`. A refusal on the page is the briefing's words or flow.
4. **Bluetooth.** `devices.choosers_closed` against `devices.connected`, `devices.dropped_while_busy`, and the `link_lost`, `small_mtu`, `failed` and `timeout` notices. Their `detail` says what the browser answered, by browser and system in `browser.opened`.
5. **Browsers that cannot connect.** `browsers.without_bluetooth_on`: a pilot on a browser with no Web Bluetooth is stranded at the door, and the page that tells them where to go instead is the lever.
6. **The notices pilots read most.** `notices`: each key has its words in `server/config/locales/briefing.*.yml`. A notice read often is a flow to change or words to make clearer, and one that comes back after the pilot acted on it says the words did not work.
7. **The fleet.** `firmware.running`, `images_not_confirmed` and `settings_fallen_back`: how far the fleet lags a release, and units in a state the page should help them out of.
8. **The log itself.** On the Events page, an event never seen is either unused or no longer emitted.

## Turn a finding into a change

Write it down as: the number now, the period, the hypothesis, the change, and the number expected after. Then make the smallest change that tests it, through the `pull-request` skill, with a `Measure:` line in the body naming the number and the period to compare.

When a question cannot be answered from the events, the change is an event: one `Rails.event.notify` line or one `journal.record` where the fact happens, an entry in `server/app/models/event/catalog.yml`, and a test.

## Check it after the deploy

A week after the deploy, read `get_insights` again and compare the number with the one in the PR. Say on the PR whether it moved, and by how much. If it did not, say so, and revert or try the next hypothesis.

## Never

- Never put what a pilot typed, their callsign or a location into an event, a PR or an issue. Name a pilot by their support code or their device, never by anything else.
- Never read one pilot's journal except to understand a problem or to help them.
