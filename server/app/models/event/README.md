# Events

Everything skyBlip does that someone may later want to understand lands as a row of `events`, in a SQLite database of its own, `storage/production_events.sqlite3`, so the log never competes with the app's writes. The server's half is fed by Rails' structured event reporter: `Rails.event.notify` anywhere in the app, and `Event` is the one subscriber that writes it down, through `config/initializers/events.rb`. The browser's half is each browser's journal, posted to `/events`. `catalog.yml` holds every event with what it says and the question it answers, and `test/models/event/catalog_test.rb` fails when an event is emitted, from Ruby or from the briefing's scripts, without an entry, or an entry is no longer emitted. The Events page and `list_events` read it.

## What is recorded

- **Domain events**, named `<model>.<past tense verb>`, emitted by the models where the fact happens: a unit enrolled, a key bound, promoted or revoked, a firmware a unit signed, an activation or an attestation refused, a user signed in or created, an agent connected. They carry ids, states, versions and counts.
- **Traces** from the framework: every request (`action_controller.request_completed`, with controller, action, status and duration), every job run, and a `rescue_from` that answered. The framework's other events, SQL and renders among them, are not kept. A post of a browser's journal is kept only when it was refused, since every open briefing posts every few seconds.
- **Errors** reported to `Rails.error`, which Rails does for every exception a request or a job raises: `error.reported`, below.
- **The browser's journal**: what only the page sees, below.

Each server row carries its context: `request_id`, `user_id` when an admin is signed in, the method, path and format of the request, and the `device` its URL names. A request from the briefing's scripts also carries the browser's support code in the `X-Briefing-Browser` header, so the activation the server refused is tied to the browser that asked. `device` and `browser` are indexed columns of their own.

An event emitted inside a transaction is written once that transaction commits, and never when it rolls back. The log keeps to the two periods the CNIL sets for audience measurement, which the analytics keep to too: traces and the browser's views and clicks are kept 13 months, `config.x.tracker_lifetime`, and everything else 25 months, `config.x.retention`. So this flying season can be compared with the last one, a pilot who comes back after a winter layup still has last season's story, and an error that returns after the winter is not taken for a new one. Both are pruned at 2:30am by `config/recurring.yml`.

## The browser's journal

`app/javascript/skyblip/journal.js` keeps an outbox in `localStorage`, `skyblip.journal.v1`, and posts it to `/events` in batches of 50, two seconds after an entry, when the page is hidden, when the browser comes back online, and on the next page load. An entry leaves the outbox only once the server answered, so a phone offline for a weekend at the airfield sends its journal when it finds a network, and the outbox keeps its newest 500 entries. A post is a `fetch` with `keepalive`, which outlives a closing page. Each entry has a uuid, and the `uuid` column is unique, so an entry posted twice is kept once. A browser that keeps no site data keeps its journal in the page until it closes.

Each entry carries the browser's support code, `browser`, eight Crockford letters minted on the first entry and kept in `localStorage` as `skyblip.browser.v1` for 13 months from then, never renewed, after which the next entry mints another, the uuid of the page load, `page`, and `seq`, which counts the page's entries from one, so a number missing from the log is an entry lost. It carries the path without its query, the `device` it is about, and `at`, the moment the browser saw it, which becomes `occurred_at`, held between thirty days ago and now in case the phone's clock is wrong.

The pilot reads the support code as `ABCD-EFGH` under Settings and beside a failed install, and gives it when they write in. Clearing the site's data mints a new one.

- `controllers/journal_controller.js` sits on the body of every page. On the briefing's it records `browser.opened` once per load, with what the browser can do, `browser.viewed` on every page, and `browser.clicked`. On every page of the site, the public ones and the admin's included, it records `browser.failed`: a script error, an unhandled rejection, or an error Stimulus caught in a controller, which `controllers/application.js` hands over as `stimulus:failed`. A public page mints a support code only once it fails.
- `skyblip/device_journal.js` watches the state of every device the fleet holds, and turns its changes into `device.*` entries: a connection and how long it took, a link that dropped and in which phase, what the device runs and holds once read, every notice the pilot read, a task asked, granted or refused, an install that came back on its new version, and the install report of one that did not. `fleet.js` records the chooser closed empty or refused, as `bluetooth.*`, and a device forgotten. The activation page's states become `activation.*`.

`Event::Journal` takes the post apart at the boundary: it keeps only the events the catalog marks `from: browser`, only the keys each one names, each cut to its type, a word to 100 characters, a message to 300, a path without its query or its host. Anything else is dropped. The endpoint takes sixty posts a minute from one address.

## Privacy

