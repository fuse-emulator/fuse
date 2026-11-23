# Internal legacy-engine build

## Scope and build inputs

`libfuselegacy.la` is a non-installed Libtool convenience library containing the
configured application engine: machine emulation, selected UI, debugger, pacing
and audio backend. It is not a toolkit-free core, installed SDK or stable binary
ABI. Consumers must use coherent sources, generated files and build configuration.

`fuse-main.c` owns normal/SDL entry and platform bootstrap. Win32 also uses
`ui/win32/win32main.c` and its executable resources. Platform entry points and
Emscripten preload/link policy are outside the library; system libraries required
by engine code remain engine dependencies.

Inspect the configured source and link inputs with:

```sh
make print-legacy-engine-inputs
```

The relevant build variables are:

| Variable | Contents |
| --- | --- |
| `libfuselegacy_la_SOURCES` | Engine and selected frontend sources, assembled by the included Automake fragments. |
| `SOUND_LIBADD`, `TIMER_LIBADD` | Configured Libtool backend objects. |
| `LEGACY_SCALER_OBJECTS` | Two compilations of `ui/scaler/scalers.c`, with `SCALER_DATA_SIZE=2` and `4`. |
| `LEGACY_ENGINE_OBJECTS` | Combined backend/scaler object list. |
| `FUSE_LEGACY_LIBS` | Configured external service link flags. |

Canonical-source consumers also need the configured headers, generated
settings/options/keysyms/menus/Z80 files, include roots, feature definitions and
compiler flags. The printed list alone is not a standalone build recipe. Do not
compile alternative UI/audio adapters together or omit the special scaler rules.

## Lifecycle

The host API is declared in `fuse-internal.h`; CLI adapter entries are in
`fuse-legacy.h`, which includes the host header for shared types and operations.
`fuse-host.c` retains only host ROM-provider callback storage/routing.
`fuse-config.c` owns the opaque configuration, engine defaults and application.
`fuse-operations.c` implements routine-backed machine/input/File Open and tape
insertion/deck/save/eject/recording entries, including admission, eligibility and
completion. Tape modules retain the existing mechanics; `tape.c` also owns tape
observation copying/freeing alongside the live tape state. `uimedia.c` keeps disk
entries that depend on its private registry/save helpers; no extra delegates or
private interfaces are introduced merely to centralize names. These are engine
implementations, not caller-side wrappers. `fuse.c` owns preparation,
lifecycle state and execution. `fuse-engine.h` declares private implementation
hooks without exposing lifecycle state; it is not a second operation API.
`fuse.h` contains other engine globals/service hooks, not the host interface.
The implementation header is not a host-consumer dependency.

There is one session attempt per process, with no restart, multiple-instance or
concurrent-entry support. All mutable engine calls belong to one owner thread compatible with the
selected frontend. Callbacks must not recursively enter the host API.

Choose one preparation entry:

- `fuse_host_prepare(config, resources)` uses explicit configuration and a ROM
  provider. It does not parse CLI options or read/write preferences.
- `fuse_legacy_prepare(argc, argv)` uses application configuration, CLI parsing
  and startup files. Its mutable argument vector and strings remain borrowed
  through shutdown. Zero arguments with NULL argv is permitted; otherwise argv
  must contain non-NULL entries followed by a NULL terminator.

Both return `fuse_init_result` (`FUSE_INIT_*`). READY retains a runnable session.
Help/version and initialization failure clean up before returning and consume the
attempt. Invalid arguments do not consume it. Active/reentrant preparation returns
BUSY; preparation after closure returns CLOSED.

After READY, use shared worker turns, typed operations or the continuous legacy
runner. `fuse_shutdown()` disposes a prepared, completed or failed-operation
session. It tolerates repeated calls and does nothing before preparation, after
shutdown or during active preparation/execution/cleanup callbacks. It is not a
cross-thread stop request.

`fuse_legacy_execute(&status)` runs the blocking application loop once after READY,
including after host turns. Its return value reports acceptance; `status` receives
the application exit code. Once that loop finishes, further execution is rejected.
`fuse_legacy_main()` combines legacy preparation, continuous execution and shutdown.

Automatic preference saving belongs only to `fuse_legacy_main()` after successful
preparation and also requires the user's autosave setting. Separate preparation,
execution and shutdown do not grant permission to save. Explicit settings-write
operations remain available to the application adapter.

The engine manages libspectrum/XML and restores the previous libspectrum error
callback after owned teardown. Completed startup registrations unwind in reverse
completion order; an initializer owns cleanup of its unfinished acquisition.
Disposal does not undo file writes or resolve media save/discard decisions.
Platform/backend partial-initialization cleanup and fatal paths are not uniformly
recoverable, particularly for Win32, Xlib, framebuffer, Wii and device backends.
Recoverable startup results are not a universal non-terminating lifecycle promise.

## Host configuration and ROMs

Allocate `fuse_host_config` with `fuse_host_config_alloc()` and use its machine,
sound, autoload preference, joystick-slot output and mouse-swap setters. The configuration owns copied
strings and may be freed after preparation returns, including failure. Other
settings use internal generated defaults; generated settings layouts are not host
configuration inputs. An unknown nonempty machine ID fails during preparation
and consumes the attempt. Joystick interface selection uses the engine's
`joystick_type_t` from `fuse-joystick.h`, also imported by emulation. There is no
parallel host output enum or translation table; existing numeric settings/name
ordering is preserved. Logical input slots remain separate from interface choice.

`fuse_host_resources` supplies a synchronous ROM acquisition callback. The engine
copies the descriptor; its context remains borrowed until preparation failure
cleanup or successful-session shutdown finishes. Each callback receives a logical
ROM name and returns zero with bytes, length and explicit `custom` provenance
(0 or 1), or nonzero on failure.

