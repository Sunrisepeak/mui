// Platform behaviour (W9, D6): one interface, drawn the same everywhere, that
// answers a person as their platform does — asking for a context menu (a right
// button pressed, or released on Windows; Control and a click on macOS; a
// touch held; the menu key or Shift+F10), a scroll's edges (Apple's stretch and
// bounce, a stop elsewhere) and the text keys (Command's line and text ends).
// The platform is chosen here, so the same input does the same on every
// machine; the process's own is the host's to set.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

HeadlessOptions options(Size size = {480, 360}) {
    HeadlessOptions made;
    made.size = size;
    made.fonts = &fonts();
    return made;
}

// The platform for one case, and back to the default after it.
struct Following {
    explicit Following(Platform platform) { set_platform(platform); }
    ~Following() { set_platform(Platform::freedesktop); }
};

bool shown(Headless& h, std::string_view id) {
    const auto node = h.tree().node(id);
    return node && node->states.has(State::expanded);
}

PointerEvent pointer(PointerKind kind, Point at, PointerType type = PointerType::mouse, MouseButton button = MouseButton::left,
                     Modifiers mods = {}) {
    PointerEvent made;
    made.kind = kind;
    made.position = at;
    made.type = type;
    made.button = button;
    made.buttons = kind == PointerKind::up ? 0 : button == MouseButton::right ? 2 : 1;
    made.mods = mods;
    return made;
}

// A document (a button, so it takes focus) and its context menu.
struct Document {
    Headless h{options()};
    int      opened = 0;  // how many times the document itself was activated
    Document() {
        h.mount("main", column({.padding = 20},
                               {button({.id = "doc", .text = std::string("Document"), .on_activated = [this] { ++opened; }, .width = 200, .height = 120}),
                                context_menu({.id = "menu", .label = "Document actions"},
                                             {menu_item({.id = "cut", .text = std::string("Cut")}), menu_item({.id = "paste", .text = std::string("Paste")})})}));
        h.settle();
    }
    void close() {
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("main/menu", "dismiss", call);
        h.settle();
    }
};

// Where the scroll view's content is drawn: its offset, as a person sees it (negative: pulled past the top).
float offset_of(Headless& h) {
    const Rect view = h.runtime().drawn_bounds(h.runtime().find("main/list"));
    const Rect content = h.runtime().drawn_bounds(h.runtime().find("main/list/first"));
    return view.y - content.y;
}

float stated_offset(Headless& h) {
    const auto node = h.tree().node("main/list");
    return node && node->value.get("y") ? static_cast<float>(node->value.get("y")->as_double()) : -1.0f;
}

void mount_list(Headless& h) {
    std::vector<Element> rows;
    for (int row = 0; row < 10; ++row)
        rows.push_back(box({.id = row == 0 ? std::string("first") : std::format("row-{}", row), .height = 60,
                            .background = Fill(std::string(row % 2 ? "surface" : "surface_variant"))}));
    h.mount("main", column({}, {scroll({.id = "list", .width = Dim(300.0f), .height = Dim(200.0f)}, std::move(rows))}));
    h.settle();
}

// A finger drawn from `from` to `to` over `steps` frames, then lifted (or held).
void drag(Headless& h, Point from, Point to, int steps, bool lift = true) {
    h.step({pointer(PointerKind::down, from, PointerType::touch)});
    for (int step = 1; step <= steps; ++step) {
        const float part = static_cast<float>(step) / static_cast<float>(steps);
        h.step({pointer(PointerKind::move, {from.x + (to.x - from.x) * part, from.y + (to.y - from.y) * part}, PointerType::touch)});
    }
    if (lift) h.step({pointer(PointerKind::up, to, PointerType::touch)});
}

}  // namespace

