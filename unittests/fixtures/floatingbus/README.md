# Optional +2A/+3 floating-bus TAP fixtures

Place the original programs in this directory using these exact filenames:

| Local filename | Original program | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `hikaru.tap` | Hikaru/Intense +2A/+3 floating-bus test v1.2 (2017) | 1148 | `48dd8eb1eb7b92e2fc7ee99b0065d7dd7da7fe87d188447795df0072e111a6b9` |
| `universal.tap` | `Plus 2A test 2.tap` from the Universal attachment | 127 | `d703d838adba23eafdd4e0457486ce1318e488667ebb12a96f72ec5e06114b10` |
| `woody.tap` | `Floating Bus Test (Universal) 2.tap` from Woody's Special | 387 | `b80c4ab7690434451fcb0c15d0af7975c7808e22e562b14186008666f4628bf3` |

A null-UI internal-test build's `make check` or `make check-floatingbus-host`
discovers these files automatically. No per-TAP environment variables are
required or consulted. `./fuse --unittests` runs the internal timing oracle,
not these fixture workflows.
Missing files are reported as optional skips independently; an unreadable or
hash-mismatched file fails its test. Present programs run on +2A, +3 and +3e.

The test suite uses its existing `FUSE_TEST_SRCDIR` source-root convention;
Both make targets set it automatically, including out-of-tree builds. Generated
screenshots, snapshots and CSVs default to `unittests/floatingbus-output/` under
the build directory, which is created automatically. The optional
`FUSE_TEST_FLOATING_VISUAL_OUTPUT` variable overrides only that artifact directory.
Hikaru captures are named `hikaru-<machine>-<stage>.scr` there.

The fixture runner uses host operations for settings, held keys, machine
selection, media and capture. Opcode stepping, memory setup and timing
observations remain internal. Settings changes are restored after each suite.

The host check also generates six benchmark snapshots (48K, 128K and +3,
using fast and slow RAM) in temporary storage, even without the optional TAPs.
To retain those snapshots, create an output directory and run directly:

```sh
FUSE_TEST_FLOATING_PERF_OUTPUT=/absolute/output/path \
  ./unittests/legacyenginelinktest --host-operations-test floatingbus-fixtures /absolute/path/to/roms
```

Direct runs use `FUSE_TEST_SRCDIR` to find TAPs (default `.`), and write visual
artifacts relative to the working directory unless overridden.

## Provenance and distribution

- Hikaru: https://github.com/redcode/ZXSpectrum/wiki/2A-3-Floating-Bus-Test
- Original attachment discussion: https://sourceforge.net/p/fuse-emulator/bugs/360/

These are third-party programs, not Fuse-generated test data. Their hashes pin
the researched versions; they do not grant redistribution rights. TAP files
are ignored locally and are not included in the source distribution. Resolve
permission before adding any of them to version control or release archives.
