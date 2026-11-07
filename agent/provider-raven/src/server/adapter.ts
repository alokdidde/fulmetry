// Raven as a T3 Code orchestration adapter. The surface mirrors ProviderAdapterV2 and
// ProviderAdapterV2SessionRuntime (T3 Code packages/provider-core/src/server/ProviderAdapter.ts @ 257e4073):
// openSession -> ensureThread -> startTurn / steerTurn / interruptTurn / respondToRuntimeRequest, with
// results as a stream of ProviderAdapterV2Event-shaped events. T3 uses Effect; this skeleton uses Promises
// and a listener so it runs without the T3 workspace. In the fork, the Raven-specific parts here
// (spawn, steering, policy, stop-reason mapping) become an AcpAdapterV2Flavor for makeAcpAdapterV2.
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";

import {
  ACP_PROTOCOL_VERSION,
  JsonRpcError,
  JsonRpcPeer,
  type InitializeResponse,
  type McpServerStdio,
  type NewSessionResponse,
  type PromptResponse,
  type RequestPermissionOutcome,
  type RequestPermissionRequest,
  type SessionNotification,
  type StopReason,
  type ToolCallFields,
  type ToolKind,
} from "./acp.ts";
import {
  buildRavenAcpSpawnInput,
  pickOption,
  RAVEN_MODEL_CONFIG_ID,
  RAVEN_STEER_CAPABILITY,
  RAVEN_STEER_METHOD,
  ravenPermissionDisposition,
  type AcpSpawnInput,
  type RuntimeMode,
} from "./acpSupport.ts";
import type { RavenSettings } from "../settings.ts";

export const RAVEN_DRIVER = "raven";

/** AcpProviderCapabilitiesV2 (packages/provider-acp/src/server/adapter.ts) with what Raven adds. */
export const RavenProviderCapabilitiesV2 = {
  sessions: { supportsMultipleProviderThreadsPerSession: false, supportsModelSwitchInSession: true },
  threads: { canCreateEmptyThread: true, canReadThreadSnapshot: false, canRollbackThread: true, canForkThread: false },
  turns: { supportsInterrupt: true, supportsActiveSteering: true, supportsQueuedMessages: true },
  tools: { supportsMcpTools: true },
} as const;

// ---- T3-shaped records (subsets of packages/contracts/src/orchestrationV2.ts) ----
export interface RuntimePolicy { readonly runtimeMode: RuntimeMode; readonly cwd: string }
export interface ModelSelection { readonly model: string }
export interface ProviderThread {
  readonly id: string; readonly threadId: string; readonly providerSessionId: string; readonly nativeThreadId: string;
}
export type TurnItemType = "reasoning" | "file_change" | "command_execution" | "file_search" | "web_search" | "dynamic_tool";
export interface TurnItem {
  readonly id: string; readonly threadId: string; readonly runId: string; readonly type: TurnItemType;
  readonly status: "running" | "completed" | "failed"; readonly title: string; readonly text?: string;
  readonly paths?: ReadonlyArray<string>; readonly meta?: Record<string, unknown>;
}
export interface RuntimeRequest {
  readonly id: string; readonly threadId: string; readonly type: "approval"; readonly title: string;
  readonly status: "pending" | "resolved"; readonly decision?: ApprovalDecision;
}
export type ApprovalDecision = "accept" | "acceptForSession" | "decline" | "cancel";
export type TerminalStatus = "completed" | "interrupted" | "cancelled" | "failed";

export type RavenAdapterEvent =
  | { readonly type: "provider_session.updated"; readonly driver: string; readonly providerSession: { readonly id: string; readonly status: "starting" | "ready" | "running" | "stopped" | "error"; readonly agentVersion?: string } }
  | { readonly type: "provider_thread.updated"; readonly driver: string; readonly providerThread: ProviderThread }
  | { readonly type: "provider_turn.updated"; readonly driver: string; readonly threadId: string; readonly providerTurn: { readonly id: string; readonly runId: string; readonly status: "running" | TerminalStatus } }
  | { readonly type: "message.updated"; readonly driver: string; readonly message: { readonly id: string; readonly threadId: string; readonly runId: string; readonly role: "assistant" | "user"; readonly text: string; readonly streaming: boolean } }
  | { readonly type: "turn_item.updated"; readonly driver: string; readonly turnItem: TurnItem }
  | { readonly type: "plan.updated"; readonly driver: string; readonly plan: { readonly threadId: string; readonly runId: string; readonly steps: ReadonlyArray<{ readonly text: string; readonly status: string }> } }
  | { readonly type: "runtime_request.updated"; readonly driver: string; readonly threadId: string; readonly runtimeRequest: RuntimeRequest }
  | { readonly type: "turn.terminal"; readonly driver: string; readonly providerThreadId: string; readonly providerTurnId: string; readonly runOrdinal: number; readonly status: TerminalStatus; readonly reason?: string; readonly failure: null | { readonly class: string; readonly message: string }; readonly threadDisposition: "reusable" | "broken"; readonly nativeMeta?: Record<string, unknown> };

