# Third-party notices

skyBlip is MIT, see [`LICENSE`](https://github.com/fcatuhe/skyblip/blob/main/LICENSE). This file lists what other people wrote that the repository carries, that a firmware image is built from, or that the browser simulator is built from, each with the license it comes under. Every release ships this file beside the image, with `LICENSE` and the texts in [`LICENSES/`](https://github.com/fcatuhe/skyblip/tree/main/LICENSES).

## Carried in this repository

| Path | What it is | License |
|---|---|---|
| `firmware/vendor/monocypher/` | Monocypher 4.0.3, the Ed25519 check | BSD-2-Clause or CC0-1.0, used under CC0-1.0 ([`LICENCE.md`](https://github.com/fcatuhe/skyblip/blob/main/firmware/vendor/monocypher/LICENCE.md)) |
| `firmware/vendor/doctest/` | doctest, the host test framework, never in an image | MIT, in `doctest.h` |
| `firmware/hardware/parts/bhi260/firmware/BHI260AP.fw` | Bosch Sensortec's BHI260AP RAM image, BHI2xy_SensorAPI v1.6.0 | BSD-3-Clause, text below and beside the image |
| `server/app/assets/fonts/FiraSans-*.woff2` | Fira Sans | SIL Open Font License 1.1, `OFL-FiraSans.txt` beside it |
| `server/app/assets/fonts/Inter-*.woff2` | Inter | SIL Open Font License 1.1, `OFL-Inter.txt` beside it |
| `server/app/assets/fonts/B612Mono-Regular.woff2` | B612 Mono | SIL Open Font License 1.1, `OFL-B612.txt` beside it |
| `docs/reference/ads-l_4_srd860_issue_*` | ADS-L 4 SRD-860, Issues 1 and 2, and their text conversions | Copyright European Union Aviation Safety Agency. Reproduction is authorised provided the source is acknowledged, see `docs/reference/README.md` |
| `firmware/test/core/test_adsb.cpp` | eight 1090 MHz frames and dump1090's decodes of them, from the [IndoorOutdoor/1090dumpCSV](https://huggingface.co/datasets/IndoorOutdoor/1090dumpCSV) dataset | Apache-2.0 |

The 5x7 font in `firmware/ui/canvas.cpp` is the character set of Hitachi's HD44780U LCD controller, ROM code A00, as Table 4 of the HD44780U datasheet draws it. Hitachi attaches no license to it. It is credited here as the font's origin.

The skyBlip wordmark in `firmware/ui/widgets/wordmark.cpp` is a bitmap rendered from Fira Sans Bold by `scripts/make_wordmark.py`. It is artwork drawn with the font, not the font itself.

## Followed in our own code

| Where | What it follows | License |
|---|---|---|
| `firmware/hardware/parts/drv2605/drv2605.cpp`, `Drv2605::begin` | the register setup of SensorLib's DRV2605 driver, Copyright (c) 2024 Lewis He | MIT, text below |
| `firmware/core/gnss/nmea.cpp` and `firmware/core/protocol/nmea_out.cpp`, the calendar conversions | Howard Hinnant's `days_from_civil` and `civil_from_days` | public domain |
| `firmware/hardware/parts/bme280/bme280.cpp`, the compensation | the integer compensation formulas of Bosch Sensortec's BME280 datasheet, section 4.2.3, rev 1.1 | reference code the datasheet publishes for implementers |

## Built into a firmware image

`firmware/west.yml` pins Zephyr v4.4.2. Zephyr's own manifest pins the module revisions, and `west update` fetches them. The C and C++ libraries come with the Zephyr SDK's toolchain. Monocypher and the Bosch image are the vendored copies above.

| Component | What it does in the image | License |
|---|---|---|
| [Zephyr](https://github.com/zephyrproject-rtos/zephyr) | the operating system, drivers and Bluetooth host | Apache-2.0 |
| [MCUboot](https://github.com/zephyrproject-rtos/mcuboot) | the bootloader that checks and swaps images, with its `ext/mbedtls-asn1` | Apache-2.0, with the NOTICE below |
| TinyCrypt, in MCUboot's `ext/tinycrypt` | the bootloader's ECDSA-P256 check, with micro-ecc inside it | BSD-3-Clause (Intel) and BSD-2-Clause (Kenneth MacKay), `LICENSES/TinyCrypt.txt` |
| [CMSIS 6](https://github.com/zephyrproject-rtos/cmsis_6) | the Cortex-M core headers | Apache-2.0 |
| [hal_nordic](https://github.com/zephyrproject-rtos/hal_nordic) | nrfx and the MDK, the nRF52840's drivers and register definitions | BSD-3-Clause, Copyright (c) 2010 - 2026, Nordic Semiconductor ASA, text below. The MDK's `system_nrf52.c` is Apache-2.0, Copyright (c) 2009-2026 ARM Limited |
| [Mbed TLS](https://github.com/zephyrproject-rtos/mbedtls) and [TF-PSA-Crypto](https://github.com/zephyrproject-rtos/tf-psa-crypto) | the PSA crypto the Bluetooth host uses | Apache-2.0, chosen from Apache-2.0 OR GPL-2.0-or-later |
| [zcbor](https://github.com/zephyrproject-rtos/zcbor) | the CBOR codec of MCUmgr | Apache-2.0 |
| picolibc, from the Zephyr SDK | the C library | BSD-3-Clause and the other permissive licenses in its `COPYING.picolibc`, copied as `LICENSES/picolibc.txt` from the revision Zephyr v4.4.2 pins. Its GPL-2.0 and AGPL-3.0 entries cover tests and a build script, not the library |
| libstdc++ and libgcc, from the Zephyr SDK's GCC | C++ and compiler runtime support | GPL-3.0-or-later WITH GCC-exception-3.1, which lets the image ship under any terms and asks for no notice |
| Monocypher | the Ed25519 check | CC0-1.0 |
| Bosch BHI260AP firmware | the inertial unit's image, in a full image only | BSD-3-Clause, text below |

## Built into the browser simulator

`skyblip_simulator.wasm` and `skyblip_simulator.js` are our firmware compiled by Emscripten, which adds its own runtime.

| Component | License |
|---|---|
| [Emscripten](https://github.com/emscripten-core/emscripten)'s runtime and JavaScript glue | MIT or NCSA, used under MIT, text below |
| [musl](https://musl.libc.org), the C library | MIT, and Sun's fdlibm notice for the math functions, texts below |
| libc++, libc++abi and compiler-rt, from LLVM | Apache-2.0 WITH LLVM-exception, and NCSA or MIT for legacy code, used under MIT, text below |

## Notices

MCUboot's NOTICE:

```
Apache Mynewt
Copyright 2015-2017 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).

Portions of this software were developed at
Runtime Inc, copyright 2015.

Portions of this software were developed at
Arm Limited, copyright 2019-2021.
```

nrfx and the MDK:

```
Copyright (c) 2010 - 2026, Nordic Semiconductor ASA
All rights reserved.

SPDX-License-Identifier: BSD-3-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

Bosch Sensortec's BHI260AP firmware:

```
Copyright (c) 2022 Bosch Sensortec GmbH. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

SensorLib:

```
MIT License

Copyright (c) 2024 Lewis He

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Emscripten:

```
Copyright (c) 2010-2014 Emscripten authors, see AUTHORS file.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

musl:

```
Copyright © 2005-2020 Rich Felker, et al.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

LLVM's libc++, libc++abi and compiler-rt, for their legacy code:

```
Copyright (c) 2009-2014 by the contributors listed in CREDITS.TXT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

musl's math functions, from Sun's fdlibm:

```
Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

Developed at SunSoft, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this
software is freely granted, provided that this notice
is preserved.
```

The Apache License 2.0, TinyCrypt's two licenses and picolibc's licenses are in `LICENSES/` in the repository, and beside this file in a release.
