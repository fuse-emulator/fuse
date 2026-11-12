# Internal legacy-engine build

## Boundary

The executable links the non-installed Libtool convenience library
`libfuselegacy.la`. This is the configured application engine, including the
selected UI, debugger, pacing and audio backend. It is not a toolkit-free core,
an installed embedding SDK or a stable binary ABI.

`fuse-main.c` owns normal/SDL entry and platform bootstrap. Win32's executable
also includes `ui/win32/win32main.c` and its resources. The private
`fuse_legacy_main()` compatibility wrapper remains inside the engine. It composes
private preparation, blocking legacy execution and shutdown, with explicit startup
outcomes. Guarded shutdown unwinds completed manager registrations on startup
failure, but the remaining frontend/backend limitations below still prevent
advertising a supported embedding lifecycle/execution API.

## Current ownership boundary

The private lifecycle accepts only one session attempt per process. Its declarations
are in `fuse-internal.h`, not an installed SDK header:

- `fuse_legacy_prepare(argc, argv)` returns READY, HELP, VERSION, FAILED, BUSY,
  CLOSED or INVALID_ARGUMENT. Help/version and failure clean up before returning;
  only READY retains a session. Invalid vector shape is rejected without consuming
  the attempt or changing callbacks/settings. Zero arguments with NULL argv is
  permitted; otherwise argv must have non-NULL entries and a NULL terminator.
- `fuse_legacy_run_frame()` accepts READY repeatedly and advances to the next
  completed frame boundary, which may be less than a full frame from the current
  position. It returns COMPLETED, PAUSED or EXITING; EXITING takes precedence over
  PAUSED and COMPLETED. A paused entry advances no emulated time. Re-entry during
  startup, execution or shutdown returns BUSY; other non-READY phases return
  NOT_READY. It does not finalize application output or shut down the session.
- `fuse_legacy_run_tstates(budget)` advances by a relative emulated T-state
  budget, sharing the frame entry's lifecycle and blocking rules. Budgets must
  be 1..0x7fffffff and the starting clock below 0x7fffffff; invalid requests do
  not execute. Instructions and due-event/frame dispatch finish before returning.
  COMPLETED means at least the requested budget; PAUSED, INTERRUPTED and EXITING
  may return earlier. This is not an exact cutoff or wall-clock deadline.
  Normal frame/RZX clock rebasing is accounted for. Ordinary reset continues;
  machine selection, snapshot restoration or debugger clock assignment during
  a turn instead returns INTERRUPTED, without restarting the budget. Exit wins
  over interruption, then pause, then completion. The next call uses the new
  state normally; replacement between calls needs no special handling.
- `fuse_legacy_execute(&status)` accepts READY once and runs the existing blocking
  loop. Its return value reports acceptance; the separate output receives the
  existing application exit code. Rejection leaves that output untouched.
  It may follow host turns. Automation arms once at the first actual execution
  entry (frame, budget or legacy), checks limits after dispatch, and writes results only
  when the legacy runner finishes, before shutdown.
- `fuse_legacy_shutdown()` disposes a READY or completed session once. It is a
  no-op in fresh/closed states and during preparation, execution or cleanup
  callbacks. Internal failure unwind and fatal abort use the separate guarded
  cleanup path; ordinary shutdown is not a stop request.

The internal states are FRESH -> STARTING -> READY -> RUNNING -> FINISHED ->
STOPPING -> CLOSED. Host turns instead go READY -> RUNNING -> READY, including
when they observe exit; the caller may then shut down or use the legacy runner
for application result finalization. READY can also go straight to shutdown;
help/version and failure go from STARTING through cleanup to CLOSED. Calls during
an active attempt return BUSY; another attempt after closure returns CLOSED.
Help/version and failed initialization also consume the supported lifetime.
All calls remain serialized on the selected frontend owner thread. This is not
thread-safe instance creation, restart, cooperative execution or a toolkit-free
worker API.
Argument storage remains borrowed and may be modified/reordered by the parser or
selected toolkit; keep it alive through shutdown.

