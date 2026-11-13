# protocols/alptas

The FLARM-generation air frame this project calls ALP-TAS, 2024 protocol, message type 2: the frame (`frame.{h,cpp}`), its two cipher stages and their keys, its CRC-16 and the erasure repair a receiver runs before it, and the sync word it shares a window with ADS-L through. `version.h` names it in `kSpecification`. It builds and passes its tests with nothing but `protocols/common/` and doctest (`../README.md`).

`decode()` and `encode()` work on `Position`, the frame's own fields in its own units: the address and its type, the FLARM aircraft type, the airborne code, the position in 1e-7 degree, the altitude in metres, speed and climb in decimetres a second and the course in degrees. skyBlip maps those to an `AircraftObservation` in `core/protocol/alptas.*`: the ADS-L category an aircraft type is, the ADS-L address table an address type is, the G.1.2 state an airborne code is, and the ADS-L units. `decode()` needs the receiver's own position, because the frame carries only the low 20 bits of each coordinate step, and the UTC second the frame arrived in, because the mask stage is keyed on it.

## Public API

Everything is in namespace `skyblip::alptas`: `kSyncWord`, `kFrameBytes`, `crc()`, `set_crc()`, `crc_ok()`, `correct()`, `keyed_second()`, `address()`, `Position`, `decode()`, `encode()`, and `kVersion`, `kSpecification`.

## Where the ALP-TAS frame comes from

ALP-TAS has no published specification. Its field layout, the two cipher stages, their keys and the longitude step table follow the community's description of the 2024 protocol, [pastebin.com/YB1ppAbt](https://pastebin.com/raw/YB1ppAbt). The frames `test/test_frame.cpp` pins guard the encoder against regressions.

## Two things an ALP-TAS frame is given before it is refused

`correct()` runs the erasure correction the ADS-L path has always had. §C.2.1 is Manchester, so a chip pair that decoded to neither symbol is an error whose position the receiver already knows: `wire::manchester_decode` marks it, skyBlip's `protocol::Frame` carries the map, and the frame CRC says which combination of those flips was transmitted. Both frames hand that search to `wire::repair` (`protocols/common/README.md`), which leaves a frame no combination repairs exactly as it arrived. Up to six marked bits, which is 64 combinations. Beyond that the burst is refused as damaged. The syndromes come out of the CRC's own linearity: flipping a data bit moves the check by `crc16_ccitt(that bit alone, init 0)`, and flipping a carried CRC bit moves it by the bit itself.

Until 2026-09-19 the ALP-TAS path checked the CRC and threw the frame away, so every burst with one dead chip pair was a `CRC` row while the same damage on an ADS-L frame was corrected and drawn. On a bench that difference is invisible, because nothing at -15 dBm has dead chips. At range it is receptions.

`keyed_second()` answers the question a `DEC` row could not. The key stage is `utc_s >> 4`, so the decrypt is correct for every second inside the sender's own 16-second block, and the 4 time bits inside the frame then pin the second to ±1. Both sides of that are cheap to exploit: decrypt once per block the search window reaches, check the two fields every frame carries as constants (the trailing byte is 0, the low nibble of the pattern at bit 56 is 3), and the time bits name the sender's second exactly. The window is `kAlptasKeyWindowS`, 18 seconds, which is GPS-UTC: a receiver reporting GPS time instead of UTC is the one clock fault a device holding a valid fix can plausibly have, and a disagreement wider than that is not a clock, it is noise that framed.

The frame is refused either way. What the search buys is the tape saying `KEY+18` instead of `DEC`, which is the difference between a support case about a radio and one about a receiver's time.

