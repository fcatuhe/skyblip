# core/protocol

The wire formats, in both directions and with no I/O: `adsl.{h,cpp}` and `adsl_uplink.{h,cpp}` are the ADS-L 4 SRD-860 packet the radio carries, `air.{h,cpp}` the shared air-frame plumbing, `alptas.{h,cpp}` and `nmea_out.{h,cpp}` what a tablet reads over the companion link.

## The name ADS-L has no field for

An iConspicuity payload carries no callsign, so a target on the glass is six hex digits and a pilot's radio call is not. The name comes from the one payload the specification hands to somebody else: F.2.1 assigns value 66 to "OGN Diagnostics" and footnotes it "payload definition is outside the scope of this document", which makes the layout Jalocha's (`oss/nrf52-ogn-tracker/src/adsl.h:120-128`) and ours to follow rather than to invent.

`from_own_callsign` writes the one record of it we speak. Type 66, the same 6+24 sender address our position frames carry, then a header byte of `TelemType` 1 and `InfoType` 5, then 14 characters. Everything around it is the frame we already build: the same sync word, Manchester coding, scramble and CRC24, which is why the radio needed no new dwell and the receiver no new configuration.

`callsign_of` is the other direction, and it is a trust boundary: text off the air is refused whole when any byte of it is not a callsign character, rather than drawn with a control character in it or spliced into a `$PFLAA` ID field as a comma. `is_callsign_char` is the set the radar menu types (blank, dash, `A`-`Z`, `0`-`9`), and the pilot's own callsign is held to the same one. What it returns goes to `core/traffic/callsigns.h` keyed on the sender's table and address, never into an `AircraftObs` - a name is an attribute of an address, a position is an observation, and the two have different lifetimes.

An OGN tracker reads what we send: `ProcessRxADSL` takes any telemetry frame, pulls `getInfo(Call, 5)` out of it and hangs the name on the target of that address (`oss/nrf52-ogn-tracker/src/proc.cpp:715-733`), with no opinion about our AMT 58. It transmits the same record itself every 40 to 60 seconds, hopping between four channels, so we hear one of theirs perhaps once in four.

## What ALP-TAS costs us

`$PFLAA` carries an `IDType` with three values, and ADS-L's address mapping table has 64. The mapping is `addr_table_to_idtype`, and it is a lie chosen from a short list of lies.

SkyDemon refuses the sentence for anything but 1 or 2 (`oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658`: "if (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon only accepts 1 or 2"). Leaving an address at IDType 0 draws nothing on that app at all, which is the one failure worth never causing again. Between the two values left, 1 (ICAO) claims a permanent, registry-issued identity; 2 (FLARM) claims a device-class kinship that is at least true of the mechanism, self-assigned and transient. So ICAO is reported as ICAO and everything else becomes FLARM.

What that costs: this device's own table, 58 (`settings::kAddrTableSkyblip`), and an OGN-Tracker address (7) both draw on the tablet as if they were FLARM. That is a lie about provenance too, and a cheaper one than claiming ICAO, because nothing downstream correlates a FLARM ID against an aircraft register the way it might an ICAO one.

## Two things an ALP-TAS frame is given before it is refused

`alptas_correct` runs the erasure correction the ADS-L path has always had. §C.2.1 is Manchester, so a chip pair that decoded to neither symbol is an error whose position the receiver already knows: `fec::manchester_decode` marks it, `protocol::Frame` carries the map, and the frame CRC says which combination of those flips was transmitted. The search is a Gray-code walk over the marked bits with the CRC syndrome of each one precomputed, so a combination costs one XOR rather than a pass over the frame, and a frame no combination repairs is restored to exactly the bytes that arrived. Up to six marked bits, which is 63 combinations; beyond that the burst is refused as damaged. The syndromes are computed rather than tabled, out of the CRC's own linearity: flipping a data bit moves the check by `crc16_ccitt(that bit alone, init 0)`, and flipping a carried CRC bit moves it by the bit itself.

Until 2026-09-19 the ALP-TAS path checked the CRC and threw the frame away, so every burst with one dead chip pair was a `CRC` row while the same damage on an ADS-L frame was corrected and drawn. On a bench that difference is invisible, because nothing at -15 dBm has dead chips. At range it is receptions.

`alptas_keyed_second` answers the question a `DEC` row could not. The key stage is `utc >> 4`, so the decrypt is correct for every second inside the sender's own 16-second block, and the 4 time bits inside the frame then pin the second to ±1. Both sides of that are cheap to exploit: decrypt once per block the search window reaches, check the two structural fields the protocol offers (`LastByte == 0`, `Needs3 == 3`), and the time bits name the sender's second exactly. The window is `kAlptasKeyWindowS`, 18 seconds, which is GPS-UTC: a receiver reporting GPS time instead of UTC is the one clock fault a device holding a valid fix can plausibly have, and a disagreement wider than that is not a clock, it is noise that framed.

The frame is refused either way. What the search buys is the tape saying `KEY+18` instead of `DEC`, which is the difference between a support case about a radio and one about a receiver's time.
