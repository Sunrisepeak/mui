module openmui.bench;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.render;
import openmui.raster;
import openmui.text;
import openmui.trace;
import openmui.ui.base;
import openmui.host.headless;
import openmui.anim;
import openmui.markdown;
import openmui.ui.canvas;
import openmui.ui.canvas_kit;
import openmui.showcase.wall;
import openmui.showcase.styles;
import openmui.theme.styles;
import openmui.rhi;
import openmui.rhi.sdl;
import mcpplibs.os;

namespace omui::bench {

namespace {

constexpr Size        full_hd{1920, 1080};
constexpr std::size_t columns = 14;
constexpr std::size_t card_rows = 12;
constexpr std::size_t card_count = columns * card_rows;  // 168 cards of 6 nodes: 1008, and the frame around them

// M1 ⑤'s dashboard: every card has a title, an action, a value and a bar.
struct Dashboard {
    std::vector<Signal<std::int32_t>> values;
    Signal<std::int64_t>              tick = signal(std::int64_t{0});
    Signal<float>                     lift = signal(0.0f);  // every card moves by it, in transition

    Dashboard() {
        values.reserve(card_count);
        for (std::size_t i = 0; i < card_count; ++i) values.push_back(signal(static_cast<std::int32_t>(i * 7 % 100)));
    }

    [[nodiscard]] View view() const {
        std::vector<Element> cards;
        cards.reserve(card_count);
        for (std::size_t i = 0; i < card_count; ++i) {
            const Signal<std::int32_t> v = values[i];
            const Signal<std::int64_t> t = tick;
            cards.push_back(column({.id = std::format("c{}", i), .padding = 6, .gap = 3, .background = "surface", .border_color = "border",
                                    .border_width = 1, .radius = 6, .translate_y = lift,
                                    .animate = {{"translate_y", Transition::tween(seconds(0.5), anim::ease::linear)}}},
                                   {
                                       row({.id = "head", .gap = 4, .align = "center"},
                                           {
                                               text({.id = "title", .text = std::format("Metric {}", i), .style = "label", .grow = 1}),
                                               button({.id = "open", .text = "Open"}),
                                           }),
                                       text({.id = "value", .text = computed([v, t] { return std::format("{} · {}", v.get(), t.get()); }),
                                             .style = "heading"}),
                                       progress({.id = "bar", .value = computed([v] { return static_cast<double>(v.get() % 101) / 100.0; })}),
                                   }));
        }
        return column({.id = "dash", .padding = 8, .gap = 8, .background = "background"},
                      {
                          text({.id = "heading", .text = "Dashboard — 1000+ nodes", .style = "title"}),
                          grid({.id = "cards", .gap = 6, .grow = 1}, std::format("repeat({}, 1fr)", columns), std::move(cards)),
                      });
    }
};

HeadlessOptions scene_options(HeadlessLevel level = HeadlessLevel::pixels) {
    HeadlessOptions o;
    o.level = level;
    o.size = full_hd;
    o.app = "org.openmui.bench";
    return o;
}

// A GPU to draw frames on (omui.rhi through SDL), when the machine has one:
// tools/ci/gpu.sh's OMUI_VULKAN_LIBRARY and OMUI_GPU_VIDEO_DRIVER as the
// tests use them. A software device (lavapipe) is counted `software`, so a
// GPU's frame budget is not held to it; the device is in the report.
class Gpu {
public:
    explicit Gpu(Size size) {
        rhi::sdl::Options o;
        o.formats = render::gpu_shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        auto device = rhi::sdl::create(o);
        if (!device) {
            why_ = std::format("no GPU device ({})", device.error().message);
            return;
        }
        device_ = std::move(*device);
        auto renderer = render::GpuRenderer::create(*device_);
        if (!renderer) {
            why_ = std::format("the GPU renderer: {}", renderer.error().message);
            return;
        }
        renderer_.emplace(std::move(*renderer));
        auto target = device_->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, static_cast<std::uint32_t>(size.width),
                                                       static_cast<std::uint32_t>(size.height),
                                                       rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "bench.frame"});
        if (!target) {
            why_ = std::format("the frame's target: {}", target.error().message);
            return;
        }
        target_ = *target;
    }
    ~Gpu() {
        if (device_ && target_) device_->destroy(target_);
    }
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;

    // False (and the benchmark skipped) without a device; else the device noted.
    bool ready(State& s) {
        if (!why_.empty()) {
            s.skip(why_);
            return false;
        }
        const rhi::Caps& caps = device_->caps();
        s.note("device", std::format("{} ({}{})", caps.device, rhi::name(caps.backend), caps.software ? ", software" : ""));
        return true;
    }