An event holds ids, states, versions, counts, durations and error messages, never what a pilot typed, the words on a button, a callsign, an address or a location. A browser is named by its family, major version and system, never its user agent, and its address is not kept. A control is named by its Stimulus action, its id or the path it leads to. The keys `filter_parameters` matches are masked in the server's events anyway. A device is named by its slug, which is its ADS-L address, the one it transmits. An error's message and a Bluetooth failure's `detail` are kept, cut short, because they are what explains the failure, and they could quote what failed. Only admins read events, on the admin pages and through the MCP server, and an agent sends what it reads to its AI provider.

## Errors

`Event::ErrorReport` turns a reported error into `error.reported`: its class, its message cut to 300 characters, its cause, the first fifteen frames of the app's backtrace, or of the gems' when the app has none, the controller or job that raised it, whether it was handled, and `revision`, the image Kamal deployed. Its `fingerprint` is the class and the method of the first frame without its line number, so the same error from the same place is one problem across edits and deploys. A script's `browser.failed` is fingerprinted by its kind, class, message with its numbers taken out, and its file without the asset digest.

`Event::Problem` groups both by fingerprint: how many times, on how many devices and browsers, first and last seen, and whether it is new in the period. The Errors page and `list_errors` read it. Nothing alerts yet: the app has no mailer, so a new problem waits for someone to look.

## Adding an event

On the server, one `Rails.event.notify` line where the fact becomes true, in the model, with `device:` when there is one. In the browser, one `journal.record(name, properties, { device })`, or a line in `device_journal.js` when it comes from a device's state. Then an entry in `catalog.yml`, with `from: browser` and its `keys` for a browser event, a type for any new key in `Event::Journal`, and a test. Keep to ids, enum values, counts, durations with their unit in the key (`connect_ms`), booleans and class names. A request or a job run is already a trace: an event is for what the trace cannot say, the outcome and what it replaced.

## Answering a pilot

Ask for the support code, or the device's address. `list_events` with `support_code` or `device`, the Events page with either, or `bin/rails events:tail CODE=ABCD-EFGH` lines the story up by time.

- **Could the browser reach a device at all?** `browser.opened`: `bluetooth` false is a browser without Web Bluetooth, such as any browser on an iPhone.
- **Did the chooser show the device?** `bluetooth.chooser_closed`: its `detail` tells a pilot who cancelled from a Bluetooth adapter that is off.
- **Did it connect, and stay connected?** `device.connected` and how long it took, `device.disconnected` and the phase it dropped in.
- **What was the device like?** `device.read`: firmware, track, image state, the signing key it runs, sensor hub blob, settings fallback, trust, activation, flight state and battery.
- **What did the pilot read?** `device.noticed`, every notice the page showed, by the key `config/locales/briefing.*.yml` words it with.
- **What did they ask, and how did it end?** `device.task_started` and `device.task_ended`, with the outcome, whether the glass asked and how far an upload got. `device.installed` for an install that took, `device.install_reported` for one that did not, with the cause the page showed and the unit's report. `device.upload_refused` for an image the unit refused once it landed, with what it found in slot 1 and the keys on both sides.
- **What broke?** `browser.failed` in the page, and `error.reported` on the server, tied to the browser when it asked.

## Reading it

`Event::Review` computes the numbers for a period, against the period before: browsers and those without Bluetooth, devices connected and how long it takes, links dropped mid-task, choosers closed empty, the notices pilots read most, tasks granted and refused, installs started, succeeded and failed by cause, the firmware the fleet runs, activations and their refusals, the browsers left stranded, errors, the slowest requests, failing jobs and agents' calls. A browser is stranded when its last step in the period was a failure: a notice other than a result, a task refused, a chooser closed empty or refused, an activation refused. Each is a pilot who may have given up there, listed with their support code so an admin can look before they write in.

- **Insights**, in the admin area: the numbers for 7, 30 or 90 days.
- **Errors**: the problems, and each one's frames and latest occurrences.
- **Events**: the catalog with how often each event happened and when it was last seen, an event never seen standing out, and everybody's events narrowed by name, device, support code, fingerprint or request. A unit's page links its timeline.
- **The MCP server**: `get_insights`, `list_errors`, `list_events` and `get_unit`, for admins only, each read of events logged as `event.listed`. `.agents/skills/improve-skyblip/SKILL.md` says how an agent turns them into changes, and `.agents/skills/help-a-pilot/SKILL.md` how it answers a pilot.
- **The command line**, through Kamal in production: `bin/rails events:review DAYS=30`, `bin/rails events:tail NAME=device.noticed`, `DEVICE=`, `CODE=`, `FINGERPRINT=` and `LAST=`.
