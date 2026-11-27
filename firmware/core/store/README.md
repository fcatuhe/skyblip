# core/store

One flash partition holds two rings: `Flights`, the pilot's log at one record a second, and `Diagnostics`, the engineering capture, which fills a sector in under a minute. They share the sectors rather than each owning a fixed slice, so a device that never captures still holds every hour the partition can.

The code is pure, host-tested and never touches flash. `SectorAllocator` decides which sector is next. The service that owns the part erases and writes.

## The header

`sector.h` is the 16 bytes at the head of every claimed sector. The boot scan reads only these: 4768 bytes of a 1.16 MB partition, where reading it all would cost a quarter of a second of SPI on every boot.

| Byte | Field |
|---|---|
| 0-1 | magic, `SB` |
| 2 | version |
| 3 | owner |
| 4-7 | sequence |
| 8-11 | session id, the owner's own name for the run this sector belongs to |
| 12 | record bytes, so a later codec's sectors are still walkable |
| 13 | flags: bit 0 is `kSectorFlagSessionStart`, the sector the session opened in |
| 14-15 | CRC-16-CCITT over bytes 0-13 |

The session-start bit tells a whole session from what is left once the ring recycles its first sector, which otherwise reads like a session that began later. The allocator sets it, as `Claim::session_start`, on a session's first claim only, under the CRC, so a session whose lowest-sequence sector lacks it is reported truncated in the `list` reply (`../../products/common/services/record_store.cpp`).

`lost_sectors(owner)` counts every sector recycled from under the session that owner is writing, by either ring, at the moment the decision is made. The owner names that session with `open_session()` before its first claim and ends it with `close_session()`. Until that first claim the ring's frontier still belongs to the last session, so a sector of the last session never counts against the new one. The capture turns each loss into a `diagnostics::Gap` followed by a fresh `Boot` and `Config` (`../diagnostics/README.md`).

`quarantine(sector)` removes a sector from the free list, from `owned()` and from an erase-all, because reclaiming a sector this build cannot read would erase evidence. The boot scan quarantines a read the part refused (counted apart, as a flash fault), a header with an unknown version or owner, and a header whose record size is not the one both rings write. Version 1 had no owner byte, and a v1 partition reads as free space.

`sequence` is one counter for the whole partition. It starts at one and never repeats, so the headers alone answer:

| Question | Answer from the headers alone |
|---|---|
| a ring's frontier | the highest sequence carrying that owner |
| a ring's oldest | the lowest sequence carrying that owner |
| a session's sectors, in order | that owner's sectors carrying that session id, by sequence |
| a free sector | a header that does not decode, or one numbered zero |

One counter per ring would lose the order in which the two rings claimed.

The allocator keeps every sector's owner, sequence and session id in RAM, in a `SectorSlots` table the product sizes to its board's partition: `kLogSectors` in the board's `log_flash.h`, which a `static_assert` holds to the devicetree. A sector costs 9 bytes, because the session-start flag rides in the top bit of the owner's byte. `session_run()` walks a ring in write order, one run of a session's back-to-back sectors at a time. `session_sector()` gives the nth sector of a session, counted from the oldest it still holds. The other ring claims in between, so neither may be arithmetic on a first sector. `unused_session_id()` names a new session: the first name from the one asked for up that the ring does not hold, since a claim under a name the ring holds continues that session. It costs one scan per name it skips. `recycled_sectors()` counts the sectors each ring has recycled since boot from the front of the session it last took one from (`../comms/README.md`, A record keeps its index while the ring rotates).

Both run on the service loop that arms the radio, so neither may scan the pool once per sector. A scan visits all 298 sectors on the T-Echo, at about 1.4 us a sector. A run costs three scans. `session_sector()` bisects on sequence: the nth sector is at least n above the session's lowest, and at least as far below its highest as the session has sectors after it.

## The claim

`claim(owner)` is the whole policy, in precedence order:

| Situation | Sector handed out |
|---|---|
| a free sector exists | the first free one forward of the newest, whoever asks |
| Flights, nothing free | the Diagnostics sector with the lowest sequence |
| Flights, no Diagnostics sector to take | its own oldest |
| Diagnostics, nothing free, Flights above the floor | the Flights oldest |
| Diagnostics, Flights at the floor | its own oldest |
| Diagnostics, Flights at the floor and owning nothing to recycle | nothing, and the caller counts the refusal |

Flights evicts Diagnostics first, because a flight is what the device exists to record. Diagnostics takes from Flights down to the floor before it recycles itself, so one capture fills the partition above the protected hours. On a 298-sector pool that leaves 170 sectors of diagnostics: 28,900 slots, about 32 minutes of a full capture at fifteen records a second (`Recorder::kPeriodicRecordsPerSecond`), or 120 hours of a power run.

A ring's frontier is never handed out, because it is being written. Taking free sectors forward of the newest keeps a lone ring contiguous.

The claim labels the sector before the service erases it and writes its header. If the part refuses either, the service calls `abandon(sector)`, which frees the label. Otherwise the session would count a sector with no header of its own, and a read would serve whatever that sector still holds. The next claim looks for a free sector forward of the abandoned one.

## The floor

`kFlightsFloorHours` is 6: a capture never evicts the last six hours of flights. At 170 slots a sector and one record a second, a sector is 2 min 50 s, so six hours is 128 of 298 sectors, and `flights_floor_sectors()` does that sum. Twelve hours would leave a capture about 11 minutes. A flight's events ride in its sectors (`../flight/README.md`), so a busy sky shortens the six hours a little.

## The prepared spare

Each ring keeps one spare, erased and without a header, so a claim does not erase next to the direct slot. `prepare(owner)` names it through the claim policy and takes it from its owner at once. The service erases it in a pass where the radio is not keying, then calls `note_erased()`. A failed erase leaves the spare reserved and dirty for the next pass, and a claim that arrives first erases it itself. A spare is never handed to the other ring nor handed out twice. A power cut turns it back into a free sector.

## Giving a ring back

`release(owner)` frees every sector that owner holds, its spare included, and leaves the other ring untouched. The sequence counter is not wound back, so survivors keep their write order. The pilot's "erase log" clears every sector that is not the capture's. A capture is never erased on request: the allocator recycles it (`../../products/skyblip_go/README.md`).
