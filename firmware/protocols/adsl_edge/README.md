# protocols/adsl_edge

What the Go sends on ADS-L's frame that ADS-L 4 SRD-860 Issue 2 leaves outside its scope. It is ahead of the specification: the record is OGN's, and a later issue is expected to define the payload. It stays here, apart from `protocols/adsl/`, so the ADS-L stack and its conformance suite hold only what Issue 2 regulates, and ADS-L 1.0.0 stays frozen while this moves (`../README.md`, Versions).

The stack builds on `protocols/adsl/`'s `Packet`, the one include the boundary allows it outside `protocols/common/`, and nothing else from skyBlip. `version.h` names what it implements in `kSpecification`.

## What is in

| File | What it holds |
|---|---|
| `registration.{h,cpp}` | OGN's payload 66 registration record: `kPayloadTypeOgnDiagnostics`, the telemetry and information types, `is_registration()`, `information_text()`, and `encode_registration()`, which writes the sender address, the header byte and 14 bytes of text on an ADS-L `Packet` and sets E.1.1's version |
| `version.h` | `kVersion` and `kSpecification` |

## Payload 66

F.2.1 lists value 66 as "OGN Diagnostics" in the broadcast half of the payload range, and footnotes it "Payload definition is outside the scope of this document". The layout is therefore OGN's, from `nrf52-ogn-tracker/src/adsl.h:120-128`: the F.2 header with the sender's 6+24 bit address, then a header byte whose top two bits are the telemetry type, 1 for information, and whose low six bits are the information type, 5 for the registration, then 14 bytes of text, zero-filled. It is scrambled and checked by the CRC as every ADS-L frame is, so a receiver hears it with the sync word and length byte it already uses. Issue 1 has no payload 66, so E.1.1 makes it version 1.

What text goes in and what is refused off the air is skyBlip's, in `core/protocol/callsign.*`: the characters the radar menu types, and nothing else.

## When it goes

The schedule is skyBlip's, in `core/timing/`: in window B's tail, the first 200 ms of the next UTC second, on channel B, in the even second whose position goes on A, so nothing of ours is on B within 100 ms of it. That is outside the direct slot Issue 2's §C.5 gives Traffic and Status. §C.5 reserves the rest of the second and says it "shall not be used for any ADS-L transmissions". Our reading is that a payload whose definition F.2.1 puts outside the specification is not one of them. That reading is one of the questions `docs/ADSL_EASA_QUESTIONS.md` puts to EASA.

## Tests

`test/` holds the record's own cases and builds with `protocols/adsl/` and doctest. `firmware/test/adsl_edge/` holds the cases that need skyBlip around it: the name a Go writes and refuses, the window it goes in, and the ICAO address it follows.
