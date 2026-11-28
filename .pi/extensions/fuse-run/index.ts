import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { truncateHead, truncateTail } from "@earendil-works/pi-coding-agent";
import { Type, type Static } from "typebox";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { isAbsolute, join, resolve } from "node:path";

const parameters = Type.Object({
	script: Type.Optional(Type.String({ minLength: 1, description: "Lua source to execute with fuse-lua; mutually exclusive with script_file and legacy run options. Full API: hacking/lua-host.md" })),
	script_file: Type.Optional(Type.String({ minLength: 1, description: "Existing Lua scenario path, relative to the project or absolute (not stdin)" })),
	script_args: Type.Optional(Type.Array(Type.String(), { description: "Lua arg[1..] values, passed literally after --" })),
	rom_directory: Type.Optional(Type.String({ description: "ROM directory for Lua scripts; default: roms" })),
	instruction_limit: Type.Optional(Type.Integer({ minimum: 1, description: "Lua instruction quota (host default: 10000000)" })),
	max_total_frames: Type.Optional(Type.Integer({ minimum: 1, description: "Lua total frame budget (host default: 1000000)" })),
	timeout_seconds: Type.Optional(Type.Integer({ minimum: 1, maximum: 2147483, description: "Wall-clock process timeout; default: 120 seconds. Killing cannot finalize evidence." })),

	executable: Type.Optional(
		Type.String({ description: "Executable path (defaults to ./fuse-lua for scripts, ./fuse otherwise)" }),
	),
	machine: Type.Optional(Type.String({ description: "Fuse machine identifier, for example 48 or 128" })),
	input_file: Type.Optional(
		Type.String({ description: "Tape, snapshot, or other Fuse input; an RZX file in RZX-completion mode" }),
	),
	frame_count: Type.Optional(
		Type.Integer({ minimum: 1, description: "Number of frames for an ordinary fixed-frame run" }),
	),
	max_frames: Type.Optional(
		Type.Integer({ minimum: 1, description: "Frame deadline for a PC-condition or RZX-completion run" }),
	),
	success_pc: Type.Optional(
		Type.Integer({ minimum: 0, maximum: 65535, description: "Program counter that ends the run successfully" }),
	),
	failure_pc: Type.Optional(
		Type.Integer({ minimum: 0, maximum: 65535, description: "Program counter that ends the run unsuccessfully" }),
	),
	failure_ignore_count: Type.Optional(
		Type.Integer({ minimum: 0, description: "Number of initial failure-PC hits to ignore" }),
	),
	run_until_rzx_complete: Type.Optional(
		Type.Boolean({ description: "Run until RZX playback completes, subject to max_frames" }),
	),
	run_until_disk_idle: Type.Optional(
		Type.Boolean({ description: "Run until observed disk activity remains inactive, subject to max_frames" }),
	),
	disk_idle_frames: Type.Optional(
		Type.Integer({ minimum: 1, description: "Required inactive settling frames (default: 50)" }),
	),
	capture_screen: Type.Optional(Type.Boolean({ description: "Capture the final display as screen.png" })),
	capture_audio: Type.Optional(Type.Boolean({ description: "Capture frame-aligned PCM audio as audio.wav" })),
});

type Params = Static<typeof parameters>;

