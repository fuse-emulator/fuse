# One-shot development automation

## Overview

Fuse has an optional, development-only automation mode for running bounded,
headless emulator scenarios from the normal `fuse` executable. A scenario is
specified with command-line options, runs through Fuse's ordinary initialization
and emulation paths, writes a structured `result.json`, and exits.

The interface is intended for regression suites and controlled experiments. It
is not a remote-control protocol, an interactive debugger API, or a persistent
emulator service. Each process runs one scenario; process startup and shutdown
provide isolation between scenarios.

The implementation currently supports:

- stopping after a fixed number of completed machine frames;
- stopping when a success or failure PC is reached;
- ignoring an initial number of failure-PC hits;
- bounded PC-condition runs with a frame deadline;
- bounded execution until RZX playback completes;
- structured RZX completion and failure outcomes;
- collection of diagnostics from the null UI;
- machine, relevant setting, media, snapshot, RZX, and ROM identity;
- unthrottled execution using synthetic time in the null timer.

The `play_disk`, `check_loaders`, `play_rzx`, and `check_rzx` tools in the
adjacent `fuse-automation` repository consume this interface. RZX tools no
longer require a locally patched Fuse executable.

## Building

Automation is excluded from normal builds. Configure a development build with:

```sh
./configure \
  --with-null-ui \
  --with-audio-driver=null \
  --enable-automation
make
```

`--enable-automation` adds the automation coordinator and command-line options.
The null UI supplies a noninteractive frontend, while the null audio driver
avoids opening a host sound device.

An automation-enabled build behaves normally unless an `--automation-*` option
is supplied. When a scenario is active, settings autosave is disabled so the
run does not modify the user's configuration.

## Command-line interface

Every scenario requires an output directory and a positive frame limit. The
frame limit is either the requested duration or the deadline, depending on the
termination mode.

### Common options

```text
--automation-output DIR
--automation-frames N
--automation-max-frames N
```

`--automation-output` selects an existing directory in which `result.json` is
written. Fuse does not create the directory; the caller owns output-directory
creation and cleanup.

`--automation-frames N` requests an ordinary fixed-frame run.
`--automation-max-frames N` supplies the deadline for a PC-condition or RZX
run. Both options populate the same frame-limit field; use the spelling which
expresses the scenario's intent.

### PC conditions

```text
--automation-success-pc ADDRESS
--automation-failure-pc ADDRESS
--automation-failure-pc-ignore COUNT
```

A PC-condition run requires a success PC. The optional failure PC terminates
with failure. `--automation-failure-pc-ignore` ignores the first `COUNT` hits
of the failure address before it becomes fatal. Addresses and counts accept the
numeric forms understood by `strtoul(..., 0)`, including decimal and `0x`-prefixed
hexadecimal.

For example:

```sh
fuse \
  --automation-output /tmp/loader-result \
  --automation-max-frames 25000 \
  --automation-success-pc 0x8000 \
  --automation-failure-pc 0x0008 \
  --automation-failure-pc-ignore 1 \
  --no-sound --no-confirm-actions \
  game.tzx
```

### RZX completion

```text
--automation-until-rzx-end
```

This requests termination when playback completes or encounters an RZX error.
It must be paired with `--automation-max-frames` so a recording can never run
indefinitely:

```sh
fuse \
  --automation-output /tmp/rzx-result \
  --automation-until-rzx-end \
  --automation-max-frames 10000000 \
  --no-sound --no-confirm-actions \
  --playback recording.rzx
```

External snapshots are supplied through Fuse's normal `--snapshot` option.
The startup sequence loads the external snapshot before beginning RZX playback.

## Execution model

Automation does not provide an alternate emulator loop. Fuse still runs:

```c
z80_do_opcodes();
event_do_events();
```

The coordinator arms after machine and startup-file initialization. Frame
progress is measured from `spectrum_get_frame_count()` and therefore counts
completed normal machine-frame cycles. The main loop observes the frame limit
after opcode and event processing.

PC matching is attached to instruction execution without opening debugger UI or
executing debugger command text. An automation condition directly records its
outcome and requests normal process termination.

RZX playback reports lifecycle events through an internal structured seam.
RZX internals do not assign debugger exit codes or terminate the process
unconditionally. They notify automation of:

- normal end of playback;
- input desynchronisation;
- RZX parse failure;
- embedded or external snapshot failure;
- playback abort.

The coordinator only turns those notifications into process termination when
`--automation-until-rzx-end` was requested. Ordinary interactive RZX behavior
is unchanged.

Fuse performs normal shutdown after writing the result. This retains the usual
startup-manager, peripheral, UI, media, module, and libspectrum cleanup paths.

## Synthetic time

With the null timer and an active scenario, host waiting is replaced by
synthetic monotonic time:

- `timer_get_time()` returns virtual time;
- `timer_sleep(ms)` advances virtual time instead of sleeping;
- normal timer scheduling and speed-estimation code remains active.

This removes emulation throttling without changing the configured emulation
speed. It is why automation corpus runs can use a full CPU core per Fuse
process. When no automation scenario is active, the null timer delegates to the
normal compatibility timer functions.

## Result file

The current result schema is version 1. Members may be added compatibly, so
consumers should ignore unknown members and classify runs using
`execution.termination.type` rather than process output text.

A representative RZX result is:

```json
{
  "schema": 1,
  "scenario": {
    "maximum_frames": 10000000,
    "until_rzx_end": true,
    "requested_machine": "48"
  },
  "execution": {
    "frames_completed": 429,
    "actual_machine": "48",
    "cpu_mode": "nmos",
    "termination": {
      "type": "rzx-end"
    }
  },
  "identity": {
    "rzx": {
      "path": "recording.rzx",
      "size": 924,
      "crc32": "6d3467e0"
    },
    "rzx_snapshot_source": "embedded",
    "active_roms": [
      {
        "page": 0,
        "size": 16384,
        "crc32": "ddee531f"
      }
    ]
  },
  "settings": {
    "autoload": true,
    "fastload": true,
    "tape_traps": true,
    "loader_acceleration": true,
    "phantom_typist_mode": "Auto"
  },
  "diagnostics": [
    {
      "severity": "info",
      "message": "Finished RZX playback"
    }
  ],
  "artifacts": {}
}
```

### Scenario

`scenario.maximum_frames` contains the fixed-frame count or deadline.
`scenario.until_rzx_end` records whether RZX completion was requested.
`scenario.requested_machine` records the configured machine identifier.

For PC runs, the scenario also contains `success_pc`, and, when configured,
`failure_pc` and `failure_pc_ignore`.

### Execution and termination

`execution.frames_completed` is measured relative to the point at which the
scenario was armed. `actual_machine` records the machine which actually ran.
`cpu_mode` is `nmos` or `cmos`; for RZX playback it preserves the mode in use
while playback was active, including legacy Spectaculator compatibility.

Current termination names are:

| Type | Meaning |
| --- | --- |
| `frames` | Requested fixed frame count completed |
| `success` | Success PC was reached |
| `failure` | Failure PC was reached after its ignore count |
| `rzx-end` | RZX playback completed normally |
| `rzx-desynchronisation` | Recorded input no longer matched execution |
| `rzx-parse-error` | The RZX container or playback stream could not be parsed |
| `rzx-snapshot-error` | Initial or later RZX snapshot restoration failed |
| `rzx-aborted` | Playback stopped without another RZX outcome |
| `deadline` | The requested condition was not reached in time |
| `error` | Other automation execution error |

A successful or failed PC termination also records the matching `pc` in the
termination object.

### Identity

Identity records use CRC-32, lowercase hexadecimal, and include the byte size.
The current implementation deliberately uses local CRC-32 rather than SHA-256
to limit dependencies. Depending on the scenario, `identity` can contain:

- `tape`: the loaded tape/PZX/TZX input;
- `rzx`: the input recording;
- `external_snapshot`: a separately loaded snapshot;
- `rzx_snapshot_source`: `embedded` or `external`;
- `active_roms`: the active 16 KiB ROM pages after startup.

Paths are diagnostic source names, not stable identity. Consumers should use
size and CRC-32 together when comparing inputs.

### Settings

The result records settings known to affect automated loading:

- automatic loading;
- fast loading;
- tape traps;
- loader acceleration;
- phantom typist mode.

This is not a complete serialization of all Fuse settings.

### Diagnostics

The null UI forwards `ui_error_specific()` messages to the coordinator. Each
diagnostic has a `severity` of `info`, `warning`, or `error`, plus its message.
RZX sentinel warnings and compatibility notices therefore remain structured
warnings rather than changing an otherwise successful termination into a
special process status.

With `--automation-capture-screen`, `artifacts.screen` describes `screen.png`
and records its dimensions, RGB24 pixel CRC-32, and serialized file CRC-32.
With `--automation-capture-audio`, `artifacts.audio` describes `audio.wav` and
records its sample rate, channels, S16LE PCM-frame count, PCM CRC-32, file
CRC-32, and zero frame-relative starting tstate. Audio contains exactly the
normal low-level PCM deliveries for completed frames after automation is armed;
startup audio and an extra partial termination frame are not included.

The `state` object is a fixed final summary of CPU registers, interrupt state,
frame-relative tstate, screen and border state, and tape/RZX activity. It is
not a general inspection interface.

## Process exit status

The process status is a coarse indication suitable for shell scripts:

| Status | Meaning                                                             |
| ------ | -------                                                             |
| `0`    | Frames, success PC, or RZX completion occurred as requested         |
| `1`    | Failure PC, RZX error/abort, startup error, or result-writing error |
| `2`    | Frame deadline was reached before the requested condition           |

Consumers needing a precise classification must read `result.json`. In
particular, diagnostics do not replace the termination type.

## Current limitations

The interface intentionally remains narrow:

- there is no persistent control channel;
- there are no arbitrary debugger commands;
- there is no arbitrary memory or register query API;
- there is no scripted keyboard/joystick input;
- screen capture requires a build with PNG support and the null UI;
- audio capture requires the null sound driver;
- there is no instruction trace or arbitrary state capture;
- hashes are CRC-32 rather than cryptographic content hashes;
- diagnostics are retained in memory for the duration of a run and are not yet
  bounded;
- host-backed peripherals and all sources of nondeterminism are not globally
  virtualized or rejected;
- failures before enough startup state exists may not provide the same identity
  fields as a completed run.

Automation is a development boundary around normal Fuse execution, not a claim
that every possible machine/peripheral configuration is deterministic. Suites
should select controlled fixtures, retain the result metadata, and treat
uncontrolled host inputs as qualifications of the experiment.

## Source layout and validation

The coordinator and JSON writer are in `automation/`. Integration points are
primarily in `fuse.c`, `settings.pl`, `utils.c`, `rzx.c`, `periph.c`, the null
UI, the null timer, and instruction/frame accounting.
