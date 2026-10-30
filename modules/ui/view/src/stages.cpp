// The runtime's frame as stage plugins (D-66): each phase of §3.1 of the plan
// is a stage that reads and writes the frame's resources, so a pipeline can
// leave stages out (H0, H1), add its own between them, or replace one.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render.vector;
import openmui.pipeline;
import :impl;

namespace omui {

namespace {

namespace pl = pipeline;
namespace res = frame_resource;

using Body = void (*)(Runtime::Impl& d, pl::Context& c);

// A runtime stage: its ports and a function of the runtime's state.
class RuntimeStage final : public pl::Stage {
public:
    RuntimeStage(Runtime::Impl& d, std::vector<pl::Port> reads, std::vector<pl::Port> writes, Body body)
        : d_(d), reads_(std::move(reads)), writes_(std::move(writes)), body_(body) {}
    [[nodiscard]] std::vector<pl::Port> reads() const override { return reads_; }
    [[nodiscard]] std::vector<pl::Port> writes() const override { return writes_; }
    std::expected<void, std::string> run(pl::Context& c) override {
        body_(d_, c);
        return {};
    }

private:
    Runtime::Impl&        d_;
    std::vector<pl::Port> reads_, writes_;
    Body                  body_;
};

pl::Port tree() { return pl::port<FrameTree>(std::string(res::tree)); }
pl::Port layout_port() { return pl::port<FrameLayout>(std::string(res::layout)); }
pl::Port bounds_port() { return pl::port<FrameBounds>(std::string(res::bounds)); }
pl::Port display() { return pl::port<render::DisplayList>(std::string(res::display)); }
pl::Port input_port() { return pl::port<FrameInput>(std::string(res::input)); }

bool debugging(const Runtime::Impl& d) noexcept {
    const DebugOptions& o = d.options.debug;
    return o.bounds || o.repaints || o.inspector || o.hud;
}

struct Plugin {
    std::string_view       name;  // openmui.view.<name>
    std::string_view       description;
    std::vector<pl::Port>  reads, writes;
    Body                   body;
};

std::vector<Plugin> plugins() {
    return {
        {"timers", "work posted to the UI thread, then the timers that are due", {tree()}, {tree()},
         [](Runtime::Impl& d, pl::Context&) {
             d.queue.drain();
             d.run_timers();
         }},
        {"input", "the frame's input events: hit testing, handlers, actions", {input_port(), tree()}, {tree()},
         [](Runtime::Impl& d, pl::Context& c) {
             const FrameInput* in = c.resources.get<FrameInput>(res::input);
             if (!in) return;
             for (const InputEvent& e : in->events) batch([&] { d.dispatch(e); });
         }},
        {"regions", "the regions whose sources changed are rebuilt and reconciled by key", {tree()}, {tree()},
         [](Runtime::Impl& d, pl::Context&) { d.reconcile_regions(); }},
        {"animations", "transitions and animations step to the frame's time; exits finish", {tree()}, {tree()},
         [](Runtime::Impl& d, pl::Context&) {
             d.run_animations();
             d.finish_removals();
             d.rebuild_paths();
         }},
        {"layout", "incremental layout", {tree()}, {layout_port()},
         [](Runtime::Impl& d, pl::Context& c) {
             d.run_layout();
             c.resources.put(res::layout, &d.layout_token);
         }},
        {"bounds", "window bounds, overlays placed against their anchors, focus traps, laid_out hooks", {tree(), layout_port()}, {bounds_port()},
         [](Runtime::Impl& d, pl::Context& c) {
             // What painting asks for (a scroll view still gliding) is for the next frame.
             d.todo = d.dirty;
             d.dirty = dirty_none;
             d.dirty_taken = true;
             if ((d.todo & (dirty_paint | dirty_layout | dirty_tree)) || debugging(d)) {
                 d.compute_bounds(d.root, {0, 0}, Affine{});
                 d.place_overlays();
                 d.trap_focus();
                 // A copy (a hook may add or remove watchers), into a vector kept from frame to frame.
                 d.watching.assign(d.watchers.begin(), d.watchers.end());
                 for (NodeId w : d.watching)
                     if (d.alive(w)) {
                         auto use = d.use_theme(w);
                         d.at(w).type->laid_out(d.at(w).instance);
                     }
                 d.bounds_ran = true;
             }
             c.resources.put(res::bounds, &d.bounds_token);
         }},
        {"paint", "the display list, with node markers", {tree(), bounds_port()}, {display()},
         [](Runtime::Impl& d, pl::Context& c) {
             if (d.bounds_ran) {
                 d.paint();
                 d.repainted = true;
             }
             c.resources.put(res::display, &d.list);
         }},
        {"debug", "the development overlays over the display list (DebugOptions)", {tree(), bounds_port(), display()}, {display()},
         [](Runtime::Impl& d, pl::Context&) {
             if (d.bounds_ran && debugging(d)) d.paint_debug();
         }},
    };
}

}  // namespace

pipeline::Description runtime_pipeline(Stages stages) {
    pl::Description d;
    auto add = [&](std::string_view name) { d.stages.push_back({std::string(name), std::format("openmui.view.{}", name), {}, {}}); };
    for (std::string_view s : {"timers", "input", "regions", "animations"}) add(s);
    if (stages >= Stages::layout) {
        add("layout");
        add("bounds");
    }
    if (stages >= Stages::paint) {
        add("paint");
        add("debug");
    }
    d.name = stages == Stages::semantics ? "runtime.h0" : stages == Stages::layout ? "runtime.h1" : "runtime.h2";
    return d;
}

void Runtime::Impl::register_stages() {
    for (Plugin& p : plugins()) {
        Runtime::Impl* d = this;
        registry.add({.name = std::format("openmui.view.{}", p.name),
                      .kind = "stage",
                      .interface = "omui.pipeline.stage@1",
                      .description = std::string(p.description),
                      .trace_category = "frame"},
                     [d, reads = p.reads, writes = p.writes, body = p.body](const pl::Config&)
                         -> std::expected<std::unique_ptr<pl::Stage>, std::string> {
                         return std::make_unique<RuntimeStage>(*d, reads, writes, body);
                     });
    }
}

void Runtime::Impl::build_pipeline() {
    tree_token.runtime = &self;
    layout_token.runtime = &self;
    bounds_token.runtime = &self;
    register_stages();
    if (options.plugins) options.plugins(registry);
    const std::array<pl::Port, 2> inputs{input_port(), tree()};
    const pl::Description wanted = options.pipeline.value_or(runtime_pipeline(options.stages));
    auto built = pl::Pipeline::build(wanted, registry, inputs);
    if (!built) {
        constexpr Log frame_log{"frame"};
        frame_log.error("the frame pipeline `{}` does not build; using the default for its level:\n{}", wanted.name, pl::to_text(built.error()));
        built = pl::Pipeline::build(runtime_pipeline(options.stages), registry, inputs);
    }
    frames.emplace(std::move(*built));
    placed = frames->produces(res::bounds);
}

}  // namespace omui
