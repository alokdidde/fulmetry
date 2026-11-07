import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { DEFAULT_CONTRACTS_DIR, REPO_ROOT } from "../src/contracts.js";
import { createMakerToolsServer, type WorkerLike } from "../src/server.js";

export const FIXTURE_MANIFEST = join(REPO_ROOT, "fixtures", "rover", "fulmetry.project.json");

/** The contracts as raw files, read independently of src/contracts.ts. */
export function rawContracts(): Array<{ method: string; since: string; params: unknown }> {
  return readdirSync(DEFAULT_CONTRACTS_DIR)
    .filter((f) => f.endsWith(".json"))
    .map((f) => JSON.parse(readFileSync(join(DEFAULT_CONTRACTS_DIR, f), "utf8")));
}

export async function connect(worker: WorkerLike, toolkits?: string[]) {
  const { server } = createMakerToolsServer({ worker, toolkits });
  const [clientSide, serverSide] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: "maker-tools-test", version: "0.0.0" });
  await Promise.all([server.connect(serverSide), client.connect(clientSide)]);
  return {
    client,
    call: (name: string, args: Record<string, unknown> = {}) =>
      client.callTool({ name, arguments: args }) as Promise<CallToolResult>,
    close: async () => {
      await client.close();
      await server.close();
    },
  };
}

export function text(r: CallToolResult): string {
  const first = r.content[0];
  if (first?.type !== "text") throw new Error(`expected text content, got ${JSON.stringify(r.content)}`);
  return first.text;
}
