# openmui-pipeline

A frame as a graph of stages (D-66). A **stage** is a plugin that reads and
writes named, typed **resources**; a **pipeline** is a list of stages built
against a **registry** of plugins.

- **Checked builds.** Every resource a stage reads is written by a stage
  before it or provided by the caller; the types agree; a resource has one
  producer and any number of updaters (stages that read and write it); `after`
  constraints have no cycle. Each problem says where it is and how to fix it
  (`to_text`).
- **Deterministic order.** Data first, then the order of declaration. An
  updater's place in the declaration decides which readers see what it
  changed: a reader declared before it sees the old value.
- **Observable.** Each run times every stage (`timings()`) and traces it as a
  span in its plugin's category (openmui-trace); `describe()` lists the
  order, each stage's plugin and its ports.
- **Live.** `replace`, `insert` and `remove` rebuild between runs; a change
  that does not build leaves the pipeline as it was.
- **Typed without RTTI.** A type names itself through `resource_type<T>`;
  `Resources::get<T>` returns nothing for the wrong type.

```cpp
template <> struct omui::pipeline::resource_type<DisplayList> { static constexpr std::string_view name = "render.DisplayList"; };

struct Badge final : omui::pipeline::Stage {
    std::vector<Port> reads() const override  { return {port<DisplayList>("display")}; }
    std::vector<Port> writes() const override { return {port<DisplayList>("display")}; }   // an updater
    std::expected<void, std::string> run(Context& c) override { c.resources.get<DisplayList>("display")->fill_rect(…); return {}; }
};
registry.add({.name = "app.badge"}, [](const Config&) { return std::make_unique<Badge>(); });
```

Stages are called once per run, never per node or per pixel (D-65).
`<name>.pipeline.omui.toml` is the file form (`to_toml`); it is read at the
edges — by the generator at build time, and as JSON by tools over the agent
protocol — so this package parses nothing and throws nothing.

The runtime's frame is a pipeline of `openmui-view.*` stages
(`runtime_pipeline(Stages)`: H0, H1, H2), and `omui agent pipeline` shows and
changes a running application's.

An independent component (D-55): the standard library and openmui-trace.
