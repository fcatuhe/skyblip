# ADS-L questions for EASA

The readings of ADS-L 4 SRD-860 Issue 2 that the text does not settle, and that skyBlip's ADS-L stack and its coming 1.0.0 rest on. Each one is flown as written here. If EASA reads a clause otherwise, the change is a new ADS-L version (`firmware/protocols/README.md`, Versions), but for question 3, which concerns payload 66 outside the ADS-L stack.

| # | Clause | Status |
|---|---|---|
| 1 | F.2.3, the ICAO address | asked |
| 2 | G.1.2, the flight state of a craft that can fly at low ground speed | to ask |
| 3 | C.5 and F.2.1, payload 66 outside the direct slot | to ask |
| 4 | G.1.1 and G.1.16, a fix measured on a quarter and sent within 500 ms | to ask |
| 5 | C.2.5, alternation after a burst that did not go out | to ask |
| 6 | G.1.16 and G.1.17, velocity accuracy with no measured bound | to ask |

## 1. F.2.3: every payload under the ICAO address

F.2.3 says the ICAO address "shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer". We read "overriding" as covering every payload the device sends. While the pilot has set an ICAO address, the Traffic, the Status and the name all go out under table 5 with that address, and none stays on the manufacturer's page (`firmware/core/settings/README.md`, The ICAO address).

## 2. G.1.2: Undefined on the ground for a craft that can fly at low ground speed

G.1.2 says "The 'Undefined' value shall be used if no reasonable decision is possible by the ADS-L device". The Go decides the flight state from ground speed. A glider or a paraglider soaring a ridge into wind, a slow ultralight, a balloon or a hovering helicopter can fly at little or no ground speed, so for those G.1.3 categories the Go cannot tell a landing from flight. It sends Undefined instead of On ground for categories 3 to 13 and 15 to 17, and so keeps G.1.16's 1 Hz rate rather than dropping to 0.1 Hz (`firmware/core/flight/state.cpp`, `announced_state()`). We ask whether this is the use of Undefined the clause intends.

## 3. C.5 and F.2.1: payload 66 outside the direct slot

C.5 gives aircraft the direct slot, 450 to 1000 ms of each UTC second, and says the rest "is reserved for future extensions and shall not be used for any ADS-L transmissions". F.2.1 lists payload 66 as "OGN Diagnostics" and puts its definition "outside the scope of this document". The Go sends its callsign in OGN's payload 66 registration record, on ADS-L's frame, from 0 to 200 ms of the second after its slot (`firmware/protocols/adsl_edge/README.md`). We read a payload whose definition the specification leaves out as not being an ADS-L transmission in C.5's sense. We ask whether that holds, or whether payload 66 belongs in the direct slot. The name lives in `firmware/protocols/adsl_edge/` and `firmware/core/timing/`, outside the ADS-L stack and its frozen suite, so a different answer changes those and not the ADS-L version.

## 4. G.1.1 and G.1.16: a fix measured on a quarter and sent within 500 ms

G.1.1 says the Timestamp counts "quarter seconds since the full hour", "refers to the time of the navigation solution (PVT fix) obtained by the GNSS receiver" and "shall be accurate to ± 10 ms". G.1.16 says "No Traffic message shall be sent containing navigation data older than 500 ms", counted "from obtaining (measuring) the PVT fix", and the performance table asks for "a maximum latency of 0.5 seconds between measurement and broadcast". C.5 puts the burst between 450 and 1000 ms of the second. Read together, every Traffic payload must carry a solution the receiver measured within 10 ms of a quarter second, and leave within 500 ms of that measurement. The Go does not extrapolate or interpolate the position to a quarter, because the result would no longer be a solution the receiver obtained.

This ties the transmission to the receiver's solution schedule, which we have not seen elsewhere. A receiver solving at 1 Hz on the full second serves only bursts between 450 and 500 ms, 50 ms of the 550 ms slot. At 5 Hz only the full second falls on a quarter. Only a rate of 4 Hz, or a multiple of it, puts every solution on a quarter. The Go's receiver, a Quectel L76K, documents 1, 2 and 5 Hz, and on the bench it ignored a request for 4 Hz. The Go therefore runs it at 2 Hz, with solutions on .000 and .500.

At 2 Hz part of the slot has no usable solution. The Go sends on 868.2 MHz between 450 and 810 ms and on 868.4 MHz between 830 and 990 ms (`firmware/core/timing/README.md`). The .000 solution serves bursts up to 490 ms, keeping 10 ms for the air time and the end of the transmission. The .500 solution reaches the Go 40 to 160 ms after it was measured (median 57 ms and maximum 93 ms for the .000 solution over 1,205 seconds on one unit, up to 160 ms with heavy NMEA output). Between 490 ms and its arrival, no solution meets both clauses. This does not touch 868.4 MHz, whose window opens at 830 ms, but it falls inside the 868.2 MHz window. In 26 minutes on the bench, before the mitigation below, 21 of the 81 positions due on 868.2 MHz were not sent.

The Go now holds a burst drawn in that gap until the .500 solution arrives. If the burst's instant has passed by then, it draws a new random instant over the rest of the 868.2 MHz window, keyed on its address and the second, so units do not bunch on the arrival. A position is then lost only if the .500 solution arrives after 810 ms, the window's last instant. The mitigation is not ideal. Bursts on 868.2 MHz no longer spread evenly over the window: about a quarter of them move from 490 to 590 ms into 590 to 810 ms, which carries about 40% more bursts. The chance that two units collide there rises from about 2.8% to about 3.9% per neighbour. Every Go has the same receiver and the same gap, so the uneven load adds up across a fleet. It also depends on the receiver's delay, which the Go measures but does not control.

We ask whether G.1.1 and G.1.16 intend to bind each transmission to a solution measured on a quarter second, or whether a device may carry its latest solution forward to the quarter the Timestamp names, with the 500 ms counted from that measurement. We also ask whether a later issue could let the Timestamp name the solution's own time more finely than a quarter second, so that receivers solving at 1, 5 or 10 Hz can meet both clauses without an uneven draw.

## 5. C.2.5: alternation after a burst that did not go out

C.2.5 says the sender "shall alternate between these frequencies for every transmission to facilitate reception of these payloads for restricted, single-channel receivers". The Go picks the channel from the UTC second's parity: a position on A in an even second and on B in an odd one, the Status the other way round. When a burst does not go out, because the channel stayed busy, the next one keeps its second's channel, so two positions in a row can leave on the same frequency. A takeoff or a landing changes the rate, and in about one case in two it puts two positions in a row on one channel, once, at the change (`firmware/core/timing/README.md`). A single-channel receiver then still hears every other second from us, which is the purpose the clause gives. We ask whether alternation by the schedule meets C.2.5, or whether each transmission must take the channel opposite to the last one that went out.

## 6. G.1.16 and G.1.17: velocity accuracy with no measured bound

G.1.17 says uncertified devices "shall however not report all quality values as 0. Instead, they shall report their positioning performance dynamically". The Go's receiver reports HDOP and VDOP but no velocity error, so the horizontal and vertical accuracy change with the fix while the velocity accuracy is sent as 0, unknown, which G.1.16's table allows. We ask whether that meets G.1.17.
