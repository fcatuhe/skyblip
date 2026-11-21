#!/usr/bin/env bash
# Build product images off the committed tip of local main, into builds/.
#
#   scripts/build_local.sh [product...]     skyblip_go when none is named
#   scripts/build_local.sh skyblip_go skyblip_bravo
#
# It runs what the product-image CI job runs, through scripts/build_image.sh. The first run
# bootstraps a Zephyr workspace and the SDK in ~/.local/opt. It builds a detached worktree of
# SKYBLIP_REF, never the checkout you are editing.
#
#   SKYBLIP_REF          what to build (default main, e.g. HEAD, a tag, a sha)
#   SKYBLIP_WORKSPACE    worktree + west workspace (default ~/.cache/skyblip/west)
#   SKYBLIP_SIGNING_KEY  MCUboot signing key, generated once if absent
#   SKYBLIP_PUBLIC_KEY   fail unless the image verifies against it (release.yml)
#   SKYBLIP_CHANNEL      development (default) or production, read by build_image.sh
#   SKYBLIP_PRISTINE=1   throw the build directory away first
#   SKYBLIP_UPDATE=1     re-run west update even if the manifest has not moved
set -euo pipefail

if [ $# -gt 0 ]; then products=("$@"); else products=(skyblip_go); fi

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
ref=${SKYBLIP_REF:-main}
workspace=${SKYBLIP_WORKSPACE:-$HOME/.cache/skyblip/west}
key=${SKYBLIP_SIGNING_KEY:-$HOME/.config/skyblip/local-signing.pem}
west=$workspace/.venv/bin/west
python=$workspace/.venv/bin/python3
# INFO: fc 20nov26 Zephyr's CMake searches here for an SDK, so the build needs no path to it
sdk_base=$HOME/.local/opt

require_host_tools() {
  local missing=()
  for tool in cmake ninja dtc gperf git python3 ruby; do
    command -v "$tool" >/dev/null || missing+=("$tool")
  done
  if [ ${#missing[@]} -gt 0 ]; then
    echo "FAIL: missing build tools: ${missing[*]}"
    echo "on Arch: sudo pacman -S --needed cmake ninja dtc gperf git python ruby"
    exit 1
  fi
}

# INFO: fc 20nov26 resolve the ref in this checkout, because the worktree's HEAD is the previous build's commit
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

# INFO: fc 20nov26 west lives in the workspace's venv so a system python upgrade cannot break the toolchain
install_west() {
  [ -x "$west" ] && return
  echo "== creating the build virtualenv"
  python3 -m venv "$workspace/.venv"
  "$python" -m pip install --quiet --upgrade pip west
}

# INFO: fc 20nov26 Zephyr and its modules are pinned, so the update is keyed on the manifest's hash
update_workspace() {
  [ -d "$workspace/.west" ] || "$west" init -l "$workspace/firmware"
  local stamp=$workspace/.west/skyblip-manifest.sha256 manifest
  manifest=$(sha256sum "$workspace/firmware/west.yml" | cut -d' ' -f1)
  if [ "${SKYBLIP_UPDATE:-0}" != 1 ] && [ "$(cat "$stamp" 2>/dev/null)" = "$manifest" ]; then
    return
  fi
  echo "== west update (long on a first run: it clones Zephyr and its modules)"
  (cd "$workspace" && "$west" update --narrow --fetch-opt=--depth=1)
  "$python" -m pip install --quiet \
    -r "$workspace/zephyr/scripts/requirements-base.txt" \
    -r "$workspace/bootloader/mcuboot/scripts/requirements.txt"
  echo "$manifest" > "$stamp"
}

# INFO: fc 20nov26 this is west sdk install without setup.sh, which refuses to run without wget
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

# INFO: fc 20nov26 keep this key, because a unit takes an update only from the key that signed its image
create_signing_key() {
  [ -f "$key" ] && return
  mkdir -p "$(dirname "$key")"
  chmod 700 "$(dirname "$key")"
  echo "== generating a local signing key: $key"
  "$python" "$workspace/bootloader/mcuboot/scripts/imgtool.py" keygen -k "$key" -t ecdsa-p256
  chmod 600 "$key"
}

require_host_tools
checkout_ref
install_west
update_workspace
install_sdk
create_signing_key

# INFO: fc 20nov26 build_image.sh empties out/ for each product, so copy each before building the next
mkdir -p "$repo/builds"
for product in "${products[@]}"; do
  slug=${product//_/-}
  SKYBLIP_TREE=$workspace PYTHON=$python WEST=$west \
    "$repo/scripts/build_image.sh" "$product" "$key" "${SKYBLIP_PUBLIC_KEY:-}"
  cp "$workspace/firmware/build/out/$slug".* "$repo/builds/"
done

echo
for product in "${products[@]}"; do
  slug=${product//_/-}
  echo "builds/$slug.uf2  <-  $(cut -d' ' -f2- "$repo/builds/$slug.version")"
done
