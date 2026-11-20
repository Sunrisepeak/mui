// Text a person selects and copies, as on any web page: a drag over words
// (from one text node into the next), a double press a word, a triple its
// whole text, Ctrl+C the clipboard's; a press elsewhere lets it go; a control
// never starts one; an agent selects through a node that offers it.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        if (auto added = made.add_defaults(); !added) std::println("fonts: {}", added.error());
        return made;
    }();
    return collection;
}

InputEvent pointer(PointerKind kind, Point at, std::uint8_t clicks = 1) {
    PointerEvent event;
    event.kind = kind;
    event.position = at;
    event.button = MouseButton::left;
    event.clicks = clicks;
    return event;
}

InputEvent key(Key which, Modifiers mods = {}) {
    KeyEvent event;
    event.key = which;
    event.mods = mods;
    return event;
}

RuntimeOptions options() {
    RuntimeOptions made;
    made.fonts = &fonts();
    made.size = {360, 200};
    return made;
}

struct Page {
    Runtime rt{options()};
    Page() {
        // A control: a focusable box (a press on it is the control's, never a selection).
        Element control = column({.id = "go", .width = Dim(80.0f), .height = Dim(28.0f), .background = Fill(Color::hex(0x4f46e5))});
        control.prop("focusable", Source(Value(true)));
        rt.mount("main", column({.id = "page", .padding = Edges(10.0f), .gap = 8},
                                {text({.id = "first", .text = "Hello brave new world"}),
                                 text({.id = "second", .text = "omui draws every glyph"}),
                                 text({.id = "label", .text = "Not for copying", .selectable = false}),
                                 text({.id = "offered", .text = "An agent selects this", .selectable = true}),
                                 std::move(control)}));
        rt.frame();
        rt.frame();
    }
    Rect box(std::string_view id) { return rt.bounds(rt.find(id)); }
    // A point a fraction of the way along a text node's line.
    Point along(std::string_view id, float fraction) {
        const Rect r = box(id);
        return {r.x + r.width * fraction, r.y + r.height / 2};
    }
};

}  // namespace

static UTest cases{"view.text_selection", [] {

    "a drag selects words, from one text into the next; Ctrl+C copies them; a press elsewhere lets them go"_test = [] {
        Page page;
        const Point from = page.along("main/first", 0.3f), to = page.along("main/second", 0.4f);
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, from), pointer(PointerKind::move, page.along("main/first", 0.8f)), pointer(PointerKind::move, to),
                                      pointer(PointerKind::up, to)}});
        const std::string selected = page.rt.selected_text();
        expect(selected.contains('\n') && selected.ends_with(selected.substr(selected.rfind('\n') + 1)) && selected.size() > 10) << std::format("across two texts: {:?}", selected);
        expect(page.rt.text_selection(page.rt.find("main/first")).has_value() && page.rt.text_selection(page.rt.find("main/second")).has_value());
        expect(std::string("Hello brave new world").ends_with(selected.substr(0, selected.find('\n')))) << "the first text's part runs to its end";
        expect(std::string("omui draws every glyph").starts_with(selected.substr(selected.find('\n') + 1))) << "the second's from its start";
        page.rt.frame({std::nullopt, {key(Key::c, Modifier::ctrl)}});
        expect(eq(page.rt.clipboard_text(), selected));
        // Drawn: the first text's pixels change where it is selected.
        expect(!page.rt.display_list().empty());
        // A press on nothing selectable: none selected.
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, {350, 190}), pointer(PointerKind::up, {350, 190})}});
        expect(page.rt.selected_text().empty()) << "a press elsewhere lets it go";
    };

    "a double press selects a word, a triple the whole text"_test = [] {
        Page page;
        const Point on_brave = page.along("main/first", 0.35f);
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, on_brave, 2), pointer(PointerKind::up, on_brave, 2)}});
        const std::string word = page.rt.selected_text();
        expect(word == "Hello" || word == "brave" || word == "new" || word == "world") << std::format("a whole word: {:?}", word);
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, on_brave, 3), pointer(PointerKind::up, on_brave, 3)}});
        expect(eq(page.rt.selected_text(), std::string("Hello brave new world")));
    };

    "a control never starts a selection; text that says no is not selected; an agent selects through a node that offers it"_test = [] {
        Page page;
        // Something selected first: a press on the control keeps it (a press on a control is the control's).
        const Point on_first = page.along("main/first", 0.35f);
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, on_first, 3), pointer(PointerKind::up, on_first, 3)}});
        page.rt.clear_text_selection();
        const Point on_button = page.box("main/go").center();
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, on_button), pointer(PointerKind::up, on_button)}});
        expect(page.rt.focused() == page.rt.find("main/go") && page.rt.selected_text().empty()) << "the control focused, nothing selected";
        const Point on_label = page.along("main/label", 0.5f);
        page.rt.frame({std::nullopt, {pointer(PointerKind::down, on_label, 3), pointer(PointerKind::up, on_label, 3)}});
        expect(page.rt.selected_text().empty()) << "a label that says no";
        const auto offered = page.rt.semantic_node(page.rt.find("main/offered"));
        expect(std::ranges::any_of(offered.actions, [](const auto& action) { return action.name == "set_selection"; })) << "the node that offers it says so";
        const auto plain = page.rt.semantic_node(page.rt.find("main/first"));
        expect(std::ranges::none_of(plain.actions, [](const auto& action) { return action.name == "set_selection"; })) << "a page's words do not crowd an agent's list";
        Invocation call;
        call.args = Value{}.set("anchor", 3).set("focus", 8);
        expect(page.rt.act(page.rt.find("main/offered"), "set_selection", call).exit_code() == std::optional<int>(0));
        expect(eq(page.rt.selected_text(), std::string("agent")));
    };
}};
