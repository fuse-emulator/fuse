# Build a host for Fuse

Use this guide to build an application that controls Fuse through its C source interface. Fuse does not supply an installed SDK or a stable binary ABI.

If you only need the standalone emulator, use the normal `configure` and `make` procedure. The standalone application controls its command line, settings file, dialogs, and event loop. See the [embedding reference](embedding-reference.md) for the standalone application adapter.

## Build the engine and host

1. Configure Fuse with output services that the host can use. Use the matching libspectrum build. If you supply a filesystem backend, select it before the first filesystem operation. Fuse and libspectrum use the same backend. See [filesystem requirements](libspectrum-filesystem.md).
2. Build Fuse. Use the configured Makefile with `examples/host-consumer.mk`. The `host-consumer-archive` target links `libfuse.la`. The `host-consumer-source` target links the configured engine objects, including the scaler, timer, and audio objects. Both example targets link the selected adapter objects. Do not mix objects or generated headers from different builds.
3. Include `fuse-host.h`. Include the input, media, or memory header if you need its operations. The example targets stage these headers in `host-include/`. To use the settings or debugger interface, supply the matching build headers separately. The example targets do not stage these headers.

See the [SDL example](embedding-reference.md#sdl-owned-event-loop-probe) for build commands and checks.

## Start the engine and manage settings

1. Start the frontend and output services that Fuse will use. Keep these services and the ROM provider context available until `fuse_shutdown()` returns.
2. Use `fuse_host_prepare()` with a `fuse_host_config`, or use `fuse_host_prepare_settings()` with an initialized `settings_info`. For the second function, include `fuse-host-settings.h` and use the `settings.h` from the same build. Fuse copies the startup settings and copies ROM bytes before it releases the provider's result.
3. Keep settings storage in the host. Fuse does not load or save settings during host preparation. To use the Fuse settings-file format, link the optional `settings-file.o`. To use a different store or no store, omit it. Do not link `settings-legacy.o` unless you need the standalone application adapter.
4. Keep startup media paths, communication endpoints, and debugger commands out of the startup settings record. Attach media and configure endpoints with host operations after preparation.

To change settings during a session, copy the current record, edit it, and submit it between turns. If Fuse returns `FUSE_HOST_OPERATION_NEEDS_RESET`, no changes take effect. Ask for permission if necessary. Submit the record again with reset permission only if the user accepts. A ROM-only edit takes effect at the next ROM load. A change to the selected machine loads the ROMs for that machine. See [runtime settings](embedding-reference.md#runtime-settings).

## Run the engine

- Call `fuse_run_frame()` or `fuse_run_tstates()` between host events. A T-state budget measures emulated time. An instruction or due event can cause the turn to exceed the budget. A turn does not process legacy UI events. It can wait for pacing, audio, or synchronous I/O.
- Translate native input in the host. The Spectrum-key operation sends matrix input, not native shortcuts. Use `fuse_host_set_paused(1)` to pause and `fuse_host_set_paused(0)` to resume. Withholding turns does not have the same effect: Pause can stop audio and finalize competition RZX recording.
- Supply the user-selected tape path to `fuse_host_insert_tape_file()` to insert a tape without autoload. If replacement of a modified tape returns a decision ID, reply with `fuse_host_answer_tape_eject()`.
- If Quit returns a decision, answer it before shutdown. Fuse can return more than one decision in sequence. A parked turn does not answer a decision. A failed save or replacement can leave some changes in effect.
- Call `fuse_shutdown()` before you stop services that Fuse uses.

Make all engine calls from one owner thread. If the UI uses a different thread, send requests to the owner thread and return copied values to the UI. Follow the toolkit's thread rules. The worker example does not interrupt a blocked turn or show that a toolkit renderer can run on a worker thread.