ROM bytes are copied before release. A supplied response release callback runs
exactly once, including after acquisition or size-validation failure. Names and
response bytes are borrowed only for the request/release interval. Embedded
snapshot ROMs take precedence over provider requests.

The host path does not silently retry filesystem search or a default ROM: fallback
is provider policy. A reported ROM failure fails host preparation/reset/selection
even when an optional-peripheral callback would otherwise disable itself and
return success. The legacy adapter can disable those peripherals and continue.

The host owns application UI setup and teardown; Fuse does not call `ui_init()`
or `ui_end()` on the host path, including on failure. Linked hooks must be usable
before preparation and through shutdown. The engine still initializes its
renderer and notifies the linked display of machine geometry. The legacy runner
owns application UI startup and shutdown.

`fuse_host_config_set_diagnostics()` optionally supplies an owner-thread
callback for errors, warnings and notices from Fuse's shared diagnostic path,
including libspectrum reporting while Fuse owns its error handler. The callback
runs during preparation, operations, execution and teardown as applicable.
Copy the message before returning if the frontend needs it later; do not block,
pump events or reenter Fuse. Host sessions bypass the linked UI diagnostic
handler even without a callback. Existing console logging and automation
reporting remain; legacy sessions keep their diagnostic UI. These notifications
are not decisions and do not replace operation results. The null host's
`--diagnostics MISSING_FILE` mode copies a failed Open diagnostic inside the
callback and prints it only after Open returns.

Host preparation retains internal defaults, RNG seeding and process-global
library/error handling. It does
not process application startup files/debugger commands, print the application
banner or drop process privileges. Preparation and resource access may block.
Only ROM acquisition uses this provider: media persistence, IDE, serial/network,
snapshot loading and synchronous decisions retain their existing internal paths.
In particular, legacy snapshot machine fallback can abort. There is no general
host-services or deterministic-checkpoint contract.

## Effective filesystem

Libspectrum owns the effective regular-file filesystem. Platforms select their
backend once with `libspectrum_fs_install`, before any filesystem operation;
otherwise the first operation selects native IO permanently. Keep the backend
state valid for all subsequent calls, including shutdown. Fuse's compatibility
calls use the library's canonical handles and checked operations. There is no
Fuse backend registry or fallback to native IO on a custom-backend failure.

Common media IO and related POK lookup, WAV tape input, IDE/MMC backing, binary
floppy images and text logs, PNG, XML/plain preferences, PSG, FMF, printer, SVG
and profile output use this filesystem. Directory and environment providers are
separate platform services. The development runner's `result.json` is a native
host artifact. ROM provenance retains its existing separate policy.

## Execution turns

`fuse_run_frame()` advances to the next completed frame boundary, not necessarily
a whole frame from the current position. `fuse_run_tstates(budget)` advances by a
relative emulated T-state budget. Both return `fuse_run_result` (`FUSE_RUN_*`) and
are repeatable after READY. Ordinary returns leave the session available for
commands or another turn; they do not finalize application output or shut down.
Only `fuse_legacy_execute()` polls native joysticks and pumps the linked UI at
frame completion. Host turns still deliver the emulated frame interrupt and
service deferred input. This does not disable synchronous UI interactions or
change input refresh at emulated port reads.

| Result | Meaning |
| --- | --- |
| COMPLETED | Requested frame boundary or budget reached. Budget completion does not imply a completed frame. |
| NEEDS_DECISION | A pending media decision blocks execution. No emulated time advances. |
| DEBUGGER_STOPPED | Host execution stopped at an opcode boundary for a breakpoint, memory watchpoint, or single-step, or already awaits a debugger command. |
| PAUSED | No execution on paused entry, or execution paused during dispatch. |
| INTERRUPTED | An active budget's machine/clock state was replaced; reconsider the next request in the new state. |
| EXITING | Exit requested inside the engine. |
| BUSY | Reentrant call during preparation, execution, another operation or cleanup. |
| NOT_READY | No runnable session. |
| INVALID_ARGUMENT | Invalid budget or unsupported starting-clock range; no execution. |

Budgets must be 1..0x7fffffff, with a starting clock below 0x7fffffff. Execution
finishes instructions and due-event/frame dispatch, so it can overshoot a budget.
Budget accounting compensates for frame rebasing and RZX clock normalization;
RZX forced dispatch can also overshoot. Ordinary reset does not interrupt a turn.
Machine selection, snapshot restoration or debugger clock assignment during an
active budget returns INTERRUPTED at a safe boundary. Replacement between calls
is ordinary; do not silently restart an interrupted request.

Exit takes precedence over interruption, then pause, then completion. Z80 HALT is
not a host pause: frame events and interrupt delivery continue. Turns preserve
complete frame/RZX/interrupt/input/UI dispatch rather than returning from inside
a partially processed frame callback.

The host owns command delivery and waits between turns. It may hold execution by
not requesting another turn, or stop by calling shutdown, including while paused.
Cross-thread requests must use host-owned transport. Legacy frontend pause bridges
use balanced `fuse_emulation_pause()` / `fuse_emulation_unpause()` calls in `fuse.h`;
release only pauses the caller owns. PAUSED does not authorize decrementing another
owner's count. The host API does not expose pause-count controls.

Audio, UI, debugger, traps, prefix chains and event work may block inside a turn.
There is no wall-clock latency bound, cross-thread cancellation or cooperative
execution guarantee. Keep native timers, audio pacing and fastload wall-time
policy in the host rather than converting them to emulated scheduler events.

For CLI automation, result writing and exit-status mapping belong to the legacy
runner. After a host turn reports EXITING, `fuse_legacy_execute(&status)` can
finalize them without another CPU batch. Without an exit request, that call
resumes continuous execution. Direct shutdown does not write an automation result.

## Machine and input operations