export interface OpenSessionInput {
  readonly threadId: string;
  readonly providerSessionId: string;
  readonly modelSelection: ModelSelection;
  readonly runtimePolicy: RuntimePolicy;
  /** T3 bridges its own MCP endpoints to stdio for ACP agents (apps/server/src/mcp/AcpMcpStdioBridge.ts). */
  readonly mcpServers?: ReadonlyArray<McpServerStdio>;
}
export interface TurnInput {
  readonly threadId: string;
  readonly runId: string;
  readonly runOrdinal: number;
  readonly providerThread: ProviderThread;
  readonly message: { readonly messageId: string; readonly text: string };
  /** Composer context T3 prepends (ProviderAdapterV2TurnInput.appContext); Fulmetry puts the comment anchor here. */
  readonly appContext?: ReadonlyArray<{ readonly key: string; readonly text: string }>;
}

export interface RavenAdapterOptions {
  readonly instanceId: string;
  readonly settings: RavenSettings;
  readonly environment?: NodeJS.ProcessEnv;
  /** Replaces the `raven acp` launch, the way KiroAdapterV2Options.makeRuntime does for replay tests. */
  readonly spawnOverride?: (input: AcpSpawnInput) => AcpSpawnInput;
}

type Listener = (event: RavenAdapterEvent) => void;

interface ActiveTurn {
  readonly input: TurnInput;
  readonly providerTurnId: string;
  readonly messageId: string;
  text: string;
  readonly tools: Map<string, TurnItem>;
  interrupted: boolean;
}

const TOOL_ITEM_TYPE: Partial<Record<ToolKind, TurnItemType>> = {
  edit: "file_change", delete: "file_change", move: "file_change", execute: "command_execution",
  read: "file_search", search: "file_search", fetch: "web_search", think: "reasoning",
};

/** Raven answers every prompt with a stopReason, never an error (raven/acp/methods.py module doc). */
function terminalFor(stop: StopReason, interrupted: boolean): { status: TerminalStatus; failure: null | { class: string; message: string } } {
  switch (stop) {
    case "end_turn": return { status: "completed", failure: null };
    case "cancelled": return { status: interrupted ? "interrupted" : "cancelled", failure: null };
    case "refusal": return { status: "failed", failure: { class: "provider_refusal", message: "Raven refused the request." } };
    default: return { status: "failed", failure: { class: "provider_limit", message: `Raven stopped: ${stop}.` } };
  }
}

export class RavenSessionRuntime {
  readonly driver = RAVEN_DRIVER;
  private readonly listeners = new Set<Listener>();
  private readonly pendingRequests = new Map<string, { readonly request: RequestPermissionRequest; readonly resolve: (o: RequestPermissionOutcome) => void; readonly threadId: string }>();
  private readonly threads = new Map<string, ProviderThread>();
  private readonly activeTurns = new Map<string, ActiveTurn>();
  private initializeResult: InitializeResponse | null = null;
  private requestSeq = 0;
  private status: "starting" | "ready" | "running" | "stopped" | "error" = "starting";

  constructor(
    readonly providerSessionId: string,
    private readonly input: OpenSessionInput,
    private readonly child: ChildProcessWithoutNullStreams,
    private readonly peer: JsonRpcPeer,
  ) {
    peer.onNotification = (method, params) => {
      if (method === "session/update") this.onSessionUpdate(params as SessionNotification);
    };
    peer.onRequest = (method, params) => this.onAgentRequest(method, params);
    peer.onClose = () => this.setStatus(this.status === "stopped" ? "stopped" : "error");
  }

