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

    s.test("progress and radio groups", [] {
        Headless h(options());
        auto done = signal(0.25);
        auto size = signal(std::string("m"));
        std::vector<std::string> picked;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   progress({.id = "upload", .label = "Upload", .value = done}),
                                   progress({.id = "busy", .label = "Working"}),
                                   column({.id = "size", .gap = 4}, {
                                       radio({.id = "s", .group = size, .label = "Small", .value = "s"}),
                                       radio({.id = "m", .group = size, .label = "Medium", .value = "m"}),
                                       radio({.id = "l", .group = size, .label = "Large", .value = "l",
                                              .on_selected = [&](std::string v) { picked.push_back(v); }}),
                                   }),
                               }));
        h.settle();
        check(sem(h, "main/upload").value == Value(0.25) && sem(h, "main/upload").role == Role::progress);
        check(sem(h, "main/busy").states.has(State::busy), "indeterminate is busy");
        check(sem(h, "main/size/m").states.has(State::checked) && !sem(h, "main/size/s").states.has(State::checked));
        check(h.click("main/size/l"));
        check(size.peek() == "l" && picked == std::vector<std::string>{"l"});
        check(!sem(h, "main/size/m").states.has(State::checked), "the group's other radios follow its signal");
        check(act(h, "main/size/s", "select") == 0 && size.peek() == "s");
        done.set(0.75);
        h.step();
        check(sem(h, "main/upload").value == Value(0.75));
    });

    s.test("tabs: click, arrows, and the panel that follows", [] {
        Headless h(options());
        auto page = signal(std::string("general"));
        std::vector<std::string> changes;
        h.mount("main", column({.padding = 12}, {
                                   tabs({.id = "tabs", .selected = page, .on_changed = [&](std::string t) { changes.push_back(t); }}, {
                                       tab({.id = "general", .text = "General"}),
                                       tab({.id = "privacy", .text = "Privacy"}),
                                       tab({.id = "about", .disabled = true, .text = "About"}),
                                       tab({.id = "advanced", .text = "Advanced"}),
                                   }),
                                   when([page] { return page.get() == "privacy"; }, [] { return text({.id = "panel", .text = "Privacy settings"}); },
                                        [] { return text({.id = "panel", .text = "General settings"}); }),
                               }));
        h.settle();
        check(sem(h, "main/tabs").role == Role::tab_list && sem(h, "main/tabs/general").states.has(State::selected));
        check(h.click("main/tabs/privacy"));
        check(page.peek() == "privacy" && changes == std::vector<std::string>{"privacy"});
        check(sem(h, "main/panel").name == "Privacy settings");
        key(h, Key::right);
        check(page.peek() == "advanced", std::format("Right skips the disabled tab: {}", page.peek()));
        check(h.runtime().path(h.runtime().focused()) == "main/tabs/advanced");
        key(h, Key::right);
        check(page.peek() == "general", "and wraps");
        check(act(h, "main/tabs/privacy", "select") == 0 && page.peek() == "privacy");
        check(act(h, "main/tabs", "next") == 1, "next is a command of the keys, not an action");
    });

    s.test("menu: opens below its button, keys and choices close it, focus comes back", [] {
        Headless h(options({360, 300}));
        auto open = signal(false);
        int closed = 0;
        std::vector<std::string> chosen;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   row({.id = "file"}, {
                                       button({.id = "open", .text = "File", .on_activated = [open] { open.set(!open.peek()); }}),
                                       menu({.id = "menu", .open = open, .on_closed = [&] { ++closed; }}, {
                                           menu_item({.id = "new", .shortcut = "Ctrl+N", .text = "New", .on_activated = [&] { chosen.push_back("new"); }}),
                                           menu_item({.id = "save", .disabled = true, .text = "Save"}),
                                           menu_item({.id = "quit", .text = "Quit", .on_activated = [&] { chosen.push_back("quit"); }}),
                                       }),
                                   }),
                                   text({.id = "below", .text = "Content"}),
                               }));
        h.settle();
        check(!h.tree().node("main/file/menu") || h.tree().find({.role = Role::menu_item}).empty(), "a closed menu is not read");
        check(h.click("main/file/open"));
        h.settle();
        check(open.peek());
        const Rect button_b = bounds(h, "main/file/open"), menu_b = bounds(h, "main/file/menu");
        check(menu_b.y >= button_b.bottom() && menu_b.x == button_b.x, std::format("below: {} {}", menu_b.x, menu_b.y));
        check(h.runtime().hit_test({menu_b.x + 20, menu_b.y + 20}) != h.runtime().find("main/below"), "above the content");
        check(h.runtime().path(h.runtime().focused()) == "main/file/menu/new", "the first item takes focus");
        key(h, Key::down);
        check(h.runtime().path(h.runtime().focused()) == "main/file/menu/quit", "Down skips the disabled item");
        key(h, Key::enter);
        h.settle();
        check(chosen == std::vector<std::string>{"quit"} && !open.peek() && closed == 1);
        check(h.runtime().path(h.runtime().focused()) == "main/file/open", "focus goes back to the button");
        open.set(true);
        h.settle();
        key(h, Key::escape);
        h.settle();
        check(!open.peek() && closed == 2, "Escape");
        open.set(true);
        h.settle();
        h.click(Point{340, 280});
        h.settle();
        check(!open.peek() && closed == 3, "a press outside");
    });

    s.test("dialog: modal, focus kept inside and given back, dismissed by Escape or the scrim", [] {
        Headless h(options({400, 300}));
        auto open = signal(false);
        std::vector<std::string> results;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   button({.id = "ask", .text = "Delete…", .on_activated = [open] { open.set(true); }}),
                                   button({.id = "other", .text = "Other"}),
                                   dialog({.id = "confirm", .open = open, .title = "Delete the file?",
                                           .on_closed = [&](std::string r) { results.push_back(r); }}, {
                                       text({.id = "body", .text = "This cannot be undone."}),
                                       row({.id = "actions", .gap = 8}, {
                                           button({.id = "cancel", .text = "Cancel", .on_activated = [open] { open.set(false); }}),
                                           button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger}),
                                       }),
                                   }),
                               }));
        h.settle();
        h.focus_next();
        check(h.runtime().path(h.runtime().focused()) == "main/ask");
        key(h, Key::enter);
        h.settle();
        check(open.peek());
        auto d = sem(h, "main/confirm");
        check(d.role == Role::dialog && d.name == "Delete the file?" && d.states.has(State::modal));
        check(h.runtime().path(h.runtime().focused()) == "main/confirm/actions/cancel", h.runtime().path(h.runtime().focused()));
        key(h, Key::tab);
        key(h, Key::tab);
        check(h.runtime().path(h.runtime().focused()) == "main/confirm/actions/cancel", "Tab stays inside");
        check(h.runtime().hit_test(bounds(h, "main/other").center()) == h.runtime().find("main/confirm"), "the scrim blocks what is below");
        key(h, Key::escape);
        h.settle();
        check(!open.peek() && results == std::vector<std::string>{"dismissed"});
        check(h.runtime().path(h.runtime().focused()) == "main/ask", "focus comes back");
        open.set(true);
        h.settle();
        h.click(Point{5, 295});
        h.settle();
        check(!open.peek() && results.size() == 2, "a press on the scrim");
        open.set(true);
        h.settle();
        check(h.click("main/confirm/actions/cancel"));
        h.settle();
        check(!open.peek() && results.size() == 3);
    });

    s.test("tooltip: after a delay on hover, gone on leave", [] {
        Headless h(options());
        h.mount("main", column({.padding = 40, .gap = 8}, {
                                   row({.id = "bar"}, {
                                       button({.id = "save", .text = "Save"}),
                                       tooltip({.id = "tip", .delay = 0.5, .text = "Save the document (Ctrl+S)"}),
                                   }),
                               }));
        h.settle();
        check(h.tree().find({.role = Role::tooltip}).empty());
        h.move(bounds(h, "main/bar/save").center());
        h.advance(seconds(0.3));
        check(h.tree().find({.role = Role::tooltip}).empty(), "not before the delay");
        h.advance(seconds(0.3));
        h.settle();
        auto tips = h.tree().find({.role = Role::tooltip});
        check(tips.size() == 1 && tips[0].name == "Save the document (Ctrl+S)");
        check(bounds(h, "main/bar/tip").bottom() <= bounds(h, "main/bar/save").y, "above its target");
        h.move({5, 230});
        h.settle();
        check(h.tree().find({.role = Role::tooltip}).empty(), "gone on leave");
    });

    s.test("select: pointer, keys and agents choose; values are checked", [] {
        Headless h(options({360, 320}));
        auto city = signal(std::string("paris"));
        std::vector<std::string> changes;
        h.mount("main", column({.padding = 12, .gap = 8}, {
                                   select({.id = "city", .label = "City", .labels = std::vector<std::string>{"Paris", "Tokyo", "Lima"},
                                           .options = std::vector<std::string>{"paris", "tokyo", "lima"}, .value = city,
                                           .on_changed = [&](std::string v) { changes.push_back(v); }}),
                               }));
        h.settle();
        auto n = sem(h, "main/city");
        check(n.role == Role::combo_box && n.name == "City" && n.value.get("value")->as_string() == "paris");
        check(n.value.get("options")->list()->size() == 3);
        check(h.click("main/city"));
        h.settle();
        check(sem(h, "main/city").states.has(State::expanded));
        auto options = h.tree().find({.role = Role::option});
        check(options.size() == 3 && options[1].name == "Tokyo");
        check(bounds(h, "main/city/popup").width >= bounds(h, "main/city").width, "at least as wide as the field");
        check(h.click(options[2].id));
        h.settle();
        check(city.peek() == "lima" && changes == std::vector<std::string>{"lima"} && !sem(h, "main/city").states.has(State::expanded));
        check(h.runtime().path(h.runtime().focused()) == "main/city");
        key(h, Key::enter);
        h.settle();
        key(h, Key::up);
        key(h, Key::enter);
        h.settle();
        check(city.peek() == "tokyo", city.peek());
        check(act(h, "main/city", "set_value", Value{}.set("value", "paris")) == 0 && city.peek() == "paris");
        check(act(h, "main/city", "set_value", Value{}.set("value", "berlin")) == 1, "not an option");
        h.settle();
    });

    s.test("overlays in a picture: an open select and an open menu", [] {
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
        HeadlessOptions o = options({360, 240});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        auto open = signal(true);
        h.mount("main", row({.padding = 16, .gap = 16, .align = "start"}, {
                                select({.id = "city", .labels = std::vector<std::string>{"Paris", "Tokyo", "Lima"},
                                        .options = std::vector<std::string>{"paris", "tokyo", "lima"}, .value = std::string("tokyo")}),
                                column({.id = "file"}, {
                                    button({.id = "open", .text = "File"}),
                                    menu({.id = "menu", .open = open}, {
                                        menu_item({.id = "new", .shortcut = "Ctrl+N", .text = "New"}),
                                        menu_item({.id = "quit", .text = "Quit"}),
                                    }),
                                }),
                            }));
        h.settle();
        check(act(h, "main/city", "expand") == 0);
        h.settle();
        const Rect field = bounds(h, "main/city");
        for (const auto& opt : h.tree().find({.role = Role::option}))
            check(bounds(h, opt.id).width >= field.width - 10, std::format("{} is {} wide", opt.id, bounds(h, opt.id).width));
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base overlays {:016x}", hash.value());
        check(h.save_png(scratch_dir("ui-base-overlays") / "overlays@2x.png").has_value());
        check(hash.value() == 0xbdb31159cd5ec8a4ULL, std::format("overlays {:016x}", hash.value()));
    });

    s.test("list: a thousand rows, the visible ones materialised, all of them queryable (S-7)", [] {
        Headless h(options({320, 400}));
        Model& notes = h.runtime().model("notes");
        std::vector<Row> rows;
        for (int i = 0; i < 1000; ++i) rows.push_back({std::format("n{}", i), Value{}.set("title", std::format("Note {}", i))});
        check(notes.push(std::move(rows)).has_value());
        check(!notes.push({{"bad key!", {}}}).has_value(), "keys are ids");
        check(!notes.push({{"n3", {}}}).has_value(), "and unique");
        auto selected = signal(std::vector<std::string>{});
        std::vector<std::string> activated;
        h.mount("main", column({.padding = 10}, {
                                   list_view({.id = "notes", .estimate = 32.0, .model = "notes", .selectable = ListSelectable::multiple,
                                              .selected = selected,
                                              .on_activated = [&](std::string k) { activated.push_back(k); },
                                              .height = 320},
                                             [](const Row& r, std::size_t i) {
                                                 return text({.text = std::format("{} — {}", Model::label_of(r), i % 7 == 0 ? "starred" : "plain")});
                                             }),
                               }));
        h.settle();
        const NodeId list = h.runtime().find("main/notes");
        const std::size_t materialised = h.runtime().children(list).size() - 2;  // less the two spacers
        check(materialised > 5 && materialised < 30, std::format("{} rows are nodes", materialised));
        check(h.runtime().node_count() < 200, std::format("{} nodes for a thousand rows", h.runtime().node_count()));
        // The semantic tree states all of them, in order.
        auto snap = h.tree().snapshot({.root = "main/notes", .depth = 1});
        check(snap.nodes.size() == 1001, std::format("{}", snap.nodes.size()));
        check(snap.nodes.front().children.size() == 1000 && snap.nodes.front().children[999] == "main/notes/n999");
        auto far = h.tree().find({.name = "Note 750"});
        check(far.size() == 1 && far[0].states.has(State::offscreen), "found though not materialised");
        // An action on it brings it into view first.
        check(act(h, "main/notes/n750", "activate") == 0);
        h.settle();
        check(activated == std::vector<std::string>{"n750"} && selected.peek() == std::vector<std::string>{"n750"});
        check(h.runtime().valid(h.runtime().find("main/notes/n750")), "now a node");
        const Rect lb = bounds(h, "main/notes"), ib = bounds(h, "main/notes/n750");
        check(ib.y >= lb.y - 0.5f && ib.bottom() <= lb.bottom() + 0.5f, std::format("in view: {}..{} in {}..{}", ib.y, ib.bottom(), lb.y, lb.bottom()));
        check(!h.tree().node("main/notes/n750")->states.has(State::offscreen));
        // Keys walk the items from the one focused, scrolling as they go.
        check(act(h, "main/notes/n750", "focus") == 0);
        for (int i = 0; i < 20; ++i) key(h, Key::down);
        h.settle();
        check(h.runtime().path(h.runtime().focused()) == "main/notes/n770", h.runtime().path(h.runtime().focused()));
        check(h.runtime().valid(h.runtime().find("main/notes/n770")));
        // The wheel moves the window.
        h.scroll(lb.center(), {0, -100000});
        h.settle();
        check(h.runtime().valid(h.runtime().find("main/notes/n0")) && !h.runtime().valid(h.runtime().find("main/notes/n750")));
        // Rows measured taller than the estimate keep their place.
        check(act(h, "main/notes", "scroll_to", Value{}.set("x", 0).set("y", "end")) == 0);
        h.settle();
        check(h.runtime().valid(h.runtime().find("main/notes/n999")));
        // The model changes: the list follows.
        check(notes.patch(999, {{"last", Value{}.set("title", "The last one")}}).has_value());
        h.settle();
        check(!h.tree().find({.name = "The last one"}).empty());
        notes.remove(0, 990);
        h.settle();
        check(h.tree().snapshot({.root = "main/notes", .depth = 1}).nodes.size() == 11);
        check(h.tree().violations().empty(), std::format("{}", h.tree().violations().size()));
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
        check(hash.value() == 0xae3e686a8040c61fULL, std::format("gallery {:016x}", hash.value()));
    });

    return s.run(argc, argv);
}
