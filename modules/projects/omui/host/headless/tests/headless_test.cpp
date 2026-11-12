// The headless host: the four levels give the same semantic tree, the clock
// steps by frames, input by point or by id, pixels and PNG, pixel
// provenance, and an agent driving an H3 session.
import std;
import omui.host.headless;
import mlibs.agent.cts;
import omui.ui.base;
import mlibs.testing;
import mcpplibs.mtest;
import xrender.text;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
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

UTest cases{"headless", [] {

    "H0, H1 and H2 give the same semantic tree; only H1 and up have places"_test = [] {
        std::array<std::string, 3> trees;
        std::array<HeadlessLevel, 3> levels{HeadlessLevel::semantics, HeadlessLevel::layout, HeadlessLevel::pixels};
        for (std::size_t i = 0; i < 3; ++i) {
            Headless h(at(levels[i]));
            Counter c;
            h.mount("main", c.view());
            h.step();
            expect(h.click("main/inc")) << "click by id at every level";
            h.settle();
            expect(c.count.peek() == 1);
            trees[i] = essence(h.tree());
            auto label = h.tree().node("main/label");
            expect(label.has_value());
            if (i == 0) expect(!label->bounds && !label->states.has(State::offscreen)) << "H0: no layout, no place";
            else expect(label->bounds.has_value() && label->bounds->width > 0);
        }
        expect(trees[0] == trees[1] && trees[1] == trees[2]) << trees[0] + "---\n" + trees[2];
        expect(trees[0].find("main/label text '1 clicks'") != std::string::npos) << trees[0];
    };

    "the clock steps by frames; settle runs until nothing moves"_test = [] {
        Headless h(at(HeadlessLevel::layout));
        Counter c;
        h.mount("main", c.view());
        h.step();
        expect(h.frame_index() == 0 && h.now() == seconds(0));
        h.advance(seconds(0.5));
        expect(h.frame_index() == 30) << std::format("{}", h.frame_index());
        h.runtime().set(h.runtime().find("main/inc"), "background", Value("#ff0000"));
        auto frames = h.settle();
        expect(frames >= 12 && frames <= 14) << std::format("a 0.2 s transition at 60 Hz: {} frames", frames);
        expect(!h.tree().node("main/inc")->states.has(State::animating));
        expect(h.settle() == 1) << "already settled: one frame";
    };

    "a gamepad, injected: to the focus as the keyboard's keys go, its handlers by name, from its JSON form"_test = [] {
        Headless h(at(HeadlessLevel::layout));
        std::vector<std::string> got;
        auto log = [&](std::string what) { return [&got, what](const Value& v) { got.push_back(what + " " + v.json()); }; };
        Element player = painter({.id = "player", .width = 40, .height = 40, .focusable = true});
        for (std::string_view e : {"connect", "down", "up", "axis", "disconnect"}) player.on(std::format("gamepad_{}", e), log(std::string(e)));
        h.mount("main", column({}, {std::move(player)}));
        h.step();
        h.runtime().focus(h.runtime().find("main/player"));
        std::vector<InputEvent> events;
        for (std::string_view json : {R"({"type":"gamepad","kind":"connected","device":2,"name":"pad"})",
                                      R"({"type":"gamepad","kind":"down","device":2,"button":"south"})",
                                      R"({"type":"gamepad","kind":"axis","device":2,"axis":"leftX","value":-0.5})",
                                      R"({"type":"gamepad","kind":"up","device":2,"button":"south"})",
                                      R"({"type":"gamepad","kind":"disconnected","device":2})"})
            events.push_back(*event_from_value(*parse_json(json)));
        h.step(events);
        expect(got.size() == 5) << std::format("{}", got);
        if (got.size() != 5) return;
        expect(got[0].starts_with("connect ") && got[0].contains(R"("name":"pad")")) << got[0];
        expect(got[1].starts_with("down ") && got[1].contains(R"("button":"south")")) << got[1];
        expect(got[2].starts_with("axis ") && got[2].contains(R"("value":-0.5)")) << got[2];
        expect(got[3].starts_with("up ") && got[4].starts_with("disconnect ")) << got[4];
    };

    "pixels, PNG, regions, and the node behind a pixel"_test = [] {
        Headless h(at(HeadlessLevel::pixels));
        Counter c;
        h.mount("main", c.view());
        h.step();
        const auto& px = h.pixels();
        expect(px.width() == 240 && px.height() == 120);
        const Rect inc = h.runtime().bounds(h.runtime().find("main/inc"));
        auto p = px.at(static_cast<int>(inc.center().x), static_cast<int>(inc.center().y));
        expect(p.b > 200 && p.r < 120) << "the accent colour";
        expect(h.node_at_pixel(inc.center()) == h.runtime().find("main/inc"));
        auto whole = h.png();
        expect(whole.has_value() && whole->size() > 100 && (*whole)[1] == 'P');
        auto part = h.png(inc);
        expect(part.has_value() && part->size() < whole->size());
        auto dir = scratch_dir("headless");
        expect(h.save_png(dir / "counter.png").has_value());
        expect(std::filesystem::file_size(dir / "counter.png") == whole->size());
        Headless h1(at(HeadlessLevel::layout));
        expect(!h1.png().has_value()) << "no pixels at H1";
    };

    "a recording: frames kept, written as an animated PNG whose first frame is the first kept"_test = [] {
        Headless h(at(HeadlessLevel::pixels));
        Counter c;
        h.mount("main", c.view());
        h.step();
        h.record();
        for (int k = 0; k < 3; ++k) {
            expect(h.click("main/inc"));
            h.step();
            h.record();
        }
        h.record();  // the same again: one frame, shown longer
        expect(eq(h.recorded(), std::size_t{5}));
        const auto file = scratch_dir("headless-recording") / "counter.png";
        expect(h.save_recording(file, {.delay_num = 1, .delay_den = 4}).has_value());
        expect(eq(h.recorded(), std::size_t{0}));
        auto back = mlibs::image::load_png(file);  // a viewer without APNG: the first frame
        expect(back.has_value() && back->width == 240 && back->height == 120);
        expect(!h.save_recording(file).has_value()) << "nothing recorded";
    };

    "a recording to a .webp file is an animated WebP: the same frames and timing, and smaller than the APNG"_test = [] {
        // The same film on a fresh host each time: three clicks, each frame kept, the last kept twice.
        struct Film {
            Headless                               h{at(HeadlessLevel::pixels)};
            Counter                                c;
            std::vector<std::vector<std::uint8_t>> shown;  // each kept frame as a PNG gives it (straight)
            Film() {
                h.mount("main", c.view());
                h.step();
                keep();
                for (int k = 0; k < 3; ++k) {
                    expect(h.click("main/inc"));
                    h.step();
                    keep();
                }
                h.record();  // the same again: one frame, shown longer
            }
            void keep() {
                h.record();
                auto png = h.png();
                if (png) shown.push_back(mlibs::image::decode_png(*png)->rgba);
            }
        };
        const auto dir = scratch_dir("headless-recording-webp");
        Film exact;
        expect(exact.h.save_recording(dir / "counter.WebP", {.delay_num = 1, .delay_den = 4, .loops = 2, .lossless = true}).has_value());
        expect(eq(exact.h.recorded(), std::size_t{0}));
        auto anim = mlibs::image::load_animated_webp(dir / "counter.WebP");
        expect(anim.has_value()) << (anim ? "" : anim.error());
        if (anim) {
            expect(anim->width == 240 && anim->height == 120 && anim->loops == 2);
            expect(eq(anim->frames.size(), std::size_t{4}));
            const std::array<std::uint32_t, 4> lasts{250, 250, 250, 500};
            for (std::size_t k = 0; k < anim->frames.size() && k < 4 && k < exact.shown.size(); ++k) {
                expect(anim->frames[k].image.rgba == exact.shown[k]) << std::format("frame {} as rendered (lossless)", k);
                expect(eq(anim->frames[k].delay_ms, lasts[k]));
            }
        }
        Film lossy, png;
        expect(lossy.h.save_recording(dir / "counter.webp").has_value());
        expect(png.h.save_recording(dir / "counter.png").has_value());
        const auto webp_size = std::filesystem::file_size(dir / "counter.webp"), apng_size = std::filesystem::file_size(dir / "counter.png");
        std::println("counter recording: animated WebP (lossy) {} bytes, APNG {} bytes", webp_size, apng_size);
        expect(webp_size < apng_size) << "smaller";
        expect(mlibs::image::load_animated_webp(dir / "counter.webp").has_value());
        expect(mlibs::image::load_png(dir / "counter.png").has_value()) << "any other name: an animated PNG";
    };

    "H3: what an agent asks before the application is ready waits, and is answered from the application as it stands"_test = [] {
        auto dir = scratch_dir("headless-held");
        HeadlessOptions o = at(HeadlessLevel::session);
        o.app = "counter-held";
        o.endpoint_directory = dir;
        Headless h(o);
        if (!h.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        h.hold_requests(true);  // its view is not mounted yet: scripts still load
        std::atomic<bool> done{false};
        std::vector<Value> seen;
        std::jthread agent_thread([&] {
            if (auto client = agent::Client::connect("counter-held", dir)) seen = client->call(parse_json(R"({"op":"snapshot","interactive":true})").value());
            done = true;
        });
        for (int i = 0; i < 200; ++i) {
            h.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        expect(!done) << "held: a window with nothing in it yet is not what it is told";
        Counter c;
        h.mount("main", c.view());
        h.step();
        h.hold_requests(false);
        for (int i = 0; i < 3000 && !done; ++i) {
            h.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        agent_thread.join();
        expect(fatal(!seen.empty() && seen.back().get("exitCode")->as_int() == 0));
        expect(seen.front().json().contains("main/inc")) << "the counter's button, mounted before the release";
    };

    "H3: an agent drives the session over the endpoint"_test = [] {
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
        std::vector<Value> acted, captured, vectors, who, traced, drew, layers;
        const Point button = h.runtime().bounds(h.runtime().find("main/inc")).center();
        std::jthread agent_thread([&] {
            auto client = agent::Client::connect("counter-h3", dir);
            if (client) {
                acted = client->call(parse_json(R"({"op":"act","node":"main/inc","action":"activate","actor":"agent:test"})").value());
                captured = client->call(parse_json(R"({"op":"capture","node":"main/inc"})").value());
                vectors = client->call(parse_json(R"({"op":"capture","format":"svg"})").value());
                // On the same connection, the agent kernel's requests: everything this process holds.
                who = client->call(parse_json(R"({"op":"subjects"})").value());
                layers = client->call(parse_json(R"({"op":"frame.layers"})").value());
                const std::string at = std::format("[{},{}]", static_cast<int>(button.x), static_cast<int>(button.y));
                traced = client->call(parse_json(std::format(R"({{"op":"frame.at","at":{}}})", at)).value());
                drew = client->call(parse_json(std::format(R"({{"op":"ui.drew","at":{}}})", at)).value());
            }
            done = true;
        });
        for (int i = 0; i < 3000 && !done; ++i) {
            h.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        agent_thread.join();
        expect(!acted.empty() && acted.back().get("exitCode")->as_int() == 0);
        expect(c.count.peek() == 1);
        expect(!captured.empty() && captured.back().get("exitCode")->as_int() == 0);
        expect(captured.front().get("data") && captured.front().get("data")->get("base64")->as_string().starts_with("iVBOR")) << "a PNG, base64";
        // The same window as vectors: its nodes' drawings marked by their paths.
        expect(!vectors.empty() && vectors.back().get("exitCode")->as_int() == 0);
        const Value* picture = vectors.empty() ? nullptr : vectors.front().get("data");
        expect(picture && picture->get("format")->as_string() == "svg");
        const std::string svg = picture ? std::string(picture->get("svg")->as_string()) : std::string{};
        expect(svg.starts_with("<svg") && svg.contains(R"(data-node="main/inc")")) << "an SVG naming the button";
        expect(picture && picture->get("islands") && picture->get("islands")->list()) << "the islands listed (none or more)";

        // The process's subjects, found at the same endpoint: the interface, and the frame the host draws it into.
        const Value* subjects = who.empty() || !who.front().get("data") ? nullptr : who.front().get("data")->get("subjects");
        expect(fatal(subjects && subjects->list() && subjects->list()->size() == 2u)) << (who.empty() ? std::string("no answer") : who.front().json());
        const Value& ui = (*subjects->list())[0];
        const Value& frame = (*subjects->list())[1];
        expect(ui.get("name")->as_string() == "ui" && ui.get("interface")->as_string() == "openmui.tree@0.1");
        expect(ui.get("capabilities")->get("provenance")->as_bool()) << "this host draws: a pixel is traced";
        expect(frame.get("name")->as_string() == "frame" && frame.get("interface")->as_string() == "xrender.frame@0.1");
        expect(!layers.empty() && layers.front().get("data") && layers.front().get("data")->get("layers")->list()->front().get("name")->as_string() == "ui")
            << "the frame's one layer is the interface";
        // A pixel of the button: the frame says which layer and which of its objects; the interface says which node that is.
        const Value* chain = traced.empty() || !traced.front().get("data") ? nullptr : traced.front().get("data")->get("chain");
        expect(fatal(chain && chain->list() && chain->list()->size() == 1u));
        expect(chain->list()->front().get("layer")->as_string() == "ui" && chain->list()->front().get("object")->as_int() > 0);
        expect(!drew.empty() && drew.front().get("data") && drew.front().get("data")->json().contains("main/inc")) << (drew.empty() ? std::string() : drew.front().json());
    };

    "the agent contract on the interface where a host draws: a pixel's source too"_test = [] {
        namespace contract = mlibs::agent::cts;
        // A counter in a headless window, the protocol the host's endpoint would serve, the interface as a subject over it.
        struct Shown final : mlibs::agent::Subject {
            Headless                               h{at(HeadlessLevel::pixels)};
            std::optional<agent::Protocol>         protocol;
            std::optional<agent::InterfaceSubject> subject;
            Shown() {
                auto access = std::make_shared<AgentAccess>();
                access->set_policy({.mode = AgentMode::development, .source = "contract-test"});
                h.runtime().set_agent_access(std::move(access));
                h.mount("main", column({.padding = 12, .gap = 8}, {text_box({.id = "name", .label = "Name"}),
                                                                   box({.id = "inc", .width = 80, .height = 28, .background = "accent", .radius = 6, .label = "Add one"})}));
                h.step();
                agent::ProtocolOptions options{.app = "page", .default_actor = "agent:nobody"};
                options.capture = h.capture();
                options.pixel_owner = [this](Point at) { return h.node_at_pixel(at); };
                protocol.emplace(h.runtime(), std::move(options));
                subject.emplace(h.runtime(), *protocol, agent::InterfaceOptions{.traces_pixels = true});
            }
            std::string_view name() const noexcept override { return subject->name(); }
            std::string_view interface_name() const noexcept override { return subject->interface_name(); }
            mlibs::agent::Capabilities capabilities() const noexcept override { return subject->capabilities(); }
            std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return subject->operations(); }
            std::string_view listing() const noexcept override { return subject->listing(); }
            std::uint64_t state_hash() const override { return subject->state_hash(); }
            void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override {
                subject->operate(operation, request, answer);
            }
        };
        register_base_components();
        Point button;
        {
            Shown first;
            button = first.h.runtime().bounds(first.h.runtime().find("main/inc")).center();
        }
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<mlibs::agent::Subject> { return std::make_unique<Shown>(); },
            .read = R"({"op":"ui.snapshot"})",
            .change = R"({"op":"ui.act","node":"main/name","action":"set_value","args":{"value":"Ada"}})",
            .undo = R"({"op":"ui.undo"})",
            .preview = R"({"op":"ui.preview","steps":[{"node":"main/name","action":"set_value","args":{"value":"Grace"}}],"pictures":false})",
            .explain = R"({"op":"ui.timeline"})",
            .source = std::format(R"({{"op":"ui.drew","at":[{},{}]}})", static_cast<int>(button.x), static_cast<int>(button.y)),
        };
        const auto report = contract::run(probe, {.subject = "omui.agent on omui.host.headless"});
        std::println("{}", report.text());
        expect(report.ok()) << report.text();
        const auto status = [&](std::string_view id) { return std::ranges::find(report.outcomes, id, &mlibs::cts::Outcome::id)->status; };
        expect(status("agent.source.chain") == mlibs::cts::Status::passed) << "a pixel traced to its node";
        std::vector<std::string> unsupported;
        for (const auto& outcome : report.outcomes)
            if (outcome.status == mlibs::cts::Status::unsupported) unsupported.push_back(outcome.id);
        expect(unsupported == std::vector<std::string>{"agent.time.replays", "agent.observe.budget"}) << "replayed time and a budget of bytes are not the interface's";
    };

    "a frame as SVG: the same frame, the nodes named, glyphs as outlines, the same bytes twice"_test = [] {
        HeadlessOptions o;
        o.size = {240, 120};
        Headless h(o);
        Counter c;
        h.mount("main", c.view());
        h.step();
        const auto first = h.svg();
        expect(first.has_value()) << (first ? "" : first.error());
        if (!first) return;
        expect(first->svg.contains(R"(data-node="main/inc")")) << "the button's path";
        expect(first->svg.contains(R"(<path id="g)") && first->svg.contains(R"(<use href="#g)")) << "its label's glyphs as outlines";
        const auto again = h.svg();
        expect(again && again->svg == first->svg) << "the same bytes for the same frame";
        const auto region = h.svg(Rect{10, 10, 100, 50});
        expect(region && region->svg.contains(R"(viewBox="10 10 100 50")")) << "a region is the picture's view";
    };

    "the program's own: the drawing as SVG through the runtime, a file offered and kept; a name with a path refused"_test = [] {
        HeadlessOptions o;
        o.size = {240, 120};
        Headless h(o);
        Counter c;
        h.mount("main", c.view());
        h.step();
        Runtime& rt = h.runtime();
        const auto drawn = rt.capture_svg();
        expect(drawn && *drawn == h.svg()->svg) << "the same picture an agent captures";
        if (!drawn) return;
        expect(rt.offer_file({"counter.svg", "image/svg+xml", *drawn})) << "taken";
        expect(!rt.offer_file({"../counter.svg", "image/svg+xml", *drawn})) << "a name with a path: refused";
        expect(rt.offered_files().size() == 1 && rt.offered_files().front().name == "counter.svg" && rt.offered_files().front().bytes == *drawn) << "kept (the refused one not)";
        for (int k = 0; k < 20; ++k) (void)rt.offer_file({std::format("{}.txt", k), "text/plain", "x"});
        expect(rt.offered_files().size() == Runtime::offered_kept && rt.offered_files().back().name == "19.txt") << "only the last ones kept";
        Runtime bare;
        expect(!bare.capture_svg()) << "a runtime without a host: no vectors";
    };

    "every incremental frame is the full render of its list, byte for byte"_test = [] {
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
        xrender::text::GlyphCache glyphs(fonts());
        int frames = 0;
        auto same_as_full = [&](std::string_view what) {
            const xrender::raster::Surface& inc = h.pixels();
            xrender::raster::Surface full(inc.width(), inc.height());
            xrender::CpuRenderer fresh;
            (void)fresh.render(h.runtime().display_list(), full, {&h.runtime().images(), &glyphs},
                               {.scale = o.scale, .background = o.background});
            ++frames;
            expect(std::ranges::equal(inc.bytes(), full.bytes())) << std::format("{} (frame {}): differs from the full render", what, frames);
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
    };
}};
