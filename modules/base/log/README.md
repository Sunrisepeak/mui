# openmui.log

Independent logging and invariants in C++23 modules. The runtime package
uses only the standard library: no UI, specification, renderer, trace or
hanim dependency. The existing `omui` namespace and diagnostic API are
preserved; `openmui.core:diag` re-exports these same entities for compatibility.

```cpp
import openmui.log;

constexpr omui::Log objects{"hanim.object"};
objects.debug("prepared {} objects", 12);
omui::invariant(12 > 0, "a prepared batch must be nonempty");
```

`Log` filters a category before formatting. The global level helpers, the
replaceable `LogSink`, caller `std::source_location`, per-category filters,
bounded record history and `to_json(LoggedRecord)` retain their existing
behavior. The default sink writes warnings and errors to standard error.
A sink may call back into the logging API; records reach history before the
sink runs, without holding the history mutex across the callback.

An unsuccessful `invariant` or an explicit `fatal` reports its location and
terminates via `std::abort`; these are programming errors, not recoverable
results. Logging with formatting and history may allocate. This extraction
adds no new performance or allocation guarantee and introduces no second
logger. Hosts remain responsible for reading environment configuration and
installing sinks or filters.

Run `mcpp test -p openmui.log` for independent diagnostics CTS, and
`mcpp test -p openmui.core diag_test` for the existing runtime facade
checks. The log and trace packages remain independently usable. Native failure probes
start a bounded subprocess through the test-only `mcpplibs.os` dependency;
Emscripten runs the nonterminating invariant checks without subprocess support.
