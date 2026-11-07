# @fulmetry/provider-raven

EverMind Raven as a T3 Code provider. Owned by S7; findings and plan are in
[docs/streams/s7.md](../../docs/streams/s7.md).

Raven serves the Agent Client Protocol itself (`raven acp`, newline-delimited JSON-RPC on stdio), so this
provider is an ACP client in the same shape as T3 Code's `packages/provider-kiro`:

| File | T3 Code counterpart |
| --- | --- |
| `src/settings.ts` | `provider-kiro/src/settings.ts` (`KiroSettings`) |
| `src/server/acpSupport.ts` | `provider-kiro/src/server/acpSupport.ts` (launch args, permission policy) |
| `src/server/adapter.ts` | `provider-kiro/src/server/adapter.ts` + `provider-core/.../ProviderAdapter.ts` (`ProviderAdapterV2SessionRuntime`) |
| `src/server/acp.ts` | `packages/effect-acp` (wire types and JSON-RPC peer) |
| `test/fake-raven.mjs` | a scripted stand-in for `raven acp`, no model needed |

It runs standalone (Promises, no Effect, no T3 workspace) until the T3 Code fork exists. Then the Raven
specifics move into an `AcpAdapterV2Flavor` passed to `makeAcpAdapterV2`, and `acp.ts` is dropped in
favour of `effect-acp`.

```sh
pnpm install && pnpm test && pnpm typecheck
```
