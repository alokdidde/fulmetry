# Contracts

The only places where streams meet. Owned by the integrator; changed only through `contract/<name>` PRs
that update the schema or header, its fake, and its conformance test together.

| Contract | Where | Conformance |
| --- | --- | --- |
| Project manifest | `schemas/project.schema.json` | `worker/tests/test_contracts.py`, `app/tests/unit/test_project.cpp` |
| Scene and picking | `app/core/include/fulmetry/core/IViewport.h` | `app/tests/conformance/ViewportConformance.h` |
| CAD kernel | `app/core/.../ICadKernel.h` | `app/tests/conformance/CadKernelConformance.h` |
| Simulation | `app/core/.../ISimulation.h` | `app/tests/conformance/SimulationConformance.h` |
| App events | `app/core/.../Events.h` | `app/tests/unit/test_events.cpp` |
| Agent wire | `agent/fulmetry-ext.schema.json` + T3 Code `packages/contracts` | `app/tests/conformance/AgentClientConformance.h` |
| Worker RPC | `worker/<method>.json` (JSON-RPC 2.0), shaped by `schemas/worker-method.schema.json` | `worker/tests/test_contracts.py` |

Version: `fulmetry::kContractsVersion` in `app/core/include/fulmetry/core/Version.h`. Bump on breaking changes.
