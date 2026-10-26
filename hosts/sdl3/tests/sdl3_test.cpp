// The desktop host on SDL's offscreen video driver with software rendering:
// what the window shows is the CPU frame; input goes through SDL's queue and
// the host's translation; idle frames are not presented; the clipboard is the
// system's; an agent's request wakes a sleeping loop.
import std;
import openmui.host.sdl3;
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

DesktopOptions offscreen(std::string app = "sdl3-test") {
    DesktopOptions o;
    o.title = "openmui test";
    o.size = {240, 160};
    o.video_driver = "offscreen";
    o.render_driver = "software";
    o.vsync = false;
    o.fonts = &fonts();
    o.app = std::move(app);
    o.agent = false;
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"host.sdl3"};

    s.test("the window shows the frame, presented only when it changes", [] {
        Desktop d(offscreen());
        auto opened = d.open();
        check(opened.has_value(), opened ? "" : opened.error());
        if (!opened) return;
        auto color = signal(std::string("#ff0000"));
        d.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40, .background = Prop<Fill>(Source{std::nullopt, [color] { return Value(color.get()); }, {}})})}));
        check(d.step());
        check(d.frames_shown() == 1);
        auto px = d.window_pixels();
        check(px.has_value(), px ? "" : px.error());
        if (!px) return;
        check(px->width() == static_cast<int>(240 * d.scale()));
        auto at = px->at(static_cast<int>(40 * d.scale()), static_cast<int>(40 * d.scale()));
        check(at.r == 255 && at.g == 0 && at.b == 0, std::format("{} {} {}", at.r, at.g, at.b));
        auto bg = px->at(5, 5);
        check(bg.r == 255 && bg.g == 255 && bg.b == 255, "the background");
        d.step();
        d.step();
        check(d.frames_shown() == 1, "nothing changed: nothing presented");
        color.set("#0000ff");
        d.step();
        check(d.frames_shown() == 2);
        auto blue = d.window_pixels();
        auto b = blue->at(static_cast<int>(40 * d.scale()), static_cast<int>(40 * d.scale()));
        check(b.b == 255 && b.r == 0);
    });

    s.test("input goes through SDL: pointer, keys, text, wheel, close", [] {
        Desktop d(offscreen());
        if (!d.open()) return;
        int presses = 0;
        std::vector<std::string> keys, texts;
        Point wheel;
        d.mount("main", column({.padding = 10, .gap = 10}, {
                                   box({.id = "button", .width = 60, .height = 30, .label = "Go",
                                        .on = {{"press", [&](const Value&) { ++presses; }}}}),
                                   painter({.id = "pad", .width = 100, .height = 60, .focusable = true,
                                            .on_event = [&](EventContext&, const InputEvent& e) {
                                                if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down)
                                                    keys.push_back(std::format("{}{}", k->mods.has(Modifier::ctrl) ? "Ctrl+" : "", name(k->key)));
                                                if (const auto* t = std::get_if<TextEvent>(&e)) texts.push_back(t->text);
                                                if (const auto* w = std::get_if<WheelEvent>(&e)) wheel = w->delta;
                                                return std::holds_alternative<KeyEvent>(e) || std::holds_alternative<TextEvent>(e);
                                            }}),
                               }));
        d.step();
        const Rect b = d.runtime().bounds(d.runtime().find("main/button"));
        PointerEvent down;
        down.kind = PointerKind::down;
        down.button = MouseButton::left;
        down.position = b.center();
        PointerEvent up = down;
        up.kind = PointerKind::up;
        std::array<InputEvent, 2> click{down, up};
        d.synthesize(click);
        d.step();
        check(presses == 1, "a click through SDL's queue");
        const Rect pad = d.runtime().bounds(d.runtime().find("main/pad"));
        PointerEvent focus = down;
        focus.position = pad.center();
        PointerEvent release = focus;
        release.kind = PointerKind::up;
        KeyEvent z;
        z.key = Key::z;
        z.mods = Modifier::ctrl;
        KeyEvent z_up = z;
        z_up.kind = KeyKind::up;
        WheelEvent w;
        w.position = pad.center();
        w.delta = {0, 80};
        std::array<InputEvent, 6> more{focus, release, z, z_up, TextEvent{"héllo"}, w};
        d.synthesize(more);
        d.step();
        check(keys == std::vector<std::string>{"Ctrl+Z"}, std::format("{}", keys));
        check(texts == std::vector<std::string>{"héllo"});
        check(wheel.y == 80, std::format("two notches: {}", wheel.y));
        std::array<InputEvent, 1> close{CloseRequestEvent{}};
        d.synthesize(close);
        check(!d.step(), "closing the window ends the loop");
    });

    s.test("the clipboard is the system's", [] {
        Desktop d(offscreen());
        if (!d.open()) return;
        d.runtime().set_clipboard_text("from openmui");
        check(d.runtime().clipboard_text() == "from openmui");
    });

    s.test("an agent's request wakes a sleeping loop", [] {
        auto dir = scratch_dir("sdl3-agent");
        DesktopOptions o = offscreen("sdl3-agent");
        o.agent = true;
        o.agent_directory = dir;
        Desktop d(o);
        if (!d.open()) return;
        d.mount("main", column({}, {box({.id = "ok", .width = 10, .height = 10, .label = "OK", .on = {{"press", [](const Value&) {}}}})}));
        d.step();
        if (!d.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        std::atomic<bool> done{false};
        std::vector<Value> got;
        std::jthread client([&] {
            auto c = agent::Client::connect("sdl3-agent", dir);
            if (c) got = c->call(parse_json(R"({"op":"find","q":{"name":"OK"}})").value(), std::chrono::seconds(10));
            done = true;
            d.wake();  // the loop may have gone back to sleep after answering
        });
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 50 && !done; ++i) d.step(true);  // sleeps: nothing animates, no timers
        client.join();
        const auto took = std::chrono::steady_clock::now() - start;
        check(!got.empty() && got.back().get("exitCode")->as_int() == 0, got.empty() ? "no answer" : got.back().json());
        check(took < std::chrono::seconds(5), "woken by the request, not by a timeout");
    });

    return s.run(argc, argv);
}
