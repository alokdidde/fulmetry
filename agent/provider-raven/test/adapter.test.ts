import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  buildRavenAcpSpawnInput,
  makeRavenAdapter,
  type RavenAdapterEvent,
  type RavenSessionRuntime,
} from "../src/server.ts";
import { DEFAULT_RAVEN_SETTINGS, decodeRavenSettings } from "../src/settings.ts";
import { fakeRavenSpawn } from "../src/testing.ts";

type RuntimeMode = "approval-required" | "auto-accept-edits" | "auto" | "full-access";

let worktree: string;
let runtime: RavenSessionRuntime | undefined;
let events: RavenAdapterEvent[];

beforeEach(() => {
  worktree = mkdtempSync(join(tmpdir(), "fulmetry-raven-"));
  events = [];
});
afterEach(async () => {
  await runtime?.close();
  runtime = undefined;
  rmSync(worktree, { recursive: true, force: true });
});

async function open(script: string, runtimeMode: RuntimeMode, model = "fake-large") {
  const adapter = makeRavenAdapter({ instanceId: "raven", settings: DEFAULT_RAVEN_SETTINGS, spawnOverride: fakeRavenSpawn(script) });
  runtime = await adapter.openSession(
    { threadId: "thread-1", providerSessionId: "ps-1", modelSelection: { model }, runtimePolicy: { runtimeMode, cwd: worktree } },
    (event) => events.push(event),
  );
  const thread = await runtime.ensureThread({ threadId: "thread-1" });
  return { runtime, thread };
}

function waitFor<T extends RavenAdapterEvent["type"]>(type: T, predicate: (e: Extract<RavenAdapterEvent, { type: T }>) => boolean = () => true) {
  return new Promise<Extract<RavenAdapterEvent, { type: T }>>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`timed out waiting for ${type}`)), 5000);
    const check = () => {
      const hit = events.find((e): e is Extract<RavenAdapterEvent, { type: T }> => e.type === type && predicate(e as Extract<RavenAdapterEvent, { type: T }>));
      if (hit) { clearTimeout(timer); resolve(hit); } else setTimeout(check, 5);
    };
    check();
  });
}

const turn = (thread: { id: string; threadId: string; providerSessionId: string; nativeThreadId: string }, runOrdinal = 1) => ({
  threadId: "thread-1", runId: `run-${runOrdinal}`, runOrdinal, providerThread: thread,
  message: { messageId: `msg-${runOrdinal}`, text: "Make the wheels 70 mm" },
  appContext: [{ key: "fulmetry-comment", text: "cmt:1 on part:rover/wheel-l" }],
});

describe("settings and launch", () => {
  it("launches `raven acp` in the thread's worktree", () => {
    const settings = decodeRavenSettings({ binaryPath: "/opt/raven/bin/raven", configPath: "/p/raven.json" });
    expect(buildRavenAcpSpawnInput(settings, "/wt/a")).toEqual({ command: "/opt/raven/bin/raven", args: ["acp", "--config", "/p/raven.json"], cwd: "/wt/a" });
    expect(decodeRavenSettings({})).toEqual(DEFAULT_RAVEN_SETTINGS);
  });
});

describe("Raven provider over ACP against a fake raven", () => {
  it("runs a supervised turn end to end: session, model, approval, edit, reply, terminal", async () => {
    const { runtime: rt, thread } = await open("edit", "approval-required");
    expect(rt.agentInfo).toEqual({ name: "raven", version: "0.2.7-fake" });
    expect(rt.supportsSteering).toBe(true);
    expect(thread.nativeThreadId).toMatch(/^acp:/);

    await rt.startTurn(turn(thread));
    const request = await waitFor("runtime_request.updated", (e) => e.runtimeRequest.status === "pending");
    expect(request.runtimeRequest.title).toBe("Edit wheel diameter");
    await rt.respondToRuntimeRequest({ requestId: request.runtimeRequest.id, decision: "accept" });

    const terminal = await waitFor("turn.terminal");
    expect(terminal).toMatchObject({ status: "completed", reason: "end_turn", failure: null, threadDisposition: "reusable", runOrdinal: 1 });
    expect(terminal.nativeMeta?.["fake.promptChars"]).toBeGreaterThan("Make the wheels 70 mm".length); // appContext was sent

    // The edit landed in the worktree with the model the thread selected.
    expect(readFileSync(join(worktree, "fulmetry-fake-edit.txt"), "utf8")).toContain("fake-large");
    const tool = events.filter((e) => e.type === "turn_item.updated").map((e) => e.turnItem).filter((i) => i.type === "file_change");
    expect(tool.at(-1)).toMatchObject({ status: "completed", title: "Edit wheel diameter" });
    expect(tool.at(-1)?.paths?.[0]).toContain("fulmetry-fake-edit.txt");
    const finalMessage = events.filter((e) => e.type === "message.updated").at(-1);
    expect(finalMessage).toMatchObject({ message: { text: "Resized the wheels to 70 mm.", streaming: false } });
    expect(events.some((e) => e.type === "plan.updated")).toBe(true);

    expect(events.map((e) => e.type).filter((t, i, all) => all.indexOf(t) === i)).toEqual([
      "provider_session.updated", "provider_thread.updated", "provider_turn.updated", "turn_item.updated",
      "runtime_request.updated", "plan.updated", "message.updated", "turn.terminal",
    ]);

    // The thread is reusable: a second turn on the same ACP session.
    events = [];
    await rt.startTurn(turn(thread, 2));
    const second = await waitFor("runtime_request.updated", (e) => e.runtimeRequest.status === "pending");
    await rt.respondToRuntimeRequest({ requestId: second.runtimeRequest.id, decision: "decline" });
    expect(await waitFor("turn.terminal")).toMatchObject({ status: "completed", runOrdinal: 2 });
    const declined = events.filter((e) => e.type === "turn_item.updated").map((e) => e.turnItem).filter((i) => i.type === "file_change");
    expect(declined.at(-1)?.status).toBe("failed");
  });

  it("auto-approves edits in full-access mode without a runtime request", async () => {
    const { runtime: rt, thread } = await open("edit", "full-access");
    await rt.startTurn(turn(thread));
    expect(await waitFor("turn.terminal")).toMatchObject({ status: "completed" });
    expect(events.some((e) => e.type === "runtime_request.updated")).toBe(false);
  });

  it("refuses a second prompt while one runs, steers the running turn, then interrupts it", async () => {
    const { runtime: rt, thread } = await open("slow", "full-access");
    await rt.startTurn(turn(thread));
    await expect(rt.startTurn(turn(thread, 2))).rejects.toThrow("previous_turn_active");
    await waitFor("message.updated");
    expect(await rt.steerTurn({ providerThread: thread, text: "use 72 mm instead" })).toBe("injected");
    await rt.interruptTurn({ providerThread: thread });
    expect(await waitFor("turn.terminal")).toMatchObject({ status: "interrupted", reason: "cancelled", failure: null });
    expect(await rt.steerTurn({ providerThread: thread, text: "too late" })).toBe("no_turn");
  });
});