The `fuse_host_*` operations run between execution turns on the owner thread,
including while paused. They do not advance CPU time. Calls during active work
return BUSY; pending exit returns EXITING. Invalid arguments return
INVALID_ARGUMENT without core mutation. Before preparation, after shutdown or
legacy execution finalization, and after FAILED machine selection/reset, they
return NOT_READY.

Machine selection borrows an ID for the call and validates it before mutation.
Selection/reset reuse the machine mechanics, UI notifications and ROM services,
without adding an input release. They preserve nested pause ownership. ID
selection does not use the legacy snapshot/enum fallback-to-48K path. FAILED may
leave partially modified state: further execution/operations are rejected and
shutdown is required. There is no rollback/retry promise. NMI queues the normal
NMI event; it is not delivered inline.

- Normal keyboard input uses portable `input_key` values with Fuse's composite
  keys, shifted arrows, recreated keyboard and configured keyboard-joystick
  translation. It does not invoke widgets, menus or mouse grabbing, even for
  Escape/function keys. Toolkit-keycode conversion belongs to the host.
- `fuse_host_spectrum_key_event` is explicit low-level Spectrum matrix injection
  for uses such as tests or an on-screen Spectrum keyboard.
- Joystick input identifies logical slots (1, 2 or keyboard), not emulated
  interfaces. Fuse applies the configured mapping. Injection does not enable
  disabled/unavailable hardware.
- Relative mouse motion uses positive y down and wraps hardware counters.
  Logical left/right buttons respect configured swapping; native pointer capture
  and middle-button grab remain host/UI policy.
- Release-all clears physical keyboard and joystick/mouse button state without
  moving the mouse or cancelling synthetic phantom/disciple typing. Hosts must
  aggregate overlapping physical sources and avoid competing native writers;
  these are set/clear operations, not reference-counted holds.

Existing reset/module semantics apply. No input-sampling policy or owned state
query is provided; do not read live globals concurrently from a UI thread.

## Host debugger stops

`fuse_host_debugger_break()` requests a manual stop at the next opcode boundary
of a host turn. Execute breakpoints stop before the trapped opcode. Port, time,
and event breakpoints stop at the next opcode boundary, after the triggering
instruction or event dispatch. These stops do not open the debugger window.
While stopped, the host can
copy the registers with `fuse_host_debugger_copy_registers()`, choose Continue
or Step, or shut down. Neither debugger command runs the CPU itself. Continue
bypasses the execute check once after an execute breakpoint or ordinary Step
stop if the next opcode is still at the stopped address. The breakpoint stays
installed.

`fuse_host_debugger_step()` arms a stop at the next opcode boundary after the
one-shot bypass, as in legacy Step. Due events and interrupts can run before
that boundary, so the new PC may be in an interrupt handler. A frame or T-state
limit can end a turn first; Step stays armed for the next turn. A halted CPU
can step without changing PC.

A read/write watchpoint hit during a host CPU batch stops at the next opcode
boundary. The triggering access and instruction finish first. Copy the access
kind and address with `fuse_host_debugger_copy_watchpoint()`; the copied
registers describe the stop, not the triggering access. A frame or budget limit
can return before the stop is reported. Continue after a watchpoint does not
skip an execute breakpoint at the new PC. Port hits and memory accesses from
event callbacks do not populate the CPU watchpoint details. Legacy execution
retains its debugger UI.

While stopped, `fuse_host_debugger_command()` evaluates the existing command
language without refreshing or deactivating the linked debugger. A resume
command changes what the next host turn will do; it does not run the CPU by
itself. `disassemble` can return a requested pane address, and
`fuse_host_debugger_disassemble()` decodes a single instruction using the
existing disassembler. A parse error does not undo earlier commands in the
same input. Semantic diagnostics retain existing debugger reporting.

`fuse_host_debugger_copy_views()` copies the breakpoint list, mapped read
pages, and scheduled events while stopped. The copy has no pointers into
live engine state and survives Continue and shutdown. Release it with
`fuse_debugger_views_free()`. Build the stack pane from the copied registers
and mapped-memory reads; use the existing command language for editing.

## Host Quit

An existing media decision returns BUSY and must be resolved before
`fuse_host_request_quit(&request)`. Quit closes the tape, then walks floppies in
registration order, Microdrives in slot order, and IDE/MMC in legacy controller
and unit order. Retained attachments are included even if their peripheral is
inactive. Each dirty item returns NEEDS_DECISION with its ID, media kind and
applicable slot or controller/unit. IDE/MMC replies always pass a NULL path.
Use `fuse_host_answer_quit()` to answer; it may return the next decision. While
waiting, run turns are blocked and other host operations return BUSY. Cancel or
a failed Save stops the walk and retains the current item, but does not restore
media already ejected. Existing pauses are preserved.

READY_TO_SHUT_DOWN prevents further execution. The host then calls
`fuse_shutdown()`, which is unconditional teardown, including if called while
a decision is pending. Legacy Quit keeps its existing modal workflow.

## Microdrive cartridges

Enable Interface 1 with `fuse_host_config_set_interface1()` before preparation.
Slots 0–7 correspond to drives 1–8. The host can copy slot state, insert a file,
create an unformatted cartridge, eject, save to a chosen or retained target,
and set write protection. A copied filename remains valid after replacement.

Dirty eject, Insert and New return a decision ID for
`fuse_host_answer_microdrive_eject()`. Incoming file bytes are staged before
asking. A failed read, Cancel or failed Save retains the old cartridge; a
load failure after an approved eject does not restore it. Save needs a path
for an unnamed cartridge. As in legacy Fuse, saving an unnamed cartridge
leaves it unnamed. `--microdrive OUTPUT` in `hacking/null-host.c` demonstrates
Cancel retaining a cartridge and Quit saving it to the supplied target.

## Interface 2 and Timex Dock cartridges

