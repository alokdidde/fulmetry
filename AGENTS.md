# Rules for agents (and people)

Fulmetry is built by parallel agent streams. These rules keep them from colliding.

## Streams

Each stream owns directories, listed in [tools/streams.json](tools/streams.json). CI rejects a PR whose
branch prefix does not own every file it changes (`tools/check_ownership.py`).

| Branch prefix | Stream | Owns |
| --- | --- | --- |
| `int/` | Integrator | everything; wires modules into `app/shell`, keeps `main` green |
| `contract/` | Contract change | `contracts/`, `app/core/`, fakes, conformance suites |
| `s1/` | Project model | `app/project/` |
| `s2/` | Viewport | `app/viewport/` |
| `s3/` | CAD kernel | `app/cad/` |
| `s4/` | Simulation | `app/sim/` |
| `s5/` | Workbench UI | `app/ui/` |
| `s6/` | Sheets | `app/sheets/`, `fixtures/rover/kicad/` |
| `s7/` | Agent bridge | `app/net/`, `agent/provider-raven/`, `agent/t3code` |
| `s8/` | Engine worker | `worker/`, `fixtures/rover/cad/` |
| `s9/` | Toolkits | `agent/maker-tools/` |
| `s10/` | Build and CI | root CMake files, `vcpkg.json`, `.github/`, `tools/`, `fixtures/`, `third_party/` |

Every stream also owns `docs/streams/s<N>.md`: its current task, blockers, decisions and the checks it added.

## The rules

1. **One agent, one stream, one worktree.** Branch `s<N>/<task>`. Never commit outside your stream.
2. **Shared files have one owner.** Need a dependency or a CMake option at the root? Ask S10 in your stream doc.
   Inside your own module directory, your `CMakeLists.txt` is yours.
3. **Contracts change first.** Need an interface change? Open `contract/<name>`: header or schema, fake and
   conformance suite together, `kContractsVersion` bumped if it breaks. It merges before code that uses it.
4. **Fakes stay honest.** Your real module must pass the same conformance suite as its fake
   (`app/tests/conformance/`). Add a test case in your module that calls it with your factory.
5. **Every task names its check before work starts**, and the check runs in CI. No skipped or retried-until-green
   tests; a flaky test is fixed or removed within a day.
6. **Small PRs.** About 800 changed lines or fewer, tests included. Green on Windows, macOS and Linux.
7. **Merge order:** contracts, then modules, then shell wiring. Rebase on `main` before merging.
8. **Stop on red.** A broken `main` blocks merges until the integrator fixes or reverts it.
9. **Heavy engines are opt-in.** A module's real implementation builds only when its `FULMETRY_WITH_*` option is
   ON, so everyone else keeps building and testing against fakes.

## Conventions

- C++20, no exceptions across module boundaries: return `fulmetry::Result<T>` / `Status`.
- Ids are strings (`obj:rover`, `part:rover/wheel-l`), never pointers.
- Units: mm, g, s, degrees in data; radians only inside math.
- Licences: GPL and AGPL engines (KiCad, OrcaSlicer, CalculiX) live in the worker process only, never linked into the app.
