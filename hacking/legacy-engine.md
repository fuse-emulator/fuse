# Internal legacy-engine build

## Boundary

The executable links the non-installed Libtool convenience library
`libfuselegacy.la`. This is the configured application engine, including the
selected UI, debugger, pacing and audio backend. It is not a toolkit-free core,
an installed embedding SDK or a stable binary ABI.

`fuse-main.c` owns normal/SDL entry and platform bootstrap. Win32's executable
also includes `ui/win32/win32main.c` and its resources. The private
`fuse_legacy_main()` runner remains inside the engine. Its guarded shutdown now
unwinds completed manager registrations on startup failure, but the remaining
frontend/backend partial-initialization audits below still prevent advertising a
supported embedding lifecycle/execution API.

## Current ownership boundary

The private application runner accepts only one session attempt per process. It
rejects another attempt before changing settings or library callbacks; this is
not thread-safe instance creation or restart support.

Settings acquire their strings before configuration/argument parsing. Parse
failure releases those strings, and `settings_end(save)` is repeatable. Preference
saving requires explicit authorization as well as the user's autosave setting.
Help/version and initialization failure do not save. Settings retain their
manager callback position, with an idempotent fallback if that registration never
completed. Process-global XML cleanup now follows SVG and automation disposal,
rather than running inside settings teardown while other XML users may be alive.

The application records successful libspectrum acquisition and restores the
previous libspectrum error callback when its runner returns. This is still an
exclusive application-runtime profile, not a host-borrowing library contract.

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
The legacy ROM search uses the executable's location; these fixtures live under
`unittests/`, not beside the source root's `roms/`. Supply explicit ROM paths for
uninstalled capture runs, for example `--rom-48 /path/to/roms/48.rom`, and the
corresponding options for other machines. Packaging does not introduce a new
resource-search policy.

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
