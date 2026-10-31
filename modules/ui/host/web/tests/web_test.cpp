// The browser host's loop, off the Web (no page: stepped by hand): a frame
// when something wants one and none while nothing moves; the frame drawn by
// the CPU renderer; input — keys, the pointer, a gamepad — reaching the
// focus as a page's would. The page's other halves as the C++ side makes
// them: the semantic DOM mirror's batches (only what changed), window.omui's
// requests decided as the bus's (never the person's), what assistive
// technology does in the hidden DOM, the store. The page's own half
// (src/js/omui_web.js) is tested under node with a fake DOM: tests/js.
import std;
import openmui.host.web;
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

Source bound(std::function<Value()> fn) { return Source{std::nullopt, std::move(fn), {}}; }
Source fixed(Value v) { return Source{std::move(v), {}, {}}; }

// A page's worth of interface: a heading, a checkbox, a button the focus
// can reach, a status, a toast that comes and goes.
struct Page {
    Signal<std::string> task{std::string("Buy milk")};
    Signal<std::string> status{std::string("Ready")};
    Signal<bool>        toast{false};
    int                 saved = 0;

    View view() {
        return column({.padding = 10, .gap = 4}, {
            text({.id = "title", .text = "Notes"}),
            box({.id = "done", .width = 20, .height = 20})
                .prop("role", fixed(Value("checkbox")))
                .prop("label", bound([t = task] { return Value(t.get()); })),
            box({.id = "save", .width = 60, .height = 24, .label = "Save", .on = {{"press", [this](const Value&) { ++saved; }}}})
                .prop("focusable", fixed(Value(true))),
            box({.id = "status", .width = 100, .height = 16})
                .prop("role", fixed(Value("status")))
                .prop("label", bound([s = status] { return Value(s.get()); })),
            when([t = toast] { return t.get(); }, [] { return box({.id = "toast", .width = 80, .height = 20, .label = "Saved"}); }),
        });
    }
};

RuntimeOptions page_options() {
    RuntimeOptions o;
    o.fonts = &fonts();
    o.size = {200, 160};
    return o;
}

const Value* node_of(const Value& batch, std::string_view id) {
    if (const Value* nodes = batch.get("nodes"); nodes && nodes->list())
        for (const Value& n : *nodes->list())
            if (n.get("id")->as_string() == id) return &n;
    return nullptr;
}

std::string set_of(const Value* node, std::string_view key) {
    if (!node) return "<no node>";
    const Value* set = node->get("set");
    const Value* v = set ? set->get(key) : nullptr;
    return v ? std::string(v->as_string()) : "<unset>";
}

std::vector<Value> records(Web& w, std::string_view line) {
    std::vector<Value> out;
    w.request(line, [&](std::string_view l) { out.push_back(parse_json(l).value()); });
    (void)w.step();
    return out;
}

