# parts/bhi260

The Bosch BHI260AP on the T-Echo Plus sensor bus, at 0x28 (0x29 with the address pin high). It is what puts the ball in the six-pack's turn coordinator, and it is the only part on this board that arrives without a program.

## Why this is not a register map

A BMA423 or an ICM-20948 is a chip with an accelerometer behind some registers: probe it, set a range, read six bytes. The BHI260AP is a sensor hub - a Fuser2 core, a boot ROM, and 100 KB of RAM with nothing in it. Until a host uploads Bosch's firmware image and boots the core from RAM, the part answers its address, names itself `0x89` on `PRODUCT_ID`, and has no accelerometer to read at all. There is no flash on this board for it to boot from itself (`BOOT_STATUS` reports `NO_FLASH`), and the rail it hangs off is switched, so the upload is paid again after every power cycle and after every `SYSTEM OFF`.

That is the whole reason this driver is a state machine rather than four register writes. `firmware/` holds the image, where it came from, and where a slim image finds it. `load()` takes a `Bhi260::Image`, read a chunk at a time: `LinkedImage` over the bytes a full image carries, or `Bhi260ImageStore` over the copy on the external flash.

## The sequence

| Stage | What it does | How it can end |
|---|---|---|
| `Absent` | nothing answered 0x28 or 0x29, or what answered is not a BHI260 | the board grants no capability |
| `Idle` | probed and named, waiting for an image | `load()`, or `NOBLOB` on the status line when a slim image has none to give it (`firmware/README.md`) |
| `Resetting` | `RESET_REQ`, then 100 ms | the settle expires |
| `HostInterface` | polls `BOOT_STATUS` for `HOST_INTERFACE_READY` every 50 ms | ready, or `Timeout` after 2 s |
| `Uploading` | one 240-byte chunk of the image per `service()` | the last chunk sends `BOOT_PROGRAM_RAM` |
| `Booting` | polls `BOOT_STATUS` for `FW_VERIFY_DONE` | `Crc` on the verify-error bit, `Timeout` after 5 s |
| `Initialising` | drains the FIFO until the hub announces itself | the `Initialized` meta event, or 2 s |
| `Configuring` | reads `KERNEL_VERSION`, checks the accelerometer is in the image, sets the range and rate, reads the configuration back | `NotFound` if the image has no accelerometer, `Unsupported` if the rate did not stick, `Down` if the kernel version reads zero |
| `Running` | drains the FIFO every 200 ms | `Down` the moment the bus stops answering |

Every one of those stages has a word, and `status` prints it beside the ball it is waiting for: `stage_text()` is where it is or, once it has failed, where it stopped, and `fault_text()` is why. `Running` has a second failure the stage cannot show - a part that boots, answers and reports nothing - so `fifo_bytes()`, `unparsed_events()` and the error register the part reads every pass (`hub_error()`, 0x2E) are on the same field. That pair is the only account the device gives of a bring-up nobody can watch, and it is what a bench reads instead of guessing from an empty cage.

### What the hub says about itself

A silent hub is rarely silent about why. Meta events (system ids 254 and 248, four bytes each) are the firmware's own commentary, and `meta_event()` keeps the last one: 16 is `Initialized`, 12 a FIFO overflow, 19 a reset nobody asked for. Meta event 11 is a sensor error and it carries the two bytes that end the investigation, which virtual sensor and which code: `errored_sensor()` and `sensor_error()`, read against the list in `examples/common/common.c` of the reference. An accelerometer the hub refused to start reads there as sensor 4 and a code in the 0x20s, where the FIFO can only report an absence.

`interrupt_status()` is the other half of that account. The Plus has no HIRQ line, but `INT_STATUS` (0x2D) still reports what the part would have raised it for: bit 0 asserted, the 0x06 field the wakeup FIFO, the 0x18 field the non-wakeup one, 0x20 a status response. Read every pass beside the error register, it separates the two ways a running hub has no ball - nothing produced, or something produced that our FIFO read never collected.

That is what it was added for and what it caught. A bench read `IMU RUN B18 I03`: nothing in the non-wakeup FIFO, and the wakeup FIFO holding data since boot, because this driver read one of the two. The hub copies its meta events into both, so the wakeup FIFO had been holding its copy of the boot traffic with the interrupt asserted the whole time, and anything the hub chose to put there was never going to arrive. Both are drained now, from `Initialising` on, the same code against a different register. That much was in the reference the whole time, one layer above the one this driver was written from: `bhy2_hif.c` is the transport and knows only the channel you name, while `bhy2_get_and_process_fifo` in `bhy2.c` reads the wakeup FIFO, then the non-wakeup FIFO, then the status channel, every call. SoftRF has nothing to say here - its BHI260AP read path is `#if 0 /* TODO */` with the axes marked `/* TBD */`, and its IMU page excludes the part.

One error code never reaches `hub_error()`. 0x77 is `Host Download Channel Empty`, which is the hub's answer to a FIFO read with nothing in it - the Plus leaves HIRQ unconnected, so every pass over an idle FIFO earns one, and reporting it would put a permanent error on the glass that means nothing more than "we polled". Every other code stands.

Nothing here blocks or sleeps. Every wait is a deadline against the `now_ms` the board already passes down, which is what lets a host test walk the whole bring-up under a clock it advances, and what keeps the upload out of the service loop's way.

