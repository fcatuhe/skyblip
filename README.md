# skyBlip

Open-source electronic conspicuity for general aviation: [ADS-L 4 SRD-860](https://www.easa.europa.eu/en/document-library/agency-decisions/ed-decision-2022024r) for aircraft (skyBlip) and ground stations (skyPost).

## What it does

[`docs/BEHAVIOR.md`](docs/BEHAVIOR.md) lists every claim the firmware makes about itself, straight from the host test suite that checks them. It is generated, so it cannot describe a behavior that stopped being true.

[`docs/TUNING.md`](docs/TUNING.md) is the other half: every number that behavior is tuned by, with the line of source that justifies it. Also generated, from the constants themselves.

## The hardware

skyBlip Go runs on the LilyGO T-Echo Plus, the reference board for the tracker. The other devices and the board they share are on [skyblip.eu/devices](https://skyblip.eu/devices).

<img align="right" width="122" alt="skyBlip Go: white T-Echo Plus with its antenna, skyBlip shown on the e-paper screen" src="https://github.com/fcatuhe/skyblip/raw/4f44ff307a161aadb9ce767cb732e3f13576e5b1/readme/01-skyblip-go-techo.jpg">

| | T-Echo Plus (skyBlip Go) |
|---|---|
| **Processor** | Nordic nRF52840, Cortex-M4F, 64 MHz |
| **GNSS** | Quectel L76K, PPS |
| **868 MHz radio** | Semtech SX1262 |
| **Barometer** | Bosch BME280 |
| **Inertial unit** | Bosch BHI260AP |
| **Display** | 1.54" e-paper, 200x200 |
| **Audible alarms** | buzzer + vibration motor |
| **Bluetooth** | BLE 5.0, 3 connections, no pairing |
| **USB** | USB-C |
| **Battery** | 2400 mAh Li-ion |

<br clear="right">

## The tree

| Directory | What lives there |
|---|---|
| [`firmware/`](firmware) | the C++ tree: `core/`, `ui/`, `ports/`, the Zephyr platform, the host test suite, and the simulator's world |
| [`simulator/`](simulator) | the development harness page that drives the WASM build of the firmware, and `device.css`, the case both it and the site draw the panel in |
| [`website/`](website) | [skyblip.eu](https://skyblip.eu), a Rails app that Parklife renders to static files |
| `docs/`, `schemas/`, `scripts/`, `skyship/` | the generated behavior and tuning indexes, the wire schemas, the build and release tooling, the artwork |

The firmware and the site share a tree so that a change in behavior and the page documenting it can land in one commit. They do not share a build: each has its own workflow, gated on the paths it owns.

## Building

The host test suite and the simulator need nothing but a C++ compiler: `make -C firmware test`, `make -C firmware simulator`.

The device image is built locally for now, off the committed tip of `main`, into `builds/`:

```
scripts/build_local.sh              # skyblip_go, the only product today
SKYBLIP_REF=HEAD scripts/build_local.sh
```

The first run needs `cmake ninja dtc gperf`, then bootstraps a Zephyr workspace under `~/.cache/skyblip/west` and installs the SDK under `~/.local/opt`. CI runs the tests and the linter but no longer builds the image: the `product-image` job in [`firmware.yml`](.github/workflows/firmware.yml) is commented out until it comes back.

## Acknowledgements

skyBlip stands on a decade of open work by the free-flight community. Thanks to the authors of the projects we learned from while building it:

- **Paweł Jałocha**: the ADS-L reference implementation and [nrf52-ogn-tracker](https://github.com/pjalocha/nrf52-ogn-tracker)
- **Linar Yusupov**: [SoftRF](https://github.com/lyusupov/SoftRF)
- **Moshe Braner**: the [SoftRF fork](https://github.com/moshe-braner/SoftRF)

## License

The repository is **MIT**: see [`LICENSE`](LICENSE). The one exception is [`firmware/`](firmware), which is **GPL-3.0-only** under [`firmware/LICENSE`](firmware/LICENSE), and that license reaches everything below it: `core/`, `ui/`, `ports/`, `hardware/`, `runtime/`, `products/`, `boards/`, `test/` and the simulated world in `firmware/simulator/`.

So the schemas, the build and release scripts, the simulator page and the website may be copied into a closed product; a tracker built from this firmware owes its sources.

The simulator page is MIT, but what `simulator/` builds is not: `make -C simulator build` copies the firmware's WASM into `simulator/build/`, and that binary stays GPL-3.0-only wherever it is served.

## Trademark

"skyBlip" and "skyPost", the wordmark and the airship mark in [`skyship/`](skyship) are trademarks of François Catuhe. The licenses above grant copyright permissions, not trademark ones: a fork may say it is compatible with skyBlip or derived from it, and may not use the names or the marks to name its own product or to suggest endorsement.

Copyright (C) 2026 François Catuhe
