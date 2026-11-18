# ADS-L questions for EASA

The readings of ADS-L 4 SRD-860 Issue 2 that skyBlip's ADS-L 1.0.0 rests on and that the text does not settle. Each one is flown as written here. If EASA reads a clause otherwise, the change is a new ADS-L version (`firmware/protocols/README.md`, Versions), but for question 3, which concerns payload 66 outside the ADS-L stack.

| # | Clause | Status |
|---|---|---|
| 1 | F.2.3, the ICAO address | asked |
| 2 | G.1.2, the flight state of a craft that can hover | to ask |
| 3 | C.5 and F.2.1, payload 66 outside the direct slot | to ask |
| 4 | G.1.1 and G.1.16, the age of the navigation data | to ask |
| 5 | C.2.5, alternation after a burst that did not go out | to ask |
| 6 | G.1.16 and G.1.17, velocity accuracy with no measured bound | to ask |

## 1. F.2.3: every payload under the ICAO address

F.2.3 says the ICAO address "shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer". We read "overriding" as covering every payload the device sends. While the pilot has set an ICAO address, the Traffic, the Status and the name all go out under table 5 with that address, and none stays on the manufacturer's page (`firmware/core/settings/README.md`, The ICAO address).

## 2. G.1.2: Undefined on the ground for a craft that can hover

G.1.2 says "The 'Undefined' value shall be used if no reasonable decision is possible by the ADS-L device". The Go decides the flight state from ground speed. A paraglider soaring a ridge into wind, a balloon or a hovering helicopter can fly at no ground speed, so for those G.1.3 categories the Go cannot tell a landing from flight. It sends Undefined instead of On ground for categories 3, 5, 7 to 13 and 15 to 17, and so keeps G.1.16's 1 Hz rate rather than dropping to 0.1 Hz (`firmware/core/flight/state.cpp`, `announced_state()`). We ask whether this is the use of Undefined the clause intends.

## 3. C.5 and F.2.1: payload 66 outside the direct slot

C.5 gives aircraft the direct slot, 450 to 1000 ms of each UTC second, and says the rest "is reserved for future extensions and shall not be used for any ADS-L transmissions". F.2.1 lists payload 66 as "OGN Diagnostics" and puts its definition "outside the scope of this document". The Go sends its callsign in OGN's payload 66 registration record, on ADS-L's frame, from 0 to 200 ms of the second after its slot (`firmware/protocols/adsl_edge/README.md`). We read a payload whose definition the specification leaves out as not being an ADS-L transmission in C.5's sense. We ask whether that holds, or whether payload 66 belongs in the direct slot. The name lives in `firmware/protocols/adsl_edge/` and `firmware/core/timing/`, outside the ADS-L stack and its frozen suite, so a different answer changes those and not the ADS-L version.

## 4. G.1.1 and G.1.16: the age of the navigation data

G.1.1 says the Timestamp "refers to the time of the navigation solution (PVT fix) obtained by the GNSS receiver", and G.1.16 that "No Traffic message shall be sent containing navigation data older than 500 ms". The Go's receiver solves once a second, at the top of the second, and C.5's direct slot runs from 450 to 1000 ms. The Go sends a fix only if it is at most 500 ms old at the top of the transmit second, carries the position forward to the burst's own instant, and names that instant, to the quarter second, in the Timestamp (`firmware/protocols/adsl/README.md`, The transmit rules). The measurement behind a burst can so be up to about 1.5 s old, while the position and Timestamp describe the instant of transmission. We ask whether extrapolated data stamped with its own instant meets G.1.16, or whether the measurement itself must be under 500 ms old when the burst leaves, which would need a receiver solving at 2 to 5 Hz.

## 5. C.2.5: alternation after a burst that did not go out

C.2.5 says the sender "shall alternate between these frequencies for every transmission to facilitate reception of these payloads for restricted, single-channel receivers". The Go picks the channel from the UTC second's parity: a position on A in an even second and on B in an odd one, the Status the other way round. When a burst does not go out, because the channel stayed busy, the next one keeps its second's channel, so two positions in a row can leave on the same frequency. A single-channel receiver then still hears every other second from us, which is the purpose the clause gives. We ask whether alternation by the schedule meets C.2.5, or whether each transmission must take the channel opposite to the last one that went out.

## 6. G.1.16 and G.1.17: velocity accuracy with no measured bound

G.1.17 says uncertified devices "shall however not report all quality values as 0. Instead, they shall report their positioning performance dynamically". The Go's receiver reports HDOP and VDOP but no velocity error, so the horizontal and vertical accuracy change with the fix while the velocity accuracy is sent as 0, unknown, which G.1.16's table allows. We ask whether that meets G.1.17.
