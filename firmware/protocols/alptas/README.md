# protocols/alptas

This stack is the ALP-TAS air frame, 2024 protocol, message type 2: the frame (`frame.{h,cpp}`), its two cipher stages and their keys, its CRC-16, the erasure repair a receiver runs before the CRC, and the sync word it shares a window with ADS-L through. `version.h` names it in `kSpecification`.

`decode()` and `encode()` work on `Position`, the frame's own fields in its own units. skyBlip maps it to an `AircraftObservation` in `core/protocol/alptas.*`. `decode()` needs the receiver's own position, because the frame carries only the low 20 bits of each coordinate step. It also needs the UTC second the frame arrived in, because the mask stage is keyed on it.

## Where the frame comes from

ALP-TAS is skyBlip's own reading of the 2024 protocol, from public sources: the community's description of the frame, [pastebin.com/YB1ppAbt](https://pastebin.com/raw/YB1ppAbt), the open receivers that read it, and published descriptions of its fields. The reading decides four layout points:

- The aircraft type is 5 bits, so types 16 to 19 are eVTOL and the three UAS classes.
- The address type is 2 bits, followed by a 2-bit urgency.
- A random address (type 0) is 32 bits, with its top byte at bit 40 and the random byte that names the next address at bit 32.
- The last byte is the SIL, SDA and NIC. Every frame heard so far left it at zero.

The ten frames `test/test_cassettes.cpp` replays are synthesized, not recorded on air: each scene in the French Alps was encoded once and pinned. Replace them with real receptions when there are some.

## Repair before refusal

`correct()` runs the same erasure repair as ADS-L. §C.2.1 is Manchester, so a chip pair that decoded to neither symbol is an error at a known position: `wire::manchester_decode` marks it, and `wire::repair` searches the flips of the marked bits for one the CRC accepts (`../common/README.md`). It accepts up to six marked bits, 64 combinations, and refuses a burst with more. A data bit moves the check by `crc16_ccitt(that bit alone, init 0)`, and a carried CRC bit moves it by the bit itself.

`keyed_second()` finds the second a sender's clock was on. The key stage is `utc_s >> 4`, so one decrypt is right for every second of the sender's 16-second block, and the 4 time bits in the frame then pin the second to +-1. It decrypts once per block in the window and checks the fields a right key leaves in range. The window is `kKeyWindowS`, 18 s, which is GPS-UTC: a receiver reporting GPS time instead of UTC is the one clock fault a device with a valid fix can plausibly have.

## What a right key looks like

The cipher starts at byte 8. The address, its type, the urgency, the flags, the sub-type and the version go in clear, so they say nothing about the key. Every field after them reads as noise under another block's key. A right key leaves the fix on a whole second, with the two quarter-second bits at 0, the aircraft type at 19 or less, the NIC at 11 or less and the track under 360 degrees. Those checks are about 3.6 bits, so a wrong key passes them about one time in twelve.

A frame whose SIL, SDA and NIC are all 0 adds 8 bits of zeros, so it keeps the +-1 second the time bits allow. A rated frame has no such byte, so its time bits must name the receiver's second exactly, and `core/protocol/alptas.cpp` refuses one that places its sender beyond radio range. `keyed_second()` names another second only from an unrated frame, and only when one block in the window passes.

The frame is refused either way. The search makes the capture say `KEY+18` instead of `DEC`, which tells a receiver's clock fault from a radio fault.
