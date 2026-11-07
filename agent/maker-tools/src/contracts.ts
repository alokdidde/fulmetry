/** Reads the worker method contracts in contracts/worker and turns each into an MCP tool definition. */
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

/** agent/maker-tools, whether this file runs from src/ (tests) or dist/ (built). */
export const PACKAGE_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const REPO_ROOT = resolve(PACKAGE_ROOT, "..", "..");
export const DEFAULT_CONTRACTS_DIR = join(REPO_ROOT, "contracts", "worker");

export type JsonSchema = { type: "object"; [key: string]: unknown };

export interface MethodContract {
  method: string;
  since: string;
  params: JsonSchema;
  result: Record<string, unknown>;
}

export interface ToolDef {
  name: string;
  method: string;
  toolkit: string;
  since: string;
  description: string;
  inputSchema: JsonSchema;
}

/** The toolkits a contract method can belong to: the part of its name before the first dot. */
export const KNOWN_TOOLKITS = ["cad", "ecad", "print", "sim", "project", "worker"] as const;

export const toolName = (method: string): string => method.replaceAll(".", "_");
export const toolkitOf = (method: string): string => method.split(".", 1)[0] ?? method;

export function loadContracts(dir: string = DEFAULT_CONTRACTS_DIR): MethodContract[] {
  const files = readdirSync(dir).filter((f) => f.endsWith(".json")).sort();
  if (files.length === 0) throw new Error(`no worker contracts in ${dir}`);
  return files.map((f) => {
    const c = JSON.parse(readFileSync(join(dir, f), "utf8")) as Partial<MethodContract>;
    if (typeof c.method !== "string" || typeof c.since !== "string" || c.params?.type !== "object") {
      throw new Error(`${join(dir, f)}: a worker contract needs method, since and an object params schema`);
    }
    return c as MethodContract;
  });
}

export function toolFor(c: MethodContract): ToolDef {
  const toolkit = toolkitOf(c.method);
  return {
    name: toolName(c.method),
    method: c.method,
    toolkit,
    since: c.since,
    description:
      `Calls the Fulmetry engine worker method ${c.method} (${toolkit} toolkit, since milestone ${c.since}). ` +
      `Arguments follow contracts/worker/${c.method}.json; the result is the method's JSON result. ` +
      `A method whose milestone has not landed answers "not implemented".`,
    inputSchema: c.params,
  };
}

/**
 * Builds the tool list, keeping only the given toolkits when a subset is asked for.
 * Throws on a toolkit name that no contract uses, so a typo in --toolkits fails loudly.
 */
export function buildTools(contracts: MethodContract[], toolkits?: readonly string[]): ToolDef[] {
  const all = contracts.map(toolFor);
  if (!toolkits || toolkits.length === 0) return all;
  const present = new Set(all.map((t) => t.toolkit));
  const unknown = toolkits.filter((t) => !present.has(t));
  if (unknown.length > 0) {
    throw new Error(`unknown toolkit ${unknown.join(", ")}; available: ${[...present].sort().join(", ")}`);
  }
  const wanted = new Set(toolkits);
  return all.filter((t) => wanted.has(t.toolkit));
}
