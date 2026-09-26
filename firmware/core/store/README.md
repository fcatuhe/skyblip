# core/store

One flash partition, two rings on it. `Flights` is the pilot's log, written at one record every four seconds; `Diagnostics` is the engineering capture, written in bursts that fill a sector in under a minute. They share the sectors rather than each owning a fixed slice, because a device that never records a diagnostics capture should still hold every hour the partition can, and a capture taken once a year should not have cost the pilot a fixed tenth of their season.

Pure, framework-free, host-tested, and it never touches flash. `SectorAllocator` decides which sector is next; the service that owns the part does the erasing and the writing.

## The label

`sector.h` is the 16 bytes at the head of every claimed sector, and the boot scan reads nothing else: one 16-byte read per sector finds both rings in 5280 bytes of a 1.29 MB partition, where reading the partition to find the end of it would be a quarter of a second of SPI on every boot.

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

The session-start bit is the answer to the one question a renumbered ring cannot otherwise be asked. A session's records are numbered from the first sector that still carries its id, so once the ring has recycled the sector a session opened in, what is left reads exactly like a whole session that began later. The bit is written on the first claim of a session and on no other, it is never set afterwards, and it is under the CRC, so a suffix cannot acquire one: a session whose lowest-sequence sector does not carry it is reported truncated in the `list` reply (`../../products/skyblip_go/services/record_store.cpp`).

Rotation itself is not the defect; a rotation nobody counted is. `lost_sectors(owner)` counts, at the moment the decision is made and not in a poll after it, every sector recycled out from under the session that owner is writing - the ring recycling its own oldest, and the other ring evicting it. The capture reads that counter as it drains and turns each loss into a `diag::Gap` on the flash, followed by a fresh `Boot` and `Config`, so every surviving suffix still names the build and the settings that produced it (`../diag/README.md`).

A sector this build cannot make sense of is quarantined rather than reclaimed: `quarantine(sector)` takes it out of the free list, out of `owned()`, and out of what an erase-all touches. Three things reach it, all from the boot scan: a read the part refused (counted apart, because a flash that will not read is a fault and not a free sector), a label that decodes but carries a version or an owner this build does not know, and a label whose record size is not the one both rings write. The alternative - treating any of them as free space - erases evidence on the next claim.

The version is 2. Version 1 had no owner byte and its CRC covered only the first 12 bytes, so there was no room to grow one under the checksum: the label was re-cut rather than extended, and a v1 partition reads as free space and is reclaimed sector by sector.

`sequence` is one counter for the whole partition, it starts at one and it never repeats. That single counter is what makes the scan sufficient:

| Question | Answer from the labels alone |
|---|---|
| a ring's frontier | the highest sequence carrying that owner |
| a ring's oldest | the lowest sequence carrying that owner |
| a session's sectors, in order | that owner's sectors carrying that session id, by sequence |
| a free sector | a header that does not decode, or one numbered zero |

Two counters, one per ring, would answer neither: the interleaving of the two rings would be lost, and with it the order the sectors were claimed in.

The allocator keeps the owner, the sequence and the session id of every sector in RAM, filled from that one scan at boot and from every claim after it. Two questions are answered out of it and neither goes back to flash: `next_sector()` walks a ring in the order it was written, which is how an index of sessions is rebuilt, and `session_sector()` gives the nth sector of one session, which is how a record index becomes an address. A session's sectors are consecutive in sequence and in nothing else, because the other ring claims in between, so neither answer may be arithmetic on a first sector.

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

Flights eats Diagnostics before it eats itself, because a flight is the thing the device exists to record and a capture is a debugging aid with a session in front of it. Diagnostics grows the other way round: it takes from Flights down to the floor and only then recycles itself, so a device that captures once fills the whole partition above the protected hours rather than stopping at the two sectors it happened to be holding. On a 330-sector pool the steady state is 64 sectors of flights and 266 of diagnostics: 45,220 slots, which is about 68 minutes of a full capture at eleven records a second and 188 hours of a power run.

`Claim::session_start` says whether the sector handed out is the first the session owns, which is what the service writes into the label. The allocator decides it rather than the caller: a session with no sector on the partition yet is opening, and one that already has a sector is continuing, even if what it is continuing is a suffix of itself.

A ring's frontier is never handed out: it is the sector being written into, and a ring that owns exactly one sector therefore has nothing to evict. Free sectors are taken forward of the newest, which is what keeps a ring's sectors contiguous while it is the only ring using the partition, and what makes the first claim on a virgin partition sector zero.

## The floor

`kFlightsFloorHours` is 12, and it is the promise the pool makes: twelve hours of flights are never evicted by a diagnostics capture. It is spelled in flight hours rather than in sectors because hours are what the promise is made in, and `flights_floor_sectors()` turns them into the count the policy compares against, from the record period and the slots a sector holds. At 170 slots a sector and one record every 4 s, a sector is 11 min 20 s, so twelve hours is 64 of the partition's 330 sectors: a device that has flown all season keeps its last dozen hours whatever the bench does to it, and a diagnostics capture that wants more than the remaining 266 sectors is told no.

## The prepared spare

Each ring keeps one spare: a sector already erased and not yet labelled, so a claim is instant.

It exists because of the rate difference. At the flights cadence a sector lasts 11 minutes and the erase that claims a new one is invisible; at diagnostics rates a sector fills in under a minute, so that erase lands next to the direct slot often enough to be a problem. Preparing the spare is the same decision as claiming, so it runs through the same policy: `prepare(owner)` names the sector to erase and takes it away from its previous owner at once, since the erase is about to destroy what was in it.

Three steps, and the service owns the middle one: `prepare()` names the sector, the service erases it in a pass where the radio is not keying, `note_erased()` says it is ready. An erase that fails leaves the spare reserved and dirty, so the next pass tries the same sector again, and a claim that arrives first takes it and erases it itself. A spare is never handed to the other ring and never handed out twice.

A spare carries no label, so a power cut turns it back into a free sector, which is exactly what it is.

## Giving a ring back

`release(owner)` is the other half of the erase: every sector that owner holds goes back to free, its spare with it, and the other ring is untouched. The sequence counter is not wound back, because it is the one number that must never repeat - a released sector is claimed again with a number above everything on the partition, so the walk order of what survived is still the order it was written in.

What the service erases before it calls this is the service's business, and the two rings answer it differently: the pilot's "erase log" clears every sector that is not the capture's, because that is what it did when there was one ring on the partition, while a capture is never erased on request at all - the allocator recycles it (`../../products/skyblip_go/README.md`).