### Why the upload is paced, and why the bus runs at 400 kHz

The image is 101 KB. A 240-byte chunk is 5.4 ms of bus at 400 kHz and 22 ms at 100 kHz, and the service loop's pass is 10 ms (`runtime::kServiceStepMs`), so the bus was moved to fast mode in the devicetree rather than the chunk made smaller: at 100 kHz one chunk is longer than the pass it is sent from. One chunk per pass puts the ball on the glass about four seconds after boot, with the radio thread - which is cooperative and higher priority than the loop - untouched throughout.

Chunk framing follows the reference (`BHY2_SensorAPI`, `bhy2_hif.c`): the first packet carries the four-byte command header, `UPLOAD_TO_PROGRAM_RAM` with the length in 32-bit words, and every packet after it is raw image bytes written to the command channel at register 0x00, padded to a word.

### Why the configuration waits for a meta event

`FW_VERIFY_DONE` says the image verified, not that the sensor framework behind it is up, and a configuration that arrives in between is dropped without a word: the command channel accepts it, no error register moves, and the part then boots, answers every pass and streams nothing. That is what a bench read as `IMU RUN B18 M16` - eighteen bytes out of the FIFO, the hub announcing itself after it had already been told what to do, and no accelerometer frame ever.

So the driver follows the reference's own order (`examples/quaternion/quaternion.c`: boot, kernel version, drain the FIFO, then configure) and waits for meta event 16, `Initialized`, before it sends anything. A hub that never sends it is configured anyway once 2 s are up, because a ball that might work beats a stage word that is certainly stuck; `meta_event()` stays zero there, and the status field shows the count with no `M` beside it.

### Why the configuration is read back

Waiting for `Initialized` was not enough: the bench still read `IMU RUN B18 M16`, a hub that announced itself, took both commands and streamed nothing. Commands to this part are fire-and-forget - the command channel takes the bytes, and a hub that will not honour one says so nowhere a register read can find.

So `Configuring` asks two questions over the status channel (0x03), the way `bhy2_hif_get_parameter` does: a command whose opcode is the parameter with 0x1000 set, then a wait on the status bit of `INT_STATUS` (0x2D), then a four-byte header of code and length followed by the payload. Parameter 0x011F is the bitmap of virtual sensors this image carries, and the accelerometer missing from it is `NOSENS` - an image that has no such sensor to enable, which no amount of configuring will fix. Parameter 0x0504 is what the hub thinks sensor 4 is set to, read straight after the two commands, and a sample rate of zero there is `NOCFG`: the hub took the command and did not apply it.

Both are answered only when the hub answers. A parameter read that times out (500 ms) or comes back with a code for a different parameter leaves the bring-up as it was and moves on, because a diagnostic that can fail a working part is worse than the silence it replaces. Only a clear negative stops the bring-up.

### What is configured, and what is read

`ACC` (virtual sensor 4, the bias-corrected accelerometer) at 12.5 Hz with no report latency, range 4 g. The rate is written as an IEEE-754 word because that is what the command takes; this firmware has no floats, so the bit pattern is a named constant and the part model decodes it back in the test. 4 g rather than 2 g because a turn in turbulence pulls past 2 g, and a saturated axis would bend the ratio the ball is drawn from; the resolution it costs is 0.12 mg against the 12.5 mg the ball's pixel is worth.

Both FIFOs are read the way the reference does: two bytes for the count when nothing is outstanding, then up to 64 bytes a pass until the count is drained. The non-wakeup one (0x02) is where a non-wakeup virtual sensor puts its frames, the wakeup one (0x01) carries its own copy of every meta event, and a channel nobody reads is a channel that fills; each keeps its own carry, because an event can straddle two passes on either. Events are parsed by the size table the reference carries for system ids (padding, the three timestamp forms, meta events, filler) plus the seven bytes an accelerometer frame takes. An id with no size is an id we cannot step over, so the rest of that stream is dropped and the count is on `unparsed_events()`: the next transaction starts on an event boundary again.

## What this part does not do

No gyroscope, no fusion output, no step counter, no wrist gestures, no interrupt line - the Plus leaves HIRQ unconnected, so everything here is polled. The hub is capable of all of it and none of it has a reader, so none of it is configured: the six-pack needs one lateral acceleration, and `core/flight/slip.h` is where that becomes a ball. The gyroscope is the one worth naming, because it is in the image and costs the better part of a milliamp against the accelerometer's tens of microamps: no instrument on this device reads a body rate, so virtual sensor 13 is never subscribed to and the part stays powered down.

## The frame

The driver reports the chip's own axes. Which way the chip sits in the case is the board's fact and lives in `boards/lilygo/t_echo_plus/imu_mount.h`: the one place a rotation gets corrected, and nothing downstream has an axis in it.

The bench measured it in three readings: a quarter turn on the first unit with a working ball, the ball's direction on the second, and the g-meter reading inverted on the third, which is the one that showed the part is face down as well as turned. The mount maps the chip's +X to the case's up, its +Y to the case's right and its +Z out the nose, and the rates with it. That is the same turn the panel is mounted at (`GlassRotation::Deg270` in `hardware/platform/zephyr/platform.h`), which is what a part placed on a board that sits sideways in its case looks like from both ends.
