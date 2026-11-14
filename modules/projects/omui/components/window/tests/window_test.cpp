// The framework's own window frame (D-75) as a person, an agent and a host
// meet it: in a native window only its content; framework-drawn, a frame
// whose title bar moves the window and whose edges resize it, whose buttons
// and actions are the window.* verbs, that follows the window's state
// (maximized: no shadow, a restore button), names its buttons in the
// person's language, and draws the same pixels on every platform.
import std;

import omui.ui.window;
import omui.host.headless;
import mlibs.i18n.cldr;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

// A host's side of the window, recording what it was asked.
struct FakeHost final : WindowHost {
    bool                     framework = true;
    std::vector<std::string> asked;

    bool framework_drawn() const noexcept override { return framework; }
    void minimize() override { asked.emplace_back("minimize"); }
    void toggle_maximize() override { asked.emplace_back("toggle_maximize"); }
    void set_fullscreen(bool on) override { asked.emplace_back(on ? "fullscreen" : "windowed"); }
    void request_close() override { asked.emplace_back("close"); }
    bool pass_through(bool) override { return false; }
};

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

HeadlessOptions options(Size size, float scale = 1) {
    HeadlessOptions o;
    o.size = size;
    o.fonts = &fonts();
    o.scale = scale;
    return o;
}

Element notes(WindowFrameOptions o = {}) {
    o.title = std::string("Notes");
    return window_frame(std::move(o), {text({.id = "body", .text = "Milk, bread, 世界"})});
}

// Attached for the scope of a test: the process's window is shared.
struct Attached {
    FakeHost host;
    explicit Attached(Runtime& rt, bool framework = true) {
        host.framework = framework;
        window().set_state(WindowState::normal);
        window().set_active(true);
        window().attach(&host, rt.verbs());
    }
    ~Attached() {
        window().detach();
        window().set_state(WindowState::normal);
    }
};

std::uint64_t picture(Headless& h) {
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.value();
}

}  // namespace

