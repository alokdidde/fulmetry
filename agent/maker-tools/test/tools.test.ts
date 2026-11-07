import { describe, expect, it } from "vitest";
import { buildTools, loadContracts, toolName } from "../src/contracts.js";
import { WorkerRpcError } from "../src/worker-client.js";
import { connect, rawContracts, text } from "./helpers.js";

const noWorker = { request: async () => ({}) };

describe("tool list", () => {
  it("has exactly one tool per contracts/worker method, with its params schema", async () => {
    const mcp = await connect(noWorker);
    const { tools } = await mcp.client.listTools();
    const contracts = rawContracts();

    expect(tools.map((t) => t.name).sort()).toEqual(contracts.map((c) => toolName(c.method)).sort());
    for (const c of contracts) {
      const tool = tools.find((t) => t.name === c.method.replaceAll(".", "_"))!;
      expect(tool.inputSchema).toEqual(c.params);
      expect(tool.description).toContain(c.method);
      expect(tool.description).toContain(c.since);
    }
    expect(tools.map((t) => t.name)).toContain("cad_build");
    expect(tools.map((t) => t.name)).toContain("sim_exportMjcf");
    await mcp.close();
  });

  it("keeps only the enabled toolkits", async () => {
    const mcp = await connect(noWorker, ["worker", "project"]);
    const { tools } = await mcp.client.listTools();
    expect(tools.map((t) => t.name).sort()).toEqual(["project_validate", "worker_ping"]);

    const r = await mcp.call("cad_build", { projectRoot: ".", part: "part:rover/chassis" });
    expect(r.isError).toBe(true);
    expect(text(r)).toContain("unknown tool cad_build");
    await mcp.close();
  });

  it("groups every method under one of the known toolkits and rejects unknown ones", () => {
    const contracts = loadContracts();
    const toolkits = new Set(buildTools(contracts).map((t) => t.toolkit));
    expect([...toolkits].sort()).toEqual(["cad", "ecad", "print", "project", "sim", "worker"]);
    expect(() => buildTools(contracts, ["cad", "kad"])).toThrow(/unknown toolkit kad/);
  });

  it("turns a worker error into an isError result with code, message and data", async () => {
    const worker = {
      request: async () => {
        throw new WorkerRpcError(-32603, "engine fell over", { hint: "x" });
      },
    };
    const mcp = await connect(worker);
    const r = await mcp.call("worker_ping");
    expect(r.isError).toBe(true);
    expect(text(r)).toMatch(/worker\.ping failed with code -32603: engine fell over/);
    expect(text(r)).toContain('"hint": "x"');
    await mcp.close();
  });
});
