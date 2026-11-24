#!/bin/sh
# check-host-consumers.sh: run packaged consumer checks in both link modes
# Copyright (c) 2026 Fredrick Meunier
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.

set -eu
if [ "$#" -ne 2 ]; then
    printf 'Usage: sh %s SDL_BUILD NULL_BUILD\n' "$0" >&2
    exit 2
fi
src=$(cd "$(dirname "$0")/../.." && pwd)
sdl=$(cd "$1" && pwd)
null=$(cd "$2" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' 0
trap 'exit 1' HUP INT TERM
cd "$tmp"
export HOME="$tmp" XDG_CONFIG_HOME="$tmp" USERPROFILE="$tmp" APPDATA="$tmp"

# TAP: length 3, flag FF, one data byte 00, checksum FF.
printf '\003\000\377\000\377' > input.tap
for mode in archive source; do
    "${MAKE:-make}" -C "$sdl" -f Makefile -f "$src/examples/host-consumer.mk" \
        HOST_INPUT="$src/examples/sdl-loop-host.c" HOST_OUTPUT="$tmp/sdl" \
        "host-consumer-$mode"
    if ! SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" FUSE_DISPLAY_TIMING=1 \
        "$tmp/sdl" "$tmp/input.tap" > sdl.log 2>&1; then
        grep . sdl.log >&2 || :
        exit 1
    fi
    grep -x 'SDL host: loaded and inspected 1 tape blocks' sdl.log
    grep -x 'SDL host: received dirty tape decision; parked before answering' sdl.log
    grep -x 'SDL host: input observed, pause/resume verified, quit answered and shut down' sdl.log
    test "$(grep -c 'presentation_count=1 ' sdl.log)" -eq 3

    "${MAKE:-make}" -C "$null" -f Makefile -f "$src/examples/host-consumer.mk" \
        HOST_INPUT="$src/examples/worker-loop-host.c" HOST_OUTPUT="$tmp/worker" \
        "host-consumer-$mode"
    "$tmp/worker" > worker.log 2>&1
    grep -x 'worker host: UI dispatched work while Quit was waiting' worker.log
    grep -x 'worker host: UI received shutdown acknowledgement' worker.log
    printf 'Host consumer checks passed (%s)\n' "$mode"
done

# Use the very same SDL-configured core archive with a non-widget adapter.
archive="$sdl/.libs/libfuse.a"
before=$(cksum < "$archive")
"${MAKE:-make}" -C "$sdl" -f Makefile -f "$src/examples/host-consumer.mk" \
    HOST_INPUT="$src/examples/worker-loop-host.c" HOST_OUTPUT="$tmp/alternate" \
    host-consumer-test-adapter
"$tmp/alternate" > alternate.log 2>&1
grep -x 'worker host: UI dispatched work while Quit was waiting' alternate.log
grep -x 'worker host: UI received shutdown acknowledgement' alternate.log
test "$before" = "$(cksum < "$archive")"
printf 'Same configured archive passed with a second UI adapter\n'
