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
# A product with a sensor hub lands two images of one commit in firmware/build/out/:
# the slim .signed.bin, which leaves the BHI260AP image on the external flash, and the
# .full.signed.bin, which carries it. The .uf2 is always the full one. A product with
# no hub, CONFIG_SKYBLIP_IMU_IMAGE unset in its prj.conf, builds the one image. A
# .version line lands beside them.
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
# One build directory per product: west refuses a directory configured for another application.
build=$firmware/build/image/$product
slim_build=$firmware/build/image-slim/$product
app=$build/$product/zephyr
slim_app=$slim_build/$product/zephyr
hub_image=$firmware/hardware/parts/bhi260/firmware/BHI260AP.fw
out=$firmware/build/out
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

  local version images signed
  version=$(image_version)
  board=$(declared_board)
  echo "== building $product $version, $channel ($(git -C "$tree" rev-parse --short HEAD))"
  build_image "$version" "$build" y
  images=("$app")
  if has_hub; then
    build_image "$version" "$slim_build" n
    images+=("$slim_app")
  fi
  for image in "${images[@]}"; do
    assert_version_stamped "$version" "$image"
    if [ -n "$public_key" ]; then assert_signed_by "$public_key" "$image"; fi
  done
  assert_downgrade_rule "$build" "$app"
  if has_hub; then
    assert_downgrade_rule "$slim_build" "$slim_app"
    "$python" "$tree/scripts/check_imu_image.py" "$app/zephyr.signed.bin" "$hub_image" full
    "$python" "$tree/scripts/check_imu_image.py" "$slim_app/zephyr.signed.bin" "$hub_image" slim
  fi
  # A unit refuses an upload naming another product, and takes none while it runs an image naming none.
  signed=("$app/zephyr.signed.bin" "$app/zephyr.signed.confirmed.bin")
  if has_hub; then signed+=("$slim_app/zephyr.signed.bin"); fi
  for image in "${signed[@]}"; do
    "$python" "$tree/scripts/check_image_product.py" "$image" "$product"
  done
  assert_confirmed_image_differs
  check_budgets
  stage_artifacts "$version"
}

# The build number orders two images of one release and is NOT in the VERSION
# file: Zephyr rejects a tweak above 255 (cmake/modules/version.cmake:79-81).
# Commit count: it only ever goes up on a branch that only moves forward.
# Every product carries firmware/VERSION, and Zephyr reads it through the product's link to it.
image_version() {
  local file=$firmware/VERSION link=$firmware/products/$product/VERSION triple build major minor patch
  test -f "$firmware/products/$product/CMakeLists.txt" || { echo "FAIL: no such product: $product" >&2; exit 1; }
  test "$(realpath "$link" 2>/dev/null)" = "$(realpath "$file")" \
    || { echo "FAIL: $link is not a link to firmware/VERSION" >&2; exit 1; }
  if [ "$(git -C "$tree" rev-parse --is-shallow-repository)" = true ]; then
    echo "FAIL: shallow clone, the commit count would be wrong (checkout with fetch-depth: 0)" >&2
    exit 1
  fi
  triple=$(sed -n \
    -e 's/^VERSION_MAJOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^VERSION_MINOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^PATCHLEVEL *= *\([0-9]*\).*/\1/p' "$file" | paste -sd. -)
  test -n "${triple//./}" || { echo "FAIL: no version triple in $file" >&2; exit 1; }
  build=$(git -C "$tree" rev-list --count HEAD)
  # The update frame fits one notification on the narrowest phone only up to dfu::kWidestVersion.
  IFS=. read -r major minor patch <<< "$triple"
  if [ "$major" -gt 9 ] || [ "$minor" -gt 99 ] || [ "$patch" -gt 99 ] || [ "$build" -gt 99999 ]; then
    echo "FAIL: $triple+$build is wider than 9.99.99+99999, the widest version the update frame fits" >&2
    exit 1
  fi
  echo "$triple+$build"
}

# The board is a fact about the SKU, declared by the product, never passed in.
declared_board() {
  local declared
  declared=$(sed -n 's/^skyblip_product_board(\(.*\))$/\1/p' \
             "$firmware/products/$product/CMakeLists.txt")
  test -n "$declared" || { echo "FAIL: $product declares no board" >&2; exit 1; }
  echo "$declared"
}

# The hub's image is pinned by the product's prj.conf, and only a product that pins one has a slim build.
has_hub() {
  grep -q '^CONFIG_SKYBLIP_IMU_IMAGE=y' "$firmware/products/$product/prj.conf"
}