The compatibility wrapper preserves CLI exit mapping and the existing dispatch
order. A private `fuse_dispatch_once()` groups the CPU batch and due-event dispatch;
frame callbacks, including their RZX/interrupt/input/UI companions, are unchanged.
All execution entries use it, with automation limits checked after dispatch.
The frame entry clears a completion flag when starting a turn; the full frame
callback sets it after its work finishes. Ordinary reset leaves the flag alone;
no cumulative frame counter is needed. It returns after due events drain, not
halfway through interrupt/RZX/UI processing. Ordinary reset does not cancel the
turn; machine selection replaces its queued frame event and execution can proceed
to a boundary.

Budget turns limit CPU batches at the existing instruction boundary rather than
queueing a timer event. This is important for RZX: its forced dispatch can consume
future events and move the frame clock, so adding a deadline event can itself
change playback chronology. The optional CPU limit uses the existing per-opcode
check machinery; ordinary execution skips it. Per-dispatch accounting records
only coordinate adjustments made by frame rebasing, RZX normalization and its
long-frame sentinel. RZX's forced time advance may overshoot a requested budget,
but complete frame/interrupt dispatch is preserved. No permanent emulation clock
or generation counter is introduced. Clock-replacement hooks invalidate only the
active budget and lower its CPU limit so a stale limit cannot strand execution.
These hooks are internal bookkeeping, not new host scheduling/reset operations.

Existing audio admission, debugger and UI callbacks may block inside execution;
there is no wall-clock latency bound or cross-thread cancellation. Z80 HALT does
not stop frame execution or interrupt delivery. Hosts service commands and choose
when to wait between calls, including while paused; no polling sleep is imposed.
Legacy frontend modal/nested-loop handling remains intact. The legacy runner owns
its exit loop and application result finalization. RNG seeding, core-managed
libspectrum/XML and platform privilege handling
remain at their existing startup stages. No seeding hook, generic policy framework,
borrowed-library mode or new frontend responsibility is introduced.

Settings acquire their strings before configuration/argument parsing. Parse
failure releases those strings, and `settings_end(save)` is repeatable. Automatic
preference saving requires authorization from `fuse_legacy_main()` after READY,
as well as the user's autosave setting. Split core preparation/execution/shutdown
does not grant that permission, even when the autosave setting is enabled; it does
not change the setting or disable explicit settings-write operations. This keeps
normal CLI/frontend persistence unchanged without making it a side effect of core
resource disposal. Help/version and initialization failure do not save. Settings
retain their manager callback position, with an idempotent fallback if that
registration never completed. Process-global XML cleanup now follows SVG and automation disposal,
rather than running inside settings teardown while other XML users may be alive.

The core lifecycle records successful libspectrum acquisition and restores the
previous libspectrum error callback only after owned teardown finishes, including
when preparation returns HELP, VERSION or FAILED. Libraries remain managed behind
the core boundary; callers are not required to initialize them. This is still an
exclusive legacy-runtime profile, not a host-borrowing library contract.

Temporary startup inputs are owned by `infrastructure/startup_files.[ch]`.
File metadata/buffers and IDE paths have owned storage independent of settings;
positional IDE replacements are copied, not borrowed from argv. The recording
path remains borrowed from settings until startup operations finish. Temporary
storage and the scaler-selection string are released on each local return path.

### Manager and UI rollback

The shared shutdown path is guarded before calling out, tolerates repeated calls,
and is a no-op before the first attempt. Completed module callbacks run once in
reverse completion order; pending registration storage is then freed. A failing
initializer still owns its own unfinished work. Emergency abort remains fatal
and can only clean known acquisitions, not guarantee rollback inside an arbitrary
fatally failed initializer.

