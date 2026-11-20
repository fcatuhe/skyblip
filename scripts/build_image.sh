#!/usr/bin/env bash
# Build, sign and check one product image, in a west workspace already set up.
#
#   scripts/build_image.sh <product> <signing-key.pem> [public-key.pem]
#
# build_local.sh, the product-image job and the release job share it. With a public
# key, the image must verify against it. A product with a sensor hub lands a base
# .signed.bin and a .full.signed.bin that carries the BHI260AP image in firmware/build/out/,
# and the .uf2 is always the full one.
#
#   SKYBLIP_CHANNEL     development (default): MCUboot takes any image signed by its key
#                       production: products/common/release/ adds the refusal of an older one
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
# INFO: fc 20nov26 one build directory per product, because west refuses one configured for another application
build=$firmware/build/image/$product
base_build=$firmware/build/image-base/$product
app=$build/$product/zephyr
base_app=$base_build/$product/zephyr
hub_image=$firmware/hardware/parts/bhi260/firmware/BHI260AP.fw
out=$firmware/build/out
topdir=$(cd "$firmware" && "$west" topdir)
imgtool=$topdir/bootloader/mcuboot/scripts/imgtool.py
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
    build_image "$version" "$base_build" n
    images+=("$base_app")
  fi
  for image in "${images[@]}"; do
    assert_version_stamped "$version" "$image"
    if [ -n "$public_key" ]; then assert_signed_by "$public_key" "$image"; fi
  done
  assert_downgrade_rule "$build" "$app"
  assert_bootloader_keys "$build"
  if has_hub; then
    assert_downgrade_rule "$base_build" "$base_app"
    "$python" "$tree/scripts/check_imu_image.py" "$app/zephyr.signed.bin" "$hub_image" full
    "$python" "$tree/scripts/check_imu_image.py" "$base_app/zephyr.signed.bin" "$hub_image" base
  fi
  signed=("$app/zephyr.signed.bin" "$app/zephyr.signed.confirmed.bin")
  if has_hub; then signed+=("$base_app/zephyr.signed.bin"); fi
  for image in "${signed[@]}"; do
    "$python" "$tree/scripts/check_image_product.py" "$image" "$product"
  done
  assert_confirmed_image_differs
  check_budgets
  stage_artifacts "$version"
}

# INFO: fc 20nov26 the build number is the commit count, not a VERSION tweak, because Zephyr rejects a tweak above 255
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
  # INFO: fc 20nov26 the update frame fits one notification on the narrowest phone only up to dfu::kWidestVersion
  IFS=. read -r major minor patch <<< "$triple"
  if [ "$major" -gt 9 ] || [ "$minor" -gt 99 ] || [ "$patch" -gt 99 ] || [ "$build" -gt 99999 ]; then
    echo "FAIL: $triple+$build is wider than 9.99.99+99999, the widest version the update frame fits" >&2
    exit 1
  fi
  echo "$triple+$build"
}

declared_board() {
  local declared
  declared=$(sed -n 's/^skyblip_product_board(\(.*\))$/\1/p' \
             "$firmware/products/$product/CMakeLists.txt")
  test -n "$declared" || { echo "FAIL: $product declares no board" >&2; exit 1; }
  echo "$declared"
}

has_hub() {
  grep -q '^CONFIG_SKYBLIP_IMU_IMAGE=y' "$firmware/products/$product/prj.conf"
}

build_image() {
  local version=$1 dir=$2 linked=$3 conf=$firmware/build-conf
  mkdir -p "$conf"
  echo "SB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$(bootloader_key_files)\"" > "$conf/signing.conf"
  echo "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"$version\"" > "$conf/version.conf"
  # INFO: fc 20nov26 pass every conf each time, because a -D replaces the cached list and one left out stays cached
  local release=$firmware/products/common/release
  local app_conf=$conf/version.conf
  local mcuboot_conf=$firmware/products/$product/sysbuild/mcuboot.conf
  if [ "$channel" = production ]; then
    app_conf="$app_conf;$release/app.conf"
    mcuboot_conf="$mcuboot_conf;$release/mcuboot.conf"
  fi

  local hub_flag=()
  if has_hub; then hub_flag=(-DSB_CONFIG_SKYBLIP_IMU_IMAGE_LINKED="$linked"); fi

  if [ "${SKYBLIP_PRISTINE:-0}" = 1 ]; then rm -rf "$dir"; fi
  drop_build_of_moved_sdk "$dir"
  (cd "$firmware" && "$west" build -b "$board" -d "$dir" "products/$product" --sysbuild \
    -- -DSB_EXTRA_CONF_FILE="$conf/signing.conf" \
       -D"${product}"_EXTRA_CONF_FILE="$app_conf" \
       -Dmcuboot_EXTRA_CONF_FILE="$mcuboot_conf" \
       "${hub_flag[@]}")
}