`fuse_host_insert_cartridge()` takes a chosen image path and a slot on the
current machine. Unlike general File → Open, it never selects a different
machine for a Dock image. It borrows the path through return and uses the
regular-file backend, not the machine ROM provider. Insert and eject preserve
the existing Interface 2 soft reset and Timex Dock hard reset. A failed read
leaves the old attachment in place; a failure after reset begins does not
promise rollback. An invalid Dock image can leave the slot empty through the
existing reset fallback.

`fuse_host_copy_cartridge_info()` distinguishes unavailable, empty and loaded
slots on the selected machine. Its filename is an owned copy when available;
a snapshot-restored cartridge need not have a path. Release observations with
`fuse_cartridge_info_free()` even after teardown. Neither an observation nor an
unavailable slot authorizes a machine switch.

## File → Open

`fuse_host_open_file(path, &request)` invokes the existing general Open dispatcher with
`tape_can_autoload()`, then refreshes the display as the legacy menu does. The
current `auto_load` preference and ROM eligibility remain emulator policy;
`fuse_host_config_set_auto_load` supplies that existing value during preparation.
Configuration setters do not update a running session.

File identification, RZX stop-before-identification, tape/snapshot/cartridge/disk
loading, machine fallback, companion-POK discovery and selected-UI decisions stay
in the existing implementation. The host supplies a filename and command intent,
not decoded bytes or a copy of the dispatch rules. There is no media-type gate.
Native filename selection and command delivery/pause ownership stay with the host.

A dirty outgoing tape or floppy returns NEEDS_DECISION and retains the
identified input. `fuse_open_request` returns the ID and the question together.
Reply via `fuse_host_answer_tape_eject()` or `fuse_host_answer_disk_eject()`
as indicated. Tape Save needs a path; a floppy request names the controller
and slot and says whether Save can reuse the retained target. RZX stops before
identification, and floppy controller selection and machine fallback occur
before the request. A reply does not repeat that work. Cancel keeps the outgoing media but does not undo a
machine change. Autoload runs only on installation; tape companion POK lookup
and display refresh run when the attempt completes.

Direct snapshot Open returns NEEDS_DECISION for a supported joystick mapping
that is not already assigned. The request includes the type and recorded input
flags. Reply with a host input slot or NULL (to decline the mapping, not the
load) via `fuse_host_answer_snapshot_joystick()`. Each reply can return the
next question with a fresh ID. The decoded snapshot and proposed mappings
stay staged until the last reply, when Fuse restores the snapshot and performs
companion POK lookup and display refresh. Automatic Kempston enablement is
independent of the mapping choice.

Host Open of an RZX also returns joystick questions for its initial snapshot.
If the recording needs an external snapshot, Open returns
`FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT`; reply with a chosen filename or NULL to
cancel via `fuse_host_answer_rzx_external_snapshot()`. The reply can return a
joystick question for that snapshot. Replay starts only after the required
replies and restore. Later restores during playback retain existing behavior.
Legacy snapshot and RZX dialogs are unchanged; other File Open branches can
still call linked UI services.

