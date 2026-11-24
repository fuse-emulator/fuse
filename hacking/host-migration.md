# Migrating to host-owned execution

This guide explains how to build a host that drives Fuse through its C source interface. It does not describe a stable shared ABI or an installed SDK.

## Keep the legacy application if you only need the emulator executable

Keep the normal `configure` and `make` workflow and the existing frontend. Its command line, preferences, modal interaction, and event loop remain application-owned. Code embedding a legacy CLI-shaped entry can consult [the legacy-engine guide](legacy-engine.md) for the application adapter; it is separate from the host-owned source interface.

## Build a consumer from the same sources as the engine

1. Extract the release source and configure an engine with output hooks appropriate to the host. Use the matching libspectrum dependency; see [filesystem requirements and backend lifetime](libspectrum-filesystem.md). If you provide a filesystem backend, select it once before the first filesystem operation; it applies to both Fuse and libspectrum. It does not make file calls asynchronous.
2. Build the engine normally so its configured and generated sources and special objects agree. Use the supplied `examples/host-consumer.mk` recipes with the configured Makefile. `host-consumer-archive` uses the configured archive; `host-consumer-source` links the canonical configured objects directly, including the special scaler, timer, and audio recipes. Do not maintain a parallel copy of the engine source list or mix build directories.
3. Include `fuse-host.h`. The recipes stage the source-facing Fuse headers in `host-include/` and supply the matching dependency flags, without adding private Fuse headers or `config.h` to the consumer's include path. The source subset covers the demonstrated workflows, not every legacy menu operation. For additional workflows, opt into `fuse-host-media.h`, `fuse-host-input.h`, or `fuse-host-memory.h`. The configured debugger interface in `fuse-host-debugger.h` is not staged in `host-include/`. Private engine symbols and headers are not part of the staged subset.

See the [SDL probe build instructions](legacy-engine.md#sdl-owned-event-loop-probe) for commands and the repeatable archive/source check.

## Move event collection and command scheduling to the host

- **Start:** Bring up the selected UI and output services first. Create a `fuse_host_config` and pass a ROM byte provider to `fuse_host_prepare()`. The host retains its provider context and keeps output services alive through shutdown. Fuse copies ROM bytes before releasing the provider's result. The host preparation route does not read or save application preferences.
- **Drive:** Call `fuse_run_frame()` or `fuse_run_tstates()` between host events. A T-state budget measures emulated time, not a wall-clock deadline, and can overshoot for instructions and due events. Host turns do not pump the legacy event loop. The linked renderer and audio hooks still apply; turns may wait for pacing, audio delivery, or synchronous device or file I/O. Nonblocking execution is not part of this interface.
- **Input and pause:** Translate native keys or controls in the host. The demonstrated Spectrum-key entry maps directly to the emulated matrix, not native shortcuts. Use `fuse_host_set_paused(1)` to Pause and `fuse_host_set_paused(0)` to Resume between turns. Repeating either request is safe. Unlike withholding turns, Pause can stop audio and finalize competition RZX recording. Inspect via copies such as mapped memory or tape observations, not by holding pointers into engine state.
- **Media and Quit:** Supply the chosen tape path to `fuse_host_insert_tape_file()` for explicit insertion without autoload. A dirty replacement returns an ID; reply through `fuse_host_answer_tape_eject()`. Quit can return multiple decisions in sequence; drive them on the host loop until ready to shut down or cancelled. A parked turn does not consume the question, and a copy of attached media does not authorize a reply. Call `fuse_shutdown()` after ready, before tearing down any service Fuse still uses. Do not assume a failed save or replacement rolls back all effects.

Same-thread hosts can dispatch UI events between turns. For a separate-thread host, all engine calls (including shutdown) belong to one owner worker; the UI sends messages and consumes copied values. Keep toolkit calls on the thread permitted by that toolkit. The null-backed worker probe deliberately holds the worker while the UI continues, then acknowledges shutdown before the UI joins it. This pattern does not interrupt a blocked turn or establish that a toolkit renderer can run on the worker.
