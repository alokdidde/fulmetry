#!/usr/bin/env node
// Stages T3 Code's server for the app bundle: the packed bin.mjs and its chunks, plus the runtime packages the bundle
// leaves external (native addons and the like), at the versions the workspace installed.
//   node tools/package/t3-stage.mjs <t3code checkout> <out dir>   (Node 22 also needs --experimental-strip-types)
// Run it after `vp pack` in <t3code>/apps/server, with the same OS and CPU as the app (the addons are native).
import { execFileSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [t3, out] = process.argv.slice(2).map((p) => resolve(p));
if (!t3 || !out) throw new Error("usage: t3-stage.mjs <t3code checkout> <out dir>");
const server = join(t3, "apps/server");
const dist = join(server, "dist");
if (!existsSync(join(dist, "bin.mjs"))) throw new Error(`no ${dist}/bin.mjs: run vp pack in apps/server first`);

const { selectCliRuntimeExternalDependencies } = await import(
  pathToFileURL(join(t3, "scripts/lib/cli-external-packages.ts")).href
);
const pkg = JSON.parse(readFileSync(join(server, "package.json"), "utf8"));
const wanted = selectCliRuntimeExternalDependencies(pkg.dependencies ?? {});
const dependencies = {};
for (const name of Object.keys(wanted)) {
  const installed = join(server, "node_modules", name, "package.json");
  if (!existsSync(installed)) throw new Error(`${name} is not installed in ${server}`);
  dependencies[name] = JSON.parse(readFileSync(installed, "utf8")).version;
}

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
for (const f of readdirSync(dist)) if (!f.endsWith(".map")) cpSync(join(dist, f), join(out, f), { recursive: true });
writeFileSync(
  join(out, "package.json"),
  JSON.stringify({ name: "t3-fulmetry", private: true, type: "module", version: pkg.version, dependencies }, null, 2) + "\n",
);
execFileSync("npm", ["install", "--omit=dev", "--no-audit", "--no-fund", "--no-package-lock"], { cwd: out, stdio: "inherit" });
console.log(`staged T3 Code ${pkg.version} in ${out} with ${Object.keys(dependencies).join(", ")}`);
