#!/usr/bin/env bash
# Build a product image off the committed tip of local main, into builds/.
#
#   scripts/build_local.sh [product]        product defaults to skyblip_go
#
# What the (currently commented-out) `product-image` CI job does, on this
# machine. The first run bootstraps a Zephyr workspace and downloads the SDK into
# ~/.local/opt, which takes a while; every run after that is just the build.
#
# The tree it builds is a detached worktree of SKYBLIP_REF, never the checkout
# you are editing: an image that boots is worth a commit anyway, and the west
# workspace then has somewhere to live that is not the repo you work in.
#
#   SKYBLIP_REF          what to build (default main, e.g. HEAD, a tag, a sha)
#   SKYBLIP_WORKSPACE    worktree + west workspace (default ~/.cache/skyblip/west)
#   SKYBLIP_SIGNING_KEY  MCUboot signing key, generated once if absent
#   SKYBLIP_PRISTINE=1   throw the build directory away first
#   SKYBLIP_UPDATE=1     re-run west update even if the manifest has not moved
set -euo pipefail

product=${1:-skyblip_go}
slug=${product//_/-}

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
ref=${SKYBLIP_REF:-main}
workspace=${SKYBLIP_WORKSPACE:-$HOME/.cache/skyblip/west}
key=${SKYBLIP_SIGNING_KEY:-$HOME/.config/skyblip/local-signing.pem}
west=$workspace/.venv/bin/west
python=$workspace/.venv/bin/python3
# One of the places Zephyr's CMake searches for an SDK, so the build needs no path to it.
sdk_base=$HOME/.local/opt
# What tells mkuf2.py where Zephyr's uf2conv.py is, outside a `west build` env.
export ZEPHYR_BASE=$workspace/zephyr

require_host_tools() {
  local missing=()
  for tool in cmake ninja dtc gperf git python3; do
    command -v "$tool" >/dev/null || missing+=("$tool")
  done
  if [ ${#missing[@]} -gt 0 ]; then
    echo "FAIL: missing build tools: ${missing[*]}"
    echo "on Arch: sudo pacman -S --needed cmake ninja dtc gperf git python"
    exit 1
  fi
}

# Resolved in the checkout you ran this from: HEAD in the worktree is the tree it
# built last time, which is how a build silently ships the previous commit.
checkout_ref() {
  local commit
  commit=$(git -C "$repo" rev-parse --verify "$ref^{commit}")
  if [ -e "$workspace/.git" ]; then
    git -C "$workspace" checkout --detach --force "$commit"
  else
    mkdir -p "$(dirname "$workspace")"
    git -C "$repo" worktree add --detach "$workspace" "$commit"
  fi
}

# west and everything Zephyr's build imports live in the workspace, so a system
# python upgrade cannot take the toolchain with it.
install_west() {
  [ -x "$west" ] && return
  echo "== creating the build virtualenv"
  python3 -m venv "$workspace/.venv"
  "$python" -m pip install --quiet --upgrade pip west
}

# Zephyr and the modules are pinned, so the only thing that can make them stale
# is the manifest: keyed on it, a rebuild costs no network at all.
update_workspace() {
  [ -d "$workspace/.west" ] || "$west" init -l "$workspace/firmware"
  local stamp=$workspace/.west/skyblip-manifest.sha256 manifest
  manifest=$(sha256sum "$workspace/firmware/west.yml" | cut -d' ' -f1)
  if [ "${SKYBLIP_UPDATE:-0}" != 1 ] && [ "$(cat "$stamp" 2>/dev/null)" = "$manifest" ]; then
    return
  fi
  echo "== west update (long on a first run: it clones Zephyr and its modules)"
  # Shallow and narrow: the pinned tag and its modules, not a decade of history.
  (cd "$workspace" && "$west" update --narrow --fetch-opt=--depth=1)
  # base only: the rest of Zephyr's requirements are twister, coverage and
  # compliance tooling, none of which builds an image.
  "$python" -m pip install --quiet \
    -r "$workspace/zephyr/scripts/requirements-base.txt" \
    -r "$workspace/bootloader/mcuboot/scripts/requirements.txt"
  echo "$manifest" > "$stamp"
}

# What `west sdk install` does, minus the SDK's setup.sh, which refuses to run
# without wget. The release's own sha256.sum vouches for both archives.
install_sdk() {
  local version sdk_dir host release downloads minimal toolchain file
  version=$(cat "$workspace/zephyr/SDK_VERSION")
  sdk_dir=$sdk_base/zephyr-sdk-$version
  [ -x "$sdk_dir/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ] && return

  echo "== installing the Zephyr SDK $version (arm-zephyr-eabi) into $sdk_base"
  host=linux-$(uname -m)
  release=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$version
  minimal=zephyr-sdk-${version}_${host}_minimal.tar.xz
  toolchain=toolchain_gnu_${host}_arm-zephyr-eabi.tar.xz
  downloads=$workspace/.sdk-downloads
  rm -rf "$downloads" && mkdir -p "$downloads" "$sdk_base"
  for file in sha256.sum "$minimal" "$toolchain"; do
    curl -fL --progress-bar -o "$downloads/$file" "$release/$file"
  done
  grep -e "  $minimal\$" -e "  $toolchain\$" "$downloads/sha256.sum" > "$downloads/expected.sum" || true
  [ "$(wc -l < "$downloads/expected.sum")" = 2 ] \
    || { echo "FAIL: sha256.sum of SDK $version does not list $minimal and $toolchain"; exit 1; }
  (cd "$downloads" && sha256sum -c expected.sum)

  [ -d "$sdk_dir" ] || tar -xf "$downloads/$minimal" -C "$sdk_base"
  mkdir -p "$sdk_dir/gnu"
  tar -xf "$downloads/$toolchain" -C "$sdk_dir/gnu"
  cmake -P "$sdk_dir/cmake/zephyr_sdk_export.cmake"
  rm -rf "$downloads"
}

# MCUboot's own sample key is published, so a signature against it proves
# nothing. This one is local and kept: a unit takes an OTA only from the key
# that signed what is already on it.
create_signing_key() {
  [ -f "$key" ] && return
  mkdir -p "$(dirname "$key")"
  chmod 700 "$(dirname "$key")"
  echo "== generating a local signing key: $key"
  "$python" "$workspace/bootloader/mcuboot/scripts/imgtool.py" keygen -k "$key" -t ecdsa-p256
  chmod 600 "$key"
}

image_version() {
  local file=$workspace/firmware/products/$product/VERSION
  test -f "$file" || { echo "FAIL: no such product: $product" >&2; exit 1; }
  local triple
  triple=$(sed -n \
    -e 's/^VERSION_MAJOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^VERSION_MINOR *= *\([0-9]*\).*/\1/p' \
    -e 's/^PATCHLEVEL *= *\([0-9]*\).*/\1/p' "$file" | paste -sd. -)
  test -n "${triple//./}" || { echo "FAIL: no version triple in $file" >&2; exit 1; }
  # The build number orders two images of one release, as github.run_number does
  # in CI. Commit count: it only ever goes up on a branch that only moves forward.
  echo "$triple+$(git -C "$workspace" rev-list --count HEAD)"
}

build_image() {
  local version=$1 board
  # The board is a fact about the SKU, declared by the product, never passed in.
  board=$(sed -n 's/^skyblip_product_board(\(.*\))$/\1/p' \
          "$workspace/firmware/products/$product/CMakeLists.txt")
  test -n "$board" || { echo "FAIL: $product declares no board"; exit 1; }

  echo "SB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$key\"" > "$workspace/signing.conf"
  echo "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"$version\"" > "$workspace/version.conf"

  if [ "${SKYBLIP_PRISTINE:-0}" = 1 ]; then rm -rf "$workspace/firmware/build"; fi
  drop_build_of_moved_sdk
  (cd "$workspace/firmware" && "$west" build -b "$board" "products/$product" --sysbuild \
    -- -DSB_EXTRA_CONF_FILE="$workspace/signing.conf" \
       -D"${product}"_EXTRA_CONF_FILE="$workspace/version.conf")
}

# CMake caches the compiler by absolute path, so a build configured against an SDK
# that has since moved can only fail to configure.
drop_build_of_moved_sdk() {
  local cache=$workspace/firmware/build/$product/CMakeCache.txt sdk
  [ -f "$cache" ] || return 0
  sdk=$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:PATH=//p' "$cache")
  if [ -n "$sdk" ] && [ ! -d "$sdk" ]; then
    echo "== the last build used the SDK at $sdk, which is gone: building pristine"
    rm -rf "$workspace/firmware/build"
  fi
}

# 0.0.0+0 is what gets signed whenever the VERSION file stops being picked up,
# and it silently disables downgrade prevention on every unit that takes it.
assert_version_stamped() {
  local version=$1 stamped
  stamped=$("$python" "$workspace/bootloader/mcuboot/scripts/imgtool.py" verify \
            "$workspace/firmware/build/$product/zephyr/zephyr.signed.bin" \
            | sed -n 's/^Image version: //p')
  test "$stamped" = "$version" \
    || { echo "FAIL: signed ${stamped:-nothing}, expected $version"; exit 1; }
}

# A slot0 written straight by the bootloader never swaps, so it never gets to
# mark itself good: unconfirmed, the image is reverted on the second boot.
assert_confirmed_image_differs() {
  local out=$workspace/firmware/build/$product/zephyr
  test -f "$out/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: no confirmed hex, is CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE still y?"; exit 1; }
  ! cmp -s "$out/zephyr.signed.hex" "$out/zephyr.signed.confirmed.hex" \
    || { echo "FAIL: the confirmed hex is byte-identical to the unconfirmed one"; exit 1; }
}

# The host `size` cannot read an arm-zephyr-eabi ELF, and the SDK's own tool is
# not on PATH outside a build. SDK 1.0 moved the toolchains under gnu/.
size_tool() {
  local tool
  for tool in "${ZEPHYR_SDK_INSTALL_DIR:-}"/{gnu/,}arm-zephyr-eabi/bin/arm-zephyr-eabi-size \
              "$sdk_base"/zephyr-sdk-*/{gnu/,}arm-zephyr-eabi/bin/arm-zephyr-eabi-size; do
    if [ -x "$tool" ]; then echo "$tool"; return; fi
  done
  echo "FAIL: no arm-zephyr-eabi-size in the Zephyr SDK" >&2
  exit 1
}

# mkuf2 leaves its intermediate .merged.hex beside the .uf2, so it writes into
# the build directory and builds/ takes the copy.
stage_artifacts() {
  local version=$1 out=$workspace/firmware/build/$product/zephyr size
  size=$(size_tool)
  SIZE=$size "$python" "$workspace/scripts/size_check.py" "$out/zephyr.elf"
  "$python" "$workspace/scripts/mkuf2.py" "$workspace/firmware/build/$slug.uf2" \
    "$workspace/firmware/build/mcuboot/zephyr/zephyr.hex" \
    "$out/zephyr.signed.confirmed.hex"
  mkdir -p "$repo/builds"
  cp "$workspace/firmware/build/$slug.uf2" "$repo/builds/$slug.uf2"
  cp "$out/zephyr.signed.bin" "$repo/builds/$slug.signed.bin"
  cp "$out/zephyr.signed.confirmed.bin" "$repo/builds/$slug.signed.confirmed.bin"
  echo "$slug $version $(git -C "$workspace" rev-parse --short HEAD)" \
    > "$repo/builds/$slug.version"
}

require_host_tools
checkout_ref
install_west
update_workspace
install_sdk
create_signing_key

version=$(image_version)
echo "== building $product $version from $ref ($(git -C "$workspace" rev-parse --short HEAD))"
build_image "$version"
assert_version_stamped "$version"
assert_confirmed_image_differs
stage_artifacts "$version"

echo
echo "builds/$slug.uf2  <-  $product $version"
