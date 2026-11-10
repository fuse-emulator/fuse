# Internal legacy-engine build

## Boundary

The executable links the non-installed Libtool convenience library
`libfuselegacy.la`. This is the configured application engine, including the
selected UI, debugger, pacing and audio backend. It is not a toolkit-free core,
an installed embedding SDK or a stable binary ABI.

`fuse-main.c` owns normal/SDL entry and platform bootstrap. Win32's executable
also includes `ui/win32/win32main.c` and its resources. The private
`fuse_legacy_main()` runner remains inside the engine. It does not yet provide
safe application-level partial-initialization unwind or a supported embedding
lifecycle/execution API. Startup-manager teardown improvements alone do not
establish those guarantees.

## Current ownership boundary

The private application runner accepts only one session attempt per process. It
rejects another attempt before changing settings or library callbacks; this is
not thread-safe instance creation or restart support.

Settings acquire their strings before configuration/argument parsing. Parse
failure releases those strings, and `settings_end(save)` is repeatable. Preference
saving requires explicit authorization as well as the user's autosave setting.
Help/version and pre-manager initialization failure do not save. The settings
module no longer owns process-global XML cleanup: the application callback keeps
that cleanup at the historical settings-teardown position on normal shutdown.

The application records successful libspectrum acquisition and restores the
previous libspectrum error callback when its runner returns. This is still an
exclusive application-runtime profile, not a host-borrowing library contract.

Temporary startup inputs are owned by `infrastructure/startup_files.[ch]`.
File metadata/buffers and IDE paths have owned storage independent of settings;
positional IDE replacements are copied, not borrowed from argv. The recording
path remains borrowed from settings until startup operations finish. Temporary
storage and the scaler-selection string are released on each local return path.

These boundaries do **not** yet establish manager/UI partial-failure unwind.
After manager startup begins, the runner still lacks complete application-level
rollback. No public lifecycle capability is advertised.

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
callback restoration in isolated child-process preference directories. These are internal build fixtures, not examples
of a supported public API. The consumers also accept the existing runner's CLI,
so automation-enabled builds can use them for bounded differential captures.
The legacy ROM search uses the executable's location; these fixtures live under
`unittests/`, not beside the source root's `roms/`. Supply explicit ROM paths for
uninstalled capture runs, for example `--rom-48 /path/to/roms/48.rom`, and the
corresponding options for other machines. Packaging does not introduce a new
resource-search policy.

The audio interval-equivalence executables reuse engine Libtool objects, replacing
only `sound.lo` with their compile-time interval variants. Scaler unit tests use
the same two special scaler objects as the engine. Nothing installs the engine
library or its private headers; external source builds remain configuration-bound.
