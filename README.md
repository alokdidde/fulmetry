# Fulmetry

A native desktop workbench for making real things: robots, printed parts, boards. You point at the model,
talk to one project chat, and every comment starts an agent that edits the design and saves a revision.

- Build plan: https://claude.ai/artifact/Adh6epG9v3qNBFF5R7x5tM
- UI spec: https://claude.ai/artifact/FjREk8mBR1MS2DFdgiHiEW
- Technical stack: https://claude.ai/artifact/JqcDcwbQwjAiYJkRFfFPAw

**Status: M0 (foundations).** Contracts, fakes and conformance tests are in. Real modules land stream by stream.

## Layout

```
contracts/      schemas shared by every stream (manifest, worker RPC, agent wire)
app/            C++20 desktop app
  core/         interfaces and events (header-only contracts)
  project/      manifest load/save                     S1
  viewport/     Wicked Engine stage                    S2
  cad/          OCCT kernel                            S3
  sim/          MuJoCo                                 S4
  ui/           Raven GUI panels                       S5
  sheets/       KiCad schematic and pinout             S6
  net/          T3 Code and worker clients             S7
  shell/        composition root (main)                integrator
  tests/        fakes, conformance suites, unit tests
worker/         Python engine worker, JSON-RPC         S8
agent/          T3 Code fork, Raven provider, MCP toolkits   S7, S9
fixtures/rover/ the desk rover every test uses
tools/          ownership check, stream map            S10
```

## Build and test (fakes only, no GPU needed)

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
./build/dev/app/shell/fulmetry --project fixtures/rover/fulmetry.project.json --comment --render rover.ppm

pip install -e 'worker[test]' && (cd worker && pytest -q)
```

The full build (`--preset full`) turns on Raven GUI, Wicked, OCCT and MuJoCo through vcpkg as each stream lands.

## Working here

Read [AGENTS.md](AGENTS.md) before changing anything. Each stream owns its directories; interfaces change
only through contract PRs.
