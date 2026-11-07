// Raven-specific launch and policy, in the place T3 Code keeps Kiro's
// (packages/provider-kiro/src/server/acpSupport.ts). Every Raven constant cites its source in
// EverMind-AI/Raven @ 8448cc6280923a7b20525af259a3b6d465844c42.
import type { PermissionOption, RequestPermissionRequest, ToolKind } from "./acp.ts";
import type { RavenSettings } from "../settings.ts";

/** `raven acp`: raven/cli/acp_commands.py. No other flag is needed; cwd comes from session/new. */
export const RAVEN_ACP_ARGS = ["acp"] as const;

/** Mid-turn text: raven/acp_client/protocol.py STEER_METHOD, answered by raven/acp/methods.py _session_steer. */
export const RAVEN_STEER_METHOD = "_raven/session/steer";
/** Declared by presence in agentCapabilities._meta: raven/acp_client/protocol.py STEER_CAPABILITY. */
export const RAVEN_STEER_CAPABILITY = "raven.steer";
/** Per-session stdio MCP servers are really connected and isolated: SESSION_MCP_CAPABILITY. */
export const RAVEN_SESSION_MCP_CAPABILITY = "raven.mcp.session";
/** Model switch is a config option with category "model": raven/acp/config_options.py MODEL_OPTION_ID. */
export const RAVEN_MODEL_CONFIG_ID = "model";

/** Shape of T3 Code's AcpSessionRuntime.AcpSpawnInput (packages/provider-acp/src/server/AcpSessionRuntime.ts). */
export interface AcpSpawnInput {
  readonly command: string;
  readonly args: ReadonlyArray<string>;
  readonly cwd: string;
  readonly env?: NodeJS.ProcessEnv;
}

export function buildRavenAcpSpawnInput(
  settings: Pick<RavenSettings, "binaryPath" | "configPath">,
  cwd: string,
  env?: NodeJS.ProcessEnv,
): AcpSpawnInput {
  const args = [...RAVEN_ACP_ARGS, ...(settings.configPath ? ["--config", settings.configPath] : [])];
  return { command: settings.binaryPath || "raven", args, cwd, ...(env ? { env } : {}) };
}

/** T3 Code's RuntimeMode (packages/contracts/src/providerPolicy.ts). */
export type RuntimeMode = "approval-required" | "auto-accept-edits" | "auto" | "full-access";
export type PermissionDisposition = "allow" | "deny" | "ask";

const READ_KINDS: ReadonlySet<ToolKind> = new Set(["read", "search", "fetch", "think"]);
const MUTATION_KINDS: ReadonlySet<ToolKind> = new Set(["edit", "delete", "move"]);

/**
 * A simplified acpPermissionDisposition (packages/provider-acp/src/server/clientPolicy.ts): reads pass,
 * Supervised asks, auto-accept-edits asks for anything that is not a file edit, the rest is allowed.
 */
export function ravenPermissionDisposition(mode: RuntimeMode, request: RequestPermissionRequest): PermissionDisposition {
  const kind = request.toolCall.kind ?? "other";
  if (READ_KINDS.has(kind)) return "allow";
  if (mode === "approval-required") return "ask";
  if (mode === "auto-accept-edits" && !MUTATION_KINDS.has(kind)) return "ask";
  return "allow";
}

/**
 * The option a policy approval may pick. Raven offers allow_once / allow_always / reject_once
 * (raven/acp/permissions.py); like Kiro, a policy never persists a grant, so only allow_once.
 */
export function pickOption(
  options: ReadonlyArray<PermissionOption>,
  decision: "accept" | "acceptForSession" | "decline",
): PermissionOption | undefined {
  const kind = decision === "accept" ? "allow_once" : decision === "acceptForSession" ? "allow_always" : "reject_once";
  return options.find((option) => option.kind === kind);
}
