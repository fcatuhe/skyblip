#!/usr/bin/env bash
# Build, sign and check one product image, in a west workspace already set up.
#
#   scripts/build_image.sh <product> <signing-key.pem> [public-key.pem]
#
# What build_local.sh, the product-image job and the release job share. They
# differ in where the workspace and the key come from, never in what an image
# has to pass before it leaves. With a public key, the image must verify
# against it: that is how CI knows it signed with the key the units trust.
#
# The image, its .uf2 and a .version line land in firmware/build/out/.
#
#   SKYBLIP_CHANNEL     development (default): MCUboot takes any image signed by its key
#                       production: products/<product>/release/ adds the refusal of an older one
#   SKYBLIP_TREE        the checkout to build (default: the one holding this script)
#   SKYBLIP_PRISTINE=1  throw the build directory away first
#   PYTHON, WEST        default python3 and west
set -euo pipefail

product=${1:?usage: build_image.sh <product> <signing-key.pem> [public-key.pem]}
key=${2:?usage: build_image.sh <product> <signing-key.pem> [public-key.pem]}
public_key=${3:-}
slug=${product//_/-}
channel=${SKYBLIP_CHANNEL:-development}

tree=${SKYBLIP_TREE:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
python=${PYTHON:-python3}
west=${WEST:-west}
firmware=$tree/firmware
build=$firmware/build
app=$build/$product/zephyr
out=$build/out
topdir=$(cd "$firmware" && "$west" topdir)
imgtool=$topdir/bootloader/mcuboot/scripts/imgtool.py
# What tells mkuf2.py where Zephyr's uf2conv.py is, outside a `west build` env.
export ZEPHYR_BASE=${ZEPHYR_BASE:-$topdir/zephyr}

main() {
  case "$channel" in
    development|production) ;;
    *) echo "FAIL: SKYBLIP_CHANNEL is $channel, not development or production"; exit 1 ;;
  esac
  test -f "$key" || { echo "FAIL: no signing key at $key"; exit 1; }
  if [ -n "$public_key" ]; then
    test -f "$public_key" || { echo "FAIL: no public key at $public_key"; exit 1; }
  fi

  local version
  version=$(image_version)
  echo "== building $product $version, $channel ($(git -C "$tree" rev-parse --short HEAD))"
  build_image "$version"
  assert_version_stamped "$version"
  assert_downgrade_rule
  if [ -n "$public_key" ]; then assert_signed_by "$public_key"; fi
  assert_confirmed_image_differs
  check_flash_budget
  stage_artifacts "$version"
}

# The build number orders two images of one release and is NOT in the VERSION
# file: Zephyr rejects a tweak above 255 (cmake/modules/version.cmake:79-81).
# Commit count: it only ever goes up on a branch that only moves forward.
image_version() {
  local file=$firmware/products/$product/VERSION triple
  test -f "$file" || { echo "FAIL: no such product: $product" >&2; exit 1; }
  if [ "$(git -C "$tree" rev-parse --is-shallow-repository)" = true ]; then
    echo "FAIL: shallow clone, the commit count would be wrong (checkout with fetch-depth: 0)" >&2
    exit 1
  fi
  triple=$(sed -n \
    -e 's/^VERSION_MAJOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^VERSION_MINOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^PATCHLEVEL *= *\([0-9]*\).*/\1/p' "$file" | paste -sd. -)
  test -n "${triple//./}" || { echo "FAIL: no version triple in $file" >&2; exit 1; }
  echo "$triple+$(git -C "$tree" rev-list --count HEAD)"
}

build_image() {
  local version=$1 board conf=$firmware/build-conf
  # The board is a fact about the SKU, declared by the product, never passed in.
  board=$(sed -n 's/^skyblip_product_board(\(.*\))$/\1/p' \
          "$firmware/products/$product/CMakeLists.txt")
  test -n "$board" || { echo "FAIL: $product declares no board"; exit 1; }

  mkdir -p "$conf"
  echo "SB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$(realpath "$key")\"" > "$conf/signing.conf"
  echo "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"$version\"" > "$conf/version.conf"
  # Passed every time, sysbuild/mcuboot.conf too: a -D replaces it, and a list left out stays cached.
  local product_dir=$firmware/products/$product
  local app_conf=$conf/version.conf
  local mcuboot_conf=$product_dir/sysbuild/mcuboot.conf
  if [ "$channel" = production ]; then
    app_conf="$app_conf;$product_dir/release/app.conf"
    mcuboot_conf="$mcuboot_conf;$product_dir/release/mcuboot.conf"
  fi

  if [ "${SKYBLIP_PRISTINE:-0}" = 1 ]; then rm -rf "$build"; fi
  drop_build_of_moved_sdk
  # -D<image>_ is sysbuild's namespace for one image, named after the app dir.
  (cd "$firmware" && "$west" build -b "$board" "products/$product" --sysbuild \
    -- -DSB_EXTRA_CONF_FILE="$conf/signing.conf" \
       -D"${product}"_EXTRA_CONF_FILE="$app_conf" \
       -Dmcuboot_EXTRA_CONF_FILE="$mcuboot_conf")
}

