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

## The server key

`server.pub` is the public half of the server key, the one Ed25519 key skyblip.eu signs every identity message with (issue #126, `core/identity/README.md`): the challenge and the unlock code of an activation today, any later command that touches identity tomorrow. It is 64 lowercase hex digits. Only skyblip.eu signs for a unit, so every device build trusts it: a pull request, `main`, `scripts/build_local.sh` and a release alike, and a unit keeps its unlock code when it moves between development and release firmware. It has nothing to do with the MCUboot keys above: those decide which images a unit installs, this one which identity messages it believes.

| Key | Trusted by | Id | Private half |
|---|---|---|---|
| `server.pub` | every device build | | `identity.server_key` in the server's production credentials |
| the test key in `schemas/activation.v1.vectors.json` | the host suite and the simulator | `890026b1` | in the same file, because it is a test key |

The id is the first four bytes of the SHA-256 of the raw public key, which is how `hello` names the key a unit trusts. `cmake/skyblip.cmake` compiles the file into the image and refuses to configure a device build without it, so no image ever ships that nothing can activate.

`server.pub` is not committed yet, so until it is, every device build fails to configure, the `product image` job included: that is the intended state, not a bug. The owner generates the seed into the server's production credentials and commits its public half here (`server/README.md`, Activation); no agent touches those credentials, and no placeholder stands in for it.
