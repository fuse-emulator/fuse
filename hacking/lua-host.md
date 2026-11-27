<!--
Copyright (c) 2026 Fredrick Meunier

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
-->

# Lua automation host

Build with Lua 5.4 or 5.5 development files:

```sh
./configure --with-lua
make
mkdir -p /tmp/fuse-evidence
./fuse-lua --script examples/lua/boot.lua --output /tmp/fuse-evidence -- /path/to/roms
```

`--with-lua` selects headless display, virtual sound and synthetic time. To select
a particular pkg-config module, set `LUA_PKG_CONFIG` when configuring. Lua is
linked only into `fuse-lua`; normal builds do not require it.

Without `--script`, `fuse-lua` accepts the existing Fuse command-line interface,
including `--automation-*`, media arguments and their existing result/exit codes:

```sh
./fuse-lua --machine 48 --automation-output /tmp/fuse-evidence \
  --automation-frames 250 --automation-capture-screen --automation-capture-audio
```

## Scenarios

Each script owns one emulator session. Script arguments follow `--` and are
available as `arg[1]`, `arg[2]`, etc. Use `--script -` to read Lua source
from stdin (including a pipe or shell here-document):

```sh
printf '%s\n' 'fuse.start {}; fuse.run { frames = 100 }' | \
  ./fuse-lua --script - --output /tmp/fuse-evidence --rom-directory /path/to/roms
```

In this mode `arg[0]` and the result's script path are `-`; Lua errors identify
the source as `stdin`. Reading finishes at EOF. The same execution limits and
script arguments apply; use an external timeout if the producer may block.

```lua
fuse.start {
  machine = "48",
  rom_directory = arg[1],
  capture = { screen = true, audio = true },
}
fuse.open(arg[2])
local loaded = fuse.run {
  success_pc = 0x8000, failure_pc = 0x0008,
  failure_pc_ignore = 1, max_frames = 25000,
}
fuse.expect(loaded.termination == "success", "Loading failed")
fuse.key("space", true)
fuse.run { frames = 2 }
fuse.key("space", false)
if fuse.registers().pc < 0x4000 then
  fuse.run { frames = 100 }
else
  fuse.run { tstates = 997 }
end
```

`start` accepts `machine` (generated default, currently `48`), `rom_directory` (default `roms`),
`roms` (logical ROM names mapped to explicit custom file paths), `auto_load`,
`tape_traps`, `divmmc`, `interface1` and `capture`. ROM files are copied through
the normal host ROM provider. Standard names are resolved only in the chosen
ROM directory; an override is explicitly marked custom.

`run` accepts exactly one mode:

- `frames=N`: the next N completed frame turns;
- `tstates=N`: one existing bounded T-state turn, including instruction/event overshoot;
- `success_pc`, optional `failure_pc` / `failure_pc_ignore`, and `max_frames`;
- `until_rzx_end=true` and `max_frames`;
- `until_disk_idle=true`, optional `disk_idle_frames` (default 50), and `max_frames`.

The returned table contains `termination`, `frames_completed`, and a matching
`pc` when applicable. Conditions and deadlines are data for branching; use
`fuse.expect` to require a particular result. PC conditions and register queries
use the existing host debugger stop/continue facilities. Frame/T-state runs
resume automatically after inspection.

Other operations:

| Function | Action |
| --- | --- |
| `key(name, pressed)`, `release_all()` | Spectrum matrix input; names include lowercase letters, digits, space, enter, caps and symbol |
| `read8(address)`, `read_memory(address, length)` | Read mapped memory; the latter returns a Lua byte string |
| `registers()` | Copy CPU register values at a debugger stop |
| `settings(table, allow_reset)` | Apply auto_load, tape_traps, divmmc and interface1 through the existing settings model; reset permission defaults false |
| `reset(kind)`, `machine(id)` | Soft/hard reset or machine selection |
| `open(path)`, `save_snapshot(path)` | Existing media identification/open and chosen-target snapshot save |
| `insert_tape(path)`, `tape_play()`, `tape_stop()`, `tape_rewind()` | Explicit tape insertion/transport; play toggles the transport |
| `insert_disk(controller, slot, path)` | Explicit floppy insertion; zero-based slot and plus3/beta/plusd/opus/disciple/didaktik controller |
| `record_rzx(path)`, `stop_rzx()`, `play_rzx(path)` | Recording with an embedded snapshot and replay |

Open keeps current host joystick mappings when a snapshot offers replacements.
Other unresolved media decisions fail the scenario rather than silently saving
or discarding changes. Save/record paths are explicit overwrite authorization.

## Results and evidence

The output directory must already exist. `result.json` uses the existing schema-1
envelope and shared CPU/settings/artifact writers, with `script` and `phases`
information added. Successful script execution terminates as `script-end`.
Assertions, Lua errors, resource limits and engine errors produce nonzero exits
and include available diagnostics and Lua source/traceback information.

`screen.png` follows the existing completed/presented logical-frame capture and
scaler semantics. `audio.wav` contains continuous normal final-mix PCM delivered
after execution starts, across run phases. Available evidence is finalized even
when a script fails. Audio format changes during capture are reported as errors.

Script frame accounting counts completed host frame turns; budget requests are
recorded separately because they can cross frame boundaries. Explicit media
operations retain their normal pause/reset behavior, including its effect on
sound synthesis history. Legacy command-line mode preserves its original
accounting and evidence semantics.

## Limits and permissions

Scripts are trusted local developer code. Media/ROM/save operations use the
normal host filesystem and enabled emulated peripherals retain their normal host
services. Run untrusted inputs in an external OS sandbox.

The host provides base, table, string, math and utf8 libraries. File/code loaders,
protected error-catching functions and the io/os/package/debug/coroutine libraries
are not exposed. Lua runs under an instruction quota (default 10 million) and a
64 MiB Lua heap limit. Captured PCM is capped at 128 MiB, diagnostics at 256
entries and run phases at 1024. `--instruction-limit N` and
`--max-total-frames N` (default one million completed frame requests) configure
execution limits. `--capture-screen` and `--capture-audio` set capture defaults.

Use an external wall-clock timeout for native library calls, blocking I/O and
process-level deadlines. A forcibly killed process cannot finalize its result.

Run the end-to-end tests with `make check-lua-host`.
