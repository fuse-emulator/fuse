# Shared regular-file filesystem

Libspectrum owns the regular-file API, backend selection, and native I/O.
Fuse and libspectrum use the same namespace for their filename-based media
operations. Fuse's `fuse-filesystem.h` aliases the library handle and mode
constants; `fuse-filesystem.c` adapts the library's results to Fuse conventions.
Null-build fault hooks intercept selected Fuse calls for tests; they do not
install or replace a filesystem backend.

## Backend selection and lifetime

A host can call `libspectrum_fs_install` with a valid `libspectrum_fs_backend`
before the first filesystem operation, including a failed operation. Otherwise
that first operation selects the native backend. Selection is permanent and
independent of `libspectrum_init` and `libspectrum_end`. The library copies the
callback table and borrows its user data, which must remain valid for all
filesystem use, including shutdown. Setup and synchronous I/O are serialized
on the owner thread. Names and buffers passed to callbacks are borrowed for
that call; retained names are logical names in the selected namespace.

The installed `libspectrum.h` and libspectrum's regular-file documentation
specify the full callback contract. The checked operations are available through
`libspectrum_fs`:

- Open supports read existing, create/truncate for writing, read-write existing,
  and append/create. A successful open returns an opaque `libspectrum_fs_handle`;
  close consumes it once, even on error.
- Raw reads and writes report the transferred byte count separately from the
  error. Exact operations loop over successful short transfers and fail on
  errors or zero progress. Partial progress does not turn an error into success.
- Length, seek offsets, and positions use signed 64-bit byte values. Length
  preserves the stream position on success. Flush reports errors but does not
  guarantee durable storage.
- `exists`, `unlink`, and `writable` are optional backend callbacks. Missing
  callbacks and custom backend errors do not fall back to native I/O. `exists`
  tests presence, not readability. `unlink` removes a file or symlink, never
  a directory or the symlink's target. `writable` queries permission without
  opening or truncating the file.

The native backend validates read and read-write opens as regular files.
Symlinks to regular files are accepted. Native file-only deletion and platform
positioning are handled by libspectrum, not by Fuse-side host-path checks.

## Consumers and boundaries

Libspectrum uses this namespace for generic path loading, WAV tape decoding,
filename-based PCM WAV output, and IDE/MMC backing. IDE and MMC retain an open
handle while the medium is attached; sector I/O and cache commits use that
handle. Eject and teardown release it. Failed cached writes are not reliably
reported to Fuse; a successful controller commit is not a durability guarantee.
WAV output explicitly directed to stdout is outside the file namespace.

Fuse uses the same backend for common tape, snapshot and RZX I/O, floppy images
and text logs, preferences, PNG and SVG output, PSG and FMF recording, printer
output, and profile maps. Input discovery tests read-open when selecting a
candidate; an unreadable candidate does not hide a readable fallback. Overwrite
checks use presence, so an unreadable destination still counts as existing.

Fuse retains media admission, autoload, replacement and save policy,
related-name lookup, search-path order, ROM provenance and UI decisions.
Directory browsing, environment and path discovery, and physical devices remain
separate host services. The development runner's `result.json` is a native host
artifact.

## Examples and checks

[`memory-filesystem.h`](../unittests/integration/memory-filesystem.h) implements a RAM namespace with
non-stdio handles. The [direct null host](legacy-engine.md#direct-null-backed-example)
uses it for `--filesystem` (tape, snapshot and ZXCF) and `--divmmc` (MMC).
`check-null-host` runs both the archive-linked and source-linked consumers.
`check-host-operations` exercises short transfers, failure cleanup, readable
fallback, and media backing lifetimes. The native snapshot reference and RAM
workflow run in separate processes because backend selection is permanent.

<!--
Copyright (c) 2026 Fredrick Meunier
This document is free software; you can redistribute it and/or modify it under
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later version.
It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this
program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street,
Fifth Floor, Boston, MA 02110-1301 USA.
-->
