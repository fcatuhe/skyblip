# parts/bhi260

The Bosch BHI260AP on the T-Echo Plus sensor bus, at 0x28 (0x29 with the address pin high). It puts the ball in the six-pack's turn coordinator, and it is the only part on this board that arrives without a program.

An IMU needs a settling delay after power-up before it answers: SoftRF (`platform/nRF52.cpp:1145-1147`) waits 90 ms. Here the rails go up in `board.c` at `PRE_KERNEL_1`, and `Board::begin()` probes the hub only after the platform's 50 ms wait for the SX1262's TCXO, so boot order pays most of the delay. Keep the probe after that wait.

## Why this is not a register map

The BHI260AP is a sensor hub: a Fuser2 core, a boot ROM, and 100 KB of empty RAM. Until a host uploads Bosch's firmware image and boots the core from RAM, the part answers its address and names itself `0x89` on `PRODUCT_ID`, but has no accelerometer to read. This board gives it no flash to boot from (`BOOT_STATUS` reports `NO_FLASH`), and its rail is switched, so the upload is paid again after every power cycle and every `SYSTEM OFF`.

So the driver is a state machine. `firmware/` holds the image, where it came from, and where a base image finds it. `load()` takes a `Bhi260::Image`, read a chunk at a time: `LinkedImage` over the bytes a full image carries, or `Bhi260ImageStore` over the copy on the external flash.

## The sequence

| Stage | What it does | How it can end |
|---|---|---|
| `Absent` | nothing answered 0x28 or 0x29, or what answered is not a BHI260 | the board grants no capability |
| `Idle` | probed and named, waiting for an image | `load()`, or `NOBLOB` on the status line when a base image has none to give it ([`firmware/README.md`](firmware/README.md)) |
| `Resetting` | `RESET_REQ`, then 100 ms | the settle expires |
| `HostInterface` | polls `BOOT_STATUS` for `HOST_INTERFACE_READY` every 50 ms | ready, or `Timeout` after 2 s |
| `Uploading` | one 240-byte chunk of the image per `service()` | the last chunk sends `BOOT_PROGRAM_RAM` |
| `Booting` | polls `BOOT_STATUS` for `FW_VERIFY_DONE` | `Crc` on the verify-error bit, `Timeout` after 5 s |
| `Initialising` | drains the FIFO until the hub announces itself | the `Initialized` meta event, or 2 s |
| `Configuring` | reads `KERNEL_VERSION`, checks the accelerometer is in the image, sets the range and rate, reads the configuration back | `NotFound` if the image has no accelerometer, `Unsupported` if the rate did not stick, `Down` if the kernel version reads zero |
| `Running` | drains the FIFO every 200 ms | `Down` the moment the bus stops answering |

`status` prints each stage's word beside the ball: `stage_text()` is where it is or where it stopped, and `fault_text()` is why. A part can also boot, answer and report nothing, so the same field carries `fifo_bytes()`, `unparsed_events()` and the error register read every pass (`hub_error()`, 0x2E). That is the only account the device gives of a bring-up nobody can watch.

Nothing blocks or sleeps. Every wait is a deadline against the `now_ms` the board passes down, so a host test walks the whole bring-up under a clock it advances, and the upload stays out of the service loop's way.

### What the hub says about itself

Meta events (system ids 254 and 248, four bytes each) are the firmware's own commentary, and `meta_event()` keeps the last one: 16 is `Initialized`, 12 a FIFO overflow, 19 an unrequested reset. Meta event 11 is a sensor error and carries which virtual sensor and which code: `errored_sensor()` and `sensor_error()`, read against the list in the reference's `examples/common/common.c`. An accelerometer the hub refused to start reads there as sensor 4 and a code in the 0x20s.

`interrupt_status()` reads `INT_STATUS` (0x2D), which reports what the part would raise HIRQ for even though the Plus leaves HIRQ unconnected: bit 0 asserted, the 0x06 field the wakeup FIFO, the 0x18 field the non-wakeup one, 0x20 a status response. Read beside the error register, it tells a hub that produced nothing from one whose output our FIFO read never collected.

Drain both FIFOs, from `Initialising` on. The hub copies its meta events into both, and a FIFO nobody reads fills, as `bhy2_get_and_process_fifo` in the reference's `bhy2.c` does: wakeup, then non-wakeup, then the status channel, every call.

`hub_error()` never reports 0x77, `Host Download Channel Empty`. The hub answers it to every read of an empty FIFO, which a polled part does every idle pass, so it means only "we polled".

### Why the upload is paced, and why the bus runs at 400 kHz

The image is 101 KB. A 240-byte chunk takes 5.4 ms of bus at 400 kHz and 22 ms at 100 kHz, and the service loop's pass is 10 ms (`runtime::kServicePassCeilingMs`). So the devicetree runs the bus at 400 kHz and one chunk goes per pass. The ball reaches the glass about four seconds after boot, and the radio thread, cooperative and higher priority than the loop, is untouched.

