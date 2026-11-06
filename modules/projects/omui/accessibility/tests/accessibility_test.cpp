// The tree as assistive technology reads it: what nodes become, what changes
// say, and the requests a screen reader makes performed as the person's input;
// where AccessKit has a library, the update it would be told.
import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.accessibility;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;
namespace a11y = omui::accessibility;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

RuntimeOptions options() {
    RuntimeOptions o;
    o.fonts = &fonts();
    o.size = {320, 200};
    return o;
}

spec::SemanticNode node(std::string id, Role role, std::string name, std::vector<std::string> actions = {}) {
    spec::SemanticNode n;
    n.id = std::move(id);
    n.role = role;
    n.name = std::move(name);
    for (std::string& a : actions) n.actions.push_back({.name = std::move(a)});
    return n;
}

struct App {
    Signal<int>  count{0};
    Signal<bool> extra{false};

    View view() {
        return column({.padding = 8, .gap = 4}, {
            text({.id = "title", .text = "Notes", .style = "title"}),
            text({.id = "count", .text = derive([c = count] { return std::format("{} notes", c.get()); })}),
            box({.id = "add", .width = 60, .height = 24, .label = "Add note",
                 .on = {{"press", [c = count](const Value&) { c.set(c.peek() + 1); }}}}),
            when([e = extra] { return e.get(); }, [] { return box({.id = "more", .width = 40, .height = 20, .label = "More"}); }),
        });
    }
};

// A text field of two lines ("ab\n", "cd"), each character 10 wide, whose
// selection a test moves: what a text box says of its laid-out text.
struct Field {
    static constexpr std::string_view name = "field";
    static constexpr Role             role = Role::text_box;
    Services*   services = nullptr;
    NodeId      node;
    std::size_t anchor = 0, focus = 0;
    static inline bool joined = false;  // its two lines one, as a very long line is cut

    void mount(Services& s, NodeId n) {
        services = &s;
        node = n;
    }
    void semantics(SemanticsContext&, spec::SemanticNode& n) const {
        n.name = "Note";
        n.value = Value("ab\ncd");
        n.states.set(State::editable).set(State::focusable);
        n.actions.push_back({"set_selection", Effect::read, {}, {}, {{"anchor", "i64"}, {"focus", "i64"}}});
    }
    ActResult act(ActContext& ctx, std::string_view a) {
        if (a != "set_selection") return ActResult::fail("no such action");
        anchor = static_cast<std::size_t>(ctx.args.get("anchor")->as_int());
        focus = static_cast<std::size_t>(ctx.args.get("focus")->as_int());
        return ActResult::done();
    }
    std::optional<TextLayout> text_layout() const {
        TextLayout t;
        t.text = "ab\ncd";
        t.lines.push_back({0, 3, Rect{10, 20, 30, 16}, false, {1, 1, 1}, {0, 10, 20}, {10, 10, 0}});
        t.lines.push_back({3, 5, Rect{10, 36, 20, 16}, false, {1, 1}, {0, 10}, {10, 10}});
        t.lines[0].continues = joined;
        t.anchor = anchor;
        t.focus = focus;
        return t;
    }
};

Element field(std::string id) {
    Element e;
    e.type = &component_type<Field>();
    e.id = std::move(id);
    return e;
}

