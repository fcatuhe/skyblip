# comms

The companion link, from the side that does not know what Bluetooth is. A platform fills `ports::Link`; everything here speaks endpoints, sessions and JSON.

## Three endpoints, two shapes of send

`events::Endpoint` splits the link in three because the three have different owners. NMEA is the traffic picture and it is broadcast: every subscribed central gets the same bytes, and `ports::Link::send()` reports Ok when one of them took it, so a phone whose controller buffers are full cannot end the pass for the tablet beside it. Config and Log are conversations, and they go out through `send_to()`, addressed to the session whose request they answer.

That is why `payload_bytes()` with no argument is the *smallest* payload any connected central negotiated. One NMEA frame is formatted once and goes to all of them, so it has to fit the narrowest. An iPhone at ATT_MTU 185 sitting beside an Android at 247 pulls every broadcast frame down to 182 bytes, and that is correct, not a compromise: the alternative is formatting the picture once per central. A conversation is sized for its own session instead, by `payload_bytes_to()`, so a central that never exchanged its MTU narrows the broadcast and no one else's replies.

## Sessions

`LinkSessions` is the table, and it is the same object on both platforms: Zephyr's `connected`/`disconnected`/`att_mtu_updated` callbacks drive it on silicon, `raise_link()`/`drop_link()` drive it in the host suite. It exists so the lifecycle rules are proved once by `make test` rather than living inside a Bluetooth callback nothing can reach.

`kMaxSessions` must never exceed `CONFIG_BT_MAX_CONN`. The controller is what actually admits a central; a table larger than the controller's is a table with rows that can never fill, and a table smaller than it means a central the radio accepted that the firmware refuses to serve. A refused connect is counted, not silently served: the bus never hears of that session, so no service answers its frames, and `refused()` is how a bench sees it happened.

Events are one ordered queue rather than a queue per type. A connection is a lifecycle, and an Up read before the Down that came first leaves a service pushing at a link that is gone.

## The claim

Broadcast is for everyone. Configuration is not: two apps writing settings to one device is two apps disagreeing about what is stored, and a prompt authorised on one phone answered on another is a confirmation with no presence behind it.

So `LinkClaim` grants config and log to the first session that writes a command, and holds it until that session disconnects or releases it. A command from any other session is refused with a reason naming the state, never accepted and quietly ignored. The claim is what makes a *dropped* link safe to act on: only the holder's disconnect cancels a pending prompt and closes an upload window, and before the claim existed any second EFB walking out of range did both.

What a claim is not is access control. `CONFIG_BT_SMP` is off on this product, deliberately (see `products/skyblip_go/prj.conf`), so there is no identity behind a session id. The claim stops two cooperating apps from stepping on each other. What stops a hostile one is the MCUboot signature on the image and the confirmation gesture on the device itself.

## The log dialect

`log_link.h` is the log partition's half of the link: the same JSON `config.h` speaks, on its own endpoint, with three commands, list, read and erase. Every reply fits one frame and carries the record index it starts at, which is what makes the transfer acknowledged by construction: the next command IS the acknowledgement, a host that lost chunks asks again from the index it kept, and a chunk that never left the device consumed nothing, so asking again is the whole recovery.

`parse_log_request()` is the boundary. A request it does not understand comes back with `understood` false and `reason` set to the word a service refuses with, so a refusal names what was wrong rather than answering something adjacent.

### Either store, named on both halves

`core/store` puts two rings on the partition and this dialect addresses either through an optional `"log"` field on any command: `"flights"`, `"diagnostics"`, or absent, which is flights and therefore every client that exists today. A value that is neither is refused as `unknown_log`; it is never quietly read as flights, because the two stores share a session id space and the wrong one would answer with a plausible flight. The selector is `store::SectorOwner` itself rather than a second enum beside it, so a store this dialect can name is a store the allocator owns.

The service side refuses by name for the same reason the parser does: a store this build has no writer for is `no_diagnostics` rather than `no_storage`, because a phone told the partition is missing would stop asking, and an erase aimed at a store that is recycled rather than erased is `flights_only`.

A reply names its store by the same rule: `"log":"diagnostics"` on every diagnostics reply, absent for flights. Two fetches in flight on one link can then never misfile a chunk, and a flights chunk stays byte for byte what it was before the field existed. That symmetry is also what keeps the geometry: the chunk envelope is sized to the byte at the widest session id and record index (83), and the 20 bytes of `,"log":"diagnostics"` cost a 244-byte link one of its five records. `log_records_per_chunk()` takes the store for that reason and answers four where flights gets five; at the 498-byte ATT_MTU both reach the twelve-record ceiling.

### A window of chunks

`{"cmd":"read","session":S,"from":X,"count":K}` asks for K chunks back to back, `count` absent meaning one. One chunk per round trip was the throughput ceiling: a round trip on this link is 99 ms measured on the device, so a 288-byte chunk each time is about 2.9 kB/s, where notifications leave at a 498-byte frame per connection interval, roughly 16 kB/s. A full partition is seven minutes at the first rate and eighty seconds at the second.

K is clamped to `kLogReadChunksMax`, 8, at the parse boundary. Eight notifications are eight connection intervals, about a quarter of a second at the interval those figures were measured at, which is as much of a second as an offload may take from the traffic picture the same link is broadcasting; eight chunks is also 96 records, a little over half a sector.

`plan_log_window()` holds the arithmetic, and the three ways a window goes wrong are what it is tested on: it cuts the window at the end of the session instead of wrapping into the next one, it yields no chunks at all for a `from` past the last record, and it puts `eof` on the chunk that ends the session and on no other. A window changes nothing about the acknowledgement model, since each chunk still carries its own index: a host that received three of eight asks again from the fourth.