Chunk framing follows the reference (`BHY2_SensorAPI`, `bhy2_hif.c`): the first packet carries the four-byte command header, `UPLOAD_TO_PROGRAM_RAM` with the length in 32-bit words. Every later packet is raw image bytes written to the command channel at register 0x00, padded to a word.

### Why the configuration waits for a meta event

`FW_VERIFY_DONE` says the image verified, not that the sensor framework behind it is up. A configuration sent in between is dropped without a word, and the hub then streams nothing. So the driver follows the reference's order (`examples/quaternion/quaternion.c`: boot, kernel version, drain the FIFO, then configure) and waits for meta event 16, `Initialized`. A hub that never sends it is configured anyway after 2 s, because a ball that might work beats a stage word that is certainly stuck. `meta_event()` then stays zero, and the status field shows no `M`.

### Why the configuration is read back

Commands to this part are fire-and-forget: a hub that will not honour one says so nowhere a register read can find. So `Configuring` asks two questions over the status channel (0x03), as `bhy2_hif_get_parameter` does: a command whose opcode is the parameter with 0x1000 set, a wait on the status bit of `INT_STATUS`, then a four-byte header of code and length followed by the payload.

- Parameter 0x011F is the bitmap of virtual sensors this image carries. The accelerometer missing from it is `NOSENS`, which no configuring will fix.
- Parameter 0x0504 is what the hub thinks sensor 4 is set to, read straight after the two commands. A sample rate of zero there is `NOCFG`: the hub took the command and did not apply it.

A parameter read that times out (500 ms) or answers for a different parameter leaves the bring-up going, because a diagnostic that can fail a working part is worse than silence. Only a clear negative stops the bring-up.

### What is configured, and what is read

`ACC` (virtual sensor 4, the bias-corrected accelerometer) at 12.5 Hz with no report latency, range 4 g. The command takes the rate as an IEEE-754 word. This firmware has no floats, so the bit pattern is a named constant. The range is 4 g because a turn in turbulence pulls past 2 g, and a saturated axis would bend the ratio the ball is drawn from. It costs 0.12 mg of resolution against the 12.5 mg one pixel of ball is worth.

Each FIFO is read as the reference does: two bytes for the count when nothing is outstanding, then up to 64 bytes a pass until the count is drained. The non-wakeup FIFO (0x02) holds the accelerometer's frames, and the wakeup FIFO (0x01) a copy of every meta event. Each keeps its own carry, because an event can straddle two passes. Events are parsed by the reference's size table for system ids (padding, the three timestamp forms, meta events, filler) plus the seven bytes of an accelerometer frame. An id with no size cannot be stepped over, so the rest of that read is dropped and counted on `unparsed_events()`, and the next read starts on an event boundary.

## Names

The register, command and parameter constants are spelled in full. This is how they map to the reference's names (`BHY2_SensorAPI`).

| Our name | Reference name |
|---|---|
| `kRegisterResetRequest` (0x14) | `RESET_REQ` |
| `kRegisterProductId` (0x1C) | `PRODUCT_ID` |
| `kRegisterKernelVersion` (0x20) | `KERNEL_VERSION` |
| `kRegisterBootStatus` (0x25) | `BOOT_STATUS` |
| `kRegisterInterruptStatus` (0x2D) | `INT_STATUS` |
| `kBootHostInterfaceReady` | `HOST_INTERFACE_READY` |
| `kBootFirmwareVerifyDone` | `FW_VERIFY_DONE` |
| `kCommandUploadToProgramRam` (0x0002) | `UPLOAD_TO_PROGRAM_RAM` |
| `kCommandBootProgramRam` (0x0003) | `BOOT_PROGRAM_RAM` |
| `kSystemEventIdPadding`, `kSystemEventIdMetaEvent`, `kSystemEventIdMetaEventWakeup` (0, 254, 248) | the system ids for padding and meta events |

## What this part does not do

No gyroscope, fusion output, step counter, wrist gesture or interrupt line: the Plus leaves HIRQ unconnected, so everything is polled. Nothing reads the rest, so nothing else is configured. The six-pack needs one lateral acceleration, and `core/flight/slip.h` turns it into a ball. The gyroscope is in the image but costs the better part of a milliamp against the accelerometer's tens of microamps, and no instrument reads a body rate, so virtual sensor 13 is never subscribed to and stays powered down.

## The frame

The driver reports the chip's own axes. `boards/lilygo/t_echo_plus/imu_mount.h` holds how the chip sits in the case, the one place a rotation is corrected. The chip is face down and turned: the mount maps its +X to the case's up, its +Y to the case's right and its +Z out the nose, and the rates with it. That is the same turn as the panel's (`GlassRotation::Degrees270` in `hardware/platform/zephyr/t_echo_plus.h`).
