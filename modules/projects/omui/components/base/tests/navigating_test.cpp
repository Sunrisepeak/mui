// openmui.ui.base's components for finding one's way (W9): context menu,
// hover card, command palette, sidebar, toolbar — driven as people and agents
// drive them (a secondary press, pointing, typing, keys, actions, items),
// their semantics, and a gallery whose pixels are pinned for every platform.
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

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    const auto answer = h.tree().act(id, action, call);
    h.step();
    return answer.exit_code().value_or(-1);
}

std::int64_t undo(Headless& h) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    const auto answer = h.tree().act("app", "undo", call);
    h.step();
    return answer.exit_code().value_or(-1);
}

// Open, as it says to every reader (its drawing may still be entering or leaving).
bool shown(Headless& h, std::string_view id) {
    const auto node = h.tree().node(id);
    return node && node->states.has(State::expanded);
}

bool focused(Headless& h, std::string_view id) {
    const auto node = h.tree().node(id);
    return node && node->states.has(State::focused);
}

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

}  // namespace

UTest cases{"ui.base.navigating", [] {
    register_base_components();

    "context menu: a secondary press over its element opens it at the pointer; a choice runs and closes it, focus back; an agent shows it at a point, kept inside the window"_test = [] {
        Headless h(options());
        std::vector<std::string> chosen;
        h.mount("main", column({.padding = 20},
                               {box({.id = "doc", .width = 200, .height = 120, .background = Fill(std::string("surface_variant")), .label = "Document"}),
                                context_menu({.id = "menu", .label = "Document actions"},
                                             {menu_item({.id = "cut", .text = std::string("Cut"), .on_activated = [&] { chosen.push_back("cut"); }}),
                                              menu_item({.id = "paste", .text = std::string("Paste"), .on_activated = [&] { chosen.push_back("paste"); }})})}));
        h.step();
        expect(!shown(h, "main/menu")) << "closed until asked";
        h.click({70, 60}, MouseButton::right);
        h.step();
        expect(fatal(shown(h, "main/menu"))) << "a secondary press opens it";
        const Rect menu = h.runtime().bounds(h.runtime().find("main/menu"));
        expect(std::abs(menu.x - 70) < 1 && std::abs(menu.y - 60) < 1) << "at the pointer: " << menu.x << "," << menu.y;
        expect(focused(h, "main/menu/cut")) << "its first choice focused";
        h.settle();  // its entrance drawn
        expect(h.click("main/menu/paste"));
        h.settle();  // its exit
        expect(chosen == std::vector<std::string>{"paste"} && !shown(h, "main/menu")) << "chosen: run, closed";
        // An agent, at a point near the window's corner: flipped to stay inside.
        expect(eq(act(h, "main/menu", "show", Value{}.set("x", 470.0).set("y", 350.0)), 0));
        const Rect flipped = h.runtime().bounds(h.runtime().find("main/menu"));
        expect(shown(h, "main/menu") && flipped.right() <= 480.5f && flipped.bottom() <= 360.5f) << "inside: " << flipped.right() << "," << flipped.bottom();
        expect(eq(act(h, "main/menu", "dismiss"), 0));
        h.settle();
        expect(!shown(h, "main/menu"));
    };

    "hover card: shown after pointing at its element a moment, kept while the pointer is on the card, gone a moment after it leaves both; an agent shows and dismisses it"_test = [] {
        Headless h(options());
        h.mount("main", column({.padding = 20, .gap = 80},
                               {button({.id = "who", .text = std::string("@ada")}),
                                hover_card({.id = "card", .delay = 0.5, .label = "Ada Lovelace", .linger = 0.3},
                                           {text({.text = "Ada Lovelace"}), text({.text = "Wrote the first program.", .style = "caption"})}),
                                button({.id = "far", .text = std::string("Elsewhere")})}));
        h.step();
        const Rect who = h.runtime().drawn_bounds(h.runtime().find("main/who"));
        h.move({who.x + 10, who.y + 10});
        (void)h.advance(seconds(0.2));
        expect(!shown(h, "main/card")) << "not at once";
        (void)h.advance(seconds(0.5));
        expect(fatal(shown(h, "main/card"))) << "after its delay";
        const Rect card = h.runtime().bounds(h.runtime().find("main/card"));
        h.move({card.x + 20, card.y + 20});
        (void)h.advance(seconds(0.6));
        expect(shown(h, "main/card")) << "kept while the pointer is on it";
        const Rect far = h.runtime().drawn_bounds(h.runtime().find("main/far"));
        h.move({far.x + 5, far.bottom() - 2});
        (void)h.advance(seconds(0.6));
        expect(!shown(h, "main/card")) << "gone after it left both";
        expect(eq(act(h, "main/card", "show"), 0) && shown(h, "main/card"));
        expect(eq(act(h, "main/card", "dismiss"), 0));
        h.settle();
        expect(!shown(h, "main/card"));
    };

    "command palette: the application's commands found by their words; arrows choose, Enter runs; each an item an agent sees and runs; what reaches outside is the person's choice"_test = [] {
        Headless h(options());
        std::vector<std::string> ran;
        const auto add = [&](std::string name, std::string summary, Effect effect = Effect::reversible, std::string answer = {}) {
            (void)h.runtime().verbs().add({.name = name, .summary = summary, .effect = effect, .answer = answer}, [&ran, name](const Value&) -> VerbResult {
                ran.push_back(name);
                return Value{};
            });
        };
        add("notes.new", "New note");
        add("notes.open", "Open notes");
        add("share.post", "Share the note", Effect::external, "share");
        h.mount("main", column({.padding = 20}, {command_palette({.id = "palette", .verbs = std::vector<std::string>{"notes.new", "notes.open", "share.post"}})}));
        h.step();
        expect(!shown(h, "main/palette"));
        expect(eq(act(h, "main/palette", "show"), 0) && shown(h, "main/palette"));
        expect(eq(h.runtime().virtual_items(h.runtime().find("main/palette")).size(), std::size_t{3}));
        expect(eq(act(h, "main/palette", "set_query", Value{}.set("query", "note")), 0));
        const auto items = h.runtime().virtual_items(h.runtime().find("main/palette"));
        expect(items.size() == 3 || items.size() == 2) << "every word found in its name or summary";
        expect(eq(act(h, "main/palette", "set_query", Value{}.set("query", "open")), 0));
        const auto open = h.runtime().virtual_items(h.runtime().find("main/palette"));
        expect(fatal(open.size() == 1 && open[0].name == "Open notes"));
        expect(eq(act(h, "main/palette/notes.open", "activate"), 0) && ran == std::vector<std::string>{"notes.open"}) << "an agent runs an item";
        h.settle();
        expect(!shown(h, "main/palette")) << "it closes";
        // A person: open it, type, choose with the arrows, Enter.
        expect(eq(act(h, "main/palette", "show"), 0));
        expect(eq(act(h, "main/palette", "set_query", Value{}.set("query", "")), 0));
        expect(focused(h, "main/palette/field")) << "its field takes the words";
        h.type("share");
        h.key(Key::enter);
        expect(ran.size() == 2 && ran.back() == "share.post") << "what reaches outside: the person's choice is their answer";
        expect(eq(act(h, "main/palette", "show"), 0));
        h.runtime().focus(h.runtime().find("main/palette/field"));
        h.key(Key::escape);
        h.settle();
        expect(!shown(h, "main/palette")) << "Escape closes it";
    };

    "sidebar: the places down the side, each a link of its own, the current one marked; pressed, arrowed, chosen by an agent; collapsed to a rail; taken back"_test = [] {
        Headless h(options());
        h.mount("main", row({}, {sidebar({.id = "nav", .items = std::vector<std::string>{"inbox", "drafts", "sent"},
                                           .label = "Mail", .labels = std::vector<std::string>{"Inbox", "Drafts", "Sent"}, .selected = std::string("inbox")}),
                                  text({.text = "content"})}));
        h.step();
        expect(h.tree().node("main/nav") && h.tree().node("main/nav")->role == Role::navigation && h.tree().node("main/nav")->name == "Mail");
        expect(h.tree().node("main/nav/inbox") && h.tree().node("main/nav/inbox")->states.has(State::selected));
        expect(eq(act(h, "main/nav", "set_selected", Value{}.set("selected", "sent")), 0) && h.tree().node("main/nav")->value == Value("sent"));
        const auto drafts = h.tree().node("main/nav/drafts");
        expect(fatal(drafts && drafts->bounds.has_value()));
        h.click({static_cast<float>(drafts->bounds->x + 20), static_cast<float>(drafts->bounds->y + 10)});
        expect(h.tree().node("main/nav")->value == Value("drafts")) << "pressed";
        h.runtime().focus(h.runtime().find("main/nav"));
        h.key(Key::down);
        expect(h.tree().node("main/nav")->value == Value("sent")) << "arrowed";
        expect(eq(undo(h), 0) && h.tree().node("main/nav")->value == Value("drafts")) << "taken back";
        expect(eq(act(h, "main/nav", "collapse"), 0));
        h.step();
        expect(h.runtime().drawn_bounds(h.runtime().find("main/nav")).width < 60) << "a rail";
        expect(eq(act(h, "main/nav", "expand"), 0));
        h.step();
        expect(h.runtime().drawn_bounds(h.runtime().find("main/nav")).width > 200);
    };

    "toolbar: a row of controls; the arrows move the focus between them, Home and End to the ends"_test = [] {
        Headless h(options());
        h.mount("main", column({.padding = 20}, {toolbar({.id = "bar", .label = "Formatting"},
                                                         {button({.id = "bold", .text = std::string("B")}), button({.id = "italic", .text = std::string("I")}),
                                                          button({.id = "under", .text = std::string("U")})})}));
        h.step();
        expect(h.tree().node("main/bar") && h.tree().node("main/bar")->role == Role::toolbar && h.tree().node("main/bar")->name == "Formatting");
        h.runtime().focus(h.runtime().find("main/bar/bold"));
        h.step();
        h.key(Key::right);
        expect(focused(h, "main/bar/italic")) << "Right";
        h.key(Key::end);
        expect(focused(h, "main/bar/under")) << "End";
        h.key(Key::home);
        expect(focused(h, "main/bar/bold")) << "Home";
        h.key(Key::left);
        expect(focused(h, "main/bar/bold")) << "kept at its first";
    };

    "the gallery: a sidebar, a toolbar and an open command palette over them — the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions made = options({560, 360});
        made.fonts = &own;
        made.scale = 2;
        Headless h(made);
        (void)h.runtime().verbs().add({.name = "notes.new", .summary = "New note"}, [](const Value&) -> VerbResult { return Value{}; });
        (void)h.runtime().verbs().add({.name = "notes.open", .summary = "Open notes"}, [](const Value&) -> VerbResult { return Value{}; });
        h.mount("main", row({.width = 560, .height = 360, .align = "start"},
                            {sidebar({.items = std::vector<std::string>{"inbox", "drafts", "sent"}, .labels = std::vector<std::string>{"Inbox", "Drafts", "Sent"},
                                      .selected = std::string("drafts")}),
                             column({.padding = 16, .gap = 12},
                                    {toolbar({}, {button({.text = std::string("Bold"), .variant = ButtonVariant::ghost}),
                                                  button({.text = std::string("Italic"), .variant = ButtonVariant::ghost}), separator({.vertical = true}),
                                                  button({.text = std::string("Link"), .variant = ButtonVariant::ghost})}),
                                     command_palette({.id = "palette", .open = true, .verbs = std::vector<std::string>{"notes.new", "notes.open"}})})}));
        h.settle();
        const std::uint64_t pinned = hash_of(h.pixels());
        std::println("ui.base navigating {:016x}", pinned);
        expect(h.save_png(scratch_dir("ui-base-navigating") / "navigating@2x.png").has_value());
        expect(pinned == 0x768c1d833368b328ULL) << std::format("navigating {:016x}", pinned);
    };
}};
