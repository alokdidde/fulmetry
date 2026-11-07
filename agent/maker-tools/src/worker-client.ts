/**
 * Keeps one engine worker process alive and talks JSON-RPC 2.0 to it over stdio, one JSON object per line.
 * Requests are multiplexed by id, each has a timeout, and a worker that dies is restarted with backoff.
 */
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { EventEmitter } from "node:events";
import { join } from "node:path";
import { createInterface } from "node:readline";
import { REPO_ROOT } from "./contracts.js";

export const DEFAULT_TIMEOUT_MS = 120_000;

/** The worker's own codes, from worker/fulmetry_worker/server.py. */
export const RpcCode = {
  PARSE_ERROR: -32700,
  INVALID_REQUEST: -32600,
  METHOD_NOT_FOUND: -32601,
  INVALID_PARAMS: -32602,
  INTERNAL: -32603,
  NOT_IMPLEMENTED: -32001,
  RESULT_CONTRACT: -32002,
} as const;

/** Codes this client uses for failures that never reached a worker reply. */
export const ClientCode = {
  TIMEOUT: -32010,
  WORKER_EXITED: -32011,
  WORKER_UNAVAILABLE: -32012,
} as const;

export class WorkerRpcError extends Error {
  constructor(
    readonly code: number,
    message: string,
    readonly data?: unknown,
  ) {
    super(message);
    this.name = "WorkerRpcError";
  }
}

export interface WorkerClientOptions {
  /** Python interpreter. Default: $FULMETRY_WORKER_PYTHON or python3. */
  python?: string;
  /** Directory the worker runs in. Default: $FULMETRY_WORKER_CWD or <repo>/worker. */
  cwd?: string;
  /** Arguments after the interpreter. Default: -m fulmetry_worker.server. */
  args?: string[];
  /** Per-request timeout. Default: $FULMETRY_WORKER_TIMEOUT_MS or 120 s. */
  timeoutMs?: number;
  /** Restart backoff: first delay, doubled up to maxRestartDelayMs, reset once a worker stays up 10 s. */
  restartDelayMs?: number;
  maxRestartDelayMs?: number;
  /** Where worker stderr goes. Default: this process's stderr, prefixed. */
  onStderr?: (line: string) => void;
  env?: NodeJS.ProcessEnv;
}

interface Pending {
  method: string;
  resolve: (v: unknown) => void;
  reject: (e: Error) => void;
  timer: NodeJS.Timeout;
}

/** Events: "spawn" (pid), "exit" ({ code, signal, pid }), "error" (Error, only when listened to). */
export class WorkerClient extends EventEmitter {
  private readonly python: string;
  private readonly cwd: string;
  private readonly args: string[];
  private readonly timeoutMs: number;
  private readonly restartDelayMs: number;
  private readonly maxRestartDelayMs: number;
  private readonly onStderr: (line: string) => void;
  private readonly env: NodeJS.ProcessEnv;

  private child: ChildProcessWithoutNullStreams | undefined;
  private readonly pending = new Map<number, Pending>();
  private nextId = 1;
  private closed = false;
  private restartTimer: NodeJS.Timeout | undefined;
  private delay: number;
  private startedAt = 0;
  /** How many times a worker was started after the first one. */
  restarts = 0;

  constructor(opts: WorkerClientOptions = {}) {
    super();
    const env = opts.env ?? process.env;
    this.env = env;
    this.python = opts.python ?? env.FULMETRY_WORKER_PYTHON ?? "python3";
    this.cwd = opts.cwd ?? env.FULMETRY_WORKER_CWD ?? join(REPO_ROOT, "worker");
    this.args = opts.args ?? ["-m", "fulmetry_worker.server"];
    this.timeoutMs = opts.timeoutMs ?? (Number(env.FULMETRY_WORKER_TIMEOUT_MS) || DEFAULT_TIMEOUT_MS);
    this.restartDelayMs = opts.restartDelayMs ?? 100;
    this.maxRestartDelayMs = opts.maxRestartDelayMs ?? 5_000;
    this.delay = this.restartDelayMs;
    this.onStderr = opts.onStderr ?? ((line) => process.stderr.write(`[fulmetry-worker] ${line}\n`));
  }

  /** The running worker's pid, if one is running. */
  get pid(): number | undefined {
    return this.child?.pid;
  }

