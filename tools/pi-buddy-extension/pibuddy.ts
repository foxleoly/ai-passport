/**
 * Pi Buddy — the pi-side half of the Pi Agent Buddy link.
 *
 * The device reaches this computer over Wi-Fi through the sidecar. This
 * extension is what makes the link two-way, so the sidecar needs nothing else
 * installed:
 *
 *   ~/.pi-buddy/session-<pid>.json  <- we publish this session's live state
 *   ~/.pi-buddy/act.json         -> the sidecar asks for an action
 *   ~/.pi-buddy/act-result.json  <- we report what happened
 *   ~/.pi-buddy/status.json      -> the sidecar publishes the device link state
 *   ~/.pi-buddy/token            -> the sidecar's link code
 *
 * It also shows the link state in pi's footer and copies the pair code.
 *
 * Install: symlink or copy this file into ~/.pi/agent/extensions/ and run
 * /pibuddy. The bridge runs in every mode; only the footer and the dialogs
 * need a UI.
 */
import { spawn } from "node:child_process";
import { chmod, mkdir, readFile, rename, rm, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import type { ExtensionAPI, ExtensionContext, Theme } from "@earendil-works/pi-coding-agent";

const STATE_DIR = join(homedir(), ".pi-buddy");
const STATUS_FILE = join(STATE_DIR, "status.json");
const TOKEN_FILE = join(STATE_DIR, "token");
const SESSION_FILE = join(STATE_DIR, `session-${process.pid}.json`);
const ACT_FILE = join(STATE_DIR, "act.json");
const ACT_RESULT_FILE = join(STATE_DIR, "act-result.json");
const STATUS_KEY = "pibuddy";

/** One tick publishes our state, looks for an act, and refreshes the footer. */
const TICK_MS = 1000;

type LinkStatus = {
	connected?: boolean;
	device?: string;
	pid?: number;
	updated?: string;
};

type ActRequest = {
	id?: string;
	kind?: string;
	text?: string;
};

// The status file outlives the sidecar, so its pid decides whether it still
// means anything: a killed sidecar leaves the last state behind.
function sidecarAlive(pid: number | undefined): boolean {
	if (typeof pid !== "number" || pid <= 0) {
		return false;
	}
	try {
		process.kill(pid, 0);
		return true;
	} catch {
		return false;
	}
}

async function readJSON<T>(path: string): Promise<T | null> {
	try {
		return JSON.parse(await readFile(path, "utf8")) as T;
	} catch {
		return null; // missing, unreadable, or caught mid-replace
	}
}

// Publishing replaces the file in one step, so a reader never sees half a
// document.
async function writeAtomic(path: string, data: string): Promise<void> {
	const tmp = `${path}.${process.pid}.tmp`;
	await writeFile(tmp, data, { mode: 0o600 });
	await rename(tmp, path);
}

// The directory is the control channel now, so only the owner may write it:
// anything that can write act.json can stop the agent.
async function ensureStateDir(): Promise<void> {
	await mkdir(STATE_DIR, { recursive: true, mode: 0o700 });
	await chmod(STATE_DIR, 0o700);
}

async function publishSession(ctx: ExtensionContext): Promise<void> {
	const state = {
		pid: process.pid,
		session_file: ctx.sessionManager.getSessionFile() ?? "",
		cwd: ctx.cwd,
		title: ctx.sessionManager.getSessionName() ?? "",
		idle: ctx.isIdle(),
		updated: new Date().toISOString(),
	};
	await writeAtomic(SESSION_FILE, `${JSON.stringify(state)}\n`);
}

/**
 * Runs one device act. `interrupt` aborts the current operation; approve/deny
 * deliver the configured text as a user message, which is the same thing the
 * old terminal keystroke did.
 */
function runAct(pi: ExtensionAPI, ctx: ExtensionContext, req: ActRequest): { ok: boolean; detail: string } {
	switch (req.kind) {
		case "interrupt":
			ctx.abort();
			return { ok: true, detail: "aborted" };
		case "approve":
		case "deny": {
			const fallback = req.kind === "approve" ? "yes" : "no";
			const text = req.text && req.text.length > 0 ? req.text : fallback;
			pi.sendUserMessage(text, { deliverAs: "steer" });
			return { ok: true, detail: `sent ${JSON.stringify(text)}` };
		}
		default:
			return { ok: false, detail: `unknown act kind ${JSON.stringify(req.kind)}` };
	}
}

function renderStatus(status: LinkStatus | null, theme: Theme): string {
	if (status === null) {
		return theme.fg("dim", "pi-buddy no sidecar");
	}
	if (!sidecarAlive(status.pid)) {
		return theme.fg("dim", "pi-buddy sidecar stopped");
	}
	if (status.connected) {
		const dot = theme.fg("success", "●");
		return `${dot} ${theme.fg("dim", `pi-buddy ${status.device ?? "linked"}`)}`;
	}
	const ring = theme.fg("warning", "○");
	return `${ring} ${theme.fg("dim", "pi-buddy waiting for device")}`;
}

// The pair code has to reach the phone, and macOS shares the clipboard with a
// nearby iPhone, so copying is more useful than printing. The package's own
// clipboard helper is not resolvable from the extensions directory, hence the
// platform command.
function copyToClipboard(text: string): Promise<void> {
	return new Promise((resolve, reject) => {
		const child = spawn("pbcopy", { stdio: ["pipe", "ignore", "ignore"] });
		child.on("error", reject);
		child.on("close", (code) => {
			if (code === 0) {
				resolve();
			} else {
				reject(new Error(`pbcopy exited with ${code}`));
			}
		});
		child.stdin.end(text);
	});
}

function describe(status: LinkStatus | null): string {
	if (status === null) {
		return "No sidecar has published a link yet. Start it with:\n  pi-buddy-sidecar --ws --ws-addr :51820";
	}
	if (!sidecarAlive(status.pid)) {
		return `The sidecar that published this state (pid ${status.pid}) is gone.`;
	}
	if (status.connected) {
		return `Device linked from ${status.device ?? "an unknown address"}.`;
	}
	return "Sidecar running; waiting for the device to connect.";
}

export default function (pi: ExtensionAPI) {
	let timer: ReturnType<typeof setInterval> | undefined;
	let ctxRef: ExtensionContext | undefined;
	let lastActId = "";

	async function tick(): Promise<void> {
		const ctx = ctxRef;
		if (ctx === undefined) {
			return;
		}
		await publishSession(ctx);

		const req = await readJSON<ActRequest>(ACT_FILE);
		if (req !== null && typeof req.id === "string" && req.id !== "" && req.id !== lastActId) {
			lastActId = req.id;
			const result = runAct(pi, ctx, req);
			await writeAtomic(
				ACT_RESULT_FILE,
				`${JSON.stringify({ id: req.id, ok: result.ok, detail: result.detail })}\n`,
			);
		}

		if (ctx.hasUI) {
			ctx.ui.setStatus(STATUS_KEY, renderStatus(await readJSON<LinkStatus>(STATUS_FILE), ctx.ui.theme));
		}
	}

	pi.on("session_start", async (_event, ctx) => {
		ctxRef = ctx;
		try {
			await ensureStateDir();
		} catch {
			// Without the directory there is no bridge; the footer still works.
		}
		if (timer !== undefined) {
			clearInterval(timer);
		}
		await tick();
		timer = setInterval(() => void tick().catch(() => undefined), TICK_MS);
	});

	pi.on("session_shutdown", async (_event, ctx) => {
		if (timer !== undefined) {
			clearInterval(timer);
			timer = undefined;
		}
		ctxRef = undefined;
		// Drop our published state so the sidecar sees us go rather than waiting
		// for it to notice the pid is gone.
		await rm(SESSION_FILE, { force: true }).catch(() => undefined);
		if (ctx.hasUI) {
			ctx.ui.setStatus(STATUS_KEY, undefined);
		}
	});

	pi.registerCommand("pibuddy", {
		description: "Show the Pi Buddy link status and the pair code",
		handler: async (_args, ctx) => {
			const status = await readJSON<LinkStatus>(STATUS_FILE);
			let code: string | null = null;
			try {
				const raw = (await readFile(TOKEN_FILE, "utf8")).trim();
				code = raw.length > 0 ? raw : null;
			} catch {
				code = null; // only the sidecar creates it, so it may not exist yet
			}

			ctx.ui.notify(`Pi Buddy: ${describe(status)}`, "info");
			if (code === null) {
				ctx.ui.notify("No pair code yet; the sidecar creates it on first run.", "warning");
				return;
			}
			ctx.ui.notify(`Pair code: ${code}`, "info");

			if (!ctx.hasUI || process.platform !== "darwin") {
				return;
			}
			const copy = await ctx.ui.confirm("Pi Buddy", `Copy the pair code to the clipboard?\n\n${code}`);
			if (!copy) {
				return;
			}
			try {
				await copyToClipboard(code);
				ctx.ui.notify("Pair code copied; paste it into the device setup page.", "info");
			} catch (error) {
				ctx.ui.notify(`Could not copy the pair code: ${String(error)}`, "error");
			}
		},
	});
}