| Owner | Implemented cleanup boundary |
| --- | --- |
| Display | Records successful `ui_init`; releases that UI once in the legacy tail position and frees border storage/count/capacity. Display-setup failure after UI acquisition rolls back locally. |
| Sound | Releases acquired low-level output and partial synthesis/buffer/mixer resources independently of `sound_enabled`. Low-level init failure still belongs to the selected backend. Nonfatal no-sound fallback is preserved. |
| Machine registry | Commits completed model objects, unwinds previously committed models on failure, and clears model/snapshot-ROM metadata on shutdown. The shared +3/+3e controller is freed once. |
| Keyboard/joystick | End functions are registered as callbacks, not accidentally passed as initializer contexts. |
| RZX recording | Dependencies keep creator data and settings alive through recording finalization, including rollback after recording starts. |
| Media/registries | Releases lazy peripheral state, disk image buffers without save/eject decisions, registration lists and Interface 1 descriptors. |
| Trainers | Deactivates while machine maps/memory are still alive, before manager teardown. |
| GTK | Uses checked initialization; failed window/menu setup rolls back acquired widgets/resources. Terminal cleanup cancels display callbacks/timeouts and releases the Cairo surface before destroying widgets. |
| SDL | Releases widget/SDL state on init failure; completed UI shutdown releases independent resources even if display teardown reports an error. |

This is resource disposal, not a transaction that undoes already performed file
writes or removes legacy modal acknowledgement. Frame/event/RZX dispatch, audio
admission and normal frontend interaction policies are unchanged.

**Remaining limits:** partial-initialization cleanup inside Xlib, framebuffer
and Wii adapters, including exit/signal-hook ownership, still needs its own audit
and platform verification. A source/SDK audit of Win32 found unchecked native
allocations, incomplete window/resource disposal, missing callback readiness and
shutdown guards, unsafe WinMM failure cleanup, and unbalanced DirectSound COM
ownership. These need implementation fixes and native Windows validation; the
shared unwind tests do not establish Win32 lifecycle safety.
Inner `uidisplay_init` allocation failures
and low-level audio failure branches beyond the tested acquisition points likewise
require backend-specific checks. Recording finalization and external-I/O cleanup
errors also need targeted failure-path checks beyond successful disposal. GTK
resource rollback after creating real native widgets needs native runtime validation; a
checked display-open failure alone does not prove it. Fatal-policy paths reached
by startup operations, such as failed snapshot-machine fallback, also need
classification before a supported non-terminating lifecycle API is exposed. No
public lifecycle capability, host-borrowing profile or restart support is
advertised.

## Private host execution recipe

This is a source-local, selected-UI-bound worker-compatible profile. Build the
host and engine from the same configured revision, using `fuse-internal.h` and
one of the linking styles below. No installed SDK, feature/version negotiation,
new thread, command queue or toolkit-free initialization is provided. Preparation
still initializes the selected UI and reads legacy configuration; only automatic
preference saving is excluded from the split lifecycle.

Keep all calls on one owner thread compatible with that UI's threading rules.
Retain the host's existing command transport and scheduling. The following is a
sketch for a session **without CLI automation**; `host_*` names are placeholders,
not additional Fuse APIs:

```c
fuse_legacy_init_result init = fuse_legacy_prepare( argc, argv );
if( init != FUSE_LEGACY_INIT_READY ) {
  host_handle_startup_result( init );
  return; /* Do not shut down someone else's session after BUSY. */
}

for( ;; ) {
  host_service_commands();             /* On the owner thread, between turns. */
  if( host_stop_requested() ) break;
  if( !host_turn_due() ) {
    host_wait_for_work();              /* Wake for commands or the next turn. */
    continue;
  }

  fuse_legacy_run_result result = host_use_frame_turn()
    ? fuse_legacy_run_frame()
    : fuse_legacy_run_tstates( host_tstate_budget() );

  switch( result ) {
  case FUSE_LEGACY_RUN_COMPLETED:
  case FUSE_LEGACY_RUN_INTERRUPTED:
    host_reconsider_schedule();
    break;
  case FUSE_LEGACY_RUN_PAUSED:
    host_wait_for_work();              /* Do not busy-retry a paused core. */
    break;
  case FUSE_LEGACY_RUN_EXITING:
    goto finished;
  default:                            /* BUSY / NOT_READY / INVALID_ARGUMENT */
    host_report_usage_error( result );
    goto finished;
  }
}
finished:
fuse_legacy_shutdown();
```

- Choose a frame turn when the host needs the next complete frame boundary,
  independently of audio delivery. A budget turn may end within a frame; its
  COMPLETED result does not mean a frame completed. Frame and budget turns may
  be mixed in one prepared session.