const a11y::Node* find(const std::vector<a11y::Node>& nodes, std::string_view id) {
    for (const a11y::Node& n : nodes)
        if (n.key == a11y::key_of(id)) return &n;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"accessibility"};

    s.test("a node as AccessKit models it", [] {
        auto button = node("main/save", Role::button, "Save", {"activate", "focus", "dismiss"});
        button.states.set(State::focusable);
        button.bounds = spec::Rect{10, 20, 60, 24};
        button.children = {"main/save/icon"};
        const a11y::Node b = a11y::describe(button);
        check(b.role == "Button" && b.label == "Save" && b.key == a11y::key_of("main/save"));
        check(b.has(a11y::Action::click) && b.has(a11y::Action::focus) && b.has(a11y::Action::custom));
        check(b.custom == std::vector<std::string>{"dismiss"}, "an action AccessKit has no name for is a custom one");
        check(b.bounds && b.bounds->x == 10 && b.children == std::vector<std::uint64_t>{a11y::key_of("main/save/icon")});

        // Static text says its words as its value: AccessKit names a label from it.
        const a11y::Node t = a11y::describe(node("main/title", Role::text, "Notes"));
        check(t.role == "Label" && t.value == "Notes" && t.label.empty());

        auto box = node("main/agree", Role::checkbox, "Agree", {"toggle"});
        box.value = Value(true);
        const a11y::Node c = a11y::describe(box);
        check(c.toggled == a11y::Toggled::on && c.has(a11y::Action::click), "a checkbox's click toggles it");
        box.value = Value("mixed");
        check(a11y::describe(box).toggled == a11y::Toggled::mixed);

        auto slider = node("main/volume", Role::slider, "Volume", {"set_value", "increment", "decrement"});
        slider.value = Value{}.set("value", 0.25).set("min", 0.0).set("max", 1.0).set("step", 0.05);
        const a11y::Node v = a11y::describe(slider);
        check(v.number == 0.25 && v.min == 0.0 && v.max == 1.0 && v.step == 0.05);
        check(v.has(a11y::Action::set_value) && v.has(a11y::Action::increment) && v.has(a11y::Action::decrement) &&
              !v.has(a11y::Action::click));

        auto heading = node("doc/intro", Role::heading, "Introduction");
        heading.value = Value(std::int64_t{2});
        check(a11y::describe(heading).level == std::size_t{2});
        auto link = node("doc/home", Role::link, "Home", {"activate"});
        link.value = Value("https://example.org");
        check(a11y::describe(link).url == "https://example.org");

        auto tab = node("main/tabs/one", Role::tab, "One", {"select"});
        check(a11y::describe(tab).selected == false, "a tab says it is not selected, too");
        tab.states.set(State::selected);
        check(a11y::describe(tab).selected == true);

        auto scroll = node("main/list", Role::scroll_view, "", {"scroll_to", "scroll_by"});
        scroll.value = Value{}.set("x", 0.0).set("y", 120.0).set("max_x", 0.0).set("max_y", 400.0);
        const a11y::Node sv = a11y::describe(scroll);
        check(sv.scroll_y == 120.0 && sv.scroll_y_max == 400.0 && sv.has(a11y::Action::set_scroll_offset) &&
              sv.has(a11y::Action::scroll_down));

        check(a11y::describe(node("main/state", Role::status, "Saved")).live == a11y::Live::polite);
        auto off = node("main/off", Role::button, "Off", {"activate"});
        off.states.set(State::disabled).set(State::readonly);
        const a11y::Node o = a11y::describe(off);
        check(o.disabled && o.readonly);
        check(a11y::key_of("") != 0);
    });

    s.test("the mirror: the window, what changed, the focus", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame({seconds(0), {}});
        a11y::Mirror mirror(rt, {.name = "Notes window"});
        const auto first = mirror.refresh();
        check(first.nodes.size() == mirror.size() && mirror.size() >= 4, std::format("{}", mirror.size()));
        const a11y::Node* root = find(first.nodes, "app");
        check(root && root->role == "Window" && root->label == "Notes window" && mirror.root() == root->key);
        check(first.focus == mirror.root(), "nothing focused: the window");
        const a11y::Node* add = find(first.nodes, "main/add");
        check(add && add->label == "Add note" && add->has(a11y::Action::click));
        check(mirror.id_of(add->key) == std::optional<std::string>("main/add"));
        check(mirror.refresh().empty(), "nothing happened");

        app.count.set(3);
        rt.frame({seconds(0), {}});
        const auto d = mirror.refresh();
        check(d.nodes.size() == 1 && d.nodes[0].key == a11y::key_of("main/count") && d.nodes[0].value == "3 notes");

        app.extra.set(true);
        rt.frame({seconds(1), {}});
        const auto added = mirror.refresh();
        check(find(added.nodes, "main/more") != nullptr, "the new node");
        bool parent_sent = false;
        for (const a11y::Node& n : added.nodes)
            parent_sent = parent_sent || std::ranges::find(n.children, a11y::key_of("main/more")) != n.children.end();
        check(parent_sent, "and the node that holds it, with its children");
        app.extra.set(false);
        rt.frame({seconds(2), {}});
        (void)mirror.refresh();
        check(!mirror.id_of(a11y::key_of("main/more")), "a removed node is forgotten");

        mirror.rename("Renamed");
        bool renamed = false;
        mirror.each([&](const a11y::Node& n) { renamed = renamed || (n.key == mirror.root() && n.label == "Renamed"); });
        check(renamed);
    });

    s.test("a screen reader's requests, performed as the person's input", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame({seconds(0), {}});
        a11y::Mirror mirror(rt);
        (void)mirror.refresh();
        int woken = 0;
        mirror.set_wake([&] { ++woken; });
        // Requests come from the platform's threads, and wait.
        std::jthread at([&] { mirror.queue({.target = a11y::key_of("main/add"), .action = a11y::Action::click}); });
        at.join();
        check(woken == 1 && mirror.waiting() == 1 && app.count.peek() == 0, "nothing is done off the runtime's thread");
        const auto answers = mirror.serve();
        check(answers.size() == 1 && answers[0].exit_code() == std::optional<int>(0));
        check(app.count.peek() == 1 && mirror.waiting() == 0);

        const Envelope none = mirror.perform({.target = 12345, .action = a11y::Action::click});
        check(none.exit_code() == std::optional<int>(1));
        const Envelope wrong = mirror.perform({.target = a11y::key_of("main/add"), .action = a11y::Action::increment});
        check(wrong.exit_code() == std::optional<int>(1), "an action the node has not is refused, not guessed");
        check(app.count.peek() == 1);
    });

    s.test("one line cut in runs: each says the next and the one before on its line", [] {
        Field::joined = true;
        Runtime rt(options());
        rt.mount("main", column({}, {field("note")}));
        rt.frame({seconds(0), {}});
        a11y::Mirror mirror(rt);
        const auto first = mirror.refresh();
        Field::joined = false;
        const a11y::Node* note = find(first.nodes, "main/note");
        check(note && note->children.size() == 2);
        if (!note || note->children.size() != 2) return;
        const a11y::Node *a = nullptr, *b = nullptr;
        for (const a11y::Node& n : first.nodes) {
            if (n.key == note->children[0]) a = &n;
            if (n.key == note->children[1]) b = &n;
        }
        check(a && b && a->next_on_line == b->key && b->previous_on_line == a->key && !a->previous_on_line && !b->next_on_line,
              "the relations both ways");
    });

    s.test("a text field's text: its runs (lines), characters and caret; a screen reader moves the selection", [] {
        Runtime rt(options());
        Signal<int> unused{0};
        rt.mount("main", column({}, {field("note")}));
        rt.frame({seconds(0), {}});
        a11y::Mirror mirror(rt);
        const auto first = mirror.refresh();
        const a11y::Node* note = find(first.nodes, "main/note");
        check(note && note->role == "TextInput" && note->children.size() == 2, "the field holds a run a line");
        if (!note) return;
        std::vector<const a11y::Node*> runs;
        for (std::uint64_t k : note->children)
            for (const a11y::Node& n : first.nodes)
                if (n.key == k) runs.push_back(&n);
        check(runs.size() == 2 && runs[0]->role == "TextRun" && runs[0]->value == "ab\n" && runs[1]->value == "cd",
              "each run's words, the line break with the first");
        check(runs.size() == 2 && runs[0]->lengths == std::vector<std::uint8_t>{1, 1, 1} && runs[1]->positions == std::vector<float>{0, 10});
        check(note->selection && note->selection->focus.run == note->children[0] && note->selection->focus.index == 0, "the caret at the start");
        check(note->has(a11y::Action::set_text_selection));
        // A screen reader selects "bc" (the second character of the first run to the first of the second… one past it).
        a11y::Request r{.target = note->key, .action = a11y::Action::set_text_selection};
        r.selection = a11y::TextSelection{{note->children[0], 1}, {note->children[1], 1}};
        const Envelope e = mirror.perform(r);
        check(e.exit_code() == std::optional<int>(0), e.ndjson());
        rt.frame({seconds(0), {}});
        // Nothing in the tree changed, yet the caret moved: told.
        const auto moved = mirror.refresh();
        const a11y::Node* again = find(moved.nodes, "main/note");
        check(again == nullptr, "not focused: the caret is read when the field is focused, or when the tree changes");
        (void)rt.semantic_node(rt.find("main/note"));
        check(rt.text_layout(rt.find("main/note"))->anchor == 1 && rt.text_layout(rt.find("main/note"))->focus == 4,
              "graphemes 1 to 4: runs and characters to the field's own offsets");
        (void)unused;
    });

    s.test("the update AccessKit would be told", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame({seconds(0), {}});
        a11y::Mirror mirror(rt, {.name = "Notes"});
        (void)mirror.refresh();
        const std::string text = a11y::debug(mirror, {.scale = 2});
        if constexpr (!a11y::available) {
            check(text.empty() && a11y::service() == "none");
            return;
        }
        check(text.find("role: Window") != std::string::npos && text.find("\"Notes\"") != std::string::npos, text);
        check(text.find("label: \"Add note\"") != std::string::npos && text.find("Click") != std::string::npos, text);
        check(text.find("value: \"0 notes\"") != std::string::npos, text);
        check(text.find("toolkit_name: Some(\"openmui\")") != std::string::npos, text);
        check(!a11y::service().empty() && a11y::service() != "none");
    });

    return s.run(argc, argv);
}
