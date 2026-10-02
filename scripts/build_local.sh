#!/usr/bin/env bash
# Build a product image off the committed tip of local main, into builds/.
#
#   scripts/build_local.sh [product]        product defaults to skyblip_go
#
# What the `product-image` CI job does, on this machine and on any ref: both
# end in scripts/build_image.sh. The first run bootstraps a Zephyr workspace and downloads the SDK into
# ~/.local/opt, which takes a while; every run after that is just the build.
#
# The tree it builds is a detached worktree of SKYBLIP_REF, never the checkout
# you are editing: an image that boots is worth a commit anyway, and the west
# workspace then has somewhere to live that is not the repo you work in.
#
#   SKYBLIP_REF          what to build (default main, e.g. HEAD, a tag, a sha)
#   SKYBLIP_WORKSPACE    worktree + west workspace (default ~/.cache/skyblip/west)
#   SKYBLIP_SIGNING_KEY  MCUboot signing key, generated once if absent
#   SKYBLIP_PUBLIC_KEY   fail unless the image verifies against it (release.yml)
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

require_host_tools
checkout_ref
install_west
update_workspace
install_sdk
create_signing_key

SKYBLIP_TREE=$workspace PYTHON=$python WEST=$west \
  "$repo/scripts/build_image.sh" "$product" "$key" "${SKYBLIP_PUBLIC_KEY:-}"
mkdir -p "$repo/builds"
cp "$workspace/firmware/build/out/$slug".* "$repo/builds/"

echo
echo "builds/$slug.uf2  <-  $(cut -d' ' -f2- "$repo/builds/$slug.version")"
