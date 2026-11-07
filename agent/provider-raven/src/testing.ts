// A stand-in for `raven acp` that needs no model, for this package's tests and for S7's app/net tests.
import { fileURLToPath } from "node:url";

import type { AcpSpawnInput } from "./server/acpSupport.ts";

export const FAKE_RAVEN_PATH = fileURLToPath(new URL("../test/fake-raven.mjs", import.meta.url));

/** Swap the `raven` launch for the fake, keeping cwd and env (RavenAdapterOptions.spawnOverride). */
export function fakeRavenSpawn(script: string): (input: AcpSpawnInput) => AcpSpawnInput {
  return (input) => ({
    ...input,
    command: process.execPath,
    args: [FAKE_RAVEN_PATH, ...input.args.slice(1)],
    env: { ...process.env, ...input.env, FAKE_RAVEN_SCRIPT: script },
  });
}
