# comms

The companion link, from the side that does not know what Bluetooth is. A platform fills `ports::Link`, and everything here speaks endpoints, sessions and CBOR frames with numbered keys.

What the link and its schema decide lives here: the frames, the claim, the sessions and `ConfigChannel` (`config.h`), which admits a frame, sends a reply to the peer that asked, holds one the link refused with `WouldBlock` and streams a report. What a product answers on the config endpoint is `common::ConfigService` in `products/common/services/`: the prompts, the flight gate, the upload window, pairing, captures, activation and trusted browsers (`../../products/common/README.md`). The rules below are its rules too, because they are what a phone sees on the wire.

## The wire

Every frame on the config, log and peer endpoints is one CBOR map (RFC 8949) in one write or one notification: unsigned keys in ascending order, each head in its shortest form with an argument of at most 32 bits, definite lengths, no floats, no tags, and nothing nested but an array of unsigned integers. Key 0 is `command`, whose code names the frame and so its key table. [`schemas/link.v1.json`](../../../schemas/link.v1.json) is the one place the numbers are written, and `scripts/link_registry.py` generates `link_registry.h` and the clients' tables from it. The device carries no names: `core/util/cbor.h` writes and reads numbers.

`cbor::Writer` refuses rather than truncates: an entry that does not fit whole, a key out of order, an integer past 32 bits or text that is not UTF-8 refuses the frame, `finish()` answers 0, and the sender counts a `link_drops` rather than send a frame with a key missing. `cbor::Reader` checks the whole frame before anything reads it, and a frame it refuses has no command, so the config endpoint answers `no_command`. A command carrying a key its frame does not list, or a bare ask carrying any key, is refused `invalid` before any handler reads it (`link::host_keys()`). A `set` ignores the read-only keys of the config reply: `version`, `address`, `address_mapping_table`, `alarm_from_go` and `annunciator_fitted`. Reasons, the words a refusal or an acknowledgement names, are one enumeration for every frame, `link::Reason`.

## Three endpoints, two shapes of send

`events::Endpoint` splits a phone's link by owner. NMEA, the traffic picture, is broadcast: every subscribed central gets the same bytes, and `ports::Link::send()` reports Ok when one of them took it, so a phone with full buffers cannot end the pass for the tablet beside it. It answers `Unsubscribed` when centrals are connected but none subscribed to the endpoint, and the NMEA service then lets the pass go uncounted, as it would with nobody connected. `send_to()` answers a central that has not subscribed yet with `WouldBlock`, because a central may write before it subscribes, so its answer waits as for buffers. Config and Log are conversations, sent by `send_to()` to the connection whose request they answer (Sessions). Peer is the link between a paired Go and Bravo (The peer endpoint).

So `payload_bytes()` with no argument is the smallest payload any connected central negotiated, because one NMEA frame is formatted once for all: an iPhone at ATT_MTU 185 beside an Android at 247 pulls every broadcast frame down to 182 bytes. A conversation is sized for its own session, by `payload_bytes_to()`.

iOS always starts the MTU exchange, but Android only when the app calls `requestMtu()`. So right after a connection, `connected()` in `hardware/platform/zephyr/link.cpp` sends the unit's own Exchange MTU Request as GATT client (`CONFIG_BT_GATT_CLIENT`), which every central must answer, and records it (Measuring what links settle on). Zephyr's `CONFIG_BT_GATT_AUTO_UPDATE_MTU` would only log the outcome. A link still at 23 is a central that answered 23 or refused, and the replies it cannot carry are answered `small_mtu`.

## Sessions

`LinkSessions` is the session table on both platforms, driven by Zephyr's connection callbacks on silicon and by `raise_link()`/`drop_link()` in the host suite.

A session id is the controller's slot, `bt_conn_index() + 1`, so the next phone may reuse it. `LinkSessions` therefore numbers every connection once, the platform stamps each inbound frame with that number under its queue lock, and `ports::Link::connection()` answers the number a session id names now. `ConfigChannel::from_live_connection()` drops a frame whose connection has gone before it can take the claim or stage a prompt.

Every reply is addressed to an `events::Peer`, the session id and the connection number together. A held reply, a chunked log read and a log erase's verdict keep the peer of the request they answer. Every other late verdict answers the claim holder, and the holder's disconnect drops it. `ports::Link::send_to()` answers `Down` once the peer's connection is gone, so a reply never reaches the next phone under the same id. On Zephyr the comparison and `bt_conn_ref()` share one critical section under `g_lock`.

`kMaxSessions` must never exceed `CONFIG_BT_MAX_CONN`, because the controller admits centrals: a larger table has rows that never fill, and a smaller one refuses a central the radio accepted. A refused connect never reaches the bus, and `refused()` counts it.

A paired Go reserves one connection for its Bravo: `reserve_peer(true)` lowers `phone_ceiling()` by one whether or not Bravo is connected, and the platform advertises on `admits()`, so the Go takes two phones and refuses a third. A reservation is refused `Full` while phones hold every connection. The Go's own link to Bravo is not a session, so the NMEA broadcast never reaches Bravo. On Bravo the paired Go is a session: `reserve_peer_for()` keeps its slot by the Go's address, and Bravo keeps advertising while that Go is away.