# A full image and a slim one: SB_CONFIG_SKYBLIP_IMU_IMAGE_LINKED (products/<product>/Kconfig.sysbuild).
build_image() {
  local version=$1 dir=$2 linked=$3 conf=$firmware/build-conf
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

  local hub_flag=()
  if has_hub; then hub_flag=(-DSB_CONFIG_SKYBLIP_IMU_IMAGE_LINKED="$linked"); fi

  if [ "${SKYBLIP_PRISTINE:-0}" = 1 ]; then rm -rf "$dir"; fi
  drop_build_of_moved_sdk "$dir"
  # -D<image>_ is sysbuild's namespace for one image, named after the app dir.
  (cd "$firmware" && "$west" build -b "$board" -d "$dir" "products/$product" --sysbuild \
    -- -DSB_EXTRA_CONF_FILE="$conf/signing.conf" \
       -D"${product}"_EXTRA_CONF_FILE="$app_conf" \
       -Dmcuboot_EXTRA_CONF_FILE="$mcuboot_conf" \
       "${hub_flag[@]}")
}

# CMake caches the compiler by absolute path, so a build configured against an SDK
# that has since moved can only fail to configure.
drop_build_of_moved_sdk() {
  local dir=$1 cache=$1/$product/CMakeCache.txt sdk
  [ -f "$cache" ] || return 0
  sdk=$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:PATH=//p' "$cache")
  if [ -n "$sdk" ] && [ ! -d "$sdk" ]; then
    echo "== the last build used the SDK at $sdk, which is gone: building pristine"
    rm -rf "$dir"
  fi
}

# 0.0.0+0 is what gets signed whenever the VERSION file stops being picked up,
# and it silently disables downgrade prevention on every unit that takes it.
assert_version_stamped() {
  local version=$1 image=$2 stamped
  stamped=$("$python" "$imgtool" verify "$image/zephyr.signed.bin" | sed -n 's/^Image version: //p')
  test "$stamped" = "$version" \
    || { echo "FAIL: signed ${stamped:-nothing}, expected $version"; exit 1; }
}

# The bootloader enforces the rule and the app predicts it: the two disagreeing is
# an app that promises an install its bootloader then throws away.
assert_downgrade_rule() {
  local dir=$1 image=$2 in_bootloader in_app expected=n
  [ "$channel" = production ] && expected=y
  in_bootloader=$(sed -n 's/^CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=//p' "$dir/mcuboot/zephyr/.config")
  in_app=$(sed -n 's/^CONFIG_MCUBOOT_BOOTLOADER_NO_DOWNGRADE=//p' "$image/.config")
  test "${in_bootloader:-n}" = "$expected" && test "${in_app:-n}" = "$expected" \
    || { echo "FAIL: $channel wants downgrade prevention $expected in $dir, MCUboot has ${in_bootloader:-n}, the app ${in_app:-n}"; exit 1; }
  echo "downgrade prevention in $dir: $expected ($channel)"
}

assert_signed_by() {
  "$python" "$imgtool" verify -k "$1" "$2/zephyr.signed.bin" >/dev/null \
    || { echo "FAIL: $2/zephyr.signed.bin does not verify against $1"; exit 1; }
  echo "$2/zephyr.signed.bin: signed by the key in $1"
}

# A slot0 written straight by the bootloader never swaps, so it never gets to
# mark itself good: unconfirmed, the image is reverted on the second boot.
assert_confirmed_image_differs() {
  test -f "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: no confirmed hex, is CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE still y?"; exit 1; }
  ! cmp -s "$app/zephyr.signed.hex" "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: the confirmed hex is byte-identical to the unconfirmed one"; exit 1; }
}

# The full image only: the slim one is the same RAM and less flash.
check_budgets() {
  "$python" "$tree/scripts/size_check.py" "$app/zephyr.elf"
  if has_hub; then
    echo "slim image: $(stat -c %s "$slim_app/zephyr.signed.bin") B over the air, full: $(stat -c %s "$app/zephyr.signed.bin") B"
  else
    echo "image: $(stat -c %s "$app/zephyr.signed.bin") B over the air"
  fi
}

# The application image must be the CONFIRMED one: a slot0 written directly
# never swaps, so it never marks itself good and the bootloader reverts it.
# mkuf2 leaves its intermediate .merged.hex beside the .uf2, so it writes into
# the build directory and out/ takes the copy.
stage_artifacts() {
  local version=$1
  "$python" "$tree/scripts/mkuf2.py" "$board" "$build/$slug.uf2" \
    "$build/mcuboot/zephyr/zephyr.hex" "$app/zephyr.signed.confirmed.hex"
  rm -rf "$out" && mkdir -p "$out"
  cp "$build/$slug.uf2" "$out/$slug.uf2"
  if has_hub; then
    cp "$slim_app/zephyr.signed.bin" "$out/$slug.signed.bin"
    cp "$app/zephyr.signed.bin" "$out/$slug.full.signed.bin"
  else
    cp "$app/zephyr.signed.bin" "$out/$slug.signed.bin"
  fi
  cp "$app/zephyr.signed.confirmed.bin" "$out/$slug.signed.confirmed.bin"
  echo "$slug $version $(git -C "$tree" rev-parse --short HEAD) $channel" > "$out/$slug.version"
}

main
