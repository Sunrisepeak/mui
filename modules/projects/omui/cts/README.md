# omui.cts

The harness of the specification's conformance level S ([spec/conformance/S](../../../../spec/conformance/S)) for omui's languages. Every language runs the same scenarios through its own builders and API and must record the trace pinned beside each. The scenarios and traces are the specification's (data alone); this package runs them, on [mlibs.cts](../../../base/cts):

```cpp
const auto report = omui::cts::conform([&](const omui::cts::Scenario& s) {
    return omui::cts::run(s, invoke, settle);   // the language runner's verbs cts.load and cts.step
}, {.subject = "ts"});
check(report && report->ok(), report ? report->text() : report.error());
```

One case a scenario (`openmui.S.<name>`, level S, contract `openmui.conformance.S@0.1`): its trace compared, after `canonical`, with the pinned one. The TypeScript and JavaScript tests run it (the Lua and Python runners went with their bindings, D-138). With `OMUI_CTS_WRITE=1` the pinned traces are written instead of compared. A dev-dependency only: no artifact carries it.
