# keys

The public halves of the keys MCUboot is built to trust. A unit takes an update over Bluetooth only when the image is signed by the key its bootloader was built with, so these two files say which images can reach which units.

| File | Signs | Private half |
|---|---|---|
| `development.pub.pem` | builds of `main` (the `product-image` job) and `scripts/build_local.sh` on the maintainer's machine | the `firmware_development` environment's secret, and `~/.config/skyblip/local-signing.pem` |
| `production.pub.pem` | releases, from a `v*` tag (`.github/workflows/release.yml`) | the `firmware_production` environment's secret, and an offline copy |

CI signs, then fails unless the image verifies against the file named for its environment.

The key also decides the bootloader's rule. A release is built with `products/<product>/release/`, which refuses an image older than the running one: a unit on the production key only goes up, or installs the same version again. A development build leaves those fragments out, and its bootloader takes any image signed by the development key, older ones included. A pull request is signed with a key made for that run and checked against nothing: it proves the build, and its image updates no unit.

A unit moves from one key to the other by drag-and-drop of a `.uf2` signed by the new one, which replaces the bootloader along with the image.

Both keys are ECDSA P-256, the type `sysbuild.conf` builds MCUboot for. A new one:

```
imgtool keygen -k signing.pem -t ecdsa-p256
imgtool getpub -k signing.pem --encoding pem > firmware/keys/<environment>.pub.pem
```

Anyone who builds this firmware with their own key can install it on their own unit the same way, through the factory bootloader's drag-and-drop: no key in this directory locks a device to these images.

## Server keys

`server/` holds the public halves of the two Ed25519 keys skyblip.eu signs serials with (issue #126, `core/identity/README.md`), as 64 lowercase hex digits. They have nothing to do with the MCUboot keys above: those decide which images a unit installs, these which serial it accepts, and a unit keeps its key, its DEVICEID and its serial when it moves from one MCUboot key to the other.

| File | Trusted by | Id | Private half |
|---|---|---|---|
| `server/development.pub` | builds of `main`, pull requests and `scripts/build_local.sh` | `a9649d2d` | the server's development credentials |
| `server/production.pub` | releases (`products/<product>/release/app.conf` sets `CONFIG_SKYBLIP_SERVER_KEY_PRODUCTION`) | | the server's production credentials |
| the test key in `schemas/activation.v1.vectors.json` | the host suite and the simulator | `890026b1` | in the same file, because it is a test key |

The id is the first four bytes of the SHA-256 of the raw public key, which is how `hello` names the key a unit trusts. `cmake/skyblip.cmake` compiles the chosen file into the image and refuses to configure without it, so a release build with no `server/production.pub` fails loudly rather than shipping units nothing can activate; `release.yml` checks the same before it asks for an approval.

`server/production.pub` is not committed yet. The owner generates the production key into the server's production credentials and commits its public half here; no agent touches those credentials. A unit's stored unlock code is signed by one key only, so a unit moved between development and production firmware activates again once, and gets the same serial back.
