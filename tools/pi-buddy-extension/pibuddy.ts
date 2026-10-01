/**
 * Pi Buddy — link status in the footer, and the pair code on demand.
 *
 * The Pi Agent Buddy device (see docs/pi-agent-buddy/ in the ai-passport repo)
 * reaches this computer over Wi-Fi through the sidecar. The sidecar publishes its
 * link state to ~/.pi-buddy/status.json; this extension surfaces that where the
 * user is already looking, and shows the pair code the device's setup page asks
 * for.
 *
 * Install: symlink or copy this file into ~/.pi/agent/extensions/ and run
 * /pibuddy. Nothing here is macOS-specific except the clipboard copy.
 */
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import type { ExtensionAPI, ExtensionContext, Theme } from "@earendil-works/pi-coding-agent";

const STATE_DIR = join(homedir(), ".pi-buddy");
const STATUS_FILE = join(STATE_DIR, "status.json");
const TOKEN_FILE = join(STATE_DIR, "token");
const STATUS_KEY = "pibuddy";
const POLL_MS = 4000;

type LinkStatus = {
	connected?: boolean;
	device?: string;
	pid?: number;
	updated?: string;
};

async function readStatus(): Promise<LinkStatus | null> {
	try {
		return JSON.parse(await readFile(STATUS_FILE, "utf8")) as LinkStatus;
	} catch {
		return null; // no sidecar has published yet, or the file is unreadable
	}
}

async function readPairCode(): Promise<string | null> {
	try {
		const code = (await readFile(TOKEN_FILE, "utf8")).trim();
		return code.length > 0 ? code : null;
	} catch {
		return null; // only the sidecar creates it, so it may not exist yet
	}
}

// The status file outlives the sidecar, so its pid decides whether it still means
// anything: a killed sidecar leaves the last state behind.
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
		return "No sidecar has published a link yet. Start it with:\n  pi-buddy-sidecar --ws --ws-addr :51820 --target <pane>";
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

	async function refresh(ctx: ExtensionContext): Promise<void> {
		if (!ctx.hasUI) {
			return;
		}
		ctx.ui.setStatus(STATUS_KEY, renderStatus(await readStatus(), ctx.ui.theme));
	}

	pi.on("session_start", async (_event, ctx) => {
		if (!ctx.hasUI) {
			return;
		}
		await refresh(ctx);
		// Polling suits a file this small; a watcher would also have to cope with
		// the atomic replaces that publishing does.
		if (timer !== undefined) {
			clearInterval(timer);
		}
		timer = setInterval(() => void refresh(ctx), POLL_MS);
	});

	pi.on("session_shutdown", async (_event, ctx) => {
		if (timer !== undefined) {
			clearInterval(timer);
			timer = undefined;
		}
		if (ctx.hasUI) {
			ctx.ui.setStatus(STATUS_KEY, undefined);
		}
	});

	pi.registerCommand("pibuddy", {
		description: "Show the Pi Buddy link status and the pair code",
		handler: async (_args, ctx) => {
			const [status, code] = await Promise.all([readStatus(), readPairCode()]);

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