Events are one ordered queue, because an Up read before the Down that came first leaves a service pushing at a link that is gone.

## The claim

`LinkClaim` grants config and log to the first session that writes a command, until it disconnects or releases it, because two apps writing settings disagree about what is stored. Another session's command is refused with a reason naming the state. Only the holder's disconnect cancels a pending prompt and closes an upload window.

The claim is not access control. `CONFIG_BT_SMP` is off on every product (`products/common/prj.conf`), so no identity stands behind a session id. What stops a hostile app is the MCUboot signature on the image and the gesture on the device, or a browser key that gesture once approved (Trusted browsers).

## One prompt at a time

A prompt takes the glass from the radar and both controls from the pilot, and anyone in range can connect. So two rules hold for every command that stages a prompt, `set`, `dfu`, `recovery`, `power_off`, `gnss_cold_start`, `register`, `capture`, `pair` and the log endpoint's `erase`, even when a trusted browser or Bravo stages none:

- While a prompt stands, such a command is refused `prompt_pending`, and the standing prompt keeps its operation, its detail and its deadline. No command renews a prompt, so it expires `kPromptCeilingMs`, thirty seconds, after it was staged.
- After the pilot refuses a prompt with one press, every such command from that connection is refused `refused_on_device` until it drops. Only that press counts, not an expiry, a take-off, a dropped link or the press that stops an upload.

