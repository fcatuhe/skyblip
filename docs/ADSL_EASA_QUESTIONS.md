# ADS-L questions for EASA

The readings of ADS-L 4 SRD-860 Issue 2 that skyBlip's ADS-L 1.0.0 rests on and that the text does not settle. Each one is flown as written here. If EASA reads a clause otherwise, the change is a new ADS-L version (`firmware/protocols/README.md`, Versions).

| # | Clause | Status |
|---|---|---|
| 1 | F.2.3, the ICAO address | asked |
| 2 | G.1.2, the flight state of a craft that can hover | to ask |
| 3 | C.5 and F.2.1, payload 66 outside the direct slot | to ask |
| 4 | G.2 and D.1.1, the length of a Status frame | to ask |

## 1. F.2.3: every payload under the ICAO address

F.2.3 says the ICAO address "shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer". We read "overriding" as covering every payload the device sends. While the pilot has set an ICAO address, the Traffic, the Status and the name all go out under table 5 with that address, and none stays on the manufacturer's page (`firmware/core/settings/README.md`, The ICAO address).

## 2. G.1.2: Undefined on the ground for a craft that can hover

G.1.2 says "The 'Undefined' value shall be used if no reasonable decision is possible by the ADS-L device". The Go decides the flight state from ground speed. A paraglider soaring a ridge into wind, a balloon or a hovering helicopter can fly at no ground speed, so for those G.1.3 categories the Go cannot tell a landing from flight. It sends Undefined instead of On ground for categories 3, 5, 7 to 13 and 15 to 17, and so keeps G.1.16's 1 Hz rate rather than dropping to 0.1 Hz (`firmware/core/flight/state.cpp`, `announced_state()`). We ask whether this is the use of Undefined the clause intends.

## 3. C.5 and F.2.1: payload 66 outside the direct slot

C.5 gives aircraft the direct slot, 450 to 1000 ms of each UTC second, and says the rest "is reserved for future extensions and shall not be used for any ADS-L transmissions". F.2.1 lists payload 66 as "OGN Diagnostics" and puts its definition "outside the scope of this document". The Go sends its callsign in OGN's payload 66 registration record, on ADS-L's frame, from 0 to 200 ms of the second after its slot (`firmware/protocols/adsl_edge/README.md`). We read a payload whose definition the specification leaves out as not being an ADS-L transmission in C.5's sense. We ask whether that holds, or whether payload 66 belongs in the direct slot.

## 4. G.2 and D.1.1: a Status frame padded to 24 bytes

G.2 defines a Status payload of 56 bits, so the packet D.1.1 describes is 16 bytes long. The Go sends it in a 24-byte frame, with 8 zero bytes after the payload and a length byte of 24, as its Traffic frames are. Receivers that fix the length at 24 in their sync word, as the OGN code SoftRF carries does, hear it that way, and F.1 allows a payload of 24 + 32N bits. We ask whether a padded Status is conformant, and whether a receiver may expect the 16-byte form from other senders.
