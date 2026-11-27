#!/usr/bin/env python3
# Copyright (c) 2026 Fredrick Meunier
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

"""End-to-end tests of the actual Lua executable and its compatibility entry."""
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

program = Path(sys.argv[1]).resolve()
legacy = Path(sys.argv[2]).resolve()
roms = Path(sys.argv[3]).resolve()
with tempfile.TemporaryDirectory(prefix="fuse-lua-") as temporary:
    root = Path(temporary)
    for image in roms.glob("*.rom"):
        shutil.copyfile(image, root / image.name)
    fixture = bytearray(16384)
    fixture[:26] = bytes([
        0xf3, 0x31, 0x00, 0xff, 0xaf, 0x32, 0x00, 0x80, 0x32, 0x01, 0x80,
        0x3e, 0xa5, 0x32, 0x00, 0x40, 0x3e, 0x47, 0x32, 0x00, 0x58,
        0xc3, 0x00, 0x01, 0x00, 0x00,
    ])
    fixture[0x100:0x113] = bytes([
        0x3a, 0x00, 0x80, 0x3c, 0x32, 0x00, 0x80, 0xd3, 0xfe,
        0x3e, 0x7f, 0xdb, 0xfe, 0x32, 0x01, 0x80, 0xc3, 0x00, 0x01,
    ])
    rom = root / "fixture.rom"
    rom.write_bytes(fixture)
    start = ('fuse.start { machine="48", auto_load=false, rom_directory=' + json.dumps(str(roms)) +
             ', roms={ ["48.rom"]=' + json.dumps(str(rom)) + '} }\n')
    env = dict(os.environ)
    home = root / "home"
    home.mkdir()
    for key in ("HOME", "XDG_CONFIG_HOME", "USERPROFILE", "WINDIR", "APPDATA"):
        env[key] = str(home)

    def script(name, body, *, expected=0, capture=False, options=(), args=(), stdin=False):
        source = root / (name + ".lua")
        source.write_text(body)
        output = root / name
        output.mkdir()
        command = [str(program), "--script", "-" if stdin else str(source), "--output", str(output), *options]
        if capture:
            command += ["--capture-screen", "--capture-audio"]
        command += ["--", *map(str, args)]
        done = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=15,
                              input=body.encode() if stdin else None)
        assert done.returncode == expected, (name, done.returncode, done.stderr.decode())
        result = json.loads((output / "result.json").read_text())
        assert result["schema"] == 1
        if expected:
            assert result["execution"]["termination"]["type"] != "script-end", name
        else:
            assert result["execution"]["termination"]["type"] == "script-end", (name, result)
        return output, result

    script("defaults", 'fuse.start{rom_directory=' + json.dumps(str(roms)) + '}; fuse.run{frames=1}\n')
    whole, a = script("whole", start + 'fuse.expect(fuse.run{frames=10}.termination == "frames")\n', capture=True)
    split, b = script("split", start + 'fuse.run{frames=5}; fuse.run{frames=5}\n', capture=True)
    for key in ("state", "settings", "identity", "artifacts"):
        assert a[key] == b[key], key
    for artifact in ("screen.png", "audio.wav"):
        assert (whole / artifact).read_bytes() == (split / artifact).read_bytes(), artifact
    with wave.open(str(whole / "audio.wav"), "rb") as audio:
        assert audio.getnframes() > 0 and any(audio.readframes(audio.getnframes()))

    piped, r = script("stdin", start + '''
fuse.expect(arg[0] == "-" and arg[1] == "piped argument")
fuse.run{frames=10}
''', stdin=True, capture=True, args=("piped argument",))
    assert r["script"]["path"] == "-"
    assert r["script"]["arguments"] == ["piped argument"]
    for key in ("state", "settings", "identity", "artifacts"):
        assert r[key] == a[key], ("stdin", key)
    for artifact in ("screen.png", "audio.wav"):
        assert (piped / artifact).read_bytes() == (whole / artifact).read_bytes(), artifact
    _, r = script("stdin-syntax", 'fuse.start {\n', stdin=True, expected=1)
    assert "stdin:" in r["execution"]["termination"]["message"]
    _, r = script("stdin-assertion", start + 'fuse.run{frames=1}; fuse.expect(false, "piped assertion")\n',
                  stdin=True, expected=1, capture=True)
    assert r["execution"]["termination"]["type"] == "assertion-failure"
    assert "stdin:2" in r["execution"]["termination"]["message"]
    assert all(a["status"] == "ok" for a in r["artifacts"].values())

    invalid_options = (
        ("--instruction-limit", "0"),
        ("--max-total-frames", "2147483648"),
        ("--unknown-option", "ignored"),
        ("--instruction-limit",),
    )
    for index, extra in enumerate(invalid_options):
        output = root / ("invalid-cli-" + str(index))
        output.mkdir()
        done = subprocess.run([str(program), "--script", str(root / "whole.lua"),
                               "--output", str(output), *extra], cwd=root, env=env,
                              capture_output=True, timeout=15)
        assert done.returncode == 1, (extra, done.returncode, done.stderr.decode())
        assert not (output / "result.json").exists()
    # A --script token used as another option's value must not reach the
    # loader with a NULL script pointer.
    done = subprocess.run([str(program), "--output", "--script", "--capture-audio",
                           "--output", str(root)], cwd=root, env=env,
                          capture_output=True, timeout=15)
    assert done.returncode == 1 and b"requires --script FILE" in done.stderr

    legacy_outputs = []
    for name, executable in (("old-cli", legacy), ("lua-cli", program)):
        output = root / name
        output.mkdir()
        command = [str(executable), "--machine", "48", "--rom-48", str(rom),
                   "--no-auto-load", "--no-autosave-settings", "--no-confirm-actions",
                   "--automation-output", str(output), "--automation-frames", "10",
                   "--automation-capture-screen", "--automation-capture-audio"]
        done = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=15)
        assert done.returncode == 0, done.stderr.decode()
        result = json.loads((output / "result.json").read_text())
        for key in ("state", "settings", "artifacts"):
            assert a[key] == result[key], (name, key, a[key], result[key])
        for artifact in ("screen.png", "audio.wav"):
            assert (whole / artifact).read_bytes() == (output / artifact).read_bytes(), (name, artifact)
        legacy_outputs.append(output)
    assert (legacy_outputs[0] / "result.json").read_bytes() == (legacy_outputs[1] / "result.json").read_bytes()

    for model in ("128", "2048"):
        machine_start = 'fuse.start{machine="' + model + '", auto_load=false, rom_directory=' + json.dumps(str(roms)) + '}\n'
        lua_output, result = script("machine-" + model, machine_start + 'fuse.run{frames=10}\n', capture=True)
        output = root / ("reference-machine-" + model)
        output.mkdir()
        done = subprocess.run([str(legacy), "--machine", model, "--no-auto-load",
                               "--no-autosave-settings", "--automation-output", str(output),
                               "--automation-frames", "10", "--automation-capture-screen",
                               "--automation-capture-audio"], cwd=root, env=env, capture_output=True, timeout=15)
        assert done.returncode == 0, done.stderr.decode()
        reference = json.loads((output / "result.json").read_text())
        for key in ("state", "settings", "artifacts"):
            assert result[key] == reference[key], (model, key)
        for artifact in ("screen.png", "audio.wav"):
            assert (lua_output / artifact).read_bytes() == (output / artifact).read_bytes(), (model, artifact)

    _, r = script("branching", start + '''
fuse.run {frames=2}
fuse.key("space", true)
fuse.run {frames=1}
fuse.expect((fuse.read8(0x8001) & 1) == 0, "Space press")
fuse.key("space", false)
fuse.run {frames=1}
if (fuse.read8(0x8001) & 1) == 1 then fuse.run {frames=2} else error("Space release") end
fuse.expect(#fuse.read_memory(0x8000, 2) == 2)
fuse.expect(fuse.run{success_pc=0x100, max_frames=2}.termination == "success")
local previous = fuse.read8(0x8000)
fuse.expect(fuse.run{success_pc=0x107, max_frames=2}.termination == "success")
fuse.expect(fuse.read8(0x8000) == ((previous + 1) & 255))
fuse.expect(fuse.registers().pc == 0x107)
fuse.expect(fuse.run{success_pc=0x107, max_frames=2}.frames_completed == 0)
fuse.expect(fuse.run{success_pc=0xffff, max_frames=1}.termination == "deadline")
fuse.reset()
fuse.settings {auto_load=false, tape_traps=false}
fuse.run {tstates=997}
fuse.run {frames=2}
''', capture=True)
    assert r["execution"]["tstate_requests"] == 1

    _, r = script("pc-ignore", start + '''
fuse.expect(fuse.run{success_pc=0xffff, failure_pc=0x100, failure_pc_ignore=1,
                     max_frames=2}.termination == "failure")
fuse.expect(fuse.run{success_pc=0x100, failure_pc=0x100,
                     max_frames=2}.termination == "failure")
fuse.expect(fuse.run{success_pc=0x100, failure_pc=0x100, failure_pc_ignore=1,
                     max_frames=2}.termination == "success")
''')
    assert all(p["frames_completed"] == 0 for p in r["phases"])

    for name, condition, extra, termination in (
        ("success", "success_pc=0x100", ["--automation-success-pc", "256"], "success"),
        ("failure", "success_pc=0xffff, failure_pc=0x100, failure_pc_ignore=1",
         ["--automation-success-pc", "65535", "--automation-failure-pc", "256",
          "--automation-failure-pc-ignore", "1"], "failure"),
        ("deadline", "success_pc=0xffff", ["--automation-success-pc", "65535"], "deadline"),
    ):
        _, result = script("compare-pc-" + name, start +
                           'fuse.expect(fuse.run{' + condition + ', max_frames=1}.termination == "' + termination + '")\n')
        output = root / ("reference-pc-" + name)
        output.mkdir()
        done = subprocess.run([str(legacy), "--machine", "48", "--rom-48", str(rom),
                               "--no-auto-load", "--no-sound", "--no-autosave-settings",
                               "--automation-output", str(output), "--automation-max-frames", "1", *extra],
                              cwd=root, env=env, capture_output=True, timeout=15)
        assert done.returncode == {"success": 0, "failure": 1, "deadline": 2}[termination]
        reference = json.loads((output / "result.json").read_text())
        assert result["phases"][0]["termination"] == reference["execution"]["termination"], name
        assert result["state"] == reference["state"], (name, result["state"], reference["state"])

    recording = root / "roundtrip.rzx"
    snapshot = root / "roundtrip.szx"
    script("media", start + '''
fuse.run {frames=1}
fuse.save_snapshot(arg[1])
local saved = fuse.read8(0x8000)
fuse.run {frames=1}
fuse.open(arg[1])
fuse.expect(fuse.read8(0x8000) == saved)
fuse.record_rzx(arg[2])
fuse.run {frames=3}
fuse.stop_rzx()
fuse.play_rzx(arg[2])
fuse.expect(fuse.run{until_rzx_end=true, max_frames=20}.termination == "rzx-end")
fuse.run {frames=2}
''', capture=True, args=(snapshot, recording))
    rzx_output, rzx_result = script("rzx-comparison", start + 'fuse.play_rzx(arg[1]); fuse.expect(fuse.run{until_rzx_end=true, max_frames=20}.termination == "rzx-end")\n',
                                    capture=True, args=(recording,))
    output = root / "reference-rzx"
    output.mkdir()
    done = subprocess.run([str(legacy), "--machine", "48", "--rom-48", str(rom),
                           "--no-auto-load", "--no-autosave-settings", "--playback", str(recording),
                           "--automation-output", str(output), "--automation-until-rzx-end",
                           "--automation-max-frames", "20", "--automation-capture-screen",
                           "--automation-capture-audio"], cwd=root, env=env, capture_output=True, timeout=15)
    assert done.returncode == 0, done.stderr.decode()
    reference = json.loads((output / "result.json").read_text())
    assert rzx_result["state"] == reference["state"]
    assert rzx_result["artifacts"]["screen"] == reference["artifacts"]["screen"]
    assert (rzx_output / "screen.png").read_bytes() == (output / "screen.png").read_bytes()
    # The public playback operation pauses/resumes sound; legacy startup does
    # not have that boundary. Check its normal PCM format/duration, not a false
    # claim that the two initialization histories have identical synth state.
    for key in ("frames", "sample_rate", "channels", "format", "stage"):
        assert rzx_result["artifacts"]["audio"][key] == reference["artifacts"]["audio"][key]
    compat_output = root / "compat-rzx"
    compat_output.mkdir()
    done = subprocess.run([str(program), "--machine", "48", "--rom-48", str(rom),
                           "--no-auto-load", "--no-autosave-settings", "--playback", str(recording),
                           "--automation-output", str(compat_output), "--automation-until-rzx-end",
                           "--automation-max-frames", "20", "--automation-capture-screen",
                           "--automation-capture-audio"], cwd=root, env=env, capture_output=True, timeout=15)
    assert done.returncode == 0, done.stderr.decode()
    for artifact in ("result.json", "screen.png", "audio.wav"):
        assert (compat_output / artifact).read_bytes() == (output / artifact).read_bytes(), artifact
    script("disk-deadline", start + 'fuse.expect(fuse.run{until_disk_idle=true, max_frames=2}.termination == "deadline")\n')

    failed, r = script("assertion", start + 'fuse.run{frames=3}; fuse.expect(false, "intentional assertion")\n', expected=1, capture=True)
    assert r["execution"]["termination"]["type"] == "assertion-failure"
    assert "intentional assertion" in r["execution"]["termination"]["message"]
    assert "stack traceback" in r["execution"]["termination"]["message"]
    assert all(a["status"] == "ok" for a in r["artifacts"].values())
    assert (failed / "screen.png").exists() and (failed / "audio.wav").exists()
    script("syntax", 'fuse.start {\n', expected=1)
    script("runtime", start + 'error("intentional runtime error")\n', expected=1)
    script("no-start", 'return\n', expected=1)
    _, r = script("loop", 'while true do end\n', expected=1, options=("--instruction-limit", "10000"))
    assert r["execution"]["termination"]["type"] == "resource-limit"
    script("tiny-quota", 'local a = 1; a = a + 1\n', expected=1, options=("--instruction-limit", "1"))
    script("heap", 'local x = string.rep("x", 100 * 1024 * 1024)\n', expected=1)
    invalid_runs = (
        "success_pc=0x100",
        "frames=1, tstates=1",
        "frames=1, max_frames=1",
        "frames=1, failure_pc=0x100",
        "frames=1, failure_pc_ignore=1",
        "until_rzx_end=true, until_disk_idle=true, max_frames=1",
    )
    for index, fields in enumerate(invalid_runs):
        _, r = script("invalid-run-" + str(index), start + 'fuse.run{' + fields + '}\n', expected=1)
        assert r["phases"] == [] and r["execution"]["frames_completed"] == 0
    script("unknown-setting", 'fuse.start{wrong_option=true}\n', expected=1)
    script("missing-rom", 'fuse.start{rom_directory="missing-roms"}\n', expected=1)
    script("invalid-memory", start + 'fuse.read8(65536)\n', expected=1)
    _, r = script("failed-mutation", start.replace(json.dumps(str(roms)), '"missing-directory"') +
                  'fuse.run{frames=3}; fuse.machine("128")\n', expected=1, capture=True)
    assert r["execution"]["termination"]["type"] == "engine-error"
    assert r["artifacts"]["audio"]["status"] == "ok"
    assert r["artifacts"]["screen"]["status"] == "error"
    _, r = script("frame-limit", start + 'fuse.run{frames=2}; fuse.run{frames=1}\n',
                  expected=1, capture=True, options=("--max-total-frames", "2"))
    assert r["execution"]["frames_completed"] == 2
    script("permissions", start + '''
fuse.expect(io == nil and os == nil and package == nil and debug == nil)
fuse.expect(load == nil and loadfile == nil and dofile == nil)
fuse.expect(pcall == nil and xpcall == nil and coroutine == nil)
fuse.run {frames=1}
''')
    assert not any(p.name in {"fuserc", ".fuserc", "fuse.cfg"} for p in home.rglob("*"))
print("Lua host: scripts, PC/input/media phases, limits/errors, CLI compatibility and fixed-frame PNG/PCM parity passed")
