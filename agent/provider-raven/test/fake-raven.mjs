#!/usr/bin/env node
// A fake `raven acp`: newline-delimited JSON-RPC on stdio, answering the ACP agent methods with the
// shapes EverMind-AI/Raven @ 8448cc62 sends (raven/acp/capabilities.py, methods.py, permissions.py),
// but scripted instead of running a model. FAKE_RAVEN_SCRIPT picks the turn: "edit" (default) asks
// permission for an edit and writes a file in cwd; "slow" streams until cancelled.
import { randomBytes } from "node:crypto";
import { writeFileSync } from "node:fs";
import { isAbsolute, join } from "node:path";
import { createInterface } from "node:readline";

const SCRIPT = process.env.FAKE_RAVEN_SCRIPT ?? "edit";
const sessions = new Map(); // sessionId -> { cwd, model, turn: { cancel: () => void } | null }
const waiting = new Map(); // outbound request id -> resolve
let initialized = false;
let outboundId = 1000;

const send = (frame) => process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", ...frame })}\n`);
const update = (sessionId, u) => send({ method: "session/update", params: { sessionId, update: u } });
const text = (kind, t) => ({ sessionUpdate: kind, content: { type: "text", text: t } });
const ask = (method, params) => new Promise((resolve) => { const id = outboundId++; waiting.set(id, resolve); send({ id, method, params }); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
class RpcError extends Error { constructor(code, message) { super(message); this.code = code; } }

function sessionFor(params) {
  const s = sessions.get(params?.sessionId);
  if (!s) throw new RpcError(-32002, "unknown session");
  return s;
}

async function runEditTurn(sessionId, s, prompt) {
  update(sessionId, text("agent_thought_chunk", "The comment points at the wheel; widen it."));
  const toolCallId = `call-${randomBytes(4).toString("hex")}`;
  const target = join(s.cwd, "fulmetry-fake-edit.txt");
  update(sessionId, { sessionUpdate: "tool_call", toolCallId, title: "Edit wheel diameter", kind: "edit", status: "pending", locations: [{ path: target }], rawInput: { prompt } });
  const answer = await ask("session/request_permission", {
    sessionId,
    toolCall: { toolCallId, title: "Edit wheel diameter", kind: "edit", status: "pending" },
    options: [
      { optionId: "allow-1", name: "Allow once", kind: "allow_once" },
      { optionId: "session-1", name: "Allow for this session", kind: "allow_always" },
      { optionId: "reject-1", name: "Reject", kind: "reject_once" },
    ],
  });
  if (s.turn?.cancelled) return "cancelled";
  const allowed = answer?.outcome?.outcome === "selected" && answer.outcome.optionId !== "reject-1";
  if (allowed) {
    update(sessionId, { sessionUpdate: "tool_call_update", toolCallId, status: "in_progress" });
    writeFileSync(target, `wheel diameter 70 mm (model ${s.model})\n`);
    update(sessionId, { sessionUpdate: "tool_call_update", toolCallId, status: "completed" });
    update(sessionId, { sessionUpdate: "plan", entries: [{ content: "Widen wheel", status: "completed", priority: "high" }] });
    update(sessionId, text("agent_message_chunk", "Resized the wheels "));
    update(sessionId, text("agent_message_chunk", "to 70 mm."));
  } else {
    update(sessionId, { sessionUpdate: "tool_call_update", toolCallId, status: "failed" });
    update(sessionId, text("agent_message_chunk", "Left the wheels alone."));
  }
  update(sessionId, { sessionUpdate: "usage_update", used: 1200, size: 200000 });
  return "end_turn";
}

async function runSlowTurn(sessionId, s) {
  for (let i = 0; i < 200 && !s.turn.cancelled; i++) {
    update(sessionId, text("agent_message_chunk", `.${i}`));
    await sleep(20);
  }
  return s.turn.cancelled ? "cancelled" : "end_turn";
}

const handlers = {
  initialize: (params) => {
    initialized = true;
    return {
      protocolVersion: params?.protocolVersion === 1 ? 1 : 1,
      agentCapabilities: {
        loadSession: true,
        promptCapabilities: { image: true, audio: false, embeddedContext: true },
        mcpCapabilities: { http: false, sse: false },
        _meta: { "raven.mcp.session": {}, "raven.steer": true, "raven.residentWork": true },
        sessionCapabilities: { list: {}, resume: {}, close: {}, delete: {} },
        auth: {},
      },
      authMethods: [],
      agentInfo: { name: "raven", version: "0.2.7-fake" },
    };
  },
  "session/new": (params) => {
    if (typeof params?.cwd !== "string" || !isAbsolute(params.cwd)) throw new RpcError(-32602, "cwd must be an absolute path");
    const sessionId = `acp:${randomBytes(6).toString("hex")}`;
    sessions.set(sessionId, { cwd: params.cwd, model: "fake-default", turn: null, mcpServers: params.mcpServers ?? [] });
    setImmediate(() => update(sessionId, { sessionUpdate: "available_commands_update", availableCommands: [{ name: "playbook", description: "Run a stored orchestration" }] }));
    return {
      sessionId,
      configOptions: [{ id: "model", name: "Model", category: "model", type: "select", currentValue: "fake-default", options: [{ value: "fake-default", name: "Default" }, { value: "fake-large", name: "Large" }] }],
    };
  },
  "session/load": (params) => {
    const s = sessionFor(params);
    s.cwd = params.cwd;
    update(params.sessionId, text("user_message_chunk", "earlier question"));
    update(params.sessionId, text("agent_message_chunk", "earlier answer"));
    return {};
  },
  "session/set_config_option": (params) => {
    const s = sessionFor(params);
    if (params.configId !== "model") throw new RpcError(-32602, "unknown config option");
    s.model = params.value;
    return { configOptions: [] };
  },
  "session/prompt": async (params) => {
    const s = sessionFor(params);
    if (s.turn) throw new RpcError(-32600, "a turn is already running on this session");
    s.turn = { cancelled: false };
    const promptText = (params.prompt ?? []).filter((b) => b.type === "text").map((b) => b.text).join("\n");
    try {
      const stopReason = SCRIPT === "slow" ? await runSlowTurn(params.sessionId, s) : await runEditTurn(params.sessionId, s, promptText);
      return stopReason === "end_turn" ? { stopReason, _meta: { "fake.promptChars": promptText.length } } : { stopReason };
    } finally {
      s.turn = null;
    }
  },
  "session/cancel": (params) => {
    const s = sessions.get(params?.sessionId);
    if (s?.turn) s.turn.cancelled = true;
    for (const [id, resolve] of waiting) { waiting.delete(id); resolve({ outcome: { outcome: "cancelled" } }); }
  },
  "_raven/session/steer": (params) => {
    const s = sessionFor(params);
    if (!s.turn) return { status: "no_turn" };
    update(params.sessionId, text("user_message_chunk", params.text));
    return { status: "injected" };
  },
};

createInterface({ input: process.stdin }).on("line", async (line) => {
  if (!line.trim()) return;
  const frame = JSON.parse(line);
  if (frame.method === undefined) {
    const resolve = waiting.get(frame.id);
    if (resolve) { waiting.delete(frame.id); resolve(frame.result); }
    return;
  }
  const isRequest = frame.id !== undefined && frame.id !== null;
  try {
    if (frame.method !== "initialize" && !initialized) throw new RpcError(-32600, "initialize must be called before any other method");
    const handler = handlers[frame.method];
    if (!handler) throw new RpcError(-32601, `unknown method ${frame.method}`);
    const result = await handler(frame.params ?? {});
    if (isRequest) send({ id: frame.id, result: result ?? {} });
  } catch (error) {
    if (isRequest) send({ id: frame.id, error: { code: error.code ?? -32603, message: error.message } });
  }
}).on("close", () => process.exit(0));
