# keys

The public halves of the keys MCUboot is built to trust. A unit takes an update over Bluetooth only when the image is signed by the key its bootloader was built with, so these two files say which images can reach which units.

| File | Signs | Private half |
|---|---|---|
| `development.pub.pem` | builds of `main` (the `product-image` job) and `scripts/build_local.sh` on the maintainer's machine | the `development` environment's secret, and `~/.config/skyblip/local-signing.pem` |
| `production.pub.pem` | releases, from a `v*` tag (`.github/workflows/release.yml`) | the `production` environment's secret, and an offline copy |

CI signs, then fails unless the image verifies against the file named for its environment.

The key also decides the bootloader's rule. A release is built with `products/<product>/release/`, which refuses an image older than the running one: a unit on the production key only goes up, or installs the same version again. A development build leaves those fragments out, and its bootloader takes any image signed by the development key, older ones included. A pull request is signed with a key made for that run and checked against nothing: it proves the build, and its image updates no unit.

A unit moves from one key to the other by drag-and-drop of a `.uf2` signed by the new one, which replaces the bootloader along with the image.

Both keys are ECDSA P-256, the type `sysbuild.conf` builds MCUboot for. A new one:

```
imgtool keygen -k signing.pem -t ecdsa-p256
imgtool getpub -k signing.pem --encoding pem > firmware/keys/<environment>.pub.pem
```

Anyone who builds this firmware with their own key can install it on their own unit the same way, through the factory bootloader's drag-and-drop: no key in this directory locks a device to these images.