function validate(params: Params): void {
	const luaRun = params.script !== undefined || params.script_file !== undefined;
	if (params.script !== undefined && params.script_file !== undefined) {
		throw new Error("script and script_file are mutually exclusive");
	}
	if (params.script_file === "-") throw new Error("Use script for inline source, not script_file='-'");
	if (luaRun) {
		for (const key of ["machine", "input_file", "frame_count", "max_frames", "success_pc", "failure_pc",
			"failure_ignore_count", "run_until_rzx_complete", "run_until_disk_idle", "disk_idle_frames"] as const) {
			if (params[key] !== undefined) throw new Error(`${key} is a legacy run option; configure it inside the Lua script`);
		}
		return;
	}
	for (const key of ["script_args", "rom_directory", "instruction_limit", "max_total_frames"] as const) {
		if (params[key] !== undefined) throw new Error(`${key} requires script or script_file`);
	}
	const pcRun = params.success_pc !== undefined;
	const rzxRun = params.run_until_rzx_complete === true;
	const diskRun = params.run_until_disk_idle === true;
	const conditionalRuns = Number(pcRun) + Number(rzxRun) + Number(diskRun);

	if (conditionalRuns > 1) {
		throw new Error("success_pc, run_until_rzx_complete, and run_until_disk_idle select incompatible run modes");
	}
	if (params.failure_pc !== undefined && !pcRun) throw new Error("failure_pc requires success_pc");
	if (params.failure_ignore_count !== undefined && params.failure_pc === undefined) {
		throw new Error("failure_ignore_count requires failure_pc");
	}
	if (params.disk_idle_frames !== undefined && !diskRun) {
		throw new Error("disk_idle_frames requires run_until_disk_idle");
	}

	if (pcRun || rzxRun || diskRun) {
		if (params.max_frames === undefined) {
			throw new Error("PC-condition, RZX-completion, and disk-idle runs require max_frames");
		}
		if (params.frame_count !== undefined) throw new Error("frame_count is only valid for an ordinary fixed-frame run");
	} else {
		if (params.frame_count === undefined) throw new Error("an ordinary run requires frame_count");
		if (params.max_frames !== undefined) {
			throw new Error("max_frames is only valid for a PC-condition, RZX-completion, or disk-idle run");
		}
	}

	if (rzxRun && params.input_file === undefined) {
		throw new Error("run_until_rzx_complete requires input_file containing an RZX recording");
	}
}

function diagnosticOutput(stdout: string, stderr: string): string {
	const output = [stderr && `stderr:\n${stderr}`, stdout && `stdout:\n${stdout}`].filter(Boolean).join("\n");
	if (!output) return "";
	const truncated = truncateTail(output);
	return `\n${truncated.content}${truncated.truncated ? "\n[Subprocess output truncated]" : ""}`;
}

function automationBuildHint(stdout: string, stderr: string): string {
	const output = `${stderr}\n${stdout}`;
	const unsupportedOption =
		/(unknown|unrecognized|invalid|illegal)[^\n]*automation/i.test(output) ||
		/automation[^\n]*(unknown|unrecognized|invalid|illegal)/i.test(output);
	if (!unsupportedOption) return "";
	return (
		" This Fuse executable does not appear to have development automation enabled." +
		" Reconfigure with --with-null-ui --with-audio-driver=null --enable-automation, then rebuild."
	);
}

