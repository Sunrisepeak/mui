// The headless host: the four levels give the same semantic tree, the clock
// steps by frames, input by point or by id, pixels and PNG, pixel
// provenance, and an agent driving an H3 session.
import std;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

struct Counter {
    Signal<int> count{0};
    View view() {
        return column({.padding = 12, .gap = 8}, {
            text({.id = "label", .text = derive([c = count] { return std::format("{} clicks", c.get()); })}),
            box({.id = "inc", .width = 80, .height = 28, .background = "accent", .radius = 6, .label = "Add one",
                 .on = {{"press", [c = count](const Value&) { c.set(c.peek() + 1); }}},
                 .animate = {{"background", Transition::tween(seconds(0.2))}}}),
        });
    }
};

HeadlessOptions at(HeadlessLevel level) {
    HeadlessOptions o;
    o.level = level;
    o.size = {240, 120};
    o.fonts = &fonts();
    return o;
}

// The parts of a node every level agrees on (S level): id, role, name, value, actions.
std::string essence(semantic::Tree& t) {
    std::string out;
    for (const auto& n : t.snapshot().nodes) {
        out += std::format("{} {} '{}' {}", n.id, spec::name(n.role), n.name, n.value.json());
        for (const auto& a : n.actions) out += std::format(" {}:{}", a.name, spec::name(a.effect));
        out += '\n';
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"headless"};

    s.test("H0, H1 and H2 give the same semantic tree; only H1 and up have places", [] {
        std::array<std::string, 3> trees;
        std::array<HeadlessLevel, 3> levels{HeadlessLevel::semantics, HeadlessLevel::layout, HeadlessLevel::pixels};
        for (std::size_t i = 0; i < 3; ++i) {
            Headless h(at(levels[i]));
            Counter c;
            h.mount("main", c.view());
            h.step();
            check(h.click("main/inc"), "click by id at every level");
            h.settle();
            check(c.count.peek() == 1);
            trees[i] = essence(h.tree());
            auto label = h.tree().node("main/label");
            check(label.has_value());
            if (i == 0) check(!label->bounds && !label->states.has(State::offscreen), "H0: no layout, no place");
            else check(label->bounds.has_value() && label->bounds->width > 0);
        }
        check(trees[0] == trees[1] && trees[1] == trees[2], trees[0] + "---\n" + trees[2]);
        check(trees[0].find("main/label text '1 clicks'") != std::string::npos, trees[0]);
    });

    s.test("the clock steps by frames; settle runs until nothing moves", [] {
        Headless h(at(HeadlessLevel::layout));
        Counter c;
        h.mount("main", c.view());
        h.step();
        check(h.frame_index() == 0 && h.now() == seconds(0));
        h.advance(seconds(0.5));
        check(h.frame_index() == 30, std::format("{}", h.frame_index()));
        h.runtime().set(h.runtime().find("main/inc"), "background", Value("#ff0000"));
        auto frames = h.settle();
        check(frames >= 12 && frames <= 14, std::format("a 0.2 s transition at 60 Hz: {} frames", frames));
        check(!h.tree().node("main/inc")->states.has(State::animating));
        check(h.settle() == 1, "already settled: one frame");
    });

    s.test("pixels, PNG, regions, and the node behind a pixel", [] {
        Headless h(at(HeadlessLevel::pixels));
        Counter c;
        h.mount("main", c.view());
        h.step();
        const auto& px = h.pixels();
        check(px.width() == 240 && px.height() == 120);
        const Rect inc = h.runtime().bounds(h.runtime().find("main/inc"));
        auto p = px.at(static_cast<int>(inc.center().x), static_cast<int>(inc.center().y));
        check(p.b > 200 && p.r < 120, "the accent colour");
        check(h.node_at_pixel(inc.center()) == h.runtime().find("main/inc"));
        auto whole = h.png();
        check(whole.has_value() && whole->size() > 100 && (*whole)[1] == 'P');
        auto part = h.png(inc);
        check(part.has_value() && part->size() < whole->size());
        auto dir = scratch_dir("headless");
        check(h.save_png(dir / "counter.png").has_value());
        check(std::filesystem::file_size(dir / "counter.png") == whole->size());
        Headless h1(at(HeadlessLevel::layout));
        check(!h1.png().has_value(), "no pixels at H1");
    });

    s.test("H3: an agent drives the session over the endpoint", [] {
        auto dir = scratch_dir("headless-h3");
        HeadlessOptions o = at(HeadlessLevel::session);
        o.app = "counter-h3";
        o.endpoint_directory = dir;
        Headless h(o);
        Counter c;
        h.mount("main", c.view());
        h.step();
        if (!h.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        std::atomic<bool> done{false};
        std::vector<Value> acted, captured;
        std::jthread agent_thread([&] {
            auto client = agent::Client::connect("counter-h3", dir);
            if (client) {
                acted = client->call(parse_json(R"({"op":"act","node":"main/inc","action":"activate","actor":"agent:test"})").value());
                captured = client->call(parse_json(R"({"op":"capture","node":"main/inc"})").value());
            }
            done = true;
        });
        for (int i = 0; i < 3000 && !done; ++i) {
            h.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        agent_thread.join();
        check(!acted.empty() && acted.back().get("exitCode")->as_int() == 0);
        check(c.count.peek() == 1);
        check(!captured.empty() && captured.back().get("exitCode")->as_int() == 0);
        check(captured.front().get("data") && captured.front().get("data")->get("base64")->as_string().starts_with("iVBOR"),
              "a PNG, base64");
    });

    s.test("every incremental frame is the full render of its list, byte for byte", [] {
        // Text edits, a colour transition, a moving box, hover, a scroll:
        // each frame's pixels (only the damaged rows drawn) against a fresh
        // render of the same list.
        auto label = signal(std::string("Hello"));
        auto offset = signal(0.0f);
        auto shift = signal(0.0f);
        std::vector<Element> rows;
        for (int i = 0; i < 12; ++i) rows.push_back(text({.id = std::format("r{}", i), .text = std::format("row {} · scrolled", i)}));
        HeadlessOptions o = at(HeadlessLevel::pixels);
        o.size = {320, 200};
        Headless h(o);
        h.mount("main", column({.padding = 8, .gap = 6}, {
                                   text({.id = "label", .text = label, .style = "title"}),
                                   box({.id = "mover", .width = 60, .height = 24, .background = "accent", .radius = 6,
                                        .translate_x = shift, .label = "Mover",
                                        .on = {{"press", [](const Value&) {}}},
                                        .animate = {{"translate_x", Transition::tween(seconds(0.3))}, {"background", Transition::tween(seconds(0.2))}}}),
                                   scroll({.id = "list", .y = offset, .height = 80, .gap = 2}, std::move(rows)),
                               }));
        h.settle();
        typeset::GlyphCache glyphs(fonts());
        int frames = 0;
        auto same_as_full = [&](std::string_view what) {
            const raster::Surface& inc = h.pixels();
            raster::Surface full(inc.width(), inc.height());
            render::CpuRenderer fresh;
            (void)fresh.render(h.runtime().display_list(), full, {&h.runtime().images(), &glyphs},
                               {.scale = o.scale, .background = o.background});
            ++frames;
            check(std::ranges::equal(inc.bytes(), full.bytes()), std::format("{} (frame {}): differs from the full render", what, frames));
        };
        same_as_full("first frame");
        label.set("Hello, incremental world");
        h.step();
        same_as_full("a longer label");
        shift.set(180.0f);
        for (int i = 0; i < 6; ++i) {
            h.step();
            same_as_full("the box moving");
        }
        const Rect mover = h.runtime().bounds(h.runtime().find("main/mover"));
        h.move({mover.x + 5, mover.y + 5});
        h.step();
        same_as_full("hover");
        h.move({2, 2});
        h.step();
        same_as_full("hover gone");
        offset.set(37.5f);
        h.step();
        same_as_full("scrolled");
        label.set("Hi");
        h.settle();
        same_as_full("settled");
    });

    return s.run(argc, argv);
}
