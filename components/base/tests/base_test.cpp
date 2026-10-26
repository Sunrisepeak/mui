// The Core components, driven as people and agents drive them: pointer,
// keys, actions; two-way bindings and events; semantics; input methods and
// the clipboard; immediate mode with the real components; and a gallery
// whose pixels are pinned for every platform.
import std;
import openmui.ui.base;
import openmui.host.headless;
import openmui.image;
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

HeadlessOptions options(Size size = {360, 240}) {
    HeadlessOptions o;
    o.size = size;
    o.fonts = &fonts();
    return o;
}

Rect bounds(Headless& h, std::string_view id) { return h.runtime().bounds(h.runtime().find(id)); }

spec::SemanticNode sem(Headless& h, std::string_view id) { return *h.tree().node(id); }

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.answer = std::move(answer);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

void key(Headless& h, Key k, Modifiers m = {}) { h.key(k, m); }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.base"};
    register_base_components();

    s.test("button: pointer, keys and agents activate it alike; the verb classifies it (S-1)", [] {
        Headless h(options());
        int activated = 0, deleted = 0;
        (void)h.runtime().verbs().add({"doc.delete", "Delete", Effect::irreversible, "delete", {}}, [&](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   button({.id = "save", .text = "Save", .variant = ButtonVariant::primary,
                                           .on_activated = [&] { ++activated; }}),
                                   button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger, .verb = "doc.delete"}),
                                   button({.id = "off", .disabled = true, .text = "Off"}),
                               }));
        h.step();
        auto save = sem(h, "main/save");
        check(save.role == Role::button && save.name == "Save" && save.states.has(State::focusable));
        check(spec::check(save).empty());
        check(h.click("main/save"));
        check(activated == 1, "a click");
        check(h.runtime().path(h.runtime().focused()) == "main/save", "a press focuses (without the ring)");
        key(h, Key::enter);
        key(h, Key::space);
        check(activated == 3, "Enter and Space");
        check(act(h, "main/save", "activate") == 0 && activated == 4, "an agent");
        // The verb makes `activate` irreversible: asked for an answer, by pointer as by agent.
        auto del = sem(h, "main/delete");
        check(del.action("activate")->effect == Effect::irreversible && del.action("activate")->answer == "delete");
        check(h.click("main/delete") && deleted == 0);
        check(act(h, "main/delete", "activate") == 2 && deleted == 0);
        check(act(h, "main/delete", "activate", {}, "delete") == 0 && deleted == 1);
        check(act(h, "main/off", "activate") == 1, "disabled");
        check(sem(h, "main/off").states.has(State::disabled));
        // Size: the label and its padding.
        const Rect b = bounds(h, "main/save");
        check(b.height == 36 && b.width > 60, std::format("{}x{}", b.width, b.height));
    });

    s.test("toggle and checkbox: two-way bindings, events, states", [] {
        Headless h(options());
        auto wifi = signal(false);
        auto agree = signal(false);
        std::vector<bool> changes;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   toggle({.id = "wifi", .checked = wifi, .label = "Wi-Fi",
                                           .on_changed = [&](bool v) { changes.push_back(v); }}),
                                   checkbox({.id = "agree", .checked = agree, .label = "I agree"}),
                                   checkbox({.id = "some", .indeterminate = true, .label = "Some"}),
                               }));
        h.step();
        check(sem(h, "main/wifi").role == Role::switch_ && sem(h, "main/wifi").name == "Wi-Fi");
        check(h.click("main/wifi"));
        check(wifi.peek() && changes == std::vector<bool>{true}, "the signal learns the click");
        check(sem(h, "main/wifi").states.has(State::checked));
        wifi.set(false);
        h.step();
        check(!sem(h, "main/wifi").states.has(State::checked), "and the switch follows the signal");
        check(act(h, "main/agree", "toggle") == 0 && agree.peek());
        h.focus_next();  // from the switch (focused by its click) to the checkbox
        check(h.runtime().path(h.runtime().focused()) == "main/agree");
        key(h, Key::space);
        check(!agree.peek(), "Space on the focused checkbox");
        auto some = sem(h, "main/some");
        check(some.states.has(State::mixed) && some.value == Value("mixed"));
        check(act(h, "main/some", "toggle") == 0);
        check(sem(h, "main/some").states.has(State::checked) && !sem(h, "main/some").states.has(State::mixed),
              "indeterminate goes to checked");
        h.settle();
    });

    s.test("slider: drag, keys, actions with symbols, steps, committed", [] {
        Headless h(options());
        auto volume = signal(30.0);
        std::vector<double> committed;
        int changed = 0;
        h.mount("main", column({.padding = 12}, {
                                   slider({.id = "volume", .label = "Volume", .step = 5.0, .value = volume,
                                           .on_changed = [&](double) { ++changed; },
                                           .on_committed = [&](double v) { committed.push_back(v); },
                                           .width = 218}),
                               }));
        h.step();
        const Rect b = bounds(h, "main/volume");
        check(b.width == 218 && b.height == 24);
        auto n = sem(h, "main/volume");
        check(n.role == Role::slider && n.name == "Volume" && n.value.get("value")->as_double() == 30);
        // The track spans the width less a thumb: 9 .. 209.
        PointerEvent down;
        down.kind = PointerKind::down;
        down.button = MouseButton::left;
        down.position = {b.x + 9 + 200 * 0.5f, b.center().y};
        PointerEvent move = down;
        move.kind = PointerKind::move;
        move.position.x = b.x + 9 + 200 * 0.81f;
        PointerEvent up = move;
        up.kind = PointerKind::up;
        h.step({down});
        check(volume.peek() == 50, std::format("{}", volume.peek()));
        h.step({move});
        check(volume.peek() == 80, "snapped to the step");
        h.step({up});
        check(committed == std::vector<double>{80} && changed == 2);
        h.focus_next();
        key(h, Key::right);
        check(volume.peek() == 85);
        key(h, Key::home);
        check(volume.peek() == 0);
        check(act(h, "main/volume", "set_value", Value{}.set("value", "max")) == 0 && volume.peek() == 100);
        check(act(h, "main/volume", "set_value", Value{}.set("value", 42.0)) == 0 && volume.peek() == 40, "snapped");
        check(act(h, "main/volume", "decrement") == 0 && volume.peek() == 35);
        check(act(h, "main/volume", "set_value", Value{}.set("value", "loud")) == 1);
        volume.set(1000.0);
        h.step();
        check(sem(h, "main/volume").value.get("value")->as_double() == 1000, "a binding sets what it sets");
    });

    s.test("text box: typing, editing keys, selection, clipboard, undo, submit", [] {
        Headless h(options());
        auto name = signal(std::string("Ada"));
        std::vector<std::string> submitted;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   text_box({.id = "name", .label = "Name", .text = name,
                                             .on_submitted = [&](std::string t) { submitted.push_back(std::move(t)); }}),
                                   text_box({.id = "notes", .multiline = true, .placeholder = "Notes"}),
                               }));
        h.step();
        auto n = sem(h, "main/name");
        check(n.role == Role::text_box && n.name == "Name" && n.value == Value("Ada") && n.states.has(State::editable));
        check(h.click("main/name"));
        check(h.runtime().path(h.runtime().focused()) == "main/name");
        check(h.runtime().text_input_rect().has_value(), "the input method knows where the caret is");
        // The caret went where the click was: the end of "Ada" is past the middle.
        key(h, Key::end);
        h.type(" Lovelace");
        check(name.peek() == "Ada Lovelace", name.peek());
        key(h, Key::backspace, Modifiers(Modifier::ctrl));
        check(name.peek() == "Ada ", name.peek());
        key(h, Key::left, Modifier::shift);
        key(h, Key::left, Modifier::shift);
        h.type("-");
        check(name.peek() == "Ad-", name.peek());
        key(h, Key::z, Modifiers(primary_modifier()));
        check(name.peek() == "Ada ", "undo");
        key(h, Key::a, Modifiers(primary_modifier()));
        key(h, Key::c, Modifiers(primary_modifier()));
        check(h.runtime().clipboard_text() == "Ada ");
        key(h, Key::end);
        key(h, Key::v, Modifiers(primary_modifier()));
        check(name.peek() == "Ada Ada ", name.peek());
        key(h, Key::enter);
        check(submitted == std::vector<std::string>{"Ada Ada "}, "Enter submits a single line");
        name.set("Grace");
        h.step();
        check(sem(h, "main/name").value == Value("Grace"), "the binding sets the field");
        // A multi-line field takes Enter as a new line.
        check(h.click("main/notes"));
        h.type("one");
        key(h, Key::enter);
        h.type("two");
        check(sem(h, "main/notes").value == Value("one\ntwo"));
        check(sem(h, "main/notes").name == "Notes", "the placeholder names an unlabelled field");
        check(submitted.size() == 1);
    });

    s.test("text box: input methods, passwords, limits, and actions in graphemes", [] {
        Headless h(options());
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   text_box({.id = "msg"}),
                                   text_box({.id = "pin", .label = "PIN", .password = true}),
                                   text_box({.id = "code", .label = "Code", .max_length = 4}),
                               }));
        h.step();
        check(h.click("main/msg"));
        PreeditEvent pre;
        pre.text = "ni";
        h.step({pre});
        check(sem(h, "main/msg").value == Value(""), "a preedit is not text yet");
        h.type("你");
        check(sem(h, "main/msg").value == Value("你"), "committed by the input method");
        h.type("好👋🏽!");
        check(act(h, "main/msg", "set_selection", Value{}.set("anchor", 1).set("focus", 3)) == 0);
        check(act(h, "main/msg", "replace_selection", Value{}.set("text", "们")) == 0);
        check(sem(h, "main/msg").value == Value("你们!"), "grapheme offsets: 👋🏽 is one");
        check(act(h, "main/msg", "set_value", Value{}.set("value", "hi")) == 0 && sem(h, "main/msg").value == Value("hi"));
        check(h.click("main/pin"));
        h.type("1234");
        auto pin = sem(h, "main/pin");
        check(pin.value == Value("••••"), "a password is not read out");
        key(h, Key::a, Modifiers(primary_modifier()));
        h.runtime().set_clipboard_text("before");
        key(h, Key::c, Modifiers(primary_modifier()));
        check(h.runtime().clipboard_text() == "before", "nor copied");
        check(h.click("main/code"));
        h.type("123456");
        check(sem(h, "main/code").value == Value("1234"), "max_length counts graphemes");
        check(act(h, "main/code", "focus") == 0);
    });

    s.test("immediate mode uses the real components", [] {
        Headless h(options());
        int saves = 0;
        bool dark = false;
        double level = 10;
        h.mount("main", immediate([&](Ui& ui) {
                    ui.column({.padding = 12, .gap = 8}, [&] {
                        if (ui.button("Save", {.id = "save"})) ++saves;
                        ui.toggle("Dark", dark, {.id = "dark"});
                        ui.slider("Level", level, {.id = "level", .min = 0, .max = 20, .step = 1});
                    });
                }));
        h.step();
        auto col = h.runtime().path(h.runtime().children(h.runtime().find("main"))[0]);
        check(sem(h, col + "/save").role == Role::button, "a real button, not a stand-in");
        check(act(h, col + "/save", "activate") == 0 && saves == 1);
        check(act(h, col + "/dark", "toggle") == 0 && dark);
        check(act(h, col + "/level", "increment") == 0 && level == 11, std::format("{}", level));
    });

    s.test("the gallery is the same pixels on every platform", [] {
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
        HeadlessOptions o = options({320, 260});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16, .gap = 10}, {
                                   row({.gap = 8}, {
                                       button({.id = "p", .text = "Primary", .variant = ButtonVariant::primary}),
                                       button({.id = "s", .text = "Secondary"}),
                                       button({.id = "g", .text = "Ghost", .variant = ButtonVariant::ghost}),
                                   }),
                                   row({.gap = 16}, {
                                       toggle({.id = "t", .checked = true, .label = "On"}),
                                       checkbox({.id = "c", .checked = true, .label = "Checked"}),
                                       checkbox({.id = "m", .indeterminate = true}),
                                   }),
                                   slider({.id = "v", .value = 60.0, .width = 288}),
                                   text_box({.id = "name", .placeholder = "Your name", .width = 288}),
                                   text_box({.id = "filled", .text = "Hello, 世界", .width = 288}),
                               }));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base gallery {:016x}", hash.value());
        check(h.save_png(scratch_dir("ui-base") / "gallery@2x.png").has_value());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check(hash.value() == 0xdd3d2b69d02e9060ULL, std::format("gallery {:016x}", hash.value()));
    });

    return s.run(argc, argv);
}