    // The list drawn whole into the frame and waited for: counted as gpu_ms,
    // of which encode_ms is the CPU's (the list encoded and submitted), and
    // what it uploaded (uploaded_kb: new images, new masks, the instances).
    void draw(State& s, const render::DisplayList& list, const render::Resources& resources, Color background) {
        const auto t0 = std::chrono::steady_clock::now();
        auto r = renderer_->render(list, target_, resources, {.scale = 1, .clear = true, .background = background});
        s.counter("encode_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (r) s.counter("uploaded_kb", static_cast<double>(r->uploaded) / 1024.0);
        auto w = device_->wait_idle();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!r || !w) std::println(std::cerr, "omui-bench: the GPU frame failed: {}", !r ? r.error().message : w.error().message);
        s.counter("gpu_ms", ms);
        s.counter("software", device_->caps().software ? 1 : 0);
    }

private:
    std::unique_ptr<rhi::Device>       device_;
    std::optional<render::GpuRenderer> renderer_;
    rhi::Texture                       target_{};
    std::string                        why_;
};

// The phase times and the counts of a frame, as counters.
void frame_counters(State& s, const FrameOutput& f) {
    s.counter("nodes", static_cast<double>(f.stats.nodes));
    s.counter("laid_out", static_cast<double>(f.stats.laid_out));
    s.counter("painted", static_cast<double>(f.stats.painted));
    s.counter("layout_ms", f.stats.timings.layout);
    s.counter("paint_ms", f.stats.timings.paint);
    s.counter("runtime_ms", f.stats.timings.total);
}

// The centres of the cards' buttons, for the pointer to sweep.
std::vector<Point> button_centres(Runtime& rt) {
    std::vector<Point> out;
    rt.walk([&](NodeId id, int) {
        if (rt.type(id).name == "button") {
            const Rect b = rt.bounds(id);
            out.push_back({b.x + b.width / 2, b.y + b.height / 2});
        }
    });
    return out;
}

// A long Markdown answer as an assistant writes one: headings, paragraphs with
// inline styles and links, lists, fenced code with blank lines, a table.
std::string markdown_answer(int sections) {
    std::string out;
    for (int i = 0; i < sections; ++i)
        out += std::format(
            "## Step {0}\n\nWe compute **{1}** with `f({0})`, as [the guide](https://example.com/guide#{0}) explains; "
            "the *result* must stay positive.\n\n- check the input\n- call `f`\n- [x] compare with {1}\n\n"
            "```cpp\nauto x = f({0});\n\nassert(x > 0);\n```\n\n| n | f(n) |\n|--:|-----:|\n| {0} | {1} |\n\n",
            i, i * i);
    return out;
}

std::string paragraph_text() {
    std::string s;
    for (int i = 0; i < 12; ++i)
        s += std::format("Paragraph {} — openmui lays text out with HarfBuzz and its own line breaking (UAX #14); numbers like {} and "
                         "punctuation, “quotes”, and a long-wordthatmustbreak keep the shaper honest. ",
                         i, i * 1234.5);
    return s;
}


// canvas-markdown §3: a wall of windows on the canvas — a box far out, a
// title bar and greeked lines when larger — `count` of them on a grid.
void canvas_wall(std::string_view name, std::size_t count) {
    auto& sc = world::scene(name);
    sc.clear();
    sc.kind("window", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {80, world::Tier::Draw::custom}},
                                  .draw = [](world::DrawContext& d) {
                                      const Rect r = d.screen;
                                      d.list.fill_round_rect(r, CornerRadii::all(6), render::Paint::solid(d.scene.fill(d.node)));
                                      d.list.fill_rect({r.x, r.y, r.width, std::min(r.height, 22.0f)}, Color::hex(0x334155));
                                      for (float y = r.y + 30; y + 6 < r.bottom() - 6; y += 12)
                                          d.list.fill_rect({r.x + 8, y, (r.width - 16) * 0.8f, 5}, Color::hex(0xcbd5e1));
                                  },
                                  .role = Role::window});
    const auto across = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count) * 1.5)));
    for (std::size_t i = 0; i < count; ++i)
        sc.add({.kind = "window", .x = static_cast<double>(i % across) * 700.0, .y = static_cast<double>(i / across) * 500.0, .width = 640,
                .height = 440, .key = std::format("w{}", i), .fill = Color::hex(i % 3 ? 0xf8fafc : 0xe0f2fe), .radius = 8});
}

}  // namespace