int exit_of(const std::vector<Value>& r) {
    return r.empty() || !r.back().get("exitCode") ? -100 : static_cast<int>(r.back().get("exitCode")->as_int());
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"host.web"};

    s.test("no page (a native target, or node): the loop stepped by hand", [] {
        check(!in_browser());
        Web w({.size = {200, 100}, .fonts = &fonts()});
        auto color = signal(std::string("#ff0000"));
        w.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40,
                                                      .background = Prop<Fill>(Source{std::nullopt, [color] { return Value(color.get()); }, {}})})}));
        check(w.step(), "the first frame");
        check_eq(w.frames_shown(), std::size_t{1});
        const auto& px = w.pixels();
        check(px.width() == 200 && px.height() == 100, std::format("{}x{}", px.width(), px.height()));
        check(px.at(40, 40).r == 255 && px.at(40, 40).g == 0 && px.at(5, 5).g == 255, "the CPU frame");
        (void)w.step();
        check(w.frames_shown() == 1, "nothing changed: nothing shown");
        color.set("#0000ff");
        (void)w.step();
        check(w.pixels().at(40, 40).b == 255 && w.frames_shown() == 2, "a change: a frame");
    });

    s.test("input as the page gives it: a key and a gamepad to the focus", [] {
        Web w({.size = {200, 100}, .fonts = &fonts()});
        std::vector<std::string> seen;
        w.mount("main", column({}, {painter({.id = "game", .width = 100, .height = 60, .focusable = true,
                                             .on_event = [&](EventContext&, const InputEvent& e) {
                                                 if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down)
                                                     seen.push_back(std::string(name(k->key)));
                                                 if (const auto* g = std::get_if<GamepadEvent>(&e); g && g->kind == GamepadKind::down)
                                                     seen.push_back(std::string(name(g->button)));
                                                 return true;
                                             }})}));
        (void)w.step();
        w.runtime().focus(w.runtime().find("main/game"));
        std::array<InputEvent, 3> events{KeyEvent{.kind = KeyKind::down, .key = Key::space},
                                         GamepadEvent{.kind = GamepadKind::connected, .device = 1},
                                         GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::south}};
        w.synthesize(events);
        check(w.step(), "input: a frame");
        check(seen == std::vector<std::string>{"Space", "south"}, std::format("{}", seen));
    });

    s.test("the semantic DOM mirror: everything first, then only what changed", [] {
        Runtime rt(page_options());
        Page page;
        rt.mount("main", page.view());
        rt.frame();
        DomMirror mirror(rt);
        auto first = mirror.sync();
        check(first.has_value(), "the first batch");
        if (!first) return;
        check(first->get("root") && first->get("root")->as_string() == "app", first->json());
        check(!first->get("remove") && !first->get("live"), "nothing removed, nothing said at the start");
        // The registry's ARIA roles, the names and states as attributes.
        check_eq(set_of(node_of(*first, "main/done"), "role"), std::string("checkbox"));
        check_eq(set_of(node_of(*first, "main/done"), "aria-checked"), std::string("false"));
        check_eq(set_of(node_of(*first, "main/done"), "aria-label"), std::string("Buy milk"));
        check_eq(set_of(node_of(*first, "main/save"), "role"), std::string("button"));
        check_eq(set_of(node_of(*first, "main/save"), "tabindex"), std::string("-1"));
        check(set_of(node_of(*first, "main/status"), "aria-live") == "off", "the live region speaks for a status");
        check(set_of(node_of(*first, "app"), "role") == "<unset>", "the root is no application");
        const Value* title = node_of(*first, "main/title");
        check(title && title->get("text") && title->get("text")->as_string() == "Notes", "a text's name is its element's text");
        check(set_of(title, "aria-label") == "<unset>", "ARIA names no text");
        // Boxes over the canvas, each relative to its parent's (the column
        // is lifted: a nameless wrapper; the root is the canvas's whole).
        const Value* save = node_of(*first, "main/save");
        const Value* main = node_of(*first, "app");
        check(save && save->get("box") && main && main->get("box"), "boxes");
        if (save && main && save->get("box") && main->get("box")) {
            const auto& b = *save->get("box")->list();
            const auto at = rt.bounds(rt.find("main/save"));
            const auto& m = *main->get("box")->list();
            check(b[0].as_int() + m[0].as_int() == std::lround(at.x) && b[1].as_int() + m[1].as_int() == std::lround(at.y) &&
                      b[2].as_int() == std::lround(at.width),
                  save->json());
        }
        check(main && main->get("children") && main->get("children")->json() == R"(["main/title","main/done","main/save","main/status"])",
              "children in order");
        check(!node_of(*first, "main/save")->get("children"), "a leaf has none to send");
        check(!mirror.sync(), "nothing changed: nothing sent");

        // One name changed: one attribute.
        page.task.set("Buy oat milk");
        rt.frame();
        auto checked = mirror.sync();
        check(checked.has_value());
        if (checked) {
            const Value* done = node_of(*checked, "main/done");
            check(done && done->map()->size() == 2 && set_of(done, "aria-label") == "Buy oat milk", checked->json());
            check(checked->get("nodes")->list()->size() == 1, "nothing else");
            check(!checked->get("focus") && !checked->get("live"), checked->json());
        }

        // A status that changes is said; the focus moves; a node comes and goes.
        page.status.set("Saved 3 notes");
        page.toast.set(true);
        rt.focus(rt.find("main/save"));
        rt.frame();
        auto said = mirror.sync();
        check(said.has_value());
        if (said) {
            check(said->get("live") && said->get("live")->as_string() == "Saved 3 notes", said->json());
            check(said->get("focus") && said->get("focus")->as_string() == "main/save", said->json());
            check(node_of(*said, "main/toast") != nullptr, "the toast made");
            check(mirror.focused() == "main/save" && mirror.focused_name() == "Save");
        }
        page.toast.set(false);
        rt.focus(NodeId{});
        rt.frame();
        auto gone = mirror.sync();
        check(gone.has_value());
        if (gone) {
            const Value* removed = gone->get("remove");
            check(removed && removed->list() && removed->list()->size() == 1 && (*removed->list())[0].as_string() == "main/toast", gone->json());
            check(gone->get("focus") && gone->get("focus")->is_null(), "the focus gone");
        }
        check(mirror.focused().empty());
    });

    s.test("ARIA from the registry: roles, states, ranges", [] {
        auto attrs = [](Role role, std::initializer_list<State> states, Value value = {}) {
            spec::SemanticNode n;
            n.id = "main/x";
            n.role = role;
            n.name = "X";
            n.value = std::move(value);
            for (State st : states) n.states.set(st);
            std::string out;
            for (const auto& [k, v] : aria_attributes(n)) out += std::format("{}={} ", k, v);
            return out;
        };
        check_eq(attrs(Role::checkbox, {State::mixed}), std::string("aria-checked=mixed aria-label=X role=checkbox "));
        check_eq(attrs(Role::toggle_button, {State::checked}), std::string("aria-label=X aria-pressed=true role=button "));
        check_eq(attrs(Role::tab, {}), std::string("aria-label=X aria-selected=false role=tab "));
        check_eq(attrs(Role::disclosure, {State::collapsible}), std::string("aria-expanded=false aria-label=X role=button "));
        check_eq(attrs(Role::slider, {State::focusable}, Value(0.5)), std::string("aria-label=X aria-valuenow=0.5 role=slider tabindex=-1 "));
        check_eq(attrs(Role::progress, {}, Value("half")), std::string("aria-label=X aria-valuetext=half role=progressbar "));
        check_eq(attrs(Role::text_box, {State::disabled, State::multiline, State::required, State::invalid}),
                 std::string("aria-disabled=true aria-invalid=true aria-label=X aria-multiline=true aria-required=true role=textbox "));
        check_eq(attrs(Role::dialog, {State::modal}), std::string("aria-label=X aria-modal=true role=dialog "));
        check(attrs(Role::paragraph, {}) == "role=paragraph ", "a paragraph's name is its text, not a label");
        check(attrs(Role::generic, {State::busy}) == "aria-busy=true ", "a generic node is a plain element");
    });

    s.test("the mirror's batches are the same each run (deterministic)", [] {
        auto run = [] {
            Runtime rt(page_options());
            Page page;
            rt.mount("main", page.view());
            rt.frame();
            DomMirror m(rt);
            std::string out = m.sync().value_or(Value{}).json();
            page.toast.set(true);
            page.task.set("Call home");
            rt.frame();
            out += m.sync().value_or(Value{}).json();
            return out;
        };
        const std::string a = run();
        check(a == run() && !a.empty(), "two runs agree");
    });

    s.test("window.omui: the agent protocol over the bus, decided as any agent's", [] {
        Web w({.app = "org.example.notes", .size = {200, 160}, .fonts = &fonts()});
        Page page;
        int allowed = 0;
        VerbDesc allow{"notes.allow", "Allow what the agent asked", Effect::reversible, "", {}};
        allow.person = true;
        check(w.runtime().verbs().add(allow, [&](const Value&) -> VerbResult {
                     ++allowed;
                     return Value{};
                 }).has_value());
        (void)w.runtime().verbs().add({"notes.clear", "Remove every note", Effect::irreversible, "yes", {}},
                                       [](const Value&) -> VerbResult { return Value{}; });
        w.mount("main", page.view());
        (void)w.step();
        auto described = records(w, R"({"op":"describe","re":7})");
        check(exit_of(described) == 0 && described.front().get("data")->get("app")->as_string() == "org.example.notes",
              described.empty() ? "" : described.front().json());
        check(described.front().get("re")->as_int() == 7, "records carry the page's re");
        auto found = records(w, R"({"op":"find","q":{"role":"button","name":"Save"}})");
        check(exit_of(found) == 0 && found.front().json().contains("main/save"), found.empty() ? "" : found.front().json());
        // Acting: the press is done.
        check(exit_of(records(w, R"({"op":"act","node":"main/save","action":"activate"})")) == 0 && page.saved == 1);
        // An irreversible verb asks; the person's own is refused whatever the actor claims.
        check_eq(exit_of(records(w, R"({"op":"invoke","verb":"notes.clear"})")), 2);
        auto claimed = records(w, R"({"op":"invoke","verb":"notes.allow","actor":"person:user"})");
        check(exit_of(claimed) == 1 && claimed.front().json().contains("refused") && allowed == 0, claimed.empty() ? "" : claimed.front().json());
        // A wait answers when its condition holds, after a frame.
        std::vector<Value> waited;
        w.request(R"({"op":"wait","q":{"id":"main/toast"},"timeout":5})", [&](std::string_view l) { waited.push_back(parse_json(l).value()); });
        (void)w.step();
        check(waited.empty(), "not yet");
        page.toast.set(true);
        (void)w.step();
        check(exit_of(waited) == 0, waited.empty() ? "no answer" : waited.back().json());
        // The canvas, as PNG.
        auto shot = records(w, R"({"op":"capture","node":"main/save"})");
        check(exit_of(shot) == 0 && shot.front().get("data")->get("format")->as_string() == "png", shot.empty() ? "" : shot.front().json());
    });

    s.test("assistive technology in the hidden DOM: the focus moved, a node activated as a press", [] {
        Web w({.size = {200, 160}, .fonts = &fonts()});
        Page page;
        w.mount("main", page.view());
        (void)w.step();
        std::array<Web::PageAction, 1> focus{{{Web::PageAction::Kind::focus, "main/save"}}};
        w.synthesize(focus);
        (void)w.step();
        check(w.runtime().focused() == w.runtime().find("main/save"), "the focus where the screen reader put it");
        std::array<Web::PageAction, 2> press{{{Web::PageAction::Kind::activate, "main/save"}, {Web::PageAction::Kind::activate, "main/nothing"}}};
        w.synthesize(press);
        (void)w.step();
        check_eq(page.saved, 1);
    });

    s.test("the store: memory without a page, the contract's for scripts", [] {
        Web w({.size = {100, 100}, .fonts = &fonts()});
        check(w.store().kind() == "memory" && &w.store() == &omui::store());
        check(w.store().set("best", "1200").has_value() && w.store().get("best") == std::optional<std::string>("1200"));
        check(w.store().remove("best"));
    });

    return s.run(argc, argv);
}
