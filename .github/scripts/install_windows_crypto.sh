#!/usr/bin/env bash
# Use the normal MSYS2 packages, pinned explicitly while absent from the DB.
# The resulting cache travels with libspectrum to the downstream Fuse job.
set -euo pipefail

test "${MSYSTEM:-}" = MINGW32 || {
  echo "Run in MSYS2 MINGW32, not MSYS/UCRT64 or a Cygwin cross environment" >&2
  exit 1
}
script_dir=$(cd "$(dirname "$0")" && pwd)
lock="$script_dir/windows-mingw32-crypto.sha256"
cache=${1:-/opt/fuse/share/fuse/packages}
mkdir -p "$cache"
cache=$(cd "$cache" && pwd)
packages=()
while read -r hash name; do
  case "$hash" in ''|'#'*) continue ;; esac
  for file in "$name" "$name.sig"; do
    if [ ! -f "$cache/$file" ]; then
      curl --fail --location --retry 3 \
        "https://repo.msys2.org/mingw/mingw32/$file" -o "$cache/$file.part"
      mv "$cache/$file.part" "$cache/$file"
    fi
  done
  # Fail closed: do not disable pacman's signature policy to use retained files.
  pacman-key --verify "$cache/$name.sig" "$cache/$name"
  packages+=("$cache/$name")
done < "$lock"
(cd "$cache" && sha256sum --check "$lock")
# Install both in one transaction; dependencies such as cc-libs and gettext
# continue to resolve from the normal MINGW32 package database.
pacman --noconfirm -U --needed "${packages[@]}"