  /** Starts the worker if it is not running. Calls start it on demand, so this is optional. */
  start(): void {
    if (this.closed) throw new WorkerRpcError(ClientCode.WORKER_UNAVAILABLE, "worker client is closed");
    if (this.child) return;
    clearTimeout(this.restartTimer);
    this.restartTimer = undefined;
    if (this.startedAt !== 0) this.restarts++;
    this.startedAt = Date.now();

    const child = spawn(this.python, this.args, {
      cwd: this.cwd,
      env: { ...this.env, PYTHONUNBUFFERED: "1" },
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.child = child;
    child.stdin.on("error", () => {}); // EPIPE after a crash; the exit handler fails the pending calls
    createInterface({ input: child.stdout }).on("line", (line) => this.onLine(line));
    createInterface({ input: child.stderr }).on("line", (line) => this.onStderr(line));
    child.once("spawn", () => this.emit("spawn", child.pid));
    child.once("error", (err) => {
      // Spawn failed (bad interpreter or cwd). No "exit" follows, so clean up here.
      this.onGone(child, new WorkerRpcError(ClientCode.WORKER_UNAVAILABLE, `cannot start worker: ${err.message}`));
      if (this.listenerCount("error") > 0) this.emit("error", err);
    });
    child.once("exit", (code, signal) => {
      this.onGone(
        child,
        new WorkerRpcError(ClientCode.WORKER_EXITED, `worker exited (${signal ?? `code ${code}`}) before answering`),
      );
      this.emit("exit", { code, signal, pid: child.pid });
    });
  }

  /** Sends one request and resolves with its result, or rejects with a WorkerRpcError. */
  request<T = unknown>(method: string, params: Record<string, unknown> = {}, timeoutMs = this.timeoutMs): Promise<T> {
    if (this.closed) return Promise.reject(new WorkerRpcError(ClientCode.WORKER_UNAVAILABLE, "worker client is closed"));
    this.start();
    const child = this.child!;
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new WorkerRpcError(ClientCode.TIMEOUT, `${method} timed out after ${timeoutMs} ms`));
        // The worker answers in order, so a stuck call blocks every later one: replace the worker.
        child.kill("SIGKILL");
      }, timeoutMs);
      this.pending.set(id, { method, resolve: resolve as (v: unknown) => void, reject, timer });
      child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    });
  }

  /** Stops the worker for good and fails anything still waiting. */
  async close(): Promise<void> {
    this.closed = true;
    clearTimeout(this.restartTimer);
    const child = this.child;
    if (!child) return;
    const exited = new Promise<void>((r) => child.once("exit", () => r()));
    child.stdin.end();
    const kill = setTimeout(() => child.kill("SIGKILL"), 2_000);
    await exited;
    clearTimeout(kill);
  }

  private onLine(line: string): void {
    if (!line.trim()) return;
    let msg: { id?: unknown; result?: unknown; error?: { code: number; message: string; data?: unknown } };
    try {
      msg = JSON.parse(line);
    } catch {
      this.onStderr(`unparseable stdout line: ${line}`);
      return;
    }
    const p = typeof msg.id === "number" ? this.pending.get(msg.id) : undefined;
    if (!p) {
      if (msg.error) this.onStderr(`error for no pending request: ${JSON.stringify(msg.error)}`);
      return;
    }
    this.pending.delete(msg.id as number);
    clearTimeout(p.timer);
    if (msg.error) p.reject(new WorkerRpcError(msg.error.code, msg.error.message, msg.error.data));
    else p.resolve(msg.result);
  }

  private onGone(child: ChildProcessWithoutNullStreams, err: WorkerRpcError): void {
    if (this.child !== child) return;
    this.child = undefined;
    for (const [id, p] of this.pending) {
      clearTimeout(p.timer);
      p.reject(new WorkerRpcError(err.code, `${p.method}: ${err.message}`));
      this.pending.delete(id);
    }
    if (this.closed) return;
    // Back off on a crash loop; a worker that stayed up a while resets the delay.
    if (Date.now() - this.startedAt > 10_000) this.delay = this.restartDelayMs;
    const wait = this.delay;
    this.delay = Math.min(this.delay * 2, this.maxRestartDelayMs);
    this.restartTimer = setTimeout(() => {
      if (!this.closed && !this.child) this.start();
    }, wait);
    this.restartTimer.unref();
  }
}
