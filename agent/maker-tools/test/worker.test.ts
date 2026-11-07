/** These tests run the real Python worker (pip install -e 'worker[test]', or just python3 with jsonschema). */
import { once } from "node:events";
import { readFileSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ClientCode, RpcCode, WorkerClient, WorkerRpcError } from "../src/worker-client.js";
import { connect, FIXTURE_MANIFEST, text } from "./helpers.js";

const stderr: string[] = [];
let worker: WorkerClient;
let mcp: Awaited<ReturnType<typeof connect>>;
const unexpected = (): never => {
  throw new Error("expected the call to fail");
};

beforeAll(async () => {
  worker = new WorkerClient({ timeoutMs: 10_000, restartDelayMs: 20, onStderr: (l) => stderr.push(l) });
  mcp = await connect(worker);
});

afterAll(async () => {
  await mcp?.close();
  await worker?.close();
});

describe("through a real worker process", () => {
  it("worker_ping round-trips", async () => {
    const r = await mcp.call("worker_ping");
    expect(r.isError).toBeFalsy();
    const result = JSON.parse(text(r));
    expect(typeof result.version).toBe("string");
    expect(result.engines).toBeTypeOf("object");
    expect(worker.pid).toBeGreaterThan(0);
  });

  it("project_validate says the rover fixture is valid", async () => {
    const manifest = JSON.parse(readFileSync(FIXTURE_MANIFEST, "utf8"));
    const r = await mcp.call("project_validate", { manifest });
    expect(r.isError).toBeFalsy();
    expect(JSON.parse(text(r))).toEqual({ valid: true, errors: [] });
  });

  it("print_slice is an isError result saying not implemented", async () => {
    const r = await mcp.call("print_slice", { projectRoot: ".", part: "part:rover/wheel-l", material: "PLA" });
    expect(r.isError).toBe(true);
    expect(text(r)).toContain(`code ${RpcCode.NOT_IMPLEMENTED}`);
    expect(text(r)).toMatch(/not implemented/);
  });

  it("rejects params that break the contract", async () => {
    const missing = await mcp.call("project_validate", {});
    expect(missing.isError).toBe(true);
    expect(text(missing)).toContain(`code ${RpcCode.INVALID_PARAMS}`);
    expect(text(missing)).toContain("'manifest' is a required property");

    const badEnum = await mcp.call("print_slice", { projectRoot: ".", part: "p", material: "wood" });
    expect(badEnum.isError).toBe(true);
    expect(text(badEnum)).toContain(`code ${RpcCode.INVALID_PARAMS}`);
    expect(text(badEnum)).toContain("/material");

    const extra = await mcp.call("worker_ping", { surprise: 1 });
    expect(extra.isError).toBe(true);
    expect(text(extra)).toContain(`code ${RpcCode.INVALID_PARAMS}`);
  });

  it("multiplexes concurrent calls by id", async () => {
    const results = await Promise.all(Array.from({ length: 20 }, () => worker.request("worker.ping")));
    expect(results).toHaveLength(20);
    for (const r of results) expect(r).toHaveProperty("version");
  });

  it("restarts a worker killed mid-session, and the next call succeeds", async () => {
    await mcp.call("worker_ping");
    const before = worker.pid!;
    const restarts = worker.restarts;

    const respawned = once(worker, "spawn");
    process.kill(before, "SIGKILL");
    const [pid] = await respawned;

    expect(pid).not.toBe(before);
    expect(worker.restarts).toBe(restarts + 1);
    const r = await mcp.call("worker_ping");
    expect(r.isError).toBeFalsy();
    expect(JSON.parse(text(r))).toHaveProperty("version");
  });

  it("fails a call in flight when the worker dies, then recovers", async () => {
    // A worker that reads the request and dies without answering.
    const dying = new WorkerClient({
      args: ["-c", "import sys; sys.stdin.readline(); sys.exit(3)"],
      restartDelayMs: 20,
      onStderr: () => {},
    });
    const err = await dying.request("worker.ping").then(unexpected, (e: WorkerRpcError) => e);
    expect(err).toBeInstanceOf(WorkerRpcError);
    expect(err.code).toBe(ClientCode.WORKER_EXITED);
    expect(err.message).toMatch(/worker exited \(code 3\)/);
    await dying.close();
  });

  it("times out a call the worker never answers", async () => {
    const silent = new WorkerClient({
      args: ["-c", "import sys, time; sys.stdin.readline(); time.sleep(30)"],
      timeoutMs: 200,
      restartDelayMs: 20,
      onStderr: () => {},
    });
    const err = await silent.request("worker.ping").then(unexpected, (e: WorkerRpcError) => e);
    expect(err).toBeInstanceOf(WorkerRpcError);
    expect(err.code).toBe(ClientCode.TIMEOUT);
    await silent.close();
  });

  it("reports a worker that cannot start", async () => {
    const broken = new WorkerClient({ python: "/nonexistent/python3", onStderr: () => {} });
    const err = await broken.request("worker.ping").then(unexpected, (e: WorkerRpcError) => e);
    expect(err).toBeInstanceOf(WorkerRpcError);
    expect(err.code).toBe(ClientCode.WORKER_UNAVAILABLE);
    await broken.close();
  });
});
