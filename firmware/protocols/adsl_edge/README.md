# protocols/adsl_edge

This stack is what the Go sends on ADS-L's frame that ADS-L 4 SRD-860 Issue 2 leaves outside its scope: OGN's payload 66 registration record, in `registration.{h,cpp}`. It stays apart from `protocols/adsl/` so the ADS-L stack and its conformance suite hold only what Issue 2 regulates, and the ADS-L version does not move while this one does (`../README.md`, Versions). It includes `protocols/adsl/`'s `Packet` and nothing else from skyBlip.

## Payload 66

F.2.1 lists value 66 as "OGN Diagnostics" and footnotes it "Payload definition is outside the scope of this document". The layout is OGN's, from `nrf52-ogn-tracker/src/adsl.h:120-128`: the F.2 header with the sender's 6+24 bit address, then a header byte whose top two bits are the telemetry type, 1 for information, and whose low six bits are the information type, 5 for the registration, then 14 bytes of text, zero-filled. It is scrambled and CRC-checked as every ADS-L frame is. Issue 1 has no payload 66, so E.1.1 makes it version 1.

`core/protocol/callsign.*` decides what text goes in and what is refused off the air: the characters the radar menu types, and nothing else.

## When it goes

`core/timing/` schedules it in window B's tail, the first 200 ms of the next UTC second, on channel B, in the even second whose position goes on A, so nothing of ours is on B within 100 ms of it. §C.5 reserves the rest of the second outside its direct slot and says it "shall not be used for any ADS-L transmissions". skyBlip reads that a payload F.2.1 puts outside the specification is not one of them, and `docs/ADSL_EASA_QUESTIONS.md` puts that reading to EASA.

## Tests

`test/` holds the record's own cases. `firmware/test/adsl_edge/` holds the cases that need skyBlip around it.