# INFO: fc 20nov26 the first key signs, and a development bootloader also trusts the production key
bootloader_key_files() {
  if [ "$channel" = production ]; then
    realpath "$key"
  else
    echo "$(realpath "$key"),$firmware/keys/production.pub.pem"
  fi
}

assert_bootloader_keys() {
  local dir=$1 signing=$firmware/build-conf/signing.pub.pem trusted
  "$python" "$imgtool" getpub -k "$key" -e pem > "$signing"
  trusted=("$signing")
  [ "$channel" = production ] || trusted+=("$firmware/keys/production.pub.pem")
  "$python" "$tree/scripts/check_bootloader_keys.py" "$dir/mcuboot/zephyr/zephyr.bin" "${trusted[@]}"
}

# INFO: fc 20nov26 CMake caches the compiler by absolute path, so a build against a moved SDK cannot configure
drop_build_of_moved_sdk() {
  local dir=$1 cache=$1/$product/CMakeCache.txt sdk
  [ -f "$cache" ] || return 0
  sdk=$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:PATH=//p' "$cache")
  if [ -n "$sdk" ] && [ ! -d "$sdk" ]; then
    echo "== the last build used the SDK at $sdk, which is gone: building pristine"
    rm -rf "$dir"
  fi
}

# INFO: fc 20nov26 an unstamped 0.0.0+0 image would silently disable downgrade prevention on every unit
assert_version_stamped() {
  local version=$1 image=$2 stamped
  stamped=$("$python" "$imgtool" verify "$image/zephyr.signed.bin" | sed -n 's/^Image version: //p')
  test "$stamped" = "$version" \
    || { echo "FAIL: signed ${stamped:-nothing}, expected $version"; exit 1; }
}

# INFO: fc 20nov26 the app predicts what the bootloader enforces, so a mismatch promises installs it throws away
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

# INFO: fc 20nov26 a slot0 the bootloader writes never swaps, so an unconfirmed image reverts on the second boot
assert_confirmed_image_differs() {
  test -f "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: no confirmed hex, is CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE still y?"; exit 1; }
  ! cmp -s "$app/zephyr.signed.hex" "$app/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: the confirmed hex is byte-identical to the unconfirmed one"; exit 1; }
}

# INFO: fc 20nov26 only the full image is checked, because the base one has the same RAM and less flash
check_budgets() {
  "$python" "$tree/scripts/size_check.py" "$app/zephyr.elf"
  if has_hub; then
    echo "base image: $(stat -c %s "$base_app/zephyr.signed.bin") B over the air, full: $(stat -c %s "$app/zephyr.signed.bin") B"
  else
    echo "image: $(stat -c %s "$app/zephyr.signed.bin") B over the air"
  fi
}

# INFO: fc 20nov26 mkuf2 leaves a .merged.hex beside the .uf2, so it writes in the build directory
stage_artifacts() {
  local version=$1
  "$python" "$tree/scripts/mkuf2.py" "$board" "$build/$slug.uf2" \
    "$build/mcuboot/zephyr/zephyr.hex" "$app/zephyr.signed.confirmed.hex"
  rm -rf "$out" && mkdir -p "$out"
  cp "$build/$slug.uf2" "$out/$slug.uf2"
  if has_hub; then
    cp "$base_app/zephyr.signed.bin" "$out/$slug.signed.bin"
    cp "$app/zephyr.signed.bin" "$out/$slug.full.signed.bin"
  else
    cp "$app/zephyr.signed.bin" "$out/$slug.signed.bin"
  fi
  cp "$app/zephyr.signed.confirmed.bin" "$out/$slug.signed.confirmed.bin"
  echo "$slug $version $(git -C "$tree" rev-parse --short HEAD) $channel" > "$out/$slug.version"
}

main