export default function (pi: ExtensionAPI) {
	pi.registerTool({
		name: "fuse_run",
		label: "Fuse Run",
		description:
			"Run a bounded Fuse scenario and return authoritative result.json and evidence. Use script (Lua source) or script_file for multi-phase runs, keyboard input, memory/register inspection, media, snapshots and RZX recording through fuse-lua; see hacking/lua-host.md for the full API. Script options cannot mix with legacy run options. Otherwise use frame_count, or max_frames with success_pc/run_until_rzx_complete/run_until_disk_idle. Scripts are trusted local code, not sandboxed. Default wall timeout: 120s. JSON/logs are truncated at 2000 lines or 50KB each; full JSON remains on disk.",
		promptSnippet: "Run a bounded Fuse automation scenario and return result.json plus requested evidence",
		parameters,

		async execute(_toolCallId, params, signal, _onUpdate, ctx) {
			validate(params);

			const luaRun = params.script !== undefined || params.script_file !== undefined;
			const outputDirectory = await mkdtemp(join(tmpdir(), "fuse-run-"));
			const executable = params.executable
				? isAbsolute(params.executable)
					? params.executable
					: resolve(ctx.cwd, params.executable)
				: join(ctx.cwd, luaRun ? "fuse-lua" : "fuse");
			const args: string[] = [];
			if (luaRun) {
				let scriptPath = params.script_file === undefined ? join(outputDirectory, "scenario.lua") : resolve(ctx.cwd, params.script_file);
				if (params.script !== undefined) await writeFile(scriptPath, params.script, "utf8");
				args.push("--script", scriptPath, "--output", outputDirectory);
				if (params.rom_directory !== undefined) args.push("--rom-directory", params.rom_directory);
				if (params.instruction_limit !== undefined) args.push("--instruction-limit", String(params.instruction_limit));
				if (params.max_total_frames !== undefined) args.push("--max-total-frames", String(params.max_total_frames));
				if (params.capture_screen) args.push("--capture-screen");
				if (params.capture_audio) args.push("--capture-audio");
				args.push("--", ...(params.script_args ?? []));
			} else {
			args.push("--automation-output", outputDirectory);

			if (params.machine !== undefined) args.push("--machine", params.machine);
			if (params.frame_count !== undefined) args.push("--automation-frames", String(params.frame_count));
			if (params.max_frames !== undefined) args.push("--automation-max-frames", String(params.max_frames));
			if (params.success_pc !== undefined) args.push("--automation-success-pc", String(params.success_pc));
			if (params.failure_pc !== undefined) args.push("--automation-failure-pc", String(params.failure_pc));
			if (params.failure_ignore_count !== undefined) {
				args.push("--automation-failure-pc-ignore", String(params.failure_ignore_count));
			}
			if (params.run_until_rzx_complete) args.push("--automation-until-rzx-end");
			if (params.run_until_disk_idle) args.push("--automation-until-disk-idle");
			if (params.disk_idle_frames !== undefined) {
				args.push("--automation-disk-idle-frames", String(params.disk_idle_frames));
			}
			if (params.capture_screen) args.push("--automation-capture-screen");
			if (params.capture_audio) args.push("--automation-capture-audio");
			else args.push("--no-sound");
			args.push("--no-confirm-actions");

			if (params.input_file !== undefined) {
				if (params.run_until_rzx_complete) args.push("--playback", params.input_file);
				else args.push("--", params.input_file);
			}

			}

			const process = await pi.exec(executable, args, {
				cwd: ctx.cwd, signal, timeout: (params.timeout_seconds ?? 120) * 1000,
			});
			const resultPath = join(outputDirectory, "result.json");
			let result: unknown;
			try {
				result = JSON.parse(await readFile(resultPath, "utf8"));
			} catch (error) {
				const reason = error instanceof Error ? error.message : String(error);
				throw new Error(
					`Fuse exited with status ${process.code}, but result.json could not be read: ${reason}.${luaRun ? " Build the Lua host with ./configure --with-lua && make. See hacking/lua-host.md." : automationBuildHint(process.stdout, process.stderr)}${process.killed ? " Process was killed (timeout, cancellation or output limit); results may not have been finalized." : ""} Output directory: ${outputDirectory}${diagnosticOutput(process.stdout, process.stderr)}`,
				);
			}

			const artifactPaths: { screen?: string; audio?: string } = {};
			const artifacts = (result as { artifacts?: Record<string, { status?: string; path?: string }> }).artifacts;
			if (artifacts?.screen?.status === "ok" && artifacts.screen.path) {
				artifactPaths.screen = join(outputDirectory, artifacts.screen.path);
			}
			if (artifacts?.audio?.status === "ok" && artifacts.audio.path) {
				artifactPaths.audio = join(outputDirectory, artifacts.audio.path);
			}

			const serializedResult = JSON.stringify(result, null, 2);
			const truncatedResult = truncateHead(serializedResult);
			let text = `Fuse result.json:\n${truncatedResult.content}`;
			if (truncatedResult.truncated) text += `\n[Result truncated; full JSON: ${resultPath}]`;
			text += `\n\nProcess exit status: ${process.code}\nOutput directory: ${outputDirectory}`;
			if (artifactPaths.screen) text += `\nScreen: ${artifactPaths.screen}`;
			if (artifactPaths.audio) text += `\nAudio: ${artifactPaths.audio}`;
			if (process.code !== 0 || process.stderr.trim()) text += diagnosticOutput(process.stdout, process.stderr);

			const content: Array<
				| { type: "text"; text: string }
				| { type: "image"; data: string; mimeType: "image/png" }
			> = [{ type: "text", text }];
			if (artifactPaths.screen) {
				content.push({
					type: "image",
					data: await readFile(artifactPaths.screen, "base64"),
					mimeType: "image/png",
				});
			}

			return {
				content,
				details: { result, exitStatus: process.code, outputDirectory, artifacts: artifactPaths },
			};
		},
	});
}