- For elapsed-time scheduling, convert the host's desired interval using the
  current machine clock/speed and pass a positive budget in the documented
  range. Retain native timers, audio pacing and fastload wall-time policy in the
  host; do not turn those policies into emulated scheduler events. Account for
  instruction/event overshoot rather than treating each call as an exact slice.
- INTERRUPTED applies to a budget whose clock/state was replaced during the
  call. Reconsider the next request in the new state; do not assume the original
  budget completed or silently restart it. An ordinary reset does not interrupt
  the budget. Reset/replacement between calls starts the next turn normally.
- For host pause commands, use balanced owner-thread calls to the existing
  `fuse_emulation_pause()` / `fuse_emulation_unpause()` in `fuse.h`. Release only
  pauses the host owns; PAUSED is not a request to decrement somebody else's
  pause count. Do not introduce an independent core pause counter. Legacy menu
  and debugger nested loops retain their existing interaction behaviour.
- A host stop handled between calls can go directly to shutdown, even while
  paused. EXITING instead reports an exit already requested inside the engine.
  Neither permits recursive execution or shutdown from an active callback.
  Cross-thread requests must use the host's transport, not concurrent core calls.

The host owns waits between turns and must wake them for commands. An active
call may still block in audio, UI or debugger services; long prefix chains, tape
traps and complete event batches also prevent a hard response-time bound. There
is no cross-thread cancellation API. A queued stop cannot be promised immediate
service while such work is active. Explicit debugger-stop outcomes and removal
of synchronous waits belong to later milestones.

### Adopting the entries without replacing the host

Keep `fuse_legacy_main()` for the unchanged application path. To adopt host turns,
replace the one continuous `fuse_legacy_execute()` call with owner-thread turns
after READY, leaving initialization, native scheduling, command handling and
presentation intact. Preserve mutable startup argument storage through shutdown.
The continuous runner remains available, including after host turns, but once it
finishes further execution calls are rejected.

For an existing host with custom frame/timer helpers, replace its CPU loop and
associated frame-end flags/dispatch as one coherent group. Do not compile both
implementations or keep a deferred frame-end patch alongside the upstream frame
callback. Retain host-native timing and reconcile its existing pause bridge; no
Cocoa build, rebase or adoption is required to use these entries in this repo.
Adopting none remains valid.

For **CLI automation**, the test fixtures demonstrate one session across host and
legacy calls. After a turn returns EXITING, call `fuse_legacy_execute(&status)`
before shutdown if application result writing and exit-status mapping are wanted.
Because exit is already requested, that finalization does not execute another
CPU batch. Do not call it merely to finish an arbitrary host slice: without an
exit request it resumes continuous execution. Direct shutdown after host turns
does not write an automation result. See `check_frame_run()` / `check_budget_run()`
in `unittests/legacylifecycletest.c` for the concrete in-repo consumers.

## Canonical configured inputs

The included Automake fragments append engine sources to
`libfuselegacy_la_SOURCES`. There is no separate handwritten source manifest.
Inspect the selected inputs with:

```sh
make print-legacy-engine-inputs
```

The target lists sources, configured special objects and external link flags.
The inputs are:

- `libfuselegacy_la_SOURCES`: shared engine and selected frontend sources.
- `SOUND_LIBADD` and `TIMER_LIBADD`: configured Libtool backend objects. Possible
  backend sources are listed in `EXTRA_libfuselegacy_la_SOURCES`, so distribution
  and generated build rules account for them without compiling every backend.
- `LEGACY_SCALER_OBJECTS`: the two Libtool scaler objects. Both compile
  `ui/scaler/scalers.c`, with `SCALER_DATA_SIZE=2` and `4`, respectively.
- `LEGACY_ENGINE_OBJECTS`: the combined selected backend/scaler object list.
- `FUSE_LEGACY_LIBS`: configured external service link flags. Libtool carries
  these dependencies through the convenience library; the direct-source test
  consumer uses them explicitly after its objects.

