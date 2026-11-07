# Fulmetry maker tools (S9)

An MCP server (stdio) that gives agents one tool per engine worker method. At startup it reads
`contracts/worker/*.json` and registers each method as a tool: the name is the method with `.` replaced by `_`
(`cad.build` is `cad_build`), and the input schema is the contract's `params` schema. A call is forwarded to the
Python worker over JSON-RPC; the result comes back as JSON text, and a JSON-RPC error comes back as an `isError`
result carrying the code and message (including "not implemented" for methods of later milestones).

```sh
pnpm install && pnpm build && pnpm test   # tests spawn the real worker: pip install -e 'worker[test]'
node dist/cli.js --toolkits cad,print       # serve a subset; default is every toolkit
```

| Setting | Default |
| --- | --- |
| `--toolkits` / `FULMETRY_TOOLKITS` | all of `cad, ecad, print, sim, project, worker` |
| `--contracts` / `FULMETRY_CONTRACTS_DIR` | `<repo>/contracts/worker` |
| `FULMETRY_WORKER_PYTHON` | `python3` |
| `FULMETRY_WORKER_CWD` | `<repo>/worker` |
| `FULMETRY_WORKER_TIMEOUT_MS` | `120000` |

The `WorkerClient` keeps one worker alive, multiplexes requests by id, fails a call after its timeout (and replaces
the worker, since it answers in order), and restarts a worker that dies, backing off on a crash loop.

Client-side error codes, beside the worker's own: `-32010` timeout, `-32011` worker exited before answering,
`-32012` worker could not be started.
