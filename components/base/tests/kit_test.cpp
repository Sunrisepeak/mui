// The package's own components (contract/): badge, avatar, skeleton, card,
// accordion, toast, popover, breadcrumb, pagination, number input and the
// actor log —
// driven as people and agents drive them (pointer, keys, actions, an item of
// a component as a node of its own), their semantics, their history, and a
// gallery whose pixels are pinned for every platform.
import std;
import openmui.ui.base;
import openmui.host.headless;
import openmui.hash;
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

int main(int argc, char** argv) {
    Suite s{"ui.base.kit"};
    register_base_components();

    s.test("badge: a count past its max as max+, a label, a dot; its name and value say it", [] {
        Headless h(options());
        auto count = signal(std::int64_t{120});
        h.mount("main", row({.gap = 8}, {badge({.id = "mail", .count = count, .tone = BadgeTone::danger}), badge({.id = "new", .text = "New", .tone = BadgeTone::accent}),
                                        badge({.id = "unread", .dot = true, .text = "Unread messages"})}));
        h.step();
        check(sem(h, "main/mail")->name == "99+" && sem(h, "main/mail")->value == Value(std::int64_t{120}), "99+, its value the count");
        count.set(7);
        h.step();
        check_eq(sem(h, "main/mail")->name, std::string("7"));
        check(sem(h, "main/new")->name == "New" && sem(h, "main/unread")->name == "Unread messages", "a label; a dot named for what it marks");
        check(h.runtime().bounds(h.runtime().find("main/unread")).width == 8, "a dot is a dot");
    });

    s.test("avatar: initials, the name's own colour (the same everywhere), a picture when there is one, a status said", [] {
        check_eq(Avatar::initials("Ada Lovelace"), std::string("AL"));
        check_eq(Avatar::initials("grace brewster murray hopper"), std::string("GH"));
        check_eq(Avatar::initials("ada"), std::string("A"));
        check_eq(Avatar::initials("林黛玉"), std::string("林"));
        check_eq(Avatar::initials(""), std::string(""));
        check(Avatar::hue_of("Ada Lovelace", false) == Avatar::hue_of("Ada Lovelace", false), "the same name, the same colour");
        check(!(Avatar::hue_of("Ada Lovelace", false) == Avatar::hue_of("Alan Turing", false)), "another name, another colour (these two)");
        Headless h(options());
        h.mount("main", row({.gap = 8}, {avatar({.id = "ada", .name = "Ada Lovelace", .size = 40.0, .status = AvatarStatus::online})}));
        h.step();
        const auto n = sem(h, "main/ada");
        check(n && n->role == Role::image && n->name == "Ada Lovelace" && n->description == "online", n ? n->description : "none");
        check(h.runtime().bounds(h.runtime().find("main/ada")).width == 40, "its size");
    });

    s.test("skeleton: the shape of what is coming, busy to every reader", [] {
        Headless h(options());
        h.mount("main", column({.gap = 8}, {skeleton({.id = "text", .lines = 3, .width = 200}), skeleton({.id = "pic", .shape = SkeletonShape::circle})}));
        h.step();
        check(sem(h, "main/text")->states.has(State::busy) && sem(h, "main/text")->name == "Loading", "busy, named");
        check_eq(h.runtime().bounds(h.runtime().find("main/text")).height, 3.0f * 12 + 2 * 8);
        check_eq(h.runtime().bounds(h.runtime().find("main/pic")).width, 40.0f);
    });

    s.test("card: its title its name; interactive, activated whole (pointer, keys, agents); still, nothing to press", [] {
        Headless h(options());
        int opened = 0;
        h.mount("main", column({.padding = 8, .gap = 8}, {
                                   card({.id = "doc", .interactive = true, .subtitle = "Updated today", .title = "Q3 report", .on_activated = [&] { ++opened; }},
                                        {text({.text = "Revenue up 12%"})}),
                                   card({.id = "note", .title = "A note"}, {text({.text = "Read only"})}),
                               }));
        h.step();
        const auto doc = sem(h, "main/doc");
        check(doc && doc->name == "Q3 report" && doc->description == "Updated today" && doc->role == Role::button && offers(*doc, "activate"), "an interactive card is a button");
        check(h.click("main/doc") && opened == 1, "clicked");
        check(act(h, "main/doc", "activate") == 0 && opened == 2, "by an agent");
        const auto note = sem(h, "main/note");
        check(note && note->role == Role::group && !offers(*note, "activate") && !note->states.has(State::focusable), "a still card offers nothing");
        check(act(h, "main/note", "activate") != 0 && opened == 2, "and refuses");
    });

    s.test("accordion: its content only while expanded; its header opens it, its content does not; taken back", [] {
        Headless h(options());
        auto open = signal(false);
        h.mount("main", column({.padding = 8}, {accordion({.id = "faq", .expanded = open, .title = "What is openmui?"},
                                                         {text({.id = "answer", .text = "One semantic tree, many renderings."})})}));
        h.step();
        h.step();
        check(sem(h, "main/faq/answer")->states.has(State::hidden), "collapsed: its content hidden");
        const auto faq = sem(h, "main/faq");
        check(faq && offers(*faq, "expand") && !offers(*faq, "collapse") && !faq->states.has(State::expanded), "it offers to open");
        check(act(h, "main/faq", "expand") == 0 && open.peek(), "expanded by an agent: the binding follows");
        h.step();
        check(!sem(h, "main/faq/answer")->states.has(State::hidden), "its content shown");
        // The model is expanded before its enter motion paints the answer.
        // Test content activation at its presented box, rather than clicking
        // an invisible future layout position during the fade's initial delay.
        const auto answer = h.runtime().find("main/faq/answer");
        for (int frame = 0; frame < 120 && h.runtime().drawn_bounds(answer).empty(); ++frame) h.step();
        check(!h.runtime().drawn_bounds(answer).empty(), "the opening answer has reached a real presented frame");
        // A press on its content is the content's: it stays open.
        check(h.click("main/faq/answer") && open.peek(), "the content pressed: still open");
        // Its header: closes it.
        const Rect b = h.runtime().bounds(h.runtime().find("main/faq"));
        h.click(Point{b.x + 20, b.y + 10});
        h.step();
        check(!open.peek(), "the header pressed: closed");
        // Taken back: open again.
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        check(open.peek(), "undone: open again");
    });

    s.test("toast: over a corner of the window; gone after its time or dismissed; its action; two stack", [] {
        Headless h(options());
        auto saved = signal(false), copied = signal(false);
        int undone = 0, dismissed = 0;
        h.mount("main", column({}, {
                                   toast({.id = "saved", .action = "Undo", .duration = 3.0, .message = "Saved", .open = saved, .tone = ToastTone::success,
                                          .on_acted = [&] { ++undone; }, .on_dismissed = [&] { ++dismissed; }}),
                                   toast({.id = "copied", .duration = 0.0, .message = "Link copied", .open = copied}),
                               }));
        h.step();
        check(sem(h, "main/saved")->states.has(State::hidden), "closed: hidden");
        saved.set(true);
        copied.set(true);
        h.step();
        h.step();
        const Rect a = h.runtime().bounds(h.runtime().find("main/saved")), b = h.runtime().bounds(h.runtime().find("main/copied"));
        check(a.right() <= 420 - 15 && a.right() >= 420 - 17 && a.bottom() >= 300 - 17, std::format("bottom right: {} {}", a.right(), a.bottom()));
        check(b.bottom() <= a.y, std::format("the second above the first: {} ≤ {}", b.bottom(), a.y));
        check(sem(h, "main/saved")->role == Role::status && sem(h, "main/saved")->description == "Undo", "a status, its action said");
        check(act(h, "main/saved", "activate") == 0 && undone == 1 && !saved.peek() && dismissed == 1, "its action: done, and gone");
        saved.set(true);
        h.step();
        h.advance(seconds(3.5));
        check(!saved.peek() && dismissed == 2, "gone after its time");
        check(copied.peek(), "one without a time stays");
        check(act(h, "main/copied", "dismiss") == 0 && !copied.peek(), "until dismissed");
        check(act(h, "main/copied", "activate") != 0, "no action to perform");
    });

    s.test("popover: beside what opened it, not modal; Escape, a press outside or an agent closes it", [] {
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
        check(p.y >= anchor.bottom() && p.y <= anchor.bottom() + 16, std::format("below it: {} after {}", p.y, anchor.bottom()));
        check(sem(h, "main/menu")->name == "Share options" && !sem(h, "main/menu")->states.has(State::modal), "named, not modal");
        h.click(Point{400, 290});
        h.step();
        check(!open.peek() && closed == 1, "a press outside closes it");
        open.set(true);
        h.step();
        check(act(h, "main/menu", "dismiss") == 0 && !open.peek() && closed == 2, "an agent's dismiss");
    });

    s.test("breadcrumb: each place a link of its own in the tree; going up one leaves the ones below", [] {
        Headless h(options());
        auto path = signal(std::vector<std::string>{"Home", "Docs", "Guide", "Motion"});
        std::vector<std::int64_t> went;
        h.mount("main", column({.padding = 8}, {breadcrumb({.id = "crumbs", .items = path, .on_navigated = [&](std::int64_t i) { went.push_back(i); }})}));
        h.step();
        const auto items = h.runtime().virtual_items(h.runtime().find("main/crumbs"));
        check(items.size() == 4 && items[0].role == Role::link && items[0].name == "Home" && items[3].states.has(State::selected), "four links, the last the current one");
        check(offers(items[1], "activate") && !offers(items[3], "activate"), "the current one is not a link to press");
        check(act(h, "main/crumbs/1", "activate") == 0, "an agent goes to Docs");
        check(path.peek() == std::vector<std::string>{"Home", "Docs"} && went == std::vector<std::int64_t>{1}, "up to Docs: the ones below left");
        check(act(h, "main/crumbs", "go", Value{}.set("index", std::int64_t{0})) == 0 && path.peek().size() == 1, "go by index");
        check(sem(h, "main/crumbs")->role == Role::navigation && sem(h, "main/crumbs")->name == "Breadcrumb", "a navigation, named");
    });

    s.test("pagination: the first, the pages around, the last, gaps between; stepped, set, each page a link; taken back", [] {
        Headless h(options());
        auto page = signal(std::int64_t{10});
        h.mount("main", column({.padding = 8}, {pagination({.id = "pages", .page = page, .pages = 20})}));
        h.step();
        Pagination p;
        p.props.page = 10, p.props.pages = 20, p.props.around = 1;
        check(p.slots() == std::vector<std::int64_t>{0, 1, -2, 9, 10, 11, -2, 20, -1}, "previous 1 … 9 10 11 … 20 next");
        p.props.page = 2;
        check(p.slots() == std::vector<std::int64_t>{0, 1, 2, 3, -2, 20, -1}, "near the start: no gap there");
        check(act(h, "main/pages", "increment") == 0 && page.peek() == 11, "next");
        check(act(h, "main/pages", "set_value", Value{}.set("value", "last")) == 0 && page.peek() == 20, "the last");
        check(act(h, "main/pages", "increment") != 0 && page.peek() == 20, "none after the last");
        check(act(h, "main/pages/19", "activate") == 0 && page.peek() == 19, "a page as a link of its own");
        const auto items = h.runtime().virtual_items(h.runtime().find("main/pages"));
        check(std::ranges::any_of(items, [](const spec::SemanticNode& n) { return n.name == "Page 19" && n.states.has(State::selected); }), "the current one selected");
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        check_eq(page.peek(), std::int64_t{20});
    });

    s.test("number input: stepped and kept within its range on its step; typed and kept; taken back exactly", [] {
        Headless h(options());
        auto qty = signal(2.0);
        h.mount("main", column({.padding = 8}, {number_input({.id = "qty", .digits = 1, .label = "Quantity", .max = 10.0, .min = 0.0, .step = 0.5, .value = qty})}));
        h.step();
        check(act(h, "main/qty", "increment") == 0 && qty.peek() == 2.5, "a step");
        check(act(h, "main/qty", "set_value", Value{}.set("value", 99.0)) == 0 && qty.peek() == 10.0, "kept within its max");
        check(act(h, "main/qty", "set_value", Value{}.set("value", 3.3)) == 0 && qty.peek() == 3.5, "kept on its step");
        const auto n = sem(h, "main/qty");
        check(n && n->role == Role::spin_button && n->name == "Quantity" && n->value.get("max") && n->value.get("max")->as_double() == 10, "a spin button, its range said");
        // Typed: the digits, then Enter.
        check(h.click("main/qty"), "focused");
        h.type("7");
        h.key(Key::enter);
        h.step();
        check_eq(qty.peek(), 7.0);
        Invocation call;
        call.actor = {"agent", "test", {}};
        (void)h.tree().act("app", "undo", call);
        h.step();
        check_eq(qty.peek(), 3.5);
    });

    s.test("actor log: an agent's actions, newest last, each a node; the last of its history taken back from here, by the person or the agent; the node it acted on pointed out; the agent link said", [] {
        Headless h(options({420, 240}));
        auto dark = signal(false);
        auto volume = signal(40.0);
        h.mount("main", column({.padding = 8, .gap = 8}, {
                                   row({.id = "set", .gap = 8}, {toggle({.id = "dark", .checked = dark, .label = "Dark"}), slider({.id = "volume", .label = "Volume", .value = volume, .width = 140})}),
                                   actor_log({.id = "log", .width = Dim(400.0f)}),
                               }));
        h.step();
        check(sem(h, "main/log")->role == Role::log, "a log");
        check(h.runtime().virtual_items(h.runtime().find("main/log")).empty(), "nothing yet");
        check(sem(h, "main/log")->value.get("link")->as_string() == "unavailable", "the headless host links none");
        check(act(h, "main/set/dark", "toggle") == 0 && dark.peek(), "an agent turns it on");
        h.advance(seconds(1));
        check(act(h, "main/set/volume", "set_value", Value{}.set("value", 70.0)) == 0);
        check(h.click("main/set/dark") && !dark.peek(), "the person's own: not listed");
        h.settle();
        auto items = h.runtime().virtual_items(h.runtime().find("main/log"));
        check(items.size() == 2 && items[0].role == Role::list_item && items[0].name == "test toggle …/set/dark" && items[1].name == "test set_value …/set/volume 70" && items[0].value.get("node")->as_string() == "main/set/dark",
              items.empty() ? std::string("none") : items[0].name + " | " + items.back().name);
        check(!offers(items[0], "undo") && !offers(items[1], "undo"), "the person acted since: neither is the last its history holds");
        // The person undoes their own: the agent's volume is the last again.
        Invocation person;
        person.actor = {"person", "user", {}};
        check(h.tree().act("app", "undo", person).exit_code() == std::optional<int>(0) && dark.peek());
        h.settle();
        items = h.runtime().virtual_items(h.runtime().find("main/log"));
        check(offers(items[1], "undo"), "the volume's is the last now");
        // An agent takes back its own, through the log.
        check(act(h, std::format("main/log/{}", items[1].id.substr(items[1].id.rfind('/') + 1)), "undo") == 0 && volume.peek() == 40.0, "taken back by the agent");
        items = h.runtime().virtual_items(h.runtime().find("main/log"));
        check(offers(items[0], "undo") && !offers(items.back(), "undo"), "then the toggle's (the agent's undo listed too, as it did it)");
        // The person: its Undo pressed.
        h.settle();
        const Rect log = h.runtime().screen_bounds(h.runtime().find("main/log"));
        h.move({log.x + 200, log.y + 14});
        h.settle();
        check(h.runtime().pointed_out() == "main/set/dark", "hovered: the toggle pointed out");
        h.click({log.x + log.width - 20, log.y + 14});
        h.settle();
        check(!dark.peek(), "the person's Undo: the agent's toggle taken back");
        h.move({4, 4});
        h.settle();
        check(h.runtime().pointed_out().empty(), "left: nothing pointed out");
        // The agent link, as a host that can link one says it.
        h.runtime().set_agent_linker({[&] { return std::string("K7Q4-42MX"); }, [] {}});
        h.runtime().agent_link_changed({"waiting", "K7Q4-42MX", {}, {}, 47110});
        h.settle();
        check(sem(h, "main/log")->value.get("link")->as_string() == "waiting" && !offers(*sem(h, "main/log"), "unlink"), "waiting; ending it is the person's, not an action here");
        h.runtime().agent_link_changed({"paired", "K7Q4-42MX", "claude-code", {}, 47110});
        h.settle();
        // The picture: the agent's last toggle, its Undo, hovered — the toggle outlined.
        check(act(h, "main/set/dark", "toggle") == 0);
        h.settle();
        const auto last = *h.runtime().virtual_item(h.runtime().find("main/log"), std::to_string(h.runtime().action_journal().back().id))->bounds;
        h.move({static_cast<float>(last.x) + 100, static_cast<float>(last.y + last.height / 2)});
        h.settle();
        check(h.runtime().pointed_out() == "main/set/dark");
        check(h.save_png(scratch_dir("ui-base-actor-log") / "actor-log.png").has_value());
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        std::println("ui.base actor log {:016x}", hash.value());
        // Purposeful default motions also change the settled virtual times
        // shown by this log; the final controls and real records stay pinned.
        check(hash.value() == 0x63ff43abfa3ef767ULL, std::format("actor log {:016x}", hash.value()));
    });

    s.test("the gallery: the ten, the same pixels on every platform", [] {
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
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
        check(h.save_png(scratch_dir("ui-base-kit") / "kit@2x.png").has_value());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check(hash.value() == 0x73ad247abe7f2745ULL, std::format("kit gallery {:016x}", hash.value()));
    });

    return s.run(argc, argv);
}