`fuse_host_play_rzx()` starts replay from a chosen RZX path and returns the
same questions as Open, but refuses to replace an active replay or recording.
`fuse_host_record_movie_from_rzx()` also takes an authorized output path. Fuse
opens the FMF only after the initial restore and any replies, adding `.fmf`
if needed. The target may be overwritten without a linked UI prompt. If opening
the output fails, replay remains active, as in legacy Fuse. The existing
stop-after-RZX setting still applies. The null host's `--rzx-to-movie INPUT
OUTPUT` demonstrates this with up to five frame turns before shutdown.
`fuse_host_start_rzx_recording()` records from the current machine state, with
an optional embedded snapshot. `fuse_host_insert_rzx_snapshot()` inserts a
snapshot into the active recording. `fuse_host_stop_rzx()` stops replay or
writes the active recording, and `fuse_host_finalise_rzx()` rewrites a chosen
RZX file using the existing finalisation rules. `--record-rzx OUTPUT` in the
null host exercises that sequence, continues the saved recording, and reopens
the finalised result.

`fuse_host_record_rzx_from_snapshot()` starts from a chosen snapshot;
`fuse_host_continue_rzx_recording()` restores the final snapshot of a chosen
recording and writes back to that path on Stop. Both can return joystick
questions through the same reply API as File Open. Fuse stages the decoded
input until the choices are resolved; recording starts only after restore.

During a non-competition recording, `fuse_host_copy_rzx_rollback_points()`
returns an owned array of snapshot frame positions. Roll back to the latest
snapshot with `fuse_host_rollback_rzx()` or choose a zero-based snapshot ordinal
with `fuse_host_rollback_rzx_to()`. Use the list from the current recording;
it is an observation, not a token. The chosen snapshot must match the current
machine. Rollback truncates the recording before restoring and can park for
joystick mapping. Declining a mapping does not cancel rollback. Recording
resumes after restore; a failed restore does not undo the truncation.

OK means the dispatcher completed, which in the legacy branches can include cancelled/no-op
insertion; it does not prove replacement or program-loading completion. MEDIA_ERROR
maps nonzero, not a uniform unchanged-state or safe-to-continue outcome. File Open
can partially mutate state and retains legacy fatal paths. No rollback, extra
cleanup, strict-ID failure rule or new fallback policy is imposed. The example
shuts down on failure; this is not a universal error-safe loading API.

## Snapshot file save

`fuse_host_save_snapshot_file(path)` is chosen-target Save Snapshot, separate from
File Open and not a checkpoint/buffer interface. The borrowed path must be
nonempty. `snapshot_write` owns filename format inference, fallback to SZX for
unknown/non-snapshot names, conversion-loss warnings, export and encoding.
Warnings do not turn a successful legacy save into an error.

The engine entry uses the menu's nested pause/write/unpause sequence. First pause
can finalize competition-mode RZX recording even if the snapshot write fails;
non-competition RZX recording remains active. Existing pause/unpause return
conventions are retained, and the operation maps the writer's nonzero result to
MEDIA_ERROR, not machine FAILED. Reentrant caller operations remain BUSY.

No chooser or extra overwrite confirmation is added: target approval belongs to
the selected UI/host before this chosen-target call, and existing files can be
truncated/replaced. A codec failure before output may leave a target untouched;
a write/close failure may leave partial or even complete bytes. No cleanup retry,
output rollback, durability or full deterministic-state guarantee is imposed.
Actual snapshot restore still uses existing File Open semantics and limitations.

## Tape operations

`fuse_host_insert_tape_file(path, &id)` is explicit insertion without autoload
or a UI prompt. It reads the incoming file from the effective filesystem before
handling the outgoing tape. Failed reads leave the outgoing tape untouched. If
there are changes to confirm, NEEDS_DECISION returns a nonzero ID; Fuse retains
its own input bytes and path until the decision is answered or invalidated.
Tape and floppy share one pending media decision and one ID sequence. While a
tape replacement is pending, another insert or eject returns BUSY. A clean
replacement completes immediately when no media decision is pending.

Reply through `fuse_host_answer_tape_eject()`. Cancel or failed Save retains the
outgoing tape without restarting any finalized recording. Discard or a successful
Save ejects it and decodes the retained input. A decode failure can leave the
old tape removed and partial new blocks. Success reports insertion, not playback
or loading completion. For general **File → Open**, use `fuse_host_open_file()`
to retain file identification, tape autoload, and companion-file policy.

`fuse_host_toggle_tape_play()` uses manual `tape_toggle_play(0)`, not ensure-playing
or autoload. `fuse_host_stop_tape()` affects playback only, not recording.
`fuse_host_rewind_tape()` succeeds on an empty deck. Selection uses zero-based
`fuse_host_select_tape_block(int)`: negative is invalid, missing block is
MEDIA_ERROR. Neither rewind nor selection implicitly stops playback; queued-edge
and partial-interval behaviour remain the existing mechanics (including partial
interval reset before a failed selection).

`fuse_host_save_tape_file(path)` borrows the target and keeps format inference,
TZX fallback, recording finalization and encoding in the engine. It need not stop
playback; failure can follow recording finalization and is not a rollback.

`fuse_host_request_tape_eject(&id)` does not prompt.
It finalizes recording first, then ejects a clean tape or returns NEEDS_DECISION
with a nonzero ID. Repeating the request for the same tape returns the same ID.
Answer with `fuse_host_answer_tape_eject(id, choice, path)`: Cancel leaves the
tape loaded without restarting recording; Discard ejects; Save requires a
host-authorized destination and ejects only after a successful write. A valid
answer consumes the ID, including on write failure. A failed write leaves the
tape loaded and dirty, but can leave partial output. Replaced or modified tape
contents, a new recording, or a successful separate save retire the ID; cursor
and playback changes alone do not. Shutdown drops outstanding decisions.

While a media decision is pending, frame and T-state turns return
NEEDS_DECISION without advancing emulated time. The host stops scheduling turns
and keeps processing commands. Clearing the decision does not release any
existing pause. The null host's `--parked-tape-eject OUTPUT` demonstrates this
with scripted loop iterations and a fresh PZX output path. `--replace-tape
INPUT BAD_INPUT` demonstrates explicit replacement with a valid input and a file
that fails tape decoding. Legacy eject and
tape replacement use `ui_tape_close()` in `ui.c`, which supplies the existing
prompt and picker to the same decision logic. The adapter releases its Save
pause before stopping playback and clearing the tape.

With tape traps enabled, ROM SAVE automatically appends decoded blocks; no manual
recording command is needed. `fuse_host_config_set_tape_traps` supplies the existing
boolean during preparation, retaining its default. Explicit pulse recording
requires traps disabled: `fuse_host_start_tape_recording()` returns MEDIA_ERROR
otherwise, without changing the setting or recorder. It records into the engine
tape, including an empty deck. With traps disabled, already recording is success
(checked before RZX); otherwise missing tape object or RZX refusal is MEDIA_ERROR.
The RZX diagnostic remains. The core start routine returns its outcome directly;
legacy UI callers can still ignore it. `fuse_host_stop_tape_recording()` finalizes elapsed
intervals without implicitly stopping playback; idle Stop succeeds. On append
failure, recording ends and buffers are released, but previously appended blocks
remain; MEDIA_ERROR is not rollback and retry cannot recover the discarded tail.
Neither operation changes pause nesting. Stop remains available if traps become
enabled during an existing recording. Save/eject still finalize implicitly.

## Owned tape browser/transport observations

`fuse_host_copy_tape_info(&info)` returns an allocated `fuse_tape_info` defined in
`fuse-tape.h`. It copies presence, playing, recording, modified, zero-based current
block (-1 when none) and ordered browser rows. Each row owns its description and
detail strings using the existing GTK/widget/Win32 browser formatting: description
is an ASCII type label, details are UTF-8 and may be empty or long. This is display
text, not raw block metadata, a parallel type enum or the Cocoa archive-info panel.

There are no live tape/libspectrum pointers or generation/decision tokens. Copies
remain usable after cursor changes, replacement, eject and shutdown; release the
whole value with `fuse_tape_info_free`, which accepts NULL and accesses no engine
state. Later commands resolve then-current state, not the copied rows.

Copy uses normal owner-thread/lifecycle guards, clears output on failure and does
not pause, select/rewind, advance time or finalize recording. In particular polling
must not stop competition RZX recording. Recording on an empty deck reports
recording=true with no blocks until finalization appends intervals. A formatter
failure returns MEDIA_ERROR with no partial copy; availability is not inferred
from dirty/playing/recording flags. It is not a complete emulator status API.

## Shared floppy save implementation

`ui_media_drive_save` retains the selected-UI filename chooser and overwrite
confirmation. The host save path borrows an already authorized destination
through return and does not prompt. NULL uses the retained target and fails if
unnamed. Direct host Save and a Save reply to a media decision share the same
write and pause handling. Both legacy and host paths share format selection,
filename updates and dirty-state handling. Writes are not atomic.

`fuse_host_insert_disk_file(controller,slot,path,&id)` supplies the explicit
Media insert intent, without autoload or machine selection. It reads and retains
the incoming image before asking about a dirty outgoing disk; an unreadable
input leaves the old disk untouched.
`fuse_host_save_disk_file(controller,slot,path)` calls the chosen-target save seam;
NULL saves to the retained filename. Controllers are emulated identities and slots
are zero-based, not menu IDs. Unavailable/disabled controllers and empty-to-save
drives return MEDIA_ERROR; invalid controller/slot/arguments are rejected before
I/O. `fuse-disk.h` defines the single engine-owned `fuse_disk_controller` type
used by host operations, registration, lookup and File Open dispatch. There is
no host/UI controller enum translation. Packed menu action numbers, including
the separate Microdrive group, are decoded only in `menu.c`.

Disk operation entries in the media implementation own admission through
completion, including drive lookup, FDD preconditions and result translation.
There is no duplicate host/engine disk delegate API. Drive descriptors remain private.

`fuse_host_new_disk(controller,slot,&id)` uses configured geometry and controller
insertion hooks, including +3 preformatting. Insert and New complete immediately
if the drive is empty or clean, or return NEEDS_DECISION for a dirty disk. Insert
and File → Open can also return a side-merge question when the existing disk
options and filename call for confirmation. A dirty disk is resolved first;
a successful Save or Discard can return the merge question with a fresh ID.
`fuse_host_answer_disk_merge()` accepts or declines merging. Declining loads
only the selected side; accepting falls back to that side if its companion
cannot be opened. An image decode failure after eject does not restore the old
disk. Legacy Insert, New, and File → Open keep their synchronous behavior.
`fuse_host_eject_disk(controller,slot,&id)` ejects clean media, succeeds on an
empty available drive, or returns NEEDS_DECISION for dirty media without a
prompt. The request applies only to the current attachment in that drive.
Repeated requests for that drive return the same ID; another drive's eject or a
host tape decision cannot start while it is pending. Execution turns return
NEEDS_DECISION until the request is resolved or invalidated.

`fuse_host_answer_disk_eject(id, choice, path, &next)` consumes a valid reply. Cancel or
failed Save leaves the disk loaded; Discard or successful Save ejects it. Save
accepts NULL for the existing target or a host-authorized path with no additional
overwrite prompt. A separate successful save, replacement, or shutdown invalidates
the request. Legacy floppy eject still uses the existing modal dialog.

`fuse_host_set_disk_flipped` and `fuse_host_set_disk_write_protected` take
0/1 flags and require loaded media. Floppy WP is an emulated write gate, not
filesystem access, and changing it does not itself mark the image modified.

`fuse_host_copy_disk_drive_info(controller,slot,&info)` copies the engine-defined
`fuse_disk_drive_info` on the owner thread. It succeeds for registered inactive
or disabled drives too: controller availability and hardware enablement are
separate fields. Identity, display name, retained target, loaded and flip/WP
flags cover the existing drive UI reads, without menu IDs or FDD pointers.
Loaded is authoritative for media presence; drive latches may remain set while
empty. The value and strings survive replacement, eject and shutdown; release
with `fuse_disk_drive_info_free`, which needs no live engine. Copies are observations,
not command tokens: operations still resolve current controller/slot state.

## Memory and binary files

`fuse_host_copy_memory()` reads the current mapped address space into a
caller-owned buffer without advancing emulation. The named binary load/save
operations use the same mapped reads and writes as the legacy tools (including
normal ROM write protection). All three reject empty or wrapping ranges.
Load uses the requested prefix and rejects short input before writing; Save
can overwrite an authorized target without prompting. The file operations
retain the legacy pause side effects. `--binary-roundtrip INPUT OUTPUT` in the
null host copies four bytes through RAM at `0x8000` and saves them again.

## POK trainers

Host File → Open of a `.pok` file loads the trainer list without opening the
legacy selector or automatically applying new pokes. Copy the list with
`fuse_host_copy_pok_trainers()` and free it with `fuse_pok_trainers_free()`.
Use the copy's generation and zero-based ordinal to enable or disable a trainer
with `fuse_host_set_pok_trainer()`. Pass a byte value when enabling a trainer
that asks for one, otherwise pass `-1`. Reload or reset invalidates old selections,
but existing copies remain readable. Disabling a trainer uses the existing POK
restore rules. Companion POK discovery and the legacy selector are unchanged.
The null host's `--open-pok FILE` mode enables and then disables the first
usable inactive trainer, supplying 42 if the trainer asks for a value.

`fuse_host_reset_poke_finder()` samples writable RAM. The host can narrow the
search by exact byte value or by increases or decreases from the finder’s
baseline. `fuse_host_copy_poke_matches()` returns an owned array of physical
RAM locations, even where the legacy chooser would show only a count. Machine
reset invalidates the search, not copies already returned to the host.

`fuse_host_add_custom_pok_trainer()` appends a trainer without applying it.
It uses legacy POK addressing: physical banks 0–7 or selector 8 for mapped
RAM. Finder matches in physical banks 8 and above cannot be passed directly
as trainer banks. The null host’s `--poke-finder INPUT` mode seeds one byte,
finds its location, and verifies that disabling the new trainer restores it.

## IDE and MMC operations

The owner-thread host calls insert, commit, eject and copy drive information
using a `periph_type` and `libspectrum_ide_unit`. SimpleIDE, ZXATASP and DivIDE
accept master and slave. ZXCF, DivMMC and ZXMMC accept master only. Explicit
operations require an active controller and do not change general File Open's
controller selection. `fuse_host_config_set_divmmc` enables DivMMC at preparation.

Dirty insert and eject return NEEDS_DECISION. Hard-disk File Open returns the
same question, with the controller and unit chosen by the existing dispatcher.
Reply using `fuse_host_answer_mass_storage_eject()`: Save commits to the attached
backing, Discard ejects without committing, and Cancel keeps the attachment.
Replacement retains the path, not the backing bytes; opening the new backing
occurs after an approved eject. A failed insert after eject can leave the drive
empty. Save and Commit retain legacy backend reporting, not a durability
guarantee. Legacy menus continue to ask synchronously.

`fuse_host_copy_mass_storage_info` returns an independent value; release it with
`fuse_mass_storage_info_free`. A NULL backing name means no backing is attached,
even if a failed insert left a filename in settings. Controller activity and
attachment are separate. The copy survives replacement, eject and shutdown.

The null host exercises ZXCF in `--filesystem TAPE_FILE` and DivMMC separately
in `--divmmc CARD_FILE`, using the same RAM filesystem for both workflows.

## Direct null-backed example

[null-host.c](null-host.c) has its own `main` and uses `fuse-internal.h` directly.
It configures a 48K machine, supplies a directory ROM provider, runs frame/budget
turns, delivers input, handles focus loss, resets, selects 128 and shuts down.
The host drives the loop; the scripted key, joystick and focus events stand in
for events collected by the host between turns. Fuse does not pump frontend
events at frame completion on this path. The directory needs `48.rom`, `128-0.rom` and `128-1.rom`.

`--debugger-step` uses a synthetic ROM (no ROM files required). It stops at an
execute breakpoint, copies the registers, steps across a T-state budget boundary,
and continues until the same breakpoint fires again. Breakpoint setup uses the
existing internal debugger API; the host drives execution, inspection and Step.
`--debugger-watchpoint` also uses a synthetic ROM and demonstrates completed
read and write instructions, access metadata, and an execute breakpoint that
still fires after Continue from a watchpoint.

`--queued-input` demonstrates optional keyboard-read refresh using the existing
private `keyboard_refresh.h` hook. The host queues a Space press before the first
turn and a release between turns. A synthetic ROM keeps reading the keyboard
only if its first port read saw the press. The refresh callback drains queued
input in order on the engine thread without waiting or pumping a toolkit. It
unregisters before its queue goes out of scope. No ROM files are needed for this
mode. A host collecting events on another thread must synchronize its own queue;
only the engine thread calls Fuse. The existing refresh timing and exclusions
still apply.

An optional tape filename selects a separate scenario: insert on 48K with the
deck stopped, run five frame turns and shut down. This mode needs only `48.rom`;
it does not perform the input/reset/128K sequence or wait for tape completion.

`--open FILE` demonstrates general File → Open followed by five turns. The
example verifies that execution is parked for a returned question, maps the
first requested joystick to keyboard input, and declines any further mappings.
`--open-over-tape FILE` and `--open-over-floppy FILE` first create modified media
and exercise the same Open dispatcher with scripted Discard replies. With standard 48K ROMs, tape autoload requests the ROM again during
reset; other files can select machines and require additional ROMs. The log
reports the Open result, not that program loading finished.
`--save-disk INPUT OUTPUT` prepares a +3, explicitly inserts into slot 0 and saves
through the host controller/slot API, without running the CPU. The core reads and
encodes the image; the host does not handle disk bytes. This requires the four
`plus3-*.rom` files. The supplied output path is treated as authorized and can
overwrite an existing file.
`--disk-lifecycle OUTPUT` demonstrates New, flip/unflip, WP on/off, Save and eject
on +3 slot 0 without running the CPU. Geometry and encoding stay in the engine;
it is a bounded API example, not a new replacement/overwrite policy. It prints
owned drive copies at New, control changes and eject, then reads a retained saved
copy after shutdown. The driver never accesses drive/FDD internals.
`--tape-lifecycle INPUT OUTPUT` explicitly inserts without autoload, rewinds/selects
block 0, toggles playback, runs two frames, stops, rewinds, saves and ejects.
The codec owns conversion; this does not prove program-loading completion.
It prints owned playing/saved/ejected tape views, including browser rows, and
retains a saved view for reading/freeing after shutdown.
`--record-tape OUTPUT` explicitly disables tape traps in its preparation config,
records two frames on a fresh 48K machine, then stops recording, saves and ejects
through engine entries. No input image or host-side
pulse encoding is needed; the engine handles timestamps, signal and codec.
It also reads an empty-but-recording view and retains the finalized saved view.
Detail strings preserve existing browser formatting (including the current
millisecond label for pulse holds); they are not a new precise timing interface.
`--save-snapshot OUTPUT` runs two frames, saves through the engine, hard-resets,
reopens through general File Open, then runs one frame. Format/bytes stay in the
engine; the example is not evidence of complete checkpoint determinism.
`--filesystem TAPE_FILE` imports an encoded input into an example RAM filesystem,
installs that backend before prepare, then uses the unchanged File Open/tape save/
snapshot save/reset/Open operations against its names. It configures autoload0
for this IO demonstration, exercises successful short transfers, checks balanced
handles through shutdown, then exits. The host adapter handles storage,
not codecs. ROM provenance/provider handling remains its existing separate policy.

In a configured null-UI build:

```sh
make unittests/nullhostlink unittests/nullhostsource
./unittests/nullhostlink /path/to/fuse/roms
./unittests/nullhostsource /path/to/fuse/roms
./unittests/nullhostlink /path/to/fuse/roms /path/to/demo.tap
./unittests/nullhostlink /path/to/fuse/roms --open /path/to/demo.tap
./unittests/nullhostlink /path/to/fuse/roms --save-disk /path/to/input.dsk /path/to/copy.udi
./unittests/nullhostlink /path/to/fuse/roms --disk-lifecycle /path/to/new.udi
./unittests/nullhostlink /path/to/fuse/roms --tape-lifecycle /path/to/input.tap /path/to/copy.tzx
./unittests/nullhostlink /path/to/fuse/roms --record-tape /path/to/recording.pzx
./unittests/nullhostlink /path/to/fuse/roms --save-snapshot /path/to/state.szx
./unittests/nullhostlink /path/to/fuse/roms --filesystem /path/to/input.tap
make check-null-host
```

The first consumer links the engine archive; the second compiles canonical engine
sources. The main thread is the owner thread. Holding execution means not calling
another turn, not manipulating the nested core pause count. Reported frame counts
are completed host calls, not monotonic machine frame IDs across reset/selection.
This is a selected-null-adapter example, not a replacement CLI or toolkit-free core.

## Checks

Null-UI builds include these checks in `make check`:

| Target | Coverage |
| --- | --- |
| `check-legacy-engine-link` | Archive/source linkage and legacy help/version entry. |
| `check-startup-ownership` | Startup storage, preference-save permission and callback restoration. |
| `check-legacy-lifecycle` | One-attempt lifecycle, reentry, pause, frame/budget boundaries and clock replacement; automation builds also cover bounded RZX/finalization cases. |
| `check-host-prepare` | Configuration, all eight joystick interfaces on three slots, invalid inputs, ROM ownership/provenance and preference isolation. |
| `check-host-operations` | Typed machine/input behaviour, tape and +3/Beta disk lifecycle, File Open eligibility/restore/RZX stop, admission and errors. |
| `check-snapshot-file` | Host/legacy chosen-target saves: formats/fallback/overwrite, codec/write/close failures and allocation cleanup, nested pauses/reentry and RZX pause side effects. |
| `check-tape-save-ui` | Legacy Save/eject retain modified tape on failed or cancelled Save; pause ownership and host replacement re-entry are checked. |
| `check-tape-recording` | Recorder append failures before/after partial progress, cleanup/retention, subsequent work and selected-menu re-entry under nested pause. |
| `check-floppy-save-ui` | Host/legacy lifecycle, decisions/failure retention, flags and owned observation lifetime across changes/shutdown, nested/unpaused. |
| `check-null-host` | Direct input/Open/tape/disk lifecycle flows, ROM/media failures, saved image/legacy comparison and preference isolation in both linking forms. |
| `check-startup-unwind` | Instrumented startup failures and reverse cleanup; requires null UI with automation. |

Startup-unwind hooks compile out of the production engine. Tests run accepted
session attempts in fresh processes with isolated preferences. These checks do
not establish native frontend/device failure safety or a supported public SDK.

For uninstalled legacy CLI captures, resource search depends on argv[0] and the
usual directories. Supply explicit ROM paths when needed. For tape-autoload
checks, use standard ROM filenames resolved through those directories: a different
`--rom-*` filename marks the ROM custom and suppresses autoload even if its bytes
match the default. The direct host provider has its own resource policy instead.

## SDL-owned event loop probe

`hacking/sdl-loop-host.c` is a scripted same-thread consumer of an SDL2-backed
engine. It starts SDL video itself, drives `SDL_PollEvent()` and `fuse_run_frame()`
without entering the legacy application loop, and reuses the linked SDL2 renderer.
It loads an existing tape and inspects its blocks, checks keyboard input against
emulated RAM, pauses and resumes, and answers a parked dirty-tape Quit before
shutting down Fuse and then SDL. It uses a built-in
fixture ROM and silent worker-style turns; it is not an interactive UI or a
nonblocking execution test.

Configure with `--with-sdl --with-audio-driver=null` and build normally. From a
separate consumer directory, with `build` and `src` pointing to the configured
build and extracted source (absolute paths):

```sh
make -C "$build" -f Makefile -f "$src/hacking/host-consumer.mk" \
  HOST_INPUT="$src/hacking/sdl-loop-host.c" HOST_OUTPUT="$PWD/sdl-loop-host" \
  host-consumer-archive
