#!/usr/bin/env node
/**
 * fulmetry-maker-tools [--toolkits cad,worker] [--contracts DIR]
 *
 * Serves the Fulmetry maker toolkits over MCP stdio. Worker settings come from the environment:
 * FULMETRY_WORKER_PYTHON, FULMETRY_WORKER_CWD, FULMETRY_WORKER_TIMEOUT_MS.
 * FULMETRY_TOOLKITS is used when --toolkits is not given.
 */
import { parseArgs } from "node:util";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { createMakerToolsServer } from "./server.js";
import { WorkerClient } from "./worker-client.js";

export function parseToolkits(value: string | undefined): string[] | undefined {
  const list = value?.split(",").map((s) => s.trim()).filter(Boolean);
  return list && list.length > 0 ? list : undefined;
}

async function main(): Promise<void> {
  const { values } = parseArgs({
    options: {
      toolkits: { type: "string" },
      contracts: { type: "string" },
      help: { type: "boolean", short: "h" },
    },
  });
  if (values.help) {
    process.stdout.write("usage: fulmetry-maker-tools [--toolkits cad,ecad,print,sim,project,worker] [--contracts DIR]\n");
    return;
  }
  const worker = new WorkerClient();
  const { server, tools } = createMakerToolsServer({
    worker,
    contractsDir: values.contracts ?? process.env.FULMETRY_CONTRACTS_DIR,
    toolkits: parseToolkits(values.toolkits ?? process.env.FULMETRY_TOOLKITS),
  });
  process.stderr.write(`fulmetry-maker-tools: ${tools.map((t) => t.name).join(", ")}\n`);

  const shutdown = async () => {
    await server.close().catch(() => {});
    await worker.close();
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
  server.onclose = () => void worker.close().then(() => process.exit(0));

  worker.start();
  await server.connect(new StdioServerTransport());
}

main().catch((e) => {
  process.stderr.write(`fulmetry-maker-tools: ${e instanceof Error ? e.message : String(e)}\n`);
  process.exit(1);
});
