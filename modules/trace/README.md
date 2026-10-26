# openmui-trace

Where the time goes. Spans (begin at construction, end at scope exit),
counters and instant marks, recorded per thread into fixed buffers — no lock
and no allocation per event; while tracing is off a span costs one relaxed
atomic load. A trace exports as Chrome trace event JSON (open it in Perfetto
or `chrome://tracing`) and summarises per span: count, total, mean, p50, p95,
p99 and max (nearest rank).

```cpp
import openmui.trace;

omui::trace::start();
{ omui::trace::Span s("layout", "frame"); /* … */ }
auto t = omui::trace::stop();
std::print("{}", t.table());
write_file("frame.trace.json", t.chrome_json());
```

The runtime traces each frame's phases, hosts add render and present, and
`omui agent profile` records a running application. An independent component
(D-55): the standard library only.
