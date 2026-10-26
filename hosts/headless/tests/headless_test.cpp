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

    return s.run(argc, argv);
}