UTest cases{"ui.window", [] {
    register_window_components();

    "in a native window the frame is only its content"_test = [] {
        Headless h(options({320, 200}));
        Attached a(h.runtime(), false);
        h.mount("main", notes());
        h.settle();
        const auto frame = h.tree().node("main");
        expect(frame && frame->role == Role::window && frame->name == "Notes") << "a window named by its title";
        expect(!h.tree().node("main/controls")) << "no buttons: the system draws them";
        const Rect body = h.runtime().bounds(h.runtime().find("main/body"));
        expect(body.x == 0 && body.y == 0) << std::format("the content at the window's corner ({}, {})", body.x, body.y);
        expect(eq(static_cast<int>(window_area_at(h.runtime(), {100, 10}, {320, 200})), static_cast<int>(WindowArea::client)));
    };

    "framework-drawn: title bar, buttons, edges — and the system moves and resizes"_test = [] {
        Headless h(options({480, 300}));
        Attached a(h.runtime());
        h.mount("main", notes());
        h.settle();
        for (const char* id : {"main/controls/minimize", "main/controls/maximize", "main/controls/close"}) {
            const auto b = h.tree().node(id);
            expect(b && b->role == Role::button) << id;
        }
        expect(eq(h.tree().node("main/controls/close")->name, std::string("Close")));
        // The content sits inside the shadow's reach and under the title bar.
        const Rect body = h.runtime().bounds(h.runtime().find("main/body"));
        expect(body.x == 16 && body.y == 16 + 36) << std::format("content at ({}, {})", body.x, body.y);
        const auto area = [&](float x, float y) { return window_area_at(h.runtime(), {x, y}, {480, 300}); };
        expect(area(200, 30) == WindowArea::drag) << "the title bar moves the window";
        const Rect close = h.runtime().bounds(h.runtime().find("main/controls/close"));
        expect(area(close.x + close.width / 2, close.y + close.height / 2) == WindowArea::client) << "a button is the interface's";
        expect(area(240, 150) == WindowArea::client) << "the content is the interface's";
        expect(area(4, 150) == WindowArea::resize_left && area(476, 150) == WindowArea::resize_right) << "the edges resize";
        expect(area(240, 4) == WindowArea::resize_top && area(240, 296) == WindowArea::resize_bottom);
        expect(area(4, 4) == WindowArea::resize_top_left && area(476, 296) == WindowArea::resize_bottom_right) << "and the corners";
        expect(area(30, 4) == WindowArea::resize_top_left) << "a corner reaches along its edges";
    };

    "the buttons and the frame's actions are the window.* verbs"_test = [] {
        Headless h(options({480, 300}));
        Attached a(h.runtime());
        h.mount("main", notes());
        h.settle();
        Runtime& rt = h.runtime();
        expect(eq(rt.act(rt.find("main/controls/minimize"), "activate", {}).exit_code().value_or(-1), 0));
        expect(eq(rt.act(rt.find("main/controls/close"), "activate", {}).exit_code().value_or(-1), 0));
        expect(eq(rt.act(rt.find("main"), "toggle_maximize", {}).exit_code().value_or(-1), 0));
        expect(a.host.asked == std::vector<std::string>{"minimize", "close", "toggle_maximize"}) << std::format("{}", a.host.asked);
        // A double click on the title bar maximizes; on the content it does nothing.
        a.host.asked.clear();
        rt.frame({seconds(1), {PointerEvent{.kind = PointerKind::down, .position = {200, 30}, .button = MouseButton::left, .buttons = 1, .clicks = 2},
                               PointerEvent{.kind = PointerKind::up, .position = {200, 30}, .button = MouseButton::left}}});
        rt.frame({seconds(1.5), {PointerEvent{.kind = PointerKind::down, .position = {240, 200}, .button = MouseButton::left, .buttons = 1, .clicks = 2},
                                 PointerEvent{.kind = PointerKind::up, .position = {240, 200}, .button = MouseButton::left}}});
        expect(a.host.asked == std::vector<std::string>{"toggle_maximize"}) << std::format("{}", a.host.asked);
        // Without a window the verbs say so.
        window().detach();
        expect(eq(rt.act(rt.find("main"), "close", {}).exit_code().value_or(-1), 1));
    };

    "it follows the window: maximized, no shadow and a restore button; inactive, muted"_test = [] {
        Headless h(options({480, 300}));
        Attached a(h.runtime());
        h.mount("main", notes());
        h.settle();
        const auto normal = picture(h);
        window().set_state(WindowState::maximized);
        h.settle();
        expect(h.tree().node("main/controls/maximize")->name == "Restore") << "the maximize button restores";
        const Rect body = h.runtime().bounds(h.runtime().find("main/body"));
        expect(body.x == 0 && body.y == 36) << std::format("no shadow: content at ({}, {})", body.x, body.y);
        expect(window_area_at(h.runtime(), {2, 150}, {480, 300}) == WindowArea::client) << "a maximized window does not resize";
        expect(window_area_at(h.runtime(), {240, 10}, {480, 300}) == WindowArea::drag) << "but it still moves (out of maximized)";
        window().set_state(WindowState::normal);
        window().set_active(false);
        h.settle();
        expect(picture(h) != normal) << "an inactive window looks different";
        window().set_active(true);
        h.settle();
        expect(picture(h) == normal) << "and active again, as it was";
        window().set_state(WindowState::fullscreen);
        h.settle();
        expect(!h.tree().node("main/controls")) << "fullscreen: no title bar";
    };

    "the buttons are named in the person's language"_test = [] {
        Headless h(options({480, 300}));
        Attached a(h.runtime());
        // CLDR's packs, as an application has them: zh-TW is Traditional Chinese by its likely script.
        languages().edit([](mlibs::i18n::Library& lib) { mlibs::i18n::cldr::add_to(lib); });
        h.mount("main", notes());
        languages().request({*mlibs::i18n::Locale::parse("zh-CN")});
        h.settle();
        const auto name = [&](const char* id) { return h.tree().node(std::format("main/controls/{}", id))->name; };
        expect(name("close") == "关闭" && name("minimize") == "最小化" && name("maximize") == "最大化") << name("close");
        languages().request({*mlibs::i18n::Locale::parse("zh-TW")});
        h.settle();
        expect(eq(name("close"), std::string("關閉")));
        languages().request({*mlibs::i18n::Locale::parse("en")});
        h.settle();
        expect(eq(name("close"), std::string("Close")));
    };

    "around an application: the frame beneath it, the application inside, the same paths"_test = [] {
        Headless h(options({480, 300}));
        Attached a(h.runtime());
        h.mount("main", column({}, {text({.id = "body", .text = "Milk"})}));
        {
            FramedWindow framed(h.runtime(), {.title = "Notes"});
            h.settle();
            expect(h.tree().node("main/body").has_value()) << "the application's paths are as they were";
            expect(h.tree().node("window/controls/close").has_value()) << "the frame is the slot window";
            const Rect main = h.runtime().bounds(h.runtime().find("main"));
            expect(main.x == 16 && main.y == 52 && main.width == 448) << std::format("inside the frame: {} {} {}", main.x, main.y, main.width);
            expect(window_area_at(h.runtime(), {240, 30}, {480, 300}) == WindowArea::drag) << "the frame's title bar moves the window";
            window().set_state(WindowState::maximized);
            h.settle();
            expect(h.runtime().bounds(h.runtime().find("main")).x == 0) << "maximized: no shadow around it";
            window().set_state(WindowState::normal);
            window().set_transparent(false);
            h.settle();
            expect(h.runtime().bounds(h.runtime().find("main")).x == 0) << "nothing behind the window: no shadow either";
            window().set_transparent(true);
            // Switched to the system's frame at run time: the application fills the window.
            window().attach(nullptr, h.runtime().verbs());
            a.host.framework = false;
            window().attach(&a.host, h.runtime().verbs());
            h.settle();
            expect(h.runtime().bounds(h.runtime().find("main")).y == 0) << "a native window: the application fills it";
        }
        h.settle();
        expect(!h.tree().node("window").has_value() && h.runtime().bounds(h.runtime().find("main")).y == 0) << "gone with it";
    };

    "the two looks are the same pixels on every platform"_test = [] {
        std::vector<std::uint64_t> got;
        for (auto [look, controls] : {std::pair{WindowFrameLook::standard, WindowFrameControls::end},
                                      std::pair{WindowFrameLook::traffic, WindowFrameControls::start}}) {
            Headless h(options({360, 180}, 2));
            Attached a(h.runtime());
            h.mount("main", notes({.controls = controls, .look = look}));
            h.settle();
            got.push_back(picture(h));
            const char* name = look == WindowFrameLook::traffic ? "ui-window-traffic" : "ui-window-standard";
            expect(h.save_png(scratch_dir(name) / "frame@2x.png").has_value());
        }
        std::println("ui.window standard {:016x} traffic {:016x}", got[0], got[1]);
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        expect(got[0] == 0x5afe29521643a930ULL) << std::format("standard {:016x}", got[0]);
        expect(got[1] == 0xda046a90b8c943caULL) << std::format("traffic {:016x}", got[1]);
    };
}};
