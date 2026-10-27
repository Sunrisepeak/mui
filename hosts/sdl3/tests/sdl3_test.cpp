// The desktop host on SDL's offscreen video driver with software rendering:
// what the window shows is the CPU frame; input goes through SDL's queue and
// the host's translation; idle frames are not presented; the clipboard is the
// system's; an agent's request wakes a sleeping loop. And on the machine's
// GPU (lavapipe in CI; required with OMUI_REQUIRE_GPU=1): the swapchain shows
// the GPU renderer's frame, and a program's effect file, edited while it
// runs, shows on the next frame.
import std;
import mcpplibs.os;
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
    o.renderer = DesktopOptions::Renderer::cpu;  // the CPU frame, read back exactly
    return o;
}

void write(const std::filesystem::path& f, std::string_view text) {
    std::error_code ec;
    const auto before = std::filesystem::exists(f) ? std::filesystem::last_write_time(f) : std::filesystem::file_time_type::min();
    std::ofstream(f, std::ios::binary) << text;
    // Later than what was loaded, whatever the clock's grain.
    if (before != std::filesystem::file_time_type::min())
        std::filesystem::last_write_time(f, std::max(std::filesystem::last_write_time(f), before + std::chrono::seconds(1)), ec);
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

    s.test("on the GPU: the swapchain shows the frame, and an effect file edited while it runs shows on the next frame", [] {
        const auto dir = std::filesystem::temp_directory_path() / "openmui-host-effects";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        write(dir / "invert.effect.omui.toml", "name = \"invert\"\ndoc = \"The colours turned over.\"\n");
        const std::string invert = R"(import openmui_effect;
[shader("fragment")]
float4 invert_fs(EffectPixel px) : SV_Target {
    const uint4 s = source_texel(at(px));
    return out8(uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a));
}
)";
        write(dir / "invert.slang", invert);
        DesktopOptions o = offscreen("sdl3-gpu");
        o.renderer = DesktopOptions::Renderer::gpu;
        o.effects = dir;
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        Desktop d(o);
        auto opened = d.open();
        if (!opened) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") check(false, std::format("OMUI_REQUIRE_GPU=1: {}", opened.error()));
            else std::println("  no GPU for the window here ({})", opened.error());
            return;
        }
        check(d.gpu(), "the GPU draws");
        check(d.effects() && d.effects()->names() == std::vector<std::string>{"invert"}, "the effect file loaded with the window");
        d.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40, .background = Fill(Color::hex(0xff0000))})}));
        check(d.step());
        check_eq(d.frames_shown(), std::size_t{1});
        const auto pixel = [&](int x, int y) -> std::optional<raster::Rgba8> {
            auto px = d.window_pixels();
            check(px.has_value(), px ? "" : px.error());
            if (!px) return std::nullopt;
            return px->at(static_cast<int>(x * d.scale()), static_cast<int>(y * d.scale()));
        };
        if (auto at = pixel(40, 40)) check(at->r == 255 && at->g == 0 && at->b == 0, std::format("red: {} {} {}", at->r, at->g, at->b));
        if (auto bg = pixel(5, 5)) check(bg->r == 255 && bg->g == 255 && bg->b == 255, "the background");

        // The node under the program's effect.
        Value fx;
        fx.set("name", "invert");
        d.runtime().set(d.runtime().find("main/swatch"), "pixel_effect", fx);
        d.step();
        if (auto at = pixel(40, 40)) check(at->r == 0 && at->g == 255 && at->b == 255, std::format("inverted: {} {} {}", at->r, at->g, at->b));

        // The shader edited while the program runs: on the next frame. Three
        // edits (green, blue, green): the latency is the fastest of them — a
        // CI machine busy compiling other members adds its own time to some.
        std::vector<std::chrono::milliseconds> took;
        for (const auto& [code, want] : std::array<std::pair<std::string_view, raster::Rgba8>, 3>{{
                 {"uint4(0, s.a, 0, s.a)", {0, 255, 0, 255}}, {"uint4(0, 0, s.a, s.a)", {0, 0, 255, 255}}, {"uint4(0, s.a, 0, s.a)", {0, 255, 0, 255}}}}) {
            std::string edited = invert;
            edited.replace(edited.find("uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a)"), std::string_view("uint4(s.a - s.r, s.a - s.g, s.a - s.b, s.a)").size(), code);
            write(dir / "invert.slang", edited);
            const auto from = std::chrono::steady_clock::now();
            d.step();
            took.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - from));
            if (auto at = pixel(40, 40)) check(at->r == want.r && at->g == want.g && at->b == want.b, std::format("edited: {} {} {}", at->r, at->g, at->b));
        }
        std::println("  from the saved shader to the frame showing it: {} ms, {} ms, {} ms", took[0].count(), took[1].count(), took[2].count());
        check(std::ranges::min(took) < std::chrono::milliseconds(500), "M4 ③: within 500 ms");
        std::filesystem::remove_all(dir, ec);
    });

    s.test("frames as often as they are wanted (D-70): a lease of 20 Hz paces the loop, idle sleeps", [] {
        Desktop d(offscreen("sdl3-pacing"));
        d.mount("main", column({.width = 240, .height = 160}, {text({.text = "paced"})}));
        check(d.step(false));
        // Something asks for 20 frames a second: ten frames take about half a second.
        auto lease = d.runtime().demand_frames({.min_hz = 10, .max_hz = 20, .preferred_hz = 20, .reason = "test"});
        check(d.step(false));  // the demand is the frame's: from the next one on
        const auto from = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) check(d.step(true));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
        std::println("  10 frames at 20 Hz: {:.3f} s", took);
        check(took > 0.40 && took < 2.0, std::format("paced: {:.3f} s", took));
        check(d.runtime().frame_demand().reason == "test");
        lease.release();
        check(d.step(false));
        check(d.runtime().frame_demand().idle(), "idle once released");
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