static UTest cases{"ui.base.platform_behaviour", [] {
    register_base_components();

    "a context menu, asked for each platform's way: a right button pressed, or released on Windows; Control and a click on macOS; a touch held; the menu key or Shift+F10"_test = [] {
        const Point over{70, 60};
        {
            Following platform(Platform::freedesktop);
            Document d;
            d.h.step({pointer(PointerKind::down, over, PointerType::mouse, MouseButton::right)});
            expect(shown(d.h, "main/menu")) << "freedesktop: open as the right button goes down";
            d.h.step({pointer(PointerKind::up, over, PointerType::mouse, MouseButton::right)});
            d.close();
            d.h.step({pointer(PointerKind::down, over, PointerType::mouse, MouseButton::left, Modifier::ctrl)});
            d.h.step({pointer(PointerKind::up, over, PointerType::mouse, MouseButton::left, Modifier::ctrl)});
            expect(!shown(d.h, "main/menu") && d.opened == 1) << "Control and a click is a click here";
        }
        {
            Following platform(Platform::windows);
            Document d;
            d.h.step({pointer(PointerKind::down, over, PointerType::mouse, MouseButton::right)});
            expect(!shown(d.h, "main/menu")) << "windows: not yet, as it goes down";
            d.h.step({pointer(PointerKind::up, over, PointerType::mouse, MouseButton::right)});
            expect(shown(d.h, "main/menu")) << "windows: as it is released";
            const Rect menu = d.h.runtime().bounds(d.h.runtime().find("main/menu"));
            expect(std::abs(menu.x - over.x) < 1 && std::abs(menu.y - over.y) < 1) << "at the pointer";
        }
        {
            Following platform(Platform::macos);
            Document d;
            d.h.step({pointer(PointerKind::down, over, PointerType::mouse, MouseButton::left, Modifier::ctrl)});
            expect(shown(d.h, "main/menu")) << "macos: Control and a click asks";
            d.h.step({pointer(PointerKind::up, over, PointerType::mouse, MouseButton::left, Modifier::ctrl)});
            d.h.settle();
            expect(d.opened == 0) << "and does not press the document";
        }
        for (const Platform each : {Platform::ios, Platform::android}) {
            Following platform(each);
            const double hold = conventions(each).long_press;
            Document d;
            // Held still: the menu, and the lift activates nothing.
            d.h.step({pointer(PointerKind::down, over, PointerType::touch)});
            d.h.advance(seconds(hold * 0.6));
            expect(!shown(d.h, "main/menu")) << std::format("{}: not before {} s", name(each), hold);
            d.h.advance(seconds(hold * 0.6));
            expect(shown(d.h, "main/menu")) << std::format("{}: held {} s", name(each), hold);
            d.h.step({pointer(PointerKind::up, over, PointerType::touch)});
            d.h.settle();
            expect(d.opened == 0) << "the lift after a long press activates nothing";
            d.close();
            // A short touch is a tap; one that moves is a drag: neither asks.
            d.h.step({pointer(PointerKind::down, over, PointerType::touch)});
            d.h.step({pointer(PointerKind::up, over, PointerType::touch)});
            d.h.settle();
            expect(!shown(d.h, "main/menu") && d.opened == 1) << "a tap activates";
            d.h.step({pointer(PointerKind::down, over, PointerType::touch)});
            d.h.step({pointer(PointerKind::move, {over.x + 30, over.y}, PointerType::touch)});
            d.h.advance(seconds(hold * 2));
            expect(!shown(d.h, "main/menu")) << "a touch that travelled is a drag";
            d.h.step({pointer(PointerKind::up, {over.x + 30, over.y}, PointerType::touch)});
        }
        {
            Document d;
            d.h.runtime().focus(d.h.runtime().find("main/doc"));
            d.h.key(Key::f10, Modifier::shift);
            expect(shown(d.h, "main/menu")) << "Shift+F10";
            const Rect menu = d.h.runtime().bounds(d.h.runtime().find("main/menu"));
            const Rect doc = d.h.runtime().bounds(d.h.runtime().find("main/doc"));
            expect(std::abs(menu.y - doc.bottom()) < 1) << "at its element's lower left";
            d.close();
            d.h.runtime().focus(d.h.runtime().find("main/doc"));
            d.h.key(Key::menu);
            expect(shown(d.h, "main/menu")) << "the menu key";
        }
    };

    "a scroll's edges: Apple's stretch past an edge, less the further it is pulled, springing back without overshoot; a fling's bounce at the end; a stop elsewhere — what the tree states stays inside"_test = [] {
        // 600 of content in 200: the offset runs 0 to 400.
        {
            Following platform(Platform::freedesktop);
            Headless h(options());
            mount_list(h);
            drag(h, {150, 50}, {150, 170}, 8, false);
            expect(std::abs(offset_of(h)) < 0.5f) << "freedesktop: held at the top, " << offset_of(h);
            h.step({pointer(PointerKind::up, {150, 170}, PointerType::touch)});
            drag(h, {150, 190}, {150, 10}, 3);  // a fling toward the end
            float furthest = 0;
            for (int frame = 0; frame < 180; ++frame) {
                h.step();
                furthest = std::max(furthest, offset_of(h));
            }
            expect(furthest <= 400.5f && std::abs(offset_of(h) - 400) < 0.5f) << "stopped at the end, never past: " << furthest;
        }
        {
            Following platform(Platform::macos);
            Headless h(options());
            mount_list(h);
            drag(h, {150, 50}, {150, 170}, 8, false);  // 120 pulled down at the top
            const float stretched = offset_of(h);
            // d·(1 − 1/(x·0.55/d + 1)) with x = 120, d = 200: 49.6.
            expect(stretched < -45 && stretched > -55) << "macos: it stretches, less than the finger moved: " << stretched;
            expect(std::abs(stated_offset(h)) < 0.5f) << "what the tree states stays at the top";
            h.advance(seconds(0.1));  // held there, then let go: it throws nothing
            expect(std::abs(offset_of(h) - stretched) < 0.5f) << "held, it stays stretched";
            h.step({pointer(PointerKind::up, {150, 170}, PointerType::touch)});
            // Let go: back to the edge, every frame nearer, never past it.
            std::vector<float> back;
            for (int frame = 0; frame < 60; ++frame) {
                h.step();
                back.push_back(offset_of(h));
            }
            bool nearer = true, past = false;
            for (std::size_t frame = 1; frame < back.size(); ++frame) {
                nearer = nearer && back[frame] >= back[frame - 1] - 0.01f;
                past = past || back[frame] > 0.5f;
            }
            expect(nearer && !past) << "springs back without overshoot";
            expect(back[3] > stretched && back[3] < -5) << "a moment after: on its way, " << back[3];
            expect(std::abs(back.back()) < 0.5f) << "and rests at the edge: " << back.back();
            // A fling at the end: on past it a little, and back.
            drag(h, {150, 190}, {150, 10}, 3);
            float furthest = 0;
            for (int frame = 0; frame < 240; ++frame) {
                h.step();
                furthest = std::max(furthest, offset_of(h));
            }
            expect(furthest > 405 && furthest < 520) << "bounced past the end: " << furthest;
            expect(std::abs(offset_of(h) - 400) < 0.5f && std::abs(stated_offset(h) - 400) < 0.5f) << "and back to rest at it";
        }
    };

    "the text keys: Command with an arrow reaches the text's ends, with Backspace the line's start (macOS); Control and Backspace a word elsewhere"_test = [] {
        const auto value_of = [](Headless& h) {
            const auto node = h.tree().node("main/field");
            return node ? std::string(node->value.as_string()) : std::string();
        };
        for (const Platform each : {Platform::macos, Platform::freedesktop}) {
            Following platform(each);
            Headless h(options());
            h.mount("main", column({.padding = 20}, {text_box({.id = "field", .label = "Note"})}));
            h.settle();
            h.runtime().focus(h.runtime().find("main/field"));
            h.type("hello brave world");
            h.step();
            const Modifiers command = Modifiers(primary_modifier());
            if (each == Platform::macos) {
                h.key(Key::up, command);
                h.type(">");
                h.key(Key::down, command);
                h.type("!");
                h.step();
                expect(eq(value_of(h), std::string(">hello brave world!"))) << "Command and Up, Down: the text's ends";
                h.key(Key::backspace, Modifier::alt);
                h.step();
                expect(eq(value_of(h), std::string(">hello brave "))) << "Option and Backspace: a word";
                h.key(Key::backspace, command);
                h.step();
                expect(eq(value_of(h), std::string())) << "Command and Backspace: to the line's start";
            } else {
                h.key(Key::backspace, Modifier::ctrl);
                h.step();
                expect(eq(value_of(h), std::string("hello brave "))) << "Control and Backspace: a word";
            }
        }
    };
}};