`ConfigService::prompt_refused()` (`config_prompt.cpp`) asks both after the claim and the key table and before the command's own checks, so the answer does not depend on what the command carries. A dropped link takes the prompt, so a new connection can stage another before the product's next pass sees `pending()` go back to `None`. A product that reads a new `prompts_staged()` count treats the prompt as new: it disarms its gesture and asks again (#245).

## The flight gate

Every command that changes the unit or reads its log is refused `in_flight` while the flight state is `Airborne`, and goes ahead on `Ground` and `Unknown`: `set`, `dfu`, `recovery`, `power_off`, `gnss_cold_start`, `capture`, `pair`, every log request, and `challenge`, `register`, `unlock` and `attest` on an activated unit (Activation). A trusted browser's `set` stages no prompt, so it goes ahead in flight too. The gate reads the one value `GroundLatch` publishes (`core/flight/README.md`). A unit with no fix since boot is `Unknown`, so a bench indoors installs, sets and offloads, and the latch holds `Airborne` through a fix lost in flight.

The MCUmgr upload hook asks `upload_allowed()`, which takes an image only inside the window a press opened, because SMP is unauthenticated. A take-off does not close the window, since a fast taxi once aborted a confirmed upload. An image that lands in flight is held, and installs once the unit leaves `Airborne` and no other prompt stands, because the swap takes it dark for 15 to 25 s. The holder's dropped link drops a held image.

A take-off cancels a staged prompt, answered `in_flight`, except an unactivated unit's `register`. Otherwise a stranger in range could hide live traffic by staging a prompt every thirty seconds. `Unknown` hides none, because `GroundLatch` reports it only while no fix and no take-off are latched, and `AlarmGrader` grades traffic only with a fix. The press checks the gate again. On the Go config ticks before `OwnshipService` and hears a take-off a pass late, so `ScreenService` asks `prompt_allowed_in()` with its own pass's latch, and no prompt is answered in the pass between. The services keep their order because it is the dataflow (`../../products/skyblip_go/README.md`, What a service is). The loop meter names each service by its `diagnostics::LoopService` code, never by its place in the loop.

A staged `set` asks the power rule at the door and again at the press, and reaches flash through a `timing::DurableWriteWindow`, as a menu change does (`../timing/README.md`, A flash write waits for a receive dwell).

## The status reply

`status` is the one frame the device also sends unasked: on a change of the flight state, on a charging flip, a level change or a five-percent step of the cell, on a change of a paired Go's view of its Bravo (What the Go makes of its Bravo), of the transponder Bravo names on the ground, and of the reason beside the Go's `NO TX` (`../timing/README.md`, The transmit gate). So a page that holds its buttons in flight learns it landed without polling. It carries what a pilot's screen shows and nothing a bench reads, because it must fit `kSmallestSupportedPayloadBytes`, 182 bytes, at its widest, and a field that does not fit refuses the frame.

A status without the transponder is 20 bytes at its widest, so it fits a central that never exchanged its MTU. Key 7, `die_temperature_c`, left it for that budget and is retired. A unit on the ground that hears its transponder adds `own_transponder`, its callsign and its category, for 38 bytes, which a 20-byte link drops and counts.

What a support case reads lives in the diagnostics dump, `reset`, `link_drop_reason` and `failed_writes` in its system group and `battery_valid` and `cell_mv` in its power group (`core/power/README.md`), and the address and the callsign in the config reply. One boot fact stays in `status`, `went_dark_flat`, because "the battery went flat" is the answer a pilot is owed for a unit that switched itself off. A key with no reading behind it is left out, never sent as a zero. `scripts/check_link_registry.py` holds `format_status` to the schema.

## The 1090 MHz group

Only a unit with a 1090 MHz receiver, a skyBlip Bravo, sends the `adsb` group of the diagnostics dump. Its keys follow a frame from the LR2021 to the decoder's aircraft:

- `frames`, `frame_rate_hz`, `dropped`, `rearms`, `resets`, `command_errors`, `errors` and `rssi_dbm` are the receiver's own, `ports::AdsbReceiverStatistics`.
- `crc` counts the frames `adsb::receive` refused on parity, a DF17 whose CRC failed or a DF11 whose parity is no interrogator code. Nothing repairs them.
- `unsupported` counts the frames the decoder does not read: a downlink format other than 0, 4, 5, 11, 16, 17, 20 and 21, or a DF17 type code such as 0 and 23 to 30.
- `folded` counts the messages `adsb::Assembly` folded into an aircraft, never one from the aircraft address. `aircraft` is how many it holds now, and `tracked`, in the traffic group, how many reached the table.
- `formats` counts every frame that reached the decoder by downlink format, DF0 to DF24, so its sum is `crc`, plus `unsupported`, plus the frames read.
- `transponders`, `mode_s` and `mode_s_altitude` are the Mode S transponders Bravo holds now, those never heard in DF17, and those with a recent altitude. `unmatched` counts the replies whose address was not heard in clear first (`products/skyblip_bravo/README.md`, Mode S transponders).

## The capture group

A unit with a capture ring, a Go and a Bravo both, sends the `capture` group of the dump, so a page reads whether a capture runs and what one costs before asking for one. `common::CaptureService::describe()` fills it each second. `armed`, `profile`, `session`, `records`, `records_dropped`, `sectors` and `keeps_s` describe the running capture. `takes_sectors`, `evicts_flights`, `keeps_full_s` and `keeps_power_run_s` price the next one: every profile takes the same sectors and evicts the same flights, and keeps a different span. A flight run keeps what a power run keeps (`../diagnostics/README.md`).

## The radio protocols

`protocols` names the radio protocol stacks a unit carries, so a unit in hand can be checked against the ADS-L declaration of conformity (`protocols/README.md`, Versions). It answers one text key per stack, `<stack>_version`, the `kVersion` of `protocols/<stack>/version.h`, for the stacks the product lists in `products/<product>/radio_protocols.h`. The release's `PROTOCOLS.md` names each version's `kSpecification`, so it stays off the link. A `ConfigService` handed no stacks answers `unknown_command`.

## How wide the frames are

Each frame is measured at its widest, and `test/products/common/test_link_payload.cpp` and `test/products/common/test_comms_report.cpp` pin the figures.

| Frame | Bytes at its widest |
|---|---|
| `status` push | 20, and 38 while a unit on the ground names its own transponder |
| config reply | 37, and 41 on a paired unit |
| `update` report | 57 on a release build at version 9.99.99+99999, the widest `scripts/build_image.sh` lets through, and 59 on a development build |
| `identity` reply | 39, and never under 31 |
| install report | 30 (`core/dfu/README.md`) |
| `protocols` reply | 18 on a Go and 11 on a Bravo at this release's versions |
| registration, attestation | 146, 102 |
| challenge, `browser_challenge` reply | 27, 22 |
| `browser` entry, acknowledgement | 45, 11 |
| timing report | 147 |
| diagnostics groups | 284 for the 1090 MHz receiver, 179 for the radio, 78 for capture |

On a 182-byte link each diagnostics group answers in one frame, except the receiver's once its counters grow. On a narrower link they split, never mixing two groups.

A log chunk's envelope is 18 bytes at its widest, its records' byte string adds a head of two bytes up to ten records and three past it, and a diagnostics chunk names its store in two more. That leaves room for six records at 182 bytes, nine at 244 and nineteen at 495, so `kMaxLogRecordsPerChunk` is 19.

The widest host writes are `trust_browser`, 133 bytes, `register`, 88, and `unlock`, 74. Every other command, `set` at 34 bytes the widest, fits the two 18-byte prepare segments a link that never exchanged its MTU reassembles under `CONFIG_BT_ATT_PREPARE_COUNT=2` (`products/common/prj.conf`).

## The peer endpoint

A skyBlip Go paired with a skyBlip Bravo, a Tango, talks to it on `peer`, characteristic `69C21304-0187-4204-A91D-02EB8858B440` in the skyBlip service (`hardware/platform/zephyr/README.md`). The Go writes it as a Bluetooth central, `ports::Central`, and Bravo notifies it, so the registry's unit is Bravo and its host the Go. Bravo sends `observation` and `health`, and the Go `peer_claim`. Only a product that sets `CONFIG_SKYBLIP_PEER_SERVER`, Bravo, defines the characteristic (`products/skyblip_bravo/README.md`, Paired with a Go). The Go never reads Bravo's FLARM NMEA instead, because `$PFLAA` has no timestamp, loses whether an altitude is GNSS or pressure, and is relative to Bravo.

Each frame keeps the contracts of `core/model/README.md`. An `observation` carries one ADS-B target as `model::AircraftObservation` holds it, and a field with no valid reading is absent, never a zero. Its instant is `at_s` and `into_ms` as `events::Stamp` counts them, and `into_ms` is absent without a valid PPS phase. Each field's age counts back from that instant and is rebased whenever it moves, so a field keeps the instant it was heard. A `peer_claim` carries the Go's own-ship, its ages counting back from the instant the Go wrote it, so they hold on a Go with no UTC, and each field is valid on its own.

Each side draws a 32-bit `boot_epoch` from its hardware RNG at boot. An `observation`'s `sequence` starts at 0 each boot and wraps, and a reader orders two of one epoch by RFC 1982 serial number arithmetic, SERIAL_BITS 32. `health` carries `last_sequence` beside Bravo's `boot_epoch`, so a number from before a restart is never read as current.

A peer frame travels whole, never split. The widest is the 126-byte `observation`, so a pairing needs an ATT MTU of 129, and a Go whose link settles below it refuses the pairing and says why (#177). Zephyr agrees to the smaller of its L2CAP receive and transmit MTUs (`subsys/bluetooth/host/att_internal.h:38` at v4.4.2), and with `CONFIG_BT_SMP` off `CONFIG_BT_L2CAP_TX_MTU` defaults to 23 (`subsys/bluetooth/host/Kconfig.l2cap:40`). So Bravo sets `CONFIG_BT_L2CAP_TX_MTU` to at least 129 and `CONFIG_BT_BUF_ACL_RX_SIZE` to at least 133, the MTU plus the 4-byte L2CAP header (`include/zephyr/bluetooth/l2cap.h:39-45`), and `CONFIG_BT_BUF_ACL_TX_SIZE` of at least 133 keeps a widest notification in one ACL packet.

`simulator/world/paired_link.h` is the Bluetooth link between a Go and a Bravo in the simulator and the suite. It has no clock: a frame moves only on `pump()`, so a case runs the same way every time. Both halves are the host platform's real `Link` and `Central` and keep the silicon's limits, and a case can stall, lose or drop each direction.

## The Go's link to its Bravo

A Go pairs with one Bravo and keeps it. `products/skyblip_go/services/bravo.h` is the Go's side.

- **No key and no bonding.** The Security Manager stays off. Each side pins the other's whole address, its type included, and Bravo asks for a press of its button. That prevents a mix-up with a neighbour's unit but is not authentication: active spoofing is outside the threat model. If that changes, the step up is LE Secure Connections with a bond on the peer link alone, which no browser reads. A signed challenge with `core/identity` is not, because Bravo holds no key.
- **Pairing.** From the `BRAVO` row, the Go reserves Bravo's connection and scans at `BravoService::kPairingScanWindowUs` in every `kPairingScanPeriodUs`, listing each advertiser named `skyBlip Bravo ` by its tag, the low 16 bits of its address. A press picks one and a second confirms it. Once connected the Go writes `peer_claim`, its pairing request, once a second. The first `health` that says `paired` true stores the Go's pin, and the Go's next `peer_claim` says `paired` true, which stores Bravo's (`products/skyblip_bravo/README.md`, Paired with a Go). `kConnectCeilingMs` bounds the connection and `kAnswerCeilingMs` the wait for the press.
- **Refusals.** Each ends the pairing with a reason and stores nothing: three phones connected, a link below `comms::kPeerPayloadFloorBytes`, no answer in time, the link lost, and a `health` whose `protocol_version` is missing or not `comms::kPeerProtocolVersion`, 2.
- **Paired.** On every boot and every loss the Go reconnects to the pinned address at `kReconnectScanWindowUs` in every `kReconnectScanPeriodUs`. A narrow link or another protocol version keeps it disconnected until it is paired again or restarts. A `health` that says `paired` false means Bravo holds no pin for this Go, so the Go drops its pin and asks again on the same link: Bravo's lamp blinks blue and `BRAVO LOST THIS GO` counts down `kAnswerCeilingMs`. With no press in time the Go frees its phone slot and shows `NOT PAIRED ON BRAVO` once. Bravo raises no request from a Go it told `paired` false while its own pinned Go is connected, so a press never hands Bravo back to a replaced Go. Unpairing writes one `peer_claim` with `paired` false and waits up to `kFarewellCeilingMs` for Bravo to drop its pin too.
- **What the Go reads.** Each `health` drives `go::AircraftAddressWarning`, and its optional `own_transponder` keys name the transponder Bravo takes for the aircraft's own, which the Go proposes as its aircraft address on the ground (`products/skyblip_go/README.md`, Aircraft address).

### The alarm

While Bravo is paired, the Go's `ALARM` row sets the alarm for both units, and while the link is up the Go alone grades its ADS-L traffic and Bravo's ADS-B, and Bravo plays what it decides. A plain Go, with no buzzer and no motor, offers `BRAVO SOFT` and `BRAVO LOUD` while paired and switches to `BRAVO LOUD` when it pairs (`products/skyblip_go/pages/README.md`). `VISUAL` takes both units' lamps and the Go's glass, and `SOFT` and `LOUD` add the tone and the motor on every unit that has them.

Every `peer_claim` carries the call: `alarm`, the row's value, `announcing`, the level the Go's buzzer plays, `undismissed`, the level its glass and lamp show, and `announcement`, the count of announcements since the Go booted. `BravoService` writes a claim whenever one changes and every `kClaimPeriodMs`, so a new call reaches Bravo within the link's latency.

Bravo plays the call while a claim from its pinned Go is younger than `PeerService::kCallMaxAgeMs`, three claim periods, and the link is up. `core/annunciation`'s `Follower` starts a call whenever the count moves, so both units start each call together. A Bravo in flight plays one low note when it stops playing its Go's calls. Its own grading feeds only its NMEA and its capture while it plays the Go's call (`products/skyblip_bravo/README.md`, The alarm).

A press on either unit dismisses the alarm on both. The Go's long touch makes its next claim say `none`. A press on Bravo silences it at once and sends a `health` whose `dismissed_announcement` names the call it heard, and the Go dismisses only while that is still its current announcement, so a press never dismisses a call Bravo had not played.

`protocol_version` is 2 because a paired Bravo's role changed: under 1 it graded its own ADS-B, under 2 it plays its Go's call. Each version refuses the other's frames, as `UNKNOWN PROTOCOL` while pairing and `UPDATE NECESSARY` once paired, so both units are updated together.

### What the Go makes of its Bravo

An `observation` enters the Go's table through `traffic::TrafficTable::update`, the radio's door, from `BravoService::fold()`, and only from the pinned Bravo while the link is up and its `health` has said `paired` true. `comms::read_observation()` skips an unknown key, because `protocol_version` moves when a key changes meaning, not when one is added. It refuses the whole frame for half a field, an unknown code, or a value outside the range the Go computes with: a latitude past 90 degrees, a longitude past 180, a pressure altitude outside the -1200 to 126700 ft `protocol::altitude_ft_of` decodes, or a height outside -1318 to 39572 m, those widened by the 3125 ft GNSS-baro difference ADS-B carries, which keeps every height in millimetres inside an int32. `BravoService::refused()` counts them.

The Go dates the frame through its UTC second's edge, the PPS edge `timing::ClockState` anchored or else the fix that named it, plus `into_ms` when present, so a field known only to the second is dated at its start. The ages are copied as sent and the arrival time is never read, so a frame forwarded late keeps its own age. An instant up to `BravoService::kAheadCeilingMs` ahead keeps its place, because two GNSS clocks may disagree by a second (`../traffic/README.md`). A frame dated further ahead, or one a Go with no UTC cannot place, is refused and counted in `BravoService::unplaced()`. A `callsign` goes to `traffic::CallsignTable` under table 5. A frame whose `callsign`, or whose health's `own_transponder_callsign`, holds a byte `protocol::is_callsign_character` refuses is refused whole, as `protocol::callsign_of` refuses a name off the air, so no comma or `$` reaches a `$PFLAA` ID field and no control character reaches the glass. RSSI is not carried.

`health` alone says whether Bravo is there, since an empty sky sends no observation. `BravoService` publishes `bus::State::bravo`, a `comms::BravoState`, every pass:

| State | When |
|---|---|
| `Unpaired` | the Go holds no pin |
| `Waiting` | paired, not live, its receiver listening, and not yet counted on: no `health` younger than `kHealthMaxAgeMs`, or one that says `utc_valid` false (#193) |
| `Live` | a `health` younger than `kHealthMaxAgeMs` whose `listening` and `utc_valid` are both true |
| `ReceiverDown` | that `health` says `listening` false |
| `NoUtc` | that `health` says `utc_valid` false while the Go counts on its Bravo, so Bravo exports nothing (#192) |
| `LinkDown` | no `health` younger than `kHealthMaxAgeMs` while the Go counts on its Bravo |

`kHealthMaxAgeMs` is three `health` periods, so one lost notification is no loss. The Go counts on its Bravo once the pinned Bravo has said `utc_valid` true since the Go booted or paired, or once the Go has rolled, `state.flight.rolling`, after `kBootSettleMs` from boot. Before that a Bravo coming up reads `Waiting`, which plays no tone, so switching a Tango on in the hangar is silent, and from the taxi on a Bravo that is not live reads lost, so a pilot hears it before takeoff. `kBootSettleMs` gives a Go switched on rolling, or rebooted in flight, time to connect. `BravoService::counted_on()` lives in RAM, and unpairing drops it with the pin. `ReceiverDown` is lost from the first `health` that says so, because a receiver that stopped listening does not recover by itself. Bravo plays the Go's calls whatever the Go reads of it.

The last three states are a lost Bravo, `comms::lost()`. The glass reverses its `BRAVO` corner (`products/skyblip_go/pages/README.md`, Bravo has its corner), `common::AlarmService` plays one tone on the pass it turns lost (`../annunciation/README.md`), the flight log writes a `bravo` record (`../flight/README.md`), and `status` carries the state.

The reconnect scan is the Bluetooth Core's slow scanning mode 1, 11.25 ms every 1.28 s, and the pairing scan fast scanning, 30 ms every 60 ms, while a pilot watches the list. What the central link costs the 868 MHz timing is unmeasured until #194.

## Pairing from a browser

`pair` pairs a Go and a Bravo from a browser with one key, `tag`: on a Go the Bravo to pair with, on a Bravo the Go to accept. A unit's tag is the low 16 bits of its Bluetooth address, in its advertised name and as `bluetooth_tag` in its `identity` frame. The peer link pins that same address, because Zephyr's controller takes its static random identity address from FICR DEVICEADDR (`subsys/bluetooth/controller/ll_sw/nordic/hci/hci_vendor.c`) and neither product enables `CONFIG_BT_PRIVACY`. The config reply carries `paired_tag` while a unit is paired. `pair` is refused `in_flight`, and answered `paired` at once when already paired with that tag. The handlers are in `config_pair.cpp`, and each product names its side with `ConfigService::serve_pairing()`.

On a Go a trusted session's `pair` starts at once. Any other stands on the glass under `confirm_pair`, and the press trusts the browser and starts the pairing. `BravoService::pair_with()` unpairs any Bravo the Go holds and goes on as a menu pairing does. When the pairing ends the Go sends the session that asked, if it is still connected, an unasked acknowledgement:

| Verdict | When |
|---|---|
| `paired` | the first `health` that says `paired` true, and the Go has stored the pin |
| `not_found` | no advertiser with that name and tag heard within `BravoService::kConnectCeilingMs` |
| `phones_connected` | three phones hold every connection, so the Go cannot keep one for Bravo |
| `narrow_link` | the link to Bravo settled below `kPeerPayloadFloorBytes` |
| `no_answer` | Bravo's button was not pressed, and no trusted browser armed it, within `kAnswerCeilingMs` |
| `link_lost` | the connection failed or dropped before Bravo answered |
| `unknown_protocol` | Bravo's `health` speaks another `protocol_version` |
| `cancelled` | the pilot left the `BRAVO` page, or a newer `pair` replaced this one |

Bravo has no glass, so it answers `pair` only to a trusted session, refuses any other `untrusted`, and the pilot presses its button instead. A trusted `pair` arms Bravo for `PeerService::kAcceptanceWindowMs`, 60 s, twice the press window, because the page turns from Bravo to the Go through the browser's device chooser. While armed, that Go's request is answered through `PeerService::answer()`, the button's path.

## A reply too wide for its link

A whole reply wider than the asking session's payload is never sent short and never left unanswered: the sender counts a `link_drops` and answers `small_mtu`, 8 bytes on config and 8 or 10 on log, in `ConfigChannel::reply_to()` and `RecordPool::send()`. A timing or diagnostics report that cannot be split this narrow, a `register` or an `attest` whose frame does not fit, and a `read` whose chunk cannot hold one record are refused before any frame exists, and are not drops.

A push is never refused, because the refusal would be noise: a push that does not fit is counted and dropped. A `status` push the link answers `WouldBlock`, a central short of notification buffers, is not a drop: it goes on the next pass. One it answers `Unsubscribed` is neither counted nor retried, because no connected central subscribed to the config endpoint, such as a Go on its Bravo, which subscribes to the peer endpoint alone. The app subscribes before it asks for `status`, so nobody is owed that push. The `update` frame pushed on a connection's first `Up` goes again on the late MTU exchange's second `Up`.

## Measuring what links settle on

Each connection leaves one `Link` `down` record in a `Full` capture, with `payload_bytes`, the payload it settled on, and `mtu_exchange_started_by`: `central`, `unit`, `both` when the two requests crossed, or `none` for a link that ran at 20 bytes. `LinkSessions` works it out at the `Down`, because Zephyr calls `att_mtu_updated` for every exchange just before the unit's own response callback. To measure, arm a `Full` capture, connect each phone, browser and app a few times, run `./scripts/blip.py fetch --log diagnostics --all --out capture.ndjson`, and read the `action=down` lines of `./scripts/blip.py decode capture.ndjson --type link`. Splitting replies across notifications is worth building once a supported phone or browser ends at 20 bytes in two runs out of two.

## Activation

`challenge`, `register` and `unlock` travel on the config endpoint: `IdentityLink` in `identity_link.h` holds the session, and the handlers are in `config_identity.cpp`. `schemas/activation.v1.schema.json` says what each signature covers, and `core/identity/README.md` the messages.

`challenge` draws `unit_nonce`, 16 bytes from the hardware RNG, for the claim holder, and answers it with `server_key_id`. The nonce is single-use, and dies when `unlock` spends it, when the holder's link drops, or at `IdentityLink::kNonceForgetMs`, ten minutes. `register` verifies the server's signature over `unit_nonce` and `server_nonce`, stands `Pending::Register` under `confirm_register`, and one press answers the registration frame, signed by the unit's key, which names the product, the variant and the Bluetooth tag as the identity frame does. `unlock` verifies the unlock signature over the unit's DEVICEID, its current key, the serial and `unit_nonce`, and refuses serial 0, which would put 0x000000 on the air. The nonce is spent before the check, so a refused code is never retried on it. The code is written and read back before the `unlock` acknowledgement goes out. An activated unit's radio runs, so the code waits in `IdentityLink` for the product's own `timing::DurableWriteWindow`, and the power rule is asked again then: a cell that fell through it drops the code with `low_power`, never a forced write. A held code dies with its sender's link.

An unactivated unit transmits nothing, so none of this is gated on the flight state, and its `register` prompt survives a take-off. An activated unit refuses `challenge`, `register`, `unlock` and `attest` `in_flight`, and cancels its register prompt at take-off.

Every identity refusal but `small_mtu` and `invalid` is written in `config_identity.cpp` as `refuse(link::Reason::Word)`, so the server's activation page can grep its rows apart:

| Word | When |
|---|---|
| `keyless` | the unit has no key, drew no nonce, or cannot read its own version |
| `in_flight` | an activated unit in flight |
| `low_power` | an `unlock` while the cell is below the settings write rule (`core/power/cutoff.h`): the nonce stays live for a retry on the charger |
| `no_session` | no live nonce for this claim holder: no `challenge`, a spent nonce, a dropped link, ten minutes, or a `challenge` since the registration was staged |
| `bad_signature` | the signature does not verify with the trusted key, or the field is not a byte string of the width the registry pins |
| `small_mtu` | the link cannot carry the 146-byte registration frame, or the 27-byte challenge reply, in one notification |
| `flash` | the identity page did not take the code, or did not read it back |

A product that hands `ConfigService` no `IdentityLink` answers the three commands, and `attest`, `unknown_command`.

### Attestation

`attest` asks an activated unit to sign the firmware it runs, so skyblip.eu learns each unit's version on the unit's own word. The page sends the server's `server_nonce`, and the unit answers at once with the `attestation` frame, signed over `skyblip:attestation:v1` followed by the DEVICEID, its public key, `server_nonce`, the release, product and variant bytes and the version (`core/identity/README.md`). The unit keeps nothing between two frames.

It stages no prompt, because signing takes nothing from the pilot and SMP already lets anyone in range read the running version. The label is the attestation's alone, so no attestation passes for another message. The flight gate keeps the signature off the main thread in flight. The refusals, in order, are `in_flight`, `keyless`, `small_mtu`, checked before signing, and `invalid` for a missing 16-byte `server_nonce`. A Bravo has no key, so it answers `keyless`.

### Identity

`identity` asks any unit who it is: the product, the variant, `plus` on a Go with the inclinometer, the buzzer and the haptic motor fitted, the running version, the release flag, the DEVICEID and the Bluetooth tag, in the words `docs/ADSL_NMEA_PROPOSAL.md` gives `$PDSLV`. Nothing is signed, because an activated unit vouches for the same facts in its attestation. It is answered in flight too, and refused `no_version` only when the unit cannot read its version. The variant is worked out at every boot from the parts the probes found, and stored nowhere.

## Trusted browsers

A browser the pilot approved once with a press, or the browser that installed the unit, changes the settings, starts an update, activates the unit and pairs it with no press after that. The link has no bonding and a phone connects from a new private address each time, so the browser proves a key instead. `BrowserLink` (`browser_link.h`) holds the session, `TrustedBrowsers` (`trusted_browsers.h`) the list, and the handlers are in `config_browser.cpp`.

- **The proof.** `browser_challenge` draws 16 bytes for the claim holder. `trust_browser` carries the browser's raw Ed25519 public key, its signature over `skyblip:browser:v1` followed by that nonce, and a label of at most 24 bytes. The nonce is spent before the check. A listed key makes the session trusted until its link drops or it asks `browser_challenge` again. Any other key is answered `untrusted` and held as the session's proven key.
- **Enrollment.** The next `set`, `dfu`, `register` or `pair` a press confirms on that link stores the proven key. The Go's confirm page shows `TRUSTS BROWSER` whenever `ConfigService::prompt_trusts_browser()` holds, so a foreign page cannot enroll a key unseen. The unit keeps eight keys, and a ninth replaces the one stored first.
- **What trust skips.** A trusted command skips only the press and meets every other check of the pressed command. A trusted `set` is stored at once, in flight too, because no prompt takes the glass. A trusted `dfu` opens the upload window at once, still refused `in_flight` and `low_power`, and a trusted `register` or `pair` goes ahead at once. `recovery`, `power_off`, `gnss_cold_start`, `capture` and the log `erase` always ask.
- **The list.** `browsers` and `browser` read one entry by index, the key's first eight bytes as `id` and the label, and `forget_browser` drops one by `id`. Both answer only a trusted session. Neither unit has a gesture to clear the list, so the Briefing device page reads and clears it (`server/app/javascript/skyblip/README.md`).
- **Where it lives.** The list is one sealed NVS record, `browsers` (`core/settings/blob.h`), beside `settings` in the internal `storage_partition`, which no `.uf2` and no update writes. It is written through its own `DurableWriteWindow` under the settings write rule. A blob that will not read leaves the list empty.

A trusted key changes the settings in flight with nothing on the glass, so a lost phone keeps that power until another trusted browser forgets it. The proof authenticates the start of a session, not each frame, and cannot tell a skyBlip from a device relaying another unit's nonce, the active spoofing the peer link leaves out of its threat model too.

### The browser that installed the unit

Briefing installs a unit over USB through the factory UF2 bootloader, and adds to the `.uf2` one record that names its own browser key, on a page inside slot0, so the unit trusts that browser from its first boot. `InstallTrust` (`install_trust.h`) reads it through `ports::Dfu`'s view of slot0.

The page is the first 4096-byte boundary at or after the end of the slot0 image, where MCUboot ends it (`boot_read_image_size`): slot0's start, plus `ih_hdr_size`, `ih_img_size` and `ih_protect_tlv_size` of the image header, plus `it_tlv_tot` of the unprotected TLV info (magic 0x6907), all little-endian. slot0 starts at 0x34000 on the T-Echo, 0xB6000 bytes long, and at 0x35000 on the X1, 0xB5000 bytes long. The page must end before slot0's last sector, MCUboot's trailer, which `scripts/mkuf2.py` blanks in every `.uf2`.

The record is 77 bytes at the start of the page, and the rest of the page is 0xFF:

| Offset | Bytes | Field |
|---|---|---|
| 0 | 16 | magic, ASCII `skyblip:trust:v1` |
| 16 | 32 | the browser's raw Ed25519 public key |
| 48 | 1 | the label's length, 0 to 24 |
| 49 | 24 | the label, UTF-8, padded with zero bytes |
| 73 | 4 | CRC-32 (IEEE 802.3, zlib's `crc32`) of bytes 0 to 72, little-endian |

`schemas/trust_record.v1.vectors.json` holds the vectors the firmware and the server's writer are tested against. An invalid record is ignored silently. A valid one is enrolled, and only once the `browsers` record has landed is the page erased, through its own `DurableWriteWindow`, so a power cut between the two only enrolls the same key again.

The record gives a hand on the cable nothing it lacked, since the factory bootloader writes any byte. Over the air it must never reach slot0. MCUboot's swap using offset moves slot1's sector N+1 to slot0's sector N and lets the first sector past the new image through (`boot/bootutil/src/swap_offset.c`, `swap_run()` and `boot_swap_sectors()`, at the MCUboot Zephyr v4.5.0 pins), so an SMP upload declared longer than its image could carry a forged record onto the trust page. `dfu::ProductCheck` therefore refuses an upload whose declared length is not exactly its image's end, before any byte past the image is written (`../dfu/README.md`).

## The log dialect

`log_link.h` is the log partition's half of the link, on its own endpoint, with three commands, list, read and erase. Every reply fits one frame and carries the record index it starts at, so the next command is the acknowledgement, and asking again from the index kept is the whole recovery. `parse_log_request()` is the boundary: a request it does not understand comes back with `understood` false and the `reason` a service refuses with.

### Either store, named on both halves

`core/store` puts two rings on the partition, and an optional `log` key picks one: `flights`, `diagnostics`, or absent for flights. Any other code is refused `unknown_log`, never read as flights, because the two stores share a session id space and the wrong one would answer with a plausible flight. A number that is not an unsigned integer is refused `invalid`, never read as absent. A store this build has no writer for is `no_diagnostics`, not `no_storage`, so a phone does not take the partition for missing, and an erase of the recycled store is `flights_only`.

Every diagnostics reply carries `log` `diagnostics`, so two fetches on one link never misfile a chunk, and `log_records_per_chunk()` takes the store because that key costs two bytes. A chunk carries its records as one byte string, 24 bytes each with their CRC, so the host verifies each against the flash's checksum.

### A window of chunks

`read` with `session`, `from` and `count` asks for `count` chunks back to back, because one chunk per 99 ms round trip capped an offload at about 2.9 kB/s, against about 16 kB/s for a 498-byte notification per connection interval. `count` is clamped to `kMaxLogReadChunks`, 8, about a quarter of a second of link, the most an offload may take from the traffic picture. `plan_log_window()` cuts a window at the session's end and sets `ends_session` on its last chunk only.

### A record keeps its index while the ring rotates

A session's records are indexed from the sector it opened in, and an index names the same record for as long as the unit runs. The allocator counts the sectors each ring recycled from the front of a session (`store::SectorAllocator::recycled_sectors()`), and a session's entry names the index of its first kept record as `first`, absent while 0. A `read` below `first` is answered from `first`, and its chunk says so in `from`. The service checks that before every chunk, because the ring may recycle between two passes of one window. The count lives in RAM, so a restart numbers a truncated session from 0 again, and a host that resumes by index reads its last kept record back first (`scripts/README.md`, Fetch a capture).

### A session still being written

`closed` is false both for a session the power cut, whose last record may be torn, and for the one being written, whose last record is whole. So the session the store is writing carries `recording` true, and `blip.py` and the console keep its last record. The flag lives in RAM, so a restarted unit lists nothing as recording.
