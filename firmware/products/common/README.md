# products/common

The services more than one product runs, and the vocabulary they read. Why they live here and not in `runtime/` is in [`../../README.md`](../../README.md). What a service is, and the order a product steps its services in, is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "What a service is".

`services/` holds four services today, in namespace `common`:

- `TrafficService` owns `state.traffic`: it drains `bus.rf`, decodes each burst and feeds the table and the callsigns.
- `AlarmService` owns everything a product says out loud or shows with a light: it grades each target, then drives the buzzer, the motor and the lamp through `core/annunciation` and `core/indication`.
- `NmeaService` is the single writer of `events::Endpoint::Nmea`: the FLARM sentences a phone's map draws, once a second.
- `ConfigLinkService` is the companion link's device side: it answers `config`, `set` and the other config commands through `comms::ConfigService`, loads and persists the product's settings, names the unit on the air and takes a running image off probation.

No file here includes `products/<product>/*`. What a service needs from a product reaches it at construction, like everything else a service needs.

## Claims

`features.h` lists the claims a shared service branches on, and the capability each claim needs of the board. Which claims a build makes is the product's `kFeatures`, in its own `features.h`. A service asks `supported(declared, capabilities)` in its `setup()`, so a claim the hardware cannot serve is off rather than broken.

A feature with no reader is a line of marketing inside a header. The list said `CompanionLink` for as long as the tree had a NMEA characteristic nothing ever wrote to, and nothing anywhere could tell. So a bit joins the list on the commit that reads it, and leaves it with its last reader.

## Settings

A product owns its settings struct, its blob and its link keys. A shared service reads the few fields it needs through a small struct the product fills at construction. `AlarmSettings` holds references to the product's alarm on/off and alarm volume, so a change from the link or the menu reaches the alarm on its next tick, as it did when the alarm read the Go's `Settings` directly.

`settings_store.h` is the seam between the config link and a product's settings. `common::SettingsStore` extends `comms::ConfigStore`, the calls that answer a phone, with the four the link needs to keep the blob: `restore_defaults()`, `blob_version()`, `adopt()` and `to_blob()`. The product owns the struct, its layout, its migrations and its link keys. The link owns when the blob is read and written: the durable-write window, the power rule, a newer layout kept for a reverted image, and the copy kept for a revert. The Go's `go::SettingsStore` fills it from `go::Settings`. The link holds a blob in a buffer of `ConfigLinkService::kBlobBytes`, 64 bytes. A store returns 0 from `to_blob()` when its blob is wider than the buffer it is handed, and the link counts that, or any length past its buffer, as a failed write rather than writing it.

The link takes three more things from the product at construction. It asks `power::CutoffMonitor`, which the product's power service owns, whether the cell allows a settings write. It names the product, as `dfu::Product`, on the `update` frame. It advertises under the product's name, such as `skyBlip Go+`, which the product chooses from the parts fitted. It also reads the die temperature from `state.power`, where the power service publishes it.

The advertised name is not yet the product's alone. `identity::bluetooth_name()` puts the product's name before the unit's ADS-L address only once the unit is activated. A unit with no address advertises `activate 3F2A skyBlip` whatever name the product passed. That rule is the Go's: a Go must be activated before it transmits. Bravo is never activated and must advertise `skyBlip Bravo 3F2A`, so the name policy has to come from the product before Bravo ships (#176).

A link prompt is confirmed on the device through `comms::ConfigService`, which the product reaches as `config_link().config()`. A product service reads `pending()` and `prompt_allowed_in()`, shows the prompt, and calls `confirm()` or `cancel()`. On the Go that service is `ScreenService`, with the e-paper and the button. On Bravo it will be the LED and the button (#176). The link itself never shows or answers a prompt.

`alarm_volume.h` is the volume's three steps. They are here because the shared alarm reads them, and the Go's `Settings` stores them. Their wire codes are the link registry's `alarm_volume` enum, which `test/core/test_link_mirrors.cpp` checks against the Go's `go::AlarmVolume`, the same type.
