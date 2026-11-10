# json-render examples

[json-render](https://github.com/vercel-labs/json-render) specs the clock compiles into
custom faces (spec 047). Send one like any face:

```bash
python plugins/ai-agent-monitor/skills/ai-agent-monitor/scripts/clock.py push ci examples/json-render/ci.json --show
```

The clock's catalog, the components and props it draws, is `GET /api/catalog` (and the
prompt for a model, `GET /api/catalog/prompt`; both gzipped: `curl --compressed`), also
in [integrations/json-render/](../../integrations/json-render/).

| File | What | Variables |
|---|---|---|
| `ci.json` | A CI run: repository, branch, progress, passed and failed counts | `repo`, `branch`, `build`, `pct`, `passed`, `failed` |
| `meeting.json` | The next meeting and a countdown | `meeting`, `room`, `mins` |
| `weather.json` | Now and the next three days, from the clock's own weather | none |
| `build.json` | A build's branch, state and progress | `branch`, `build`, `progress` |
| `metrics.json` | Counters, a sparkline and a bar chart | `deploys` (seeded by the spec's `state`) |
| `status.json` | Status lines and a timeline | none |

`samples.json` holds the values the integration tests push with each one.

These six exercise the compiler (one component of each kind). Faces made for a use, with
demo values, are in [gallery/](gallery/README.md) and in the face gallery.
