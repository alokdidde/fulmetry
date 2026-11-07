/** The MCP server: one tool per worker contract method, each call forwarded to the engine worker. */
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  type CallToolResult,
} from "@modelcontextprotocol/sdk/types.js";
import { buildTools, DEFAULT_CONTRACTS_DIR, loadContracts, type ToolDef } from "./contracts.js";
import { WorkerRpcError } from "./worker-client.js";

export const SERVER_NAME = "fulmetry-maker-tools";
export const SERVER_VERSION = "0.0.1";

/** What a tool call needs from the worker; WorkerClient is the real one. */
export interface WorkerLike {
  request(method: string, params: Record<string, unknown>): Promise<unknown>;
}

export interface MakerToolsOptions {
  worker: WorkerLike;
  contractsDir?: string;
  /** Toolkits to expose (cad, ecad, print, sim, project, worker). Default: all. */
  toolkits?: readonly string[];
}

export function createMakerToolsServer(opts: MakerToolsOptions): { server: Server; tools: ToolDef[] } {
  const tools = buildTools(loadContracts(opts.contractsDir ?? DEFAULT_CONTRACTS_DIR), opts.toolkits);
  const byName = new Map(tools.map((t) => [t.name, t]));

  const server = new Server(
    { name: SERVER_NAME, version: SERVER_VERSION },
    {
      capabilities: { tools: {} },
      instructions:
        "Fulmetry maker tools act on the open design through the engine worker. Tool names are worker " +
        "methods with dots as underscores (cad.build is cad_build). Paths and ids follow the project manifest; " +
        "units are mm, g, s and degrees.",
    },
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: tools.map((t) => ({ name: t.name, description: t.description, inputSchema: t.inputSchema })),
  }));

  server.setRequestHandler(CallToolRequestSchema, async (req): Promise<CallToolResult> => {
    const tool = byName.get(req.params.name);
    if (!tool) {
      return errorResult(`unknown tool ${req.params.name}; enabled: ${[...byName.keys()].join(", ")}`);
    }
    try {
      const result = await opts.worker.request(tool.method, req.params.arguments ?? {});
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    } catch (e) {
      if (e instanceof WorkerRpcError) {
        const data = e.data === undefined ? "" : `\n${JSON.stringify(e.data, null, 2)}`;
        return errorResult(`${tool.method} failed with code ${e.code}: ${e.message}${data}`);
      }
      return errorResult(`${tool.method} failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  });

  return { server, tools };
}

function errorResult(text: string): CallToolResult {
  return { isError: true, content: [{ type: "text", text }] };
}
