// The subset of the Agent Client Protocol (protocolVersion 1) this provider speaks, and a
// newline-delimited JSON-RPC 2.0 peer over a child's stdio. T3 Code uses its own effect-acp package
// for this (packages/effect-acp); in the fork the adapter switches to that and this file goes away.
import type { Readable, Writable } from "node:stream";
import { createInterface } from "node:readline";

export const ACP_PROTOCOL_VERSION = 1;

export type ToolKind =
  | "read" | "edit" | "delete" | "move" | "search" | "execute" | "think" | "fetch" | "switch_mode" | "other";
export type ToolStatus = "pending" | "in_progress" | "completed" | "failed";
export type StopReason = "end_turn" | "max_tokens" | "max_turn_requests" | "refusal" | "cancelled";

export interface TextContent { readonly type: "text"; readonly text: string }
export type ContentBlock = TextContent | { readonly type: string; readonly [key: string]: unknown };

export interface ToolCallFields {
  readonly toolCallId: string;
  readonly title?: string;
  readonly kind?: ToolKind;
  readonly status?: ToolStatus;
  readonly locations?: ReadonlyArray<{ readonly path: string; readonly line?: number }>;
  readonly rawInput?: unknown;
  readonly rawOutput?: unknown;
  readonly content?: ReadonlyArray<unknown>;
  readonly _meta?: Record<string, unknown>;
}

export type SessionUpdate =
  | { readonly sessionUpdate: "agent_message_chunk" | "agent_thought_chunk" | "user_message_chunk"; readonly content: ContentBlock; readonly _meta?: Record<string, unknown> }
  | ({ readonly sessionUpdate: "tool_call" | "tool_call_update" } & ToolCallFields)
  | { readonly sessionUpdate: "plan"; readonly entries: ReadonlyArray<{ readonly content: string; readonly status: string; readonly priority?: string }> }
  | { readonly sessionUpdate: string; readonly [key: string]: unknown };

export interface SessionNotification { readonly sessionId: string; readonly update: SessionUpdate }

export interface PermissionOption {
  readonly optionId: string;
  readonly name: string;
  readonly kind: "allow_once" | "allow_always" | "reject_once" | "reject_always";
}
export interface RequestPermissionRequest {
  readonly sessionId: string;
  readonly toolCall: ToolCallFields;
  readonly options: ReadonlyArray<PermissionOption>;
}
export type RequestPermissionOutcome =
  | { readonly outcome: "selected"; readonly optionId: string }
  | { readonly outcome: "cancelled" };

export interface InitializeResponse {
  readonly protocolVersion: number;
  readonly agentCapabilities?: {
    readonly loadSession?: boolean;
    readonly promptCapabilities?: { readonly image?: boolean; readonly audio?: boolean; readonly embeddedContext?: boolean };
    readonly mcpCapabilities?: { readonly http?: boolean; readonly sse?: boolean };
    readonly sessionCapabilities?: Record<string, unknown>;
    readonly _meta?: Record<string, unknown>;
  };
  readonly authMethods?: ReadonlyArray<unknown>;
  readonly agentInfo?: { readonly name: string; readonly version: string };
}

export interface SessionConfigOption {
  readonly id: string;
  readonly category?: string;
  readonly type: string;
  readonly currentValue?: string;
  readonly options?: ReadonlyArray<{ readonly value: string; readonly name?: string }>;
}
export interface NewSessionResponse {
  readonly sessionId: string;
  readonly configOptions?: ReadonlyArray<SessionConfigOption>;
  readonly modes?: unknown;
}
export interface PromptResponse { readonly stopReason: StopReason; readonly _meta?: Record<string, unknown> }

export interface McpServerStdio {
  readonly name: string;
  readonly command: string;
  readonly args: ReadonlyArray<string>;
  readonly env: ReadonlyArray<{ readonly name: string; readonly value: string }>;
}

export class JsonRpcError extends Error {
  constructor(readonly code: number, message: string, readonly data?: unknown) {
    super(message);
  }
}

type RequestHandler = (method: string, params: unknown) => Promise<unknown>;
type NotificationHandler = (method: string, params: unknown) => void;

/** One JSON-RPC connection, one frame per line, as `raven acp` speaks it (raven/acp/stdio.py). */
export class JsonRpcPeer {
  private nextId = 1;
  private readonly pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void; method: string }>();
  private closedError: Error | null = null;
  onRequest: RequestHandler = async (method) => {
    throw new JsonRpcError(-32601, `unknown method ${method}`);
  };
  onNotification: NotificationHandler = () => {};
  onClose: (error: Error) => void = () => {};

  constructor(input: Readable, private readonly output: Writable) {
    const lines = createInterface({ input, crlfDelay: Infinity });
    lines.on("line", (line) => this.receive(line));
    lines.on("close", () => this.close(new Error("agent closed its stdout")));
  }

  request<T>(method: string, params: unknown): Promise<T> {
    if (this.closedError) return Promise.reject(this.closedError);
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (v: unknown) => void, reject, method });
      this.send({ jsonrpc: "2.0", id, method, params });
    });
  }

  notify(method: string, params: unknown): void {
    if (!this.closedError) this.send({ jsonrpc: "2.0", method, params });
  }

  close(error: Error): void {
    if (this.closedError) return;
    this.closedError = error;
    for (const { reject } of this.pending.values()) reject(error);
    this.pending.clear();
    this.onClose(error);
  }

  private send(frame: unknown): void {
    this.output.write(`${JSON.stringify(frame)}\n`);
  }

  private receive(line: string): void {
    if (line.trim().length === 0) return;
    let frame: Record<string, unknown>;
    try {
      frame = JSON.parse(line) as Record<string, unknown>;
    } catch {
      return; // A stray non-JSON line; raven claims fd 1 so this should not happen (raven/acp/stdio.py).
    }
    const { id, method } = frame;
    if (typeof method === "string") {
      if (id === undefined || id === null) {
        this.onNotification(method, frame.params);
        return;
      }
      this.onRequest(method, frame.params).then(
        (result) => this.send({ jsonrpc: "2.0", id, result: result ?? null }),
        (error: unknown) => {
          const e = error instanceof JsonRpcError ? error : new JsonRpcError(-32603, String(error));
          this.send({ jsonrpc: "2.0", id, error: { code: e.code, message: e.message, data: e.data } });
        },
      );
      return;
    }
    if (typeof id !== "number") return;
    const waiter = this.pending.get(id);
    if (!waiter) return;
    this.pending.delete(id);
    const err = frame.error as { code: number; message: string; data?: unknown } | undefined;
    if (err) waiter.reject(new JsonRpcError(err.code, `${waiter.method}: ${err.message}`, err.data));
    else waiter.resolve(frame.result);
  }
}