# CMake caches the compiler by absolute path, so a build configured against an SDK
# that has since moved can only fail to configure.
drop_build_of_moved_sdk() {
  local cache=$build/$product/CMakeCache.txt sdk
  [ -f "$cache" ] || return 0
  sdk=$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:PATH=//p' "$cache")
  if [ -n "$sdk" ] && [ ! -d "$sdk" ]; then
    echo "== the last build used the SDK at $sdk, which is gone: building pristine"
    rm -rf "$build"
  fi
}

# 0.0.0+0 is what gets signed whenever the VERSION file stops being picked up,
# and it silently disables downgrade prevention on every unit that takes it.
assert_version_stamped() {
  local version=$1 stamped
  stamped=$("$python" "$imgtool" verify "$app/zephyr.signed.bin" | sed -n 's/^Image version: //p')
  test "$stamped" = "$version" \
    || { echo "FAIL: signed ${stamped:-nothing}, expected $version"; exit 1; }
}

# The bootloader enforces the rule and the app predicts it: the two disagreeing is
# an app that promises an install its bootloader then throws away.
assert_downgrade_rule() {
  local in_bootloader in_app expected=n
  [ "$channel" = production ] && expected=y
  in_bootloader=$(sed -n 's/^CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=//p' "$build/mcuboot/zephyr/.config")
  in_app=$(sed -n 's/^CONFIG_MCUBOOT_BOOTLOADER_NO_DOWNGRADE=//p' "$app/.config")
  test "${in_bootloader:-n}" = "$expected" && test "${in_app:-n}" = "$expected" \
    || { echo "FAIL: $channel wants downgrade prevention $expected, MCUboot has ${in_bootloader:-n}, the app ${in_app:-n}"; exit 1; }
  echo "downgrade prevention: $expected ($channel)"
}

assert_signed_by() {
  "$python" "$imgtool" verify -k "$1" "$app/zephyr.signed.bin" >/dev/null \
    || { echo "FAIL: the image does not verify against $1"; exit 1; }
  echo "signed by the key in $1"
}

# A slot0 written straight by the bootloader never swaps, so it never gets to
# mark itself good: unconfirmed, the image is reverted on the second boot.
assert_confirmed_image_differs() {
  test -f "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: no confirmed hex, is CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE still y?"; exit 1; }
  ! cmp -s "$app/zephyr.signed.hex" "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: the confirmed hex is byte-identical to the unconfirmed one"; exit 1; }
}

# The host `size` cannot read an arm-zephyr-eabi ELF, and the SDK's own tool is
# not on PATH outside a build: it sits beside the objcopy the build used.
check_flash_budget() {
  local objcopy
  objcopy=$(sed -n 's/^CMAKE_OBJCOPY:FILEPATH=//p' "$build/$product/CMakeCache.txt")
  test -n "$objcopy" || { echo "FAIL: the build cached no CMAKE_OBJCOPY to find the SDK's size by"; exit 1; }
  SIZE=${objcopy%objcopy}size "$python" "$tree/scripts/size_check.py" "$app/zephyr.elf"
}

# The application image must be the CONFIRMED one: a slot0 written directly
# never swaps, so it never marks itself good and the bootloader reverts it.
# mkuf2 leaves its intermediate .merged.hex beside the .uf2, so it writes into
# the build directory and out/ takes the copy.
stage_artifacts() {
  local version=$1
  "$python" "$tree/scripts/mkuf2.py" "$build/$slug.uf2" \
    "$build/mcuboot/zephyr/zephyr.hex" "$app/zephyr.signed.confirmed.hex"
  rm -rf "$out" && mkdir -p "$out"
  cp "$build/$slug.uf2" "$out/$slug.uf2"
  cp "$app/zephyr.signed.bin" "$out/$slug.signed.bin"
  cp "$app/zephyr.signed.confirmed.bin" "$out/$slug.signed.confirmed.bin"
  echo "$slug $version $(git -C "$tree" rev-parse --short HEAD) $channel" > "$out/$slug.version"
}

main