  get agentInfo() { return this.initializeResult?.agentInfo; }
  get supportsSteering(): boolean {
    return this.initializeResult?.agentCapabilities?._meta?.[RAVEN_STEER_CAPABILITY] !== undefined;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async start(): Promise<void> {
    this.emit(this.sessionEvent());
    this.initializeResult = await this.peer.request<InitializeResponse>("initialize", {
      protocolVersion: ACP_PROTOCOL_VERSION,
      clientCapabilities: { fs: { readTextFile: false, writeTextFile: false }, terminal: false },
      clientInfo: { name: "t3code-fulmetry", version: "0.0.0" },
    });
    this.setStatus("ready");
  }

  /** ensureThread: one ACP session per app thread, pinned to the thread's worktree as cwd. */
  async ensureThread(input: { readonly threadId: string; readonly modelSelection?: ModelSelection; readonly existing?: ProviderThread }): Promise<ProviderThread> {
    const known = this.threads.get(input.threadId);
    if (known) return known;
    const mcpServers = this.input.mcpServers ?? [];
    let nativeThreadId: string;
    if (input.existing && this.initializeResult?.agentCapabilities?.loadSession) {
      await this.peer.request("session/load", { sessionId: input.existing.nativeThreadId, cwd: this.input.runtimePolicy.cwd, mcpServers });
      nativeThreadId = input.existing.nativeThreadId;
    } else {
      const created = await this.peer.request<NewSessionResponse>("session/new", { cwd: this.input.runtimePolicy.cwd, mcpServers });
      nativeThreadId = created.sessionId;
      await this.applyModel(created, input.modelSelection ?? this.input.modelSelection);
    }
    const thread: ProviderThread = { id: `raven:${nativeThreadId}`, threadId: input.threadId, providerSessionId: this.providerSessionId, nativeThreadId };
    this.threads.set(input.threadId, thread);
    this.emit({ type: "provider_thread.updated", driver: RAVEN_DRIVER, providerThread: thread });
    return thread;
  }

  /** Resolves once the prompt is sent; the outcome arrives as turn.terminal, as in T3. */
  async startTurn(input: TurnInput): Promise<void> {
    const native = input.providerThread.nativeThreadId;
    if (this.activeTurns.has(native)) throw new Error("previous_turn_active");
    const turn: ActiveTurn = {
      input, providerTurnId: `${input.runId}:turn`, messageId: `${input.runId}:assistant`, text: "", tools: new Map(), interrupted: false,
    };
    this.activeTurns.set(native, turn);
    this.setStatus("running");
    this.emit({ type: "provider_turn.updated", driver: RAVEN_DRIVER, threadId: input.threadId, providerTurn: { id: turn.providerTurnId, runId: input.runId, status: "running" } });
    const context = (input.appContext ?? []).map((entry) => ({ type: "text", text: `<${entry.key}>\n${entry.text}\n</${entry.key}>` }));
    const prompt = [...context, { type: "text", text: input.message.text }];
    this.peer.request<PromptResponse>("session/prompt", { sessionId: native, prompt }).then(
      (response) => this.finishTurn(native, response.stopReason, response._meta),
      (error: unknown) => this.failTurn(native, error),
    );
  }

  async steerTurn(input: { readonly providerThread: ProviderThread; readonly text: string }): Promise<"injected" | "no_turn"> {
    if (!this.supportsSteering) throw new Error("steering unsupported");
    const result = await this.peer.request<{ status: "injected" | "no_turn" }>(RAVEN_STEER_METHOD, { sessionId: input.providerThread.nativeThreadId, text: input.text });
    return result.status;
  }

  /** session/cancel is a notification; the prompt then answers stopReason "cancelled". */
  async interruptTurn(input: { readonly providerThread: ProviderThread }): Promise<void> {
    const native = input.providerThread.nativeThreadId;
    const turn = this.activeTurns.get(native);
    if (turn) turn.interrupted = true;
    for (const [id, pending] of this.pendingRequests) {
      if (pending.request.sessionId === native) this.resolveRequest(id, "cancel");
    }
    this.peer.notify("session/cancel", { sessionId: native });
  }

  async respondToRuntimeRequest(input: { readonly requestId: string; readonly decision: ApprovalDecision }): Promise<void> {
    if (!this.pendingRequests.has(input.requestId)) throw new Error(`unknown runtime request ${input.requestId}`);
    this.resolveRequest(input.requestId, input.decision);
  }

  async close(): Promise<void> {
    this.status = "stopped";
    this.child.stdin.end();
    await new Promise<void>((resolve) => {
      if (this.child.exitCode !== null || this.child.signalCode !== null) return resolve();
      const timer = setTimeout(() => this.child.kill("SIGTERM"), 2000);
      this.child.once("exit", () => { clearTimeout(timer); resolve(); });
    });
    this.peer.close(new Error("session closed"));
    this.emit(this.sessionEvent());
  }

  // ---- inbound ----

  private async applyModel(created: NewSessionResponse, selection: ModelSelection): Promise<void> {
    const option = created.configOptions?.find((o) => o.category === "model" || o.id === RAVEN_MODEL_CONFIG_ID);
    const wanted = selection.model.trim();
    if (!option || wanted === "" || wanted === "default" || option.currentValue === wanted) return;
    await this.peer.request("session/set_config_option", { sessionId: created.sessionId, configId: option.id, value: wanted });
  }

  private onSessionUpdate(notification: SessionNotification): void {
    const turn = this.activeTurns.get(notification.sessionId);
    if (!turn) return; // Replay from session/load, or an update after the turn settled.
    const { threadId, runId } = turn.input;
    const update = notification.update;
    switch (update.sessionUpdate) {
      case "agent_message_chunk": {
        const content = (update as { content: { type: string; text?: string } }).content;
        if (content.type !== "text" || !content.text) return;
        turn.text += content.text;
        this.emit({ type: "message.updated", driver: RAVEN_DRIVER, message: { id: turn.messageId, threadId, runId, role: "assistant", text: turn.text, streaming: true } });
        return;
      }
      case "agent_thought_chunk": {
        const content = (update as { content: { type: string; text?: string } }).content;
        this.emit({ type: "turn_item.updated", driver: RAVEN_DRIVER, turnItem: { id: `${runId}:reasoning`, threadId, runId, type: "reasoning", status: "running", title: "Thinking", text: content.text ?? "" } });
        return;
      }
      case "tool_call":
      case "tool_call_update": {
        const fields = update as unknown as ToolCallFields;
        const previous = turn.tools.get(fields.toolCallId);
        const status = fields.status === "completed" ? "completed" : fields.status === "failed" ? "failed" : previous?.status ?? "running";
        const meta = fields._meta ?? previous?.meta;
        const item: TurnItem = {
          id: `${runId}:tool:${fields.toolCallId}`, threadId, runId,
          type: (fields.kind && TOOL_ITEM_TYPE[fields.kind]) ?? previous?.type ?? "dynamic_tool",
          status, title: fields.title ?? previous?.title ?? "Tool",
          ...(fields.locations ?? previous?.paths ? { paths: fields.locations?.map((l) => l.path) ?? previous?.paths ?? [] } : {}),
          ...(meta ? { meta } : {}),
        };
        turn.tools.set(fields.toolCallId, item);
        this.emit({ type: "turn_item.updated", driver: RAVEN_DRIVER, turnItem: item });
        return;
      }
      case "plan": {
        const entries = (update as { entries: ReadonlyArray<{ content: string; status: string }> }).entries;
        this.emit({ type: "plan.updated", driver: RAVEN_DRIVER, plan: { threadId, runId, steps: entries.map((e) => ({ text: e.content, status: e.status })) } });
        return;
      }
      default:
        return; // usage_update, session_info_update, available_commands_update: session metadata, not turn items.
    }
  }

  private async onAgentRequest(method: string, params: unknown): Promise<unknown> {
    if (method !== "session/request_permission") {
      // fs/* and terminal/* are not declared in clientCapabilities, so a conformant agent never sends them.
      throw new JsonRpcError(-32601, `${method} is not supported by this client`);
    }
    const request = params as RequestPermissionRequest;
    const turn = this.activeTurns.get(request.sessionId);
    const threadId = turn?.input.threadId ?? "";
    const disposition = ravenPermissionDisposition(this.input.runtimePolicy.runtimeMode, request);
    if (disposition !== "ask") {
      const option = pickOption(request.options, disposition === "allow" ? "accept" : "decline");
      return { outcome: option ? { outcome: "selected", optionId: option.optionId } : { outcome: "cancelled" } };
    }
    const id = `raven-req-${++this.requestSeq}`;
    const outcome = await new Promise<RequestPermissionOutcome>((resolve) => {
      this.pendingRequests.set(id, { request, resolve, threadId });
      this.emit({ type: "runtime_request.updated", driver: RAVEN_DRIVER, threadId, runtimeRequest: { id, threadId, type: "approval", title: request.toolCall.title ?? "Permission", status: "pending" } });
    });
    return { outcome };
  }

  private resolveRequest(id: string, decision: ApprovalDecision): void {
    const pending = this.pendingRequests.get(id);
    if (!pending) return;
    this.pendingRequests.delete(id);
    const option = decision === "cancel" ? undefined : pickOption(pending.request.options, decision);
    pending.resolve(option ? { outcome: "selected", optionId: option.optionId } : { outcome: "cancelled" });
    this.emit({ type: "runtime_request.updated", driver: RAVEN_DRIVER, threadId: pending.threadId, runtimeRequest: { id, threadId: pending.threadId, type: "approval", title: pending.request.toolCall.title ?? "Permission", status: "resolved", decision } });
  }

  private finishTurn(native: string, stop: StopReason, meta?: Record<string, unknown>): void {
    const turn = this.activeTurns.get(native);
    if (!turn) return;
    this.activeTurns.delete(native);
    const { threadId, runId, runOrdinal, providerThread } = turn.input;
    if (turn.text) {
      this.emit({ type: "message.updated", driver: RAVEN_DRIVER, message: { id: turn.messageId, threadId, runId, role: "assistant", text: turn.text, streaming: false } });
    }
    const { status, failure } = terminalFor(stop, turn.interrupted);
    this.emit({ type: "provider_turn.updated", driver: RAVEN_DRIVER, threadId, providerTurn: { id: turn.providerTurnId, runId, status } });
    this.emit({
      type: "turn.terminal", driver: RAVEN_DRIVER, providerThreadId: providerThread.id, providerTurnId: turn.providerTurnId,
      runOrdinal, status, reason: stop, failure, threadDisposition: "reusable", ...(meta ? { nativeMeta: meta } : {}),
    });
    if (this.activeTurns.size === 0) this.setStatus("ready");
  }

  private failTurn(native: string, error: unknown): void {
    const turn = this.activeTurns.get(native);
    if (!turn) return;
    this.activeTurns.delete(native);
    const message = error instanceof Error ? error.message : String(error);
    this.emit({
      type: "turn.terminal", driver: RAVEN_DRIVER, providerThreadId: turn.input.providerThread.id, providerTurnId: turn.providerTurnId,
      runOrdinal: turn.input.runOrdinal, status: "failed", failure: { class: "provider_error", message }, threadDisposition: "broken",
    });
  }

  private sessionEvent(): RavenAdapterEvent {
    const agentVersion = this.initializeResult?.agentInfo?.version;
    return { type: "provider_session.updated", driver: RAVEN_DRIVER, providerSession: { id: this.providerSessionId, status: this.status, ...(agentVersion ? { agentVersion } : {}) } };
  }

  private setStatus(status: RavenSessionRuntime["status"]): void {
    if (this.status === status) return;
    this.status = status;
    this.emit(this.sessionEvent());
  }

  private emit(event: RavenAdapterEvent): void {
    for (const listener of this.listeners) listener(event);
  }
}

/** ProviderAdapterV2's service shape: instanceId, driver, getCapabilities, openSession. */
export function makeRavenAdapter(options: RavenAdapterOptions) {
  return {
    instanceId: options.instanceId,
    driver: RAVEN_DRIVER,
    getCapabilities: () => RavenProviderCapabilitiesV2,
    async openSession(input: OpenSessionInput, onEvent?: Listener): Promise<RavenSessionRuntime> {
      let launch = buildRavenAcpSpawnInput(options.settings, input.runtimePolicy.cwd, options.environment);
      if (options.spawnOverride) launch = options.spawnOverride(launch);
      const child = spawn(launch.command, [...launch.args], { cwd: launch.cwd, env: launch.env ?? process.env, stdio: ["pipe", "pipe", "pipe"] });
      child.stderr.resume(); // Raven sends WARNING+ to stderr (raven/cli/acp_commands.py); T3 logs it.
      const spawned = new Promise<void>((resolve, reject) => {
        child.once("spawn", () => resolve());
        child.once("error", reject);
      });
      await spawned;
      const runtime = new RavenSessionRuntime(input.providerSessionId, input, child, new JsonRpcPeer(child.stdout, child.stdin));
      if (onEvent) runtime.subscribe(onEvent);
      await runtime.start();
      return runtime;
    },
  };
}
export type RavenAdapter = ReturnType<typeof makeRavenAdapter>;
