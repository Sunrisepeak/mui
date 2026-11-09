// The package's own components (contract/): badge, avatar, skeleton, card,
// accordion, toast, popover, breadcrumb, pagination, number input and the
// actor log —
// driven as people and agents drive them (pointer, keys, actions, an item of
// a component as a node of its own), their semantics, their history, and a
// gallery whose pixels are pinned for every platform.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.hash;
import mlibs.testing;
import mcpplibs.mtest;

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

HeadlessOptions options(Size size = {420, 300}) {
    HeadlessOptions o;
    o.size = size;
    o.fonts = &fonts();
    return o;
}

std::optional<spec::SemanticNode> sem(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? std::optional<spec::SemanticNode>(*n) : std::nullopt;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

bool offers(const spec::SemanticNode& n, std::string_view action) {
    return std::ranges::any_of(n.actions, [&](const spec::ActionDesc& a) { return a.name == action; });
}

}  // namespace

UTest cases{"ui.base.kit", [] {
    register_base_components();

    "badge: a count past its max as max+, a label, a dot; its name and value say it"_test = [] {
        Headless h(options());
        auto count = signal(std::int64_t{120});
        h.mount("main", row({.gap = 8}, {badge({.id = "mail", .count = count, .tone = BadgeTone::danger}), badge({.id = "new", .text = "New", .tone = BadgeTone::accent}),
                                        badge({.id = "unread", .dot = true, .text = "Unread messages"})}));
        h.step();
        expect(sem(h, "main/mail")->name == "99+" && sem(h, "main/mail")->value == Value(std::int64_t{120})) << "99+, its value the count";
        count.set(7);
        h.step();
        expect(eq(sem(h, "main/mail")->name, std::string("7")));
        expect(sem(h, "main/new")->name == "New" && sem(h, "main/unread")->name == "Unread messages") << "a label; a dot named for what it marks";
        expect(h.runtime().bounds(h.runtime().find("main/unread")).width == 8) << "a dot is a dot";
    };

    "avatar: initials, the name's own colour (the same everywhere), a picture when there is one, a status said"_test = [] {
        expect(eq(Avatar::initials("Ada Lovelace"), std::string("AL")));
        expect(eq(Avatar::initials("grace brewster murray hopper"), std::string("GH")));
        expect(eq(Avatar::initials("ada"), std::string("A")));
        expect(eq(Avatar::initials("林黛玉"), std::string("林")));
        expect(eq(Avatar::initials(""), std::string("")));
        expect(Avatar::hue_of("Ada Lovelace", false) == Avatar::hue_of("Ada Lovelace", false)) << "the same name, the same colour";
        expect(!(Avatar::hue_of("Ada Lovelace", false) == Avatar::hue_of("Alan Turing", false))) << "another name, another colour (these two)";
        Headless h(options());
        h.mount("main", row({.gap = 8}, {avatar({.id = "ada", .name = "Ada Lovelace", .size = 40.0, .status = AvatarStatus::online})}));
        h.step();
        const auto n = sem(h, "main/ada");
        expect(n && n->role == Role::image && n->name == "Ada Lovelace" && n->description == "online") << (n ? n->description : "none");
        expect(h.runtime().bounds(h.runtime().find("main/ada")).width == 40) << "its size";
    };

    "skeleton: the shape of what is coming, busy to every reader"_test = [] {
        Headless h(options());
        h.mount("main", column({.gap = 8}, {skeleton({.id = "text", .lines = 3, .width = 200}), skeleton({.id = "pic", .shape = SkeletonShape::circle})}));
        h.step();
        expect(sem(h, "main/text")->states.has(State::busy) && sem(h, "main/text")->name == "Loading") << "busy, named";
        expect(eq(h.runtime().bounds(h.runtime().find("main/text")).height, 3.0f * 12 + 2 * 8));
        expect(eq(h.runtime().bounds(h.runtime().find("main/pic")).width, 40.0f));
    };

    "card: its title its name; interactive, activated whole (pointer, keys, agents); still, nothing to press"_test = [] {
        Headless h(options());
        int opened = 0;
        h.mount("main", column({.padding = 8, .gap = 8}, {
                                   card({.id = "doc", .interactive = true, .subtitle = "Updated today", .title = "Q3 report", .on_activated = [&] { ++opened; }},
                                        {text({.text = "Revenue up 12%"})}),
                                   card({.id = "note", .title = "A note"}, {text({.text = "Read only"})}),
                               }));
        h.step();
        const auto doc = sem(h, "main/doc");
        expect(doc && doc->name == "Q3 report" && doc->description == "Updated today" && doc->role == Role::button && offers(*doc, "activate")) << "an interactive card is a button";
        expect(h.click("main/doc") && opened == 1) << "clicked";
        expect(act(h, "main/doc", "activate") == 0 && opened == 2) << "by an agent";
        const auto note = sem(h, "main/note");
        expect(note && note->role == Role::group && !offers(*note, "activate") && !note->states.has(State::focusable)) << "a still card offers nothing";
        expect(act(h, "main/note", "activate") != 0 && opened == 2) << "and refuses";
    };

    "accordion: its content only while expanded; its header opens it, its content does not; taken back"_test = [] {
        Headless h(options());
        auto open = signal(false);
        h.mount("main", column({.padding = 8}, {accordion({.id = "faq", .expanded = open, .title = "What is openmui?"},
                                                         {text({.id = "answer", .text = "One semantic tree, many renderings."})})}));
        h.step();
        h.step();
        expect(sem(h, "main/faq/answer")->states.has(State::hidden)) << "collapsed: its content hidden";
        const auto faq = sem(h, "main/faq");
        expect(faq && offers(*faq, "expand") && !offers(*faq, "collapse") && !faq->states.has(State::expanded)) << "it offers to open";
        expect(act(h, "main/faq", "expand") == 0 && open.peek()) << "expanded by an agent: the binding follows";
        h.step();
        expect(!sem(h, "main/faq/answer")->states.has(State::hidden)) << "its content shown";
        // The model is expanded before its enter motion paints the answer.
        // Test content activation at its presented box, rather than clicking
        // an invisible future layout position during the fade's initial delay.
        const auto answer = h.runtime().find("main/faq/answer");
        for (int frame = 0; frame < 120 && h.runtime().drawn_bounds(answer).empty(); ++frame) h.step();
        expect(!h.runtime().drawn_bounds(answer).empty()) << "the opening answer has reached a real presented frame";
        // A press on its content is the content's: it stays open.
        expect(h.click("main/faq/answer") && open.peek()) << "the content pressed: still open";
        // Its header: closes it.
        const Rect b = h.runtime().bounds(h.runtime().find("main/faq"));
        h.click(Point{b.x + 20, b.y + 10});
        h.step();
        expect(!open.peek()) << "the header pressed: closed";
        // Taken back: open again.
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        expect(open.peek()) << "undone: open again";
    };

    "toast: over a corner of the window; gone after its time or dismissed; its action; two stack"_test = [] {
        Headless h(options());
        auto saved = signal(false), copied = signal(false);
        int undone = 0, dismissed = 0;
        h.mount("main", column({}, {
                                   toast({.id = "saved", .action = "Undo", .duration = 3.0, .message = "Saved", .open = saved, .tone = ToastTone::success,
                                          .on_acted = [&] { ++undone; }, .on_dismissed = [&] { ++dismissed; }}),
                                   toast({.id = "copied", .duration = 0.0, .message = "Link copied", .open = copied}),
                               }));
        h.step();
        expect(sem(h, "main/saved")->states.has(State::hidden)) << "closed: hidden";
        saved.set(true);
        copied.set(true);
        h.step();
        h.step();
        const Rect a = h.runtime().bounds(h.runtime().find("main/saved")), b = h.runtime().bounds(h.runtime().find("main/copied"));
        expect(a.right() <= 420 - 15 && a.right() >= 420 - 17 && a.bottom() >= 300 - 17) << std::format("bottom right: {} {}", a.right(), a.bottom());
        expect(b.bottom() <= a.y) << std::format("the second above the first: {} ≤ {}", b.bottom(), a.y);
        expect(sem(h, "main/saved")->role == Role::status && sem(h, "main/saved")->description == "Undo") << "a status, its action said";
        expect(act(h, "main/saved", "activate") == 0 && undone == 1 && !saved.peek() && dismissed == 1) << "its action: done, and gone";
        saved.set(true);
        h.step();
        h.advance(seconds(3.5));
        expect(!saved.peek() && dismissed == 2) << "gone after its time";
        expect(copied.peek()) << "one without a time stays";
        expect(act(h, "main/copied", "dismiss") == 0 && !copied.peek()) << "until dismissed";
        expect(act(h, "main/copied", "activate") != 0) << "no action to perform";
    };

    "popover: beside what opened it, not modal; Escape, a press outside or an agent closes it"_test = [] {
        Headless h(options());
        auto open = signal(false);
        int closed = 0;
        h.mount("main", column({.padding = 40}, {
                                   button({.id = "share", .text = "Share"}),
                                   popover({.id = "menu", .label = "Share options", .open = open, .on_closed = [&] { ++closed; }},
                                           {text({.text = "Copy link"})}),
                               }));
        h.step();
        open.set(true);
        h.step();
        h.step();
        const Rect anchor = h.runtime().bounds(h.runtime().find("main/share")), p = h.runtime().bounds(h.runtime().find("main/menu"));
        expect(p.y >= anchor.bottom() && p.y <= anchor.bottom() + 16) << std::format("below it: {} after {}", p.y, anchor.bottom());
        expect(sem(h, "main/menu")->name == "Share options" && !sem(h, "main/menu")->states.has(State::modal)) << "named, not modal";
        h.click(Point{400, 290});
        h.step();
        expect(!open.peek() && closed == 1) << "a press outside closes it";
        open.set(true);
        h.step();
        expect(act(h, "main/menu", "dismiss") == 0 && !open.peek() && closed == 2) << "an agent's dismiss";
    };

    "breadcrumb: each place a link of its own in the tree; going up one leaves the ones below"_test = [] {
        Headless h(options());
        auto path = signal(std::vector<std::string>{"Home", "Docs", "Guide", "Motion"});
        std::vector<std::int64_t> went;
        h.mount("main", column({.padding = 8}, {breadcrumb({.id = "crumbs", .items = path, .on_navigated = [&](std::int64_t i) { went.push_back(i); }})}));
        h.step();
        const auto items = h.runtime().virtual_items(h.runtime().find("main/crumbs"));
        expect(items.size() == 4 && items[0].role == Role::link && items[0].name == "Home" && items[3].states.has(State::selected)) << "four links, the last the current one";
        expect(offers(items[1], "activate") && !offers(items[3], "activate")) << "the current one is not a link to press";
        expect(act(h, "main/crumbs/1", "activate") == 0) << "an agent goes to Docs";
        expect(path.peek() == std::vector<std::string>{"Home", "Docs"} && went == std::vector<std::int64_t>{1}) << "up to Docs: the ones below left";
        expect(act(h, "main/crumbs", "go", Value{}.set("index", std::int64_t{0})) == 0 && path.peek().size() == 1) << "go by index";
        expect(sem(h, "main/crumbs")->role == Role::navigation && sem(h, "main/crumbs")->name == "Breadcrumb") << "a navigation, named";
    };

    "pagination: the first, the pages around, the last, gaps between; stepped, set, each page a link; taken back"_test = [] {
        Headless h(options());
        auto page = signal(std::int64_t{10});
        h.mount("main", column({.padding = 8}, {pagination({.id = "pages", .page = page, .pages = 20})}));
        h.step();
        Pagination p;
        p.props.page = 10, p.props.pages = 20, p.props.around = 1;
        expect(p.slots() == std::vector<std::int64_t>{0, 1, -2, 9, 10, 11, -2, 20, -1}) << "previous 1 … 9 10 11 … 20 next";
        p.props.page = 2;
        expect(p.slots() == std::vector<std::int64_t>{0, 1, 2, 3, -2, 20, -1}) << "near the start: no gap there";
        expect(act(h, "main/pages", "increment") == 0 && page.peek() == 11) << "next";
        expect(act(h, "main/pages", "set_value", Value{}.set("value", "last")) == 0 && page.peek() == 20) << "the last";
        expect(act(h, "main/pages", "increment") != 0 && page.peek() == 20) << "none after the last";
        expect(act(h, "main/pages/19", "activate") == 0 && page.peek() == 19) << "a page as a link of its own";
        const auto items = h.runtime().virtual_items(h.runtime().find("main/pages"));
        expect(std::ranges::any_of(items, [](const spec::SemanticNode& n) { return n.name == "Page 19" && n.states.has(State::selected); })) << "the current one selected";
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        expect(eq(page.peek(), std::int64_t{20}));
    };

    "number input: stepped and kept within its range on its step; typed and kept; taken back exactly"_test = [] {
        Headless h(options());
        auto qty = signal(2.0);
        h.mount("main", column({.padding = 8}, {number_input({.id = "qty", .digits = 1, .label = "Quantity", .max = 10.0, .min = 0.0, .step = 0.5, .value = qty})}));
        h.step();
        expect(act(h, "main/qty", "increment") == 0 && qty.peek() == 2.5) << "a step";
        expect(act(h, "main/qty", "set_value", Value{}.set("value", 99.0)) == 0 && qty.peek() == 10.0) << "kept within its max";
        expect(act(h, "main/qty", "set_value", Value{}.set("value", 3.3)) == 0 && qty.peek() == 3.5) << "kept on its step";
        const auto n = sem(h, "main/qty");
        expect(n && n->role == Role::spin_button && n->name == "Quantity" && n->value.get("max") && n->value.get("max")->as_double() == 10) << "a spin button, its range said";
        // Typed: the digits, then Enter.
        expect(h.click("main/qty")) << "focused";
        h.type("7");
        h.key(Key::enter);
        h.step();
        expect(eq(qty.peek(), 7.0));
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        expect(eq(qty.peek(), 3.5));
    };

    "actor log: an agent's actions, newest last, each a node; the last of its history taken back from here, by the person or the agent; the node it acted on pointed out; the agent link said"_test = [] {
        Headless h(options({420, 240}));
        auto dark = signal(false);
        auto volume = signal(40.0);
        h.mount("main", column({.padding = 8, .gap = 8}, {
                                   row({.id = "set", .gap = 8}, {toggle({.id = "dark", .checked = dark, .label = "Dark"}), slider({.id = "volume", .label = "Volume", .value = volume, .width = 140})}),
                                   actor_log({.id = "log", .width = Dim(400.0f)}),
                               }));
        h.step();
        expect(sem(h, "main/log")->role == Role::log) << "a log";
        expect(h.runtime().virtual_items(h.runtime().find("main/log")).empty()) << "nothing yet";
        expect(sem(h, "main/log")->value.get("link")->as_string() == "unavailable") << "the headless host links none";
        expect(act(h, "main/set/dark", "toggle") == 0 && dark.peek()) << "an agent turns it on";
        h.advance(seconds(1));
        expect(act(h, "main/set/volume", "set_value", Value{}.set("value", 70.0)) == 0);
        expect(h.click("main/set/dark") && !dark.peek()) << "the person's own: not listed";
        h.settle();
        auto items = h.runtime().virtual_items(h.runtime().find("main/log"));
        expect(items.size() == 2 && items[0].role == Role::list_item && items[0].name == "test toggle …/set/dark" && items[1].name == "test set_value …/set/volume 70" && items[0].value.get("node")->as_string() == "main/set/dark") << (items.empty() ? std::string("none") : items[0].name + " | " + items.back().name);
        expect(!offers(items[0], "undo") && !offers(items[1], "undo")) << "the person acted since: neither is the last its history holds";
        // The person undoes their own: the agent's volume is the last again.
        Invocation person;
        person.actor = {"person", "user", {}};
        expect(h.tree().act("app", "undo", person).exit_code() == std::optional<int>(0) && dark.peek());
        h.settle();
        items = h.runtime().virtual_items(h.runtime().find("main/log"));
        expect(offers(items[1], "undo")) << "the volume's is the last now";
        // An agent takes back its own, through the log.
        expect(act(h, std::format("main/log/{}", items[1].id.substr(items[1].id.rfind('/') + 1)), "undo") == 0 && volume.peek() == 40.0) << "taken back by the agent";
        items = h.runtime().virtual_items(h.runtime().find("main/log"));
        expect(offers(items[0], "undo") && !offers(items.back(), "undo")) << "then the toggle's (the agent's undo listed too, as it did it)";
        // The person: its Undo pressed.
        h.settle();
        const Rect log = h.runtime().screen_bounds(h.runtime().find("main/log"));
        h.move({log.x + 200, log.y + 14});
        h.settle();
        expect(h.runtime().pointed_out() == "main/set/dark") << "hovered: the toggle pointed out";
        h.click({log.x + log.width - 20, log.y + 14});
        h.settle();
        expect(!dark.peek()) << "the person's Undo: the agent's toggle taken back";
        h.move({4, 4});
        h.settle();
        expect(h.runtime().pointed_out().empty()) << "left: nothing pointed out";
        // The agent link, as a host that can link one says it.
        h.runtime().set_agent_linker({[&] { return std::string("K7Q4-42MX"); }, [] {}});
        h.runtime().agent_link_changed({"waiting", "K7Q4-42MX", {}, {}, 47110});
        h.settle();
        expect(sem(h, "main/log")->value.get("link")->as_string() == "waiting" && !offers(*sem(h, "main/log"), "unlink")) << "waiting; ending it is the person's, not an action here";
        h.runtime().agent_link_changed({"paired", "K7Q4-42MX", "claude-code", {}, 47110});
        h.settle();
        // The picture: the agent's last toggle, its Undo, hovered — the toggle outlined.
        expect(act(h, "main/set/dark", "toggle") == 0);
        h.settle();
        const auto last = *h.runtime().virtual_item(h.runtime().find("main/log"), std::to_string(h.runtime().action_journal().back().id))->bounds;
        h.move({static_cast<float>(last.x) + 100, static_cast<float>(last.y + last.height / 2)});
        h.settle();
        expect(h.runtime().pointed_out() == "main/set/dark");
        expect(h.save_png(scratch_dir("ui-base-actor-log") / "actor-log.png").has_value());
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base actor log {:016x}", hash.value());
        // Purposeful default motions also change the settled virtual times
        // shown by this log; the final controls and real records stay pinned.
        expect(hash.value() == 0x3e359291c557bc68ULL) << std::format("actor log {:016x}", hash.value());
    };

    "the gallery: the ten, the same pixels on every platform"_test = [] {
        mlibs::text::FontCollection own;
        expect(own.add_defaults().has_value());
        HeadlessOptions o = options({440, 360});
        o.fonts = &own;
        o.scale = 2;
        Headless h(o);
        h.mount("main", column({.padding = 16, .gap = 12}, {
                                   row({.gap = 10, .align = "center"}, {
                                       avatar({.id = "a1", .name = "Ada Lovelace", .size = 36.0, .status = AvatarStatus::online}),
                                       avatar({.id = "a2", .name = "Alan Turing", .shape = AvatarShape::square, .size = 36.0}),
                                       badge({.id = "b1", .text = "New", .tone = BadgeTone::accent}),
                                       badge({.id = "b2", .count = std::int64_t{120}, .tone = BadgeTone::danger}),
                                       badge({.id = "b3", .text = "Draft"}),
                                       badge({.id = "b4", .dot = true, .text = "Unread"}),
                                   }),
                                   breadcrumb({.id = "crumbs", .items = std::vector<std::string>{"Home", "Docs", "Motion"}}),
                                   pagination({.id = "pages", .page = std::int64_t{5}, .pages = std::int64_t{12}}),
                                   row({.gap = 12}, {
                                       card({.id = "c", .interactive = true, .subtitle = "Updated today", .title = "Q3 report", .width = 200},
                                            {skeleton({.id = "sk", .lines = 2, .width = 168})}),
                                       column({.gap = 8}, {number_input({.id = "n", .digits = 1, .step = 0.5, .suffix = "kg", .value = 3.5}),
                                                           accordion({.id = "acc", .title = "Details", .width = 170}, {text({.text = "Hidden"})})}),
                                   }),
                               }));
        h.settle();
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base kit gallery {:016x}", hash.value());
        expect(h.save_png(scratch_dir("ui-base-kit") / "kit@2x.png").has_value());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        expect(hash.value() == 0x1052ce5d88a6bc86ULL) << std::format("kit gallery {:016x}", hash.value());
    };
}};