SceneFacts reference_scene_facts() {
    register_base_components();
    Dashboard d;
    Headless h(scene_options(HeadlessLevel::layout));
    h.mount("main", d.view());
    const FrameOutput f = h.step();
    return {f.stats.nodes, card_count, full_hd.width, full_hd.height};
}

void add_framework_benchmarks() {
    static bool added = false;
    if (added) return;
    added = true;
    register_base_components();

    // ── canvas-markdown §3.5: the canvas's frame while the camera moves ──
    register_canvas_component();
    auto canvas_frame = [](State& s, std::string_view scene, std::size_t count, double zoom, bool sweep) {
        canvas_wall(scene, count);
        // The runtime's frame — the visible set, tiers, the display list — and
        // no raster (pixels are rasterised only when asked for).
        HeadlessOptions o = scene_options();
        Headless h(o);
        h.mount("main", column({.width = full_hd.width, .height = full_hd.height},
                               {canvas({.id = "wall", .scene = std::string(scene), .x = 9000, .y = 6000, .zoom = zoom,
                                        .width = full_hd.width, .height = full_hd.height})}));
        h.settle();
        int i = 0;
        while (s.next()) {
            ++i;
            if (sweep) h.scroll({960, 540}, {0, (i / 40) % 2 ? 60.0f : -60.0f}, false);  // the wheel, in and out
            else h.scroll({960, 540}, {(i / 60) % 2 ? 37.0f : -37.0f, 23.0f * ((i / 30) % 2 ? 1 : -1)}, true);  // a trackpad pan
            const FrameOutput f = h.step();
            frame_counters(s, f);
            s.counter("display_commands", static_cast<double>(h.runtime().display_list().size()));
        }
    };
    add("canvas/sharpen", "40 windows of content on screen, the camera stops at a new zoom: until every texture is rasterised at it (§3.5, < 300 ms)",
        [](State& s) {
            auto& sc = world::scene("bench-sharpen");
            sc.clear();
            sc.kind("doc", world::Kind{.tiers = {{0, world::Tier::Draw::box}, {60, world::Tier::Draw::texture}},
                                       .content = [](const world::Scene& sc2, world::NodeRef n, render::DisplayList& dl) {
                                           const auto r = sc2.rect(n);
                                           const float w = static_cast<float>(r.width), h = static_cast<float>(r.height);
                                           dl.fill_round_rect({0, 0, w, h}, CornerRadii::all(8), render::Paint::solid(Color::hex(0xf8fafc)));
                                           dl.fill_rect({0, 0, w, 28}, Color::hex(0x334155));
                                           for (float y = 40; y + 10 < h; y += 18) dl.fill_rect({12, y, (w - 24) * (0.6f + 0.4f * std::fmod(y * 0.37f, 1.0f)), 9}, Color::hex(0x94a3b8));
                                       }});
            for (int i = 0; i < 40; ++i)
                sc.add({.kind = "doc", .x = (i % 8) * 700.0, .y = (i / 8) * 500.0, .width = 640, .height = 440, .key = std::format("d{}", i),
                        .fill = Color::hex(0xe2e8f0)});
            Headless h(scene_options());
            auto zoom = signal(0.3);
            h.mount("main", column({.width = full_hd.width, .height = full_hd.height},
                                   {canvas({.id = "wall", .scene = "bench-sharpen", .x = 2650, .y = 1200, .zoom = zoom,
                                            .width = full_hd.width, .height = full_hd.height})}));
            h.settle();
            int i = 0;
            while (s.next()) {
                // Another mip level, and new content (the cache would answer an old one).
                zoom.set(++i % 2 ? 0.62 : 0.3);
                sc.each([&](world::NodeRef n) { sc.touch(n); });
                h.step();
                h.settle();  // frames until every texture is in
            }
        });
    add("canvas/pan-1000", "1000 windows, a trackpad pans every frame: the visible set, tiers and the display list", [=](State& s) {
        canvas_frame(s, "bench-pan", 1000, 0.18, false);
    });
    add("canvas/zoom-sweep-1000", "1000 windows, the wheel zooms in and out about the centre every frame", [=](State& s) {
        canvas_frame(s, "bench-zoom", 1000, 0.18, true);
    });
    add("canvas/pan-100k", "100,000 windows, a few hundred on screen: the frame follows what is visible, not the scene", [=](State& s) {
        canvas_frame(s, "bench-big", 100000, 0.25, false);
    });
    // canvas-kit: a person dragging a selection about a large scene, with
    // everything on — selection outlines, snapping to what is in view, the
    // grid and the minimap over 100,000 nodes.
    register_canvas_kit();
    add("canvas-kit/drag-100k", "100,000 windows, 10 chosen dragged every frame with select, drag, snap, grid and minimap on", [](State& s) {
        canvas_wall("bench-kit", 100000);
        Headless h(scene_options());
        h.mount("main", column({.width = full_hd.width, .height = full_hd.height},
                               {canvas({.id = "wall",
                                        .behaviors = std::vector<std::string>{"select", "drag", "snap", "grid", "minimap"},
                                        .scene = "bench-kit",
                                        .x = 9000,
                                        .y = 6000,
                                        .zoom = 0.25,
                                        .width = full_hd.width,
                                        .height = full_hd.height})}));
        h.settle();
        // Ten windows near the centre, chosen as an agent would.
        auto& sc = world::scene("bench-kit");
        std::vector<world::NodeRef> near;
        sc.query({8000, 5000, 2000, 2000}, near);
        Invocation call;
        call.actor = {"agent", "bench", {}};
        for (std::size_t i = 0; i < std::min<std::size_t>(10, near.size()); ++i) (void)h.tree().act(std::format("main/wall/{}", sc.key(near[i])), "select", call);
        h.step();
        const world::WorldRect first = sc.rect(near.front());
        const Point grab{static_cast<float>(960 + (first.x + 40 - 9000) * 0.25), static_cast<float>(540 + (first.y + 40 - 6000) * 0.25)};
        h.step({PointerEvent{.kind = PointerKind::down, .position = grab, .button = MouseButton::left, .buttons = 1}});
        int i = 0;
        while (s.next()) {
            ++i;
            // Back and forth, past the snapping distance each way.
            const float dx = 40.0f * std::sin(static_cast<float>(i) * 0.05f), dy = 25.0f * std::cos(static_cast<float>(i) * 0.031f);
            const FrameOutput f = h.step({PointerEvent{.kind = PointerKind::move, .position = {grab.x + dx, grab.y + dy}, .buttons = 1}});
            frame_counters(s, f);
            s.counter("display_commands", static_cast<double>(h.runtime().display_list().size()));
        }
        h.step({PointerEvent{.kind = PointerKind::up, .position = grab, .button = MouseButton::left}});
    });

    // ── S1, the Markdown wall at 1080p (canvas-markdown §3.5): what a frame
    // costs the UI thread while many windows move at once. The pool's work —
    // laying pages out and rasterising their textures — is waited for before
    // each frame, outside the timed part: it is not the frame's.
    struct Wall {
        omui::wall::Model model;
        Headless          h{scene_options()};
        Wall(std::size_t count, std::size_t groups, std::string scene) {
            model.docs = omui::wall::synthetic(count, 1, groups);
            model.scene = std::move(scene);
            model.hud.set(false);
            auto spec = omui::wall::app(model);
            spec.setup(h.runtime());
            h.mount("main", spec.view());
            h.settle();
        }
        Canvas& canvas() { return *model.canvas(); }
        void camera(double x, double y, double zoom) {
            canvas().set_camera({x, y, zoom});
            h.settle();
        }
        // One frame, timed alone.
        FrameOutput frame(State& s, std::vector<InputEvent> events = {}) {
            s.pause();
            Tasks::shared().wait_idle();
            s.resume();
            const FrameOutput f = h.step(std::move(events));
            const auto& st = canvas().stats();
            s.counter("visible", static_cast<double>(st.visible));
            s.counter("textured", static_cast<double>(st.textured));
            s.counter("stretched", static_cast<double>(st.stretched));
            s.counter("live", static_cast<double>(st.promoted));
            s.counter("canvas_paint_ms", st.paint_ms);
            frame_counters(s, f);
            return f;
        }
        Point at(double x, double y) const {
            return world::Camera{model.x.peek(), model.y.peek(), model.zoom.peek()}.to_screen(x, y, full_hd);
        }
    };
    add("wall/zoom-sweep-1000", "1000 windows, the wheel sweeps 1% ↔ 400% about the centre: tiers, textures asked for and stretched", [](State& s) {
        Wall w(1000, 40, "bench-wall-zoom");
        const auto b = world::scene("bench-wall-zoom").bounds();
        w.camera(b.x + b.width / 2, b.y + b.height / 2, 0.01);
        float dir = -1;  // in
        while (s.next()) {
            const double z = w.model.zoom.peek();
            if (z >= 4) dir = 1;
            if (z <= 0.01) dir = -1;
            w.frame(s, {WheelEvent{.position = {960, 540}, .delta = {0, 60 * dir}}});
        }
    });
    add("wall/zoom-sweep-1000-gpu", "the same sweep with the GPU drawing every frame (S1: 120 Hz, frame p99 under 8.3 ms)", [](State& s) {
        Gpu gpu(full_hd);
        if (!gpu.ready(s)) return;
        Wall w(1000, 40, "bench-wall-zoom-gpu");
        const auto b = world::scene("bench-wall-zoom-gpu").bounds();
        w.camera(b.x + b.width / 2, b.y + b.height / 2, 0.01);
        typeset::GlyphCache glyphs(w.h.runtime().fonts());
        float dir = -1;
        while (s.next()) {
            const double z = w.model.zoom.peek();
            if (z >= 4) dir = 1;
            if (z <= 0.01) dir = -1;
            w.frame(s, {WheelEvent{.position = {960, 540}, .delta = {0, 60 * dir}}});
            gpu.draw(s, w.h.runtime().display_list(), {&w.h.runtime().images(), &glyphs}, Color::white());
        }
    });
    add("wall/pan-1000", "1000 windows as textures (about 160 px each), a trackpad pans every frame", [](State& s) {
        Wall w(1000, 40, "bench-wall-pan");
        const auto b = world::scene("bench-wall-pan").bounds();
        w.camera(b.x + b.width / 2, b.y + b.height / 2, 0.25);
        int i = 0;
        while (s.next()) {
            ++i;
            w.frame(s, {WheelEvent{.position = {960, 540}, .delta = {(i / 90) % 2 ? 41.0f : -41.0f, (i / 45) % 2 ? 23.0f : -23.0f}, .precise = true}});
        }
    });
    add("wall/workspace-drag-100", "a workspace of 100 windows dragged by its label every frame (a declarative behaviour moving 101 nodes)", [](State& s) {
        Wall w(1000, 10, "bench-wall-workspace");
        auto& sc = world::scene("bench-wall-workspace");
        world::NodeRef ws;
        sc.each([&](world::NodeRef n) {
            if (!ws.valid() && sc.kind_name(n) == "workspace") ws = n;
        });
        const auto r = sc.rect(ws);
        w.camera(r.x + r.width / 2, r.y + r.height / 2, 0.12);
        const Point label = w.at(r.x + 300, r.y + 30);
        w.h.step({PointerEvent{.kind = PointerKind::down, .position = label, .button = MouseButton::left, .buttons = 1}});
        int i = 0;
        while (s.next()) {
            ++i;
            const float t = static_cast<float>(i) * 0.07f;
            w.frame(s, {PointerEvent{.kind = PointerKind::move, .position = {label.x + 120 * std::sin(t), label.y + 70 * std::cos(t * 0.7f)}, .buttons = 1}});
        }
        w.h.step({PointerEvent{.kind = PointerKind::up, .position = label, .button = MouseButton::left}});
    });
    add("wall/selection-drag-1000", "all 1000 windows chosen, dragged by one title bar every frame (canvas-kit's drag and snap)", [](State& s) {
        Wall w(1000, 40, "bench-wall-selection");
        auto& sc = world::scene("bench-wall-selection");
        std::vector<std::string> all;
        world::NodeRef first;
        sc.each([&](world::NodeRef n) {
            if (sc.kind_name(n) != "window") return;
            if (!first.valid()) first = n;
            all.emplace_back(sc.key(n));
        });
        w.canvas().select(all);
        const auto r = sc.rect(first);
        w.camera(r.x + r.width / 2, r.y + r.height / 2, 0.4);
        const Point title = w.at(r.x + r.width / 2, r.y + 12);
        w.h.step({PointerEvent{.kind = PointerKind::down, .position = title, .button = MouseButton::left, .buttons = 1}});
        int i = 0;
        while (s.next()) {
            ++i;
            const float t = static_cast<float>(i) * 0.05f;
            w.frame(s, {PointerEvent{.kind = PointerKind::move, .position = {title.x + 150 * std::sin(t), title.y + 90 * std::sin(t * 0.6f)}, .buttons = 1}});
        }
        w.h.step({PointerEvent{.kind = PointerKind::up, .position = title, .button = MouseButton::left}});
    });
    add("wall/stream-20", "20 windows written into at 50 tokens a second each: live ones append, textures stale four times a second", [](State& s) {
        Wall w(1000, 40, "bench-wall-stream");
        const auto b = world::scene("bench-wall-stream").bounds();
        w.camera(b.x + b.width / 4, b.y + b.height / 4, 0.5);
        w.model.stream(20, 50);
        while (s.next()) w.frame(s);
        w.model.stop_stream();
    });
    add("wall/sharpen", "the camera stops at a new zoom over a wall of 1000 pages: until every window on screen is sharp (§3.5, < 300 ms)", [](State& s) {
        Wall w(1000, 40, "bench-wall-sharpen");
        const auto b = world::scene("bench-wall-sharpen").bounds();
        int i = 0;
        while (s.next()) {
            // Another level, and the pages' content new (the cache would answer an old one).
            w.canvas().set_camera({b.x + b.width / 3, b.y + b.height / 3, ++i % 2 ? 0.31 : 0.17});
            auto& sc = world::scene("bench-wall-sharpen");
            sc.each([&](world::NodeRef n) { sc.touch(n); });
            w.h.step();
            w.h.settle();
        }
    });

    // ── liquid glass (D-64): ten glass panels over a busy 1080p backdrop,
    // the frame drawn whole — by the CPU reference, and on the GPU when the
    // machine has one (lavapipe in CI: a CPU's Vulkan; a real GPU's number is
    // the report's, M4's 120 Hz target).
    auto glass_frame = [](render::DisplayList& dl) {
        dl.clear();
        dl.fill_rect({0, 0, 1920, 1080}, Color::hex(0x0f172a));
        for (int i = 0; i < 24; ++i)
            dl.fill_rect({static_cast<float>(i) * 80.0f + 6, 0, 36, 1080}, Color{0.2f + 0.03f * static_cast<float>(i), 0.35f, 0.85f - 0.025f * static_cast<float>(i), 1});
        for (int i = 0; i < 6; ++i) {
            render::Path ring;
            ring.add_circle({300.0f + static_cast<float>(i) * 260, 540}, 140).add_circle({300.0f + static_cast<float>(i) * 260, 540}, 110);
            dl.fill_path(ring, render::FillRule::even_odd, render::Paint::solid(Color::hex(0xf59e0b, 0.8f)));
        }
        const auto ref = render::effects().find("liquid_glass");
        const std::array<std::pair<std::string_view, float>, 3> values{{{"blur", 8}, {"tint_a", 0.12f}, {"dispersion", 0.4f}}};
        const auto params = render::effects().params(*ref, values);
        for (int i = 0; i < 10; ++i) {
            const Rect r{80.0f + static_cast<float>(i % 5) * 360, 140.0f + static_cast<float>(i / 5) * 440, 300, 360};
            const std::array<render::EffectShape, 1> shape{{{r, CornerRadii::all(28)}}};
            dl.push_layer(1, render::BlendMode::normal, *ref, params, r, shape);
            dl.fill_rect({r.x + 24, r.y + 24, 120, 18}, Color::hex(0xffffff, 0.85f));
            dl.pop_layer();
        }
    };
    add("glass/10-1080p-cpu", "ten liquid glass panels at 1920×1080, the frame drawn whole by the CPU reference (on the task pool, in bands)", [glass_frame](State& s) {
        render::DisplayList dl;
        glass_frame(dl);
        raster::Surface target(1920, 1080);
        render::CpuRenderer r;
        while (s.next()) (void)r.render(dl, target, {}, {.parallel = &raster_parallel()});
    });
    add("glass/10-1080p-gpu", "the same frame on the GPU (omui.rhi), submitted and waited for", [glass_frame](State& s) {
        Gpu gpu(full_hd);
        if (!gpu.ready(s)) return;
        render::DisplayList dl;
        glass_frame(dl);
        render::ImageStore images;
        while (s.next()) gpu.draw(s, dl, {&images, nullptr}, Color::hex(0x0f172a));
    });

    // ── S2 (§19.3): every style pack's frame on the GPU — the studio at
    // 1080p, the volume slider moving every frame so every frame is drawn;
    // the runtime's frame (CPU) and the GPU's, both counted.
    for (std::string_view pack : themes::style_names) {
        add(std::format("styles/{}-gpu", pack),
            std::format("S2's interface in the {} pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole", pack),
            [pack](State& s) {
                Gpu gpu(full_hd);
                if (!gpu.ready(s)) return;
                Headless       h(scene_options());
                styles::Studio studio;
                auto spec = styles::app(studio, pack);
                spec.setup(h.runtime());
                h.mount("main", spec.view());
                h.settle();
                typeset::GlyphCache glyphs(h.runtime().fonts());
                std::size_t i = 0;
                while (s.next()) {
                    studio.volume.set(static_cast<double>(++i % 100) / 100.0);
                    const auto f = h.step();
                    frame_counters(s, f);
                    gpu.draw(s, h.runtime().display_list(), {&h.runtime().images(), &glyphs}, Color::white());
                }
            });
    }

    // ── M1 ⑤: the 1000-node dashboard at 1080p, through the CPU renderer ──
    add("ui/frame/idle", "nothing changed: the frame the runtime skips", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        while (s.next()) {
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/one-label", "M1 ⑤: one card's value changes; relayout, repaint and raster at 1920x1080", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        std::size_t i = 0;
        while (s.next()) {
            const Signal<std::int32_t>& v = d.values[i++ % card_count];
            v.set(v.peek() + 1);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/all-labels", "every card's value changes at once (the worst ordinary frame)", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        while (s.next()) {
            d.tick.set(d.tick.peek() + 1);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/animate-all", "every card moves at once (a transition on all 168): the whole frame is redrawn", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        float to = 0;
        while (s.next()) {
            if (h.step().animating == false) {
                to = to == 0 ? 6.0f : 0.0f;  // turn around: another half second of motion
                d.lift.set(to);
            }
            (void)h.pixels();
        }
    });
    add("ui/frame/hover-sweep", "the pointer moves from button to button: hit testing, hover restyle, repaint", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        const std::vector<Point> targets = button_centres(h.runtime());
        std::size_t i = 0;
        while (s.next()) {
            h.move(targets[i++ % targets.size()]);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/layout/resize", "the window's width changes: the whole dashboard is laid out again (no raster)", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        h.mount("main", d.view());
        h.settle();
        std::size_t i = 0;
        while (s.next()) {
            h.runtime().resize({full_hd.width - static_cast<float>(i++ % 2) * 40, full_hd.height}, 1);
            frame_counters(s, h.step());
        }
    });
    add("ui/raster/full-1080p", "the whole dashboard's display list rasterised by the CPU renderer", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        typeset::GlyphCache glyphs(h.runtime().fonts());
        render::CpuRenderer renderer;
        raster::Surface target(static_cast<int>(full_hd.width), static_cast<int>(full_hd.height));
        render::CpuOptions o;
        o.background = Color::white();
        const render::DisplayList& list = h.runtime().display_list();
        s.counter("commands", static_cast<double>(list.size()));
        while (s.next()) (void)renderer.render(list, target, {&h.runtime().images(), &glyphs}, o);
        // Where the time goes, per kind of command (one measured pass after the timed ones).
        o.measure = true;
        const render::RenderReport r = renderer.render(list, target, {&h.runtime().images(), &glyphs}, o);
        for (std::size_t k = 0; k < r.kinds.size(); ++k)
            if (r.kinds[k].count) s.counter(std::format("{}_ms", render::command_kind_name(k)), r.kinds[k].ms);
    });
    add("ui/raster/full-1080p-parallel", "the same, in bands of 64 rows on the task pool (the same bytes)", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        typeset::GlyphCache glyphs(h.runtime().fonts());
        render::CpuRenderer renderer;
        raster::Surface target(static_cast<int>(full_hd.width), static_cast<int>(full_hd.height));
        render::CpuOptions o;
        o.background = Color::white();
        o.parallel = &raster_parallel();
        s.counter("threads", static_cast<double>(Tasks::shared().threads()));
        while (s.next()) (void)renderer.render(h.runtime().display_list(), target, {&h.runtime().images(), &glyphs}, o);
    });
    add("ui/mount", "the dashboard is built, mounted and laid out (its first frame)", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        while (s.next()) {
            h.mount("main", d.view());
            frame_counters(s, h.step());
            s.pause();
            h.runtime().unmount("main");
            (void)h.step();
            s.resume();
        }
    });

    // ── what agents and tools read ──
    add("agent/snapshot", "the semantic snapshot of the dashboard, as the JSON an agent receives", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        h.mount("main", d.view());
        h.settle();
        std::size_t bytes = 0;
        while (s.next()) {
            const std::string json = h.tree().snapshot().to_value().json();
            bytes = json.size();
        }
        s.counter("bytes", static_cast<double>(bytes));
    });

    // ── pieces alone ──
    add("text/paragraph", "shaping and breaking 2 KB of text into lines 600 px wide", [](State& s) {
        typeset::FontCollection fonts;
        if (auto r = fonts.add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        const std::string source = paragraph_text();
        s.items(source.size());
        while (s.next()) {
            typeset::ParagraphStyle style;
            typeset::Paragraph p(fonts, source, style);
            p.layout(600);
            s.counter("height", static_cast<double>(p.size().height));
        }
    });
    add("markdown/parse", "parsing a 40 KB Markdown answer into the document (md4c and the block / inline model)", [](State& s) {
        const std::string source = markdown_answer(160);
        s.items(source.size());
        while (s.next()) {
            const auto doc = md::parse(source);
            s.counter("blocks", static_cast<double>(doc.root.children.size()));
        }
    });
    add("markdown/stream-token", "appending a 40 KB answer 4 bytes at a time: the cost of one streamed token", [](State& s) {
        const std::string source = markdown_answer(160);
        constexpr std::size_t token = 4;
        s.items(source.size() / token);
        while (s.next()) {
            md::Stream stream;
            for (std::size_t pos = 0; pos < source.size(); pos += token) stream.append(std::string_view(source).substr(pos, token));
            s.counter("frozen_blocks", static_cast<double>(stream.stable_blocks()));
        }
    });
    add("markdown/to-html", "the document of a 40 KB answer back to HTML", [](State& s) {
        const std::string source = markdown_answer(160);
        const auto doc = md::parse(source);
        s.items(source.size());
        while (s.next()) s.counter("bytes", static_cast<double>(md::to_html(doc).size()));
    });
    add("text/glyph-motion", "one frame of glyph motion over 2 KB of laid-out text: every glyph's style (D-68)", [](State& s) {
        typeset::FontCollection fonts;
        if (auto r = fonts.add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        typeset::Paragraph p(fonts, paragraph_text(), {});
        p.layout(600);
        std::vector<std::span<const render::Glyph>> runs;
        std::size_t n = 0;
        for (const auto& l : p.lines())
            for (const auto& r : l.runs) {
                runs.emplace_back(r.glyphs);
                n += r.glyphs.size();
            }
        const GlyphMotion m = GlyphMotion::gather(3);
        s.items(n);
        double t = 0;
        while (s.next()) {
            const GlyphFrame f = glyph_frame(m, anim::seconds(t += 0.016), runs);
            s.counter("moving", f.moving ? 1 : 0);
        }
    });
    add("render/effect-particles", "a 480 × 320 layer bursting into particles: offscreen, effect, composite (D-68)", [](State& s) {
        render::DisplayList dl;
        const Rect area{20, 20, 480, 320};
        const auto ref = *render::effects().find("particles");
        const std::array<std::pair<std::string_view, float>, 2> given{{{"progress", 0.4f}, {"seed", 2}}};
        const auto params = render::effects().params(ref, given);
        dl.push_layer(1, render::BlendMode::normal, ref, params, area);
        for (int i = 0; i < 12; ++i)
            dl.fill_round_rect({area.x + 10.0f + 38.0f * static_cast<float>(i), area.y + 20, 30, 280}, CornerRadii::all(8),
                               render::Paint::solid(Color::hex(0x4f46e5 + 0x0a0a00 * static_cast<std::uint32_t>(i))));
        dl.pop_layer();
        raster::Surface surf(560, 400);
        render::CpuRenderer cpu;
        s.items(static_cast<std::size_t>(area.width * area.height));
        while (s.next()) {
            const auto r = cpu.render(dl, surf, {}, {.background = Color::white()});
            s.counter("skipped", static_cast<double>(r.effects_skipped));
        }
    });
    add("trace/span-off", "10 000 spans while nothing records: the cost tracing adds to a normal run", [](State& s) {
        (void)trace::stop();
        constexpr std::size_t n = 10'000;
        s.items(n);
        while (s.next())
            for (std::size_t i = 0; i < n; ++i) trace::Span span("bench", "bench");
    });
    add("trace/span-on", "10 000 spans while a profile records", [](State& s) {
        constexpr std::size_t n = 10'000;
        s.items(n);
        while (s.next()) {
            s.pause();
            trace::start({.events_per_thread = 2 * n});
            s.resume();
            for (std::size_t i = 0; i < n; ++i) trace::Span span("bench", "bench");
        }
        (void)trace::stop();
    });
    add("log/filtered-out", "10 000 debug records in a category the filter leaves out", [](State& s) {
        constexpr Log bench_log{"bench"};
        constexpr std::size_t n = 10'000;
        if (auto r = set_log_filter("*=info"); !r) warn("{}", r.error());
        s.items(n);
        while (s.next())
            for (std::size_t i = 0; i < n; ++i) bench_log.debug("record {}", i);
    });
}

}  // namespace omui::bench