SDL_VIDEODRIVER=dummy FUSE_DISPLAY_TIMING=1 ./sdl-loop-host input.tap
```

Use `host-consumer-source` instead to link the configured canonical source
objects and special objects, without the archive. Both recipes compile the
consumer against `fuse-host.h`, `fuse-disk.h`, `fuse-peripheral.h` and
`fuse-tape.h` staged in `host-include/`, plus the configured dependency headers. They do not put private
Fuse or generated headers on the consumer's include path. This is a source
subset for the demonstrated operations, not an installed SDK or a shared ABI.
With display timing enabled, a nonzero `presentation_count` confirms a call to
the SDL renderer; it does not measure what the OS displayed or validate GTK4.

`hacking/worker-loop-host.c` uses the same recipes with a separate build
configured `--with-null-ui --with-audio-driver=null`. A GLib main loop queues
Quit to a single Fuse owner thread and dispatches another callback while the
worker is deliberately held. The UI joins the worker only after it acknowledges
shutdown. The hold is a test gate, not an interruptible engine turn. This probe
does not move a toolkit-bound renderer to a worker.

After building both configurations, run the repeatable check from any directory:

```sh
sh "$src/hacking/check-host-consumers.sh" "$build" "$null_build"
```

The script creates an isolated TAP fixture and checks SDL presentation and both
host lifecycles for both link modes. Set `SDL_VIDEODRIVER=cocoa` on macOS to
exercise the system video driver rather than the default SDL dummy driver.
See [the migration guide](host-migration.md) for the source boundary and thread
ownership rules.