Source builds also need the configured headers, generated
settings/options/keysyms/menus/Z80 files, source/build include roots and the same
feature definitions and compiler flags. The printed source list alone is not a
standalone build recipe. Do not compile both selected and alternative UI/audio
adapters or omit the custom scaler compilation rules.

Entry points, platform bootstrap, Windows executable resources and Emscripten
preload/link policy stay outside the library. System libraries required by engine
code remain engine link dependencies, even when platform-specific.

## Build checks

With the null UI:

```sh
make check-legacy-engine-link
```

This builds the same generic consumer in two forms:

- `unittests/legacyenginelinktest` links the convenience library.
- `unittests/legacyenginesourcetest` compiles the canonical engine sources and
  links the same configured special objects and service dependencies, without
  linking an engine archive.

The check invokes help/version through both consumers and is included in
`make check` for null-UI builds. `make check-startup-ownership` additionally
checks temporary storage, settings save permission, early runner cleanup and
callback restoration in isolated child-process preference directories. These
are internal build fixtures, not examples of a supported public API. The consumers also accept the existing runner's CLI,
so automation-enabled builds can use them for bounded differential captures.
`make check-legacy-lifecycle` uses those same two consumers to exercise the private
split interface: vector validation without consuming the attempt, zero-argument
startup, distinct help/version/failure outcomes, active/closed rejection,
preparation without execution (including no false automation result), one-shot
execution with success/nonzero application exit status, and repeated shutdown.
Frame-contract tests also cover repeatable turns, direct shutdown after a turn,
nested pause and paused commands, reset during/between turns, machine selection
replacing the queue, HALT/interrupt delivery, debugger exit, and recursive entry.
Budget-contract tests add subframe execution, instruction overshoot, multi-frame
rebasing, reset preservation, clock/queue replacement interruption, invalid input
and repeat entry after interruption. A breakpoint command that edits the clock
and continues without opening a UI must interrupt before another opcode executes.
These frame/budget contract tests run with or without automation. Automation
builds additionally generate an RZX covering its long-frame sentinel and
mid-recording snapshot restore, compare legacy/frame/budget/mixed result JSON,
and exercise deadline exits.
Each accepted attempt uses a fresh process and isolated preference directory.
Instrumented unwind tests additionally attempt reentrant prepare/frame/budget/
execute/shutdown from STARTING, RUNNING and STOPPING, without disposing active operations. Preference
checks use both consumers in empty and pre-existing configuration directories:
core shutdown neither creates nor rewrites preferences despite autosave being
enabled, while the real compatibility wrapper still saves after startup and
honors the user's no-autosave choice. A changed in-memory value makes unwanted
rewrites observable; an existing exit request bounds these wrapper tests without
changing production execution.

The legacy ROM search uses the executable's location; these fixtures live under
`unittests/`, not beside the source root's `roms/`. Supply explicit ROM paths for
uninstalled capture runs, for example `--rom-48 /path/to/roms/48.rom`, and the
corresponding options for other machines. Packaging does not introduce a new
resource-search policy. Preserve a meaningful program path in argv[0] when
reusing startup parsing. For tape-autoload checks, prefer standard ROM filenames
resolved from the usual search directories: a different `--rom-*` filename marks
that ROM custom and suppresses autoload, even when its bytes match the default.

With null UI and automation, `make check-startup-unwind` builds a separate source
consumer with `FUSE_STARTUP_TEST`. It injects failures before/after configured
manager modules, during model/display/audio setup and after machine/startup-file
acquisition. It checks reverse cleanup, no preference save, cleared ownership,
disposal of partially populated drive image storage, recording finalization on
rollback and repeated shutdown, plus real invalid-ROM/scaler/snapshot error paths. Optional
unconfigured module points report explicit skips. These hooks compile out of the
production engine: there is no environment variable, CLI option or installed test
API. The instrumented fixture uses the configured backend/scaler objects, not
instrumented copies of every backend or external library.

The audio interval-equivalence executables reuse engine Libtool objects, replacing
only `sound.lo` with their compile-time interval variants. Scaler unit tests use
the same two special scaler objects as the engine. Nothing installs the engine
library or its private headers; external source builds remain configuration-bound.
