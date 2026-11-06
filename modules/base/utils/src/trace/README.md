# mlibs.trace

Where the time goes. Spans (begin at construction, end at scope exit),
counters and instant marks, recorded per thread into fixed buffers — no lock
and no allocation per event; while tracing is off a span costs one relaxed
atomic load. A trace exports as Chrome trace event JSON (open it in Perfetto
or `chrome://tracing`) and summarises per span: count, total, mean, p50, p95,
p99 and max (nearest rank).

```cpp
import mlibs.trace;

mlibs::trace::start();
{ mlibs::trace::Span s("layout", "frame"); /* … */ }
auto t = mlibs::trace::stop();
std::print("{}", t.table());
write_file("frame.trace.json", t.chrome_json());
```

The runtime traces each frame's phases, hosts add render and present, and
`omui agent profile` records a running application. An independent component
(D-55): the standard library only.

The package and public import are `mlibs.trace`. The legacy
`import openmui.trace;` and `omui::trace` API remain source-compatible
within this same package; rebuild C++ consumers. Both entry points share the
same types, implementation and state. No extra compatibility package is needed.
See the [namespace migration guide](../../../../../.agents/docs/2026-11-05-product-namespace-migration-design.md#5-n1首组五个基础库的实际迁移).
