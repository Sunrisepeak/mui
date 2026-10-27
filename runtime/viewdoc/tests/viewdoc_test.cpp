// View documents, as the design states them (view-data-files §3): the same
// tree as a builder (CTS S level), checking with located diagnostics,
// bindings, events, conditions and loops (virtualised in a list), slots,
// reloads that keep state, components defined by documents, the schema and
// the canonical format, and random documents that never crash the reader.
import std;
import openmui.viewdoc;
import openmui.ui.base;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace vd = omui::viewdoc;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

HeadlessOptions options(Size size = {320, 200}) {
    HeadlessOptions o;
    o.size = size;
    o.fonts = &fonts();
    return o;
}

// Every semantic node under (and including) a node, in tree order.
void collect(Headless& h, NodeId n, std::vector<spec::SemanticNode>& out) {
    out.push_back(h.runtime().semantic_node(n));
    for (NodeId c : h.runtime().children(n)) collect(h, c, out);
}

std::vector<spec::SemanticNode> tree_of(Headless& h, std::string_view root = "main") {
    std::vector<spec::SemanticNode> out;
    collect(h, h.runtime().find(root), out);
    return out;
}

std::uint64_t pixels(Headless& h) {
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.value();
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.answer = std::move(answer);
    call.actor = {"agent", "test", {}};
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

std::string name_of(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? n->name : std::string("<none>");
}

vd::View load(Headless& h, std::string_view text, std::source_location where = std::source_location::current()) {
    auto v = vd::View::load(h.runtime(), text, {.file = "test.view.omui.json"});
    if (!v) {
        for (const auto& d : v.error()) fail(d.text("test.view.omui.json"), where);
        std::abort();
    }
    return std::move(*v);
}

// The diagnostics of a text, which must include one with `code` (and, when
// given, at line:column, and with `hint` in its hint).
void diagnoses(std::string_view text, std::string_view code, std::string_view message, std::uint32_t line = 0, std::uint32_t column = 0,
               std::string_view hint = {}, std::source_location where = std::source_location::current()) {
    const vd::ReadResult r = vd::read(text);
    for (const vd::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(message) != std::string::npos) {
            if (line && (d.line != line || d.column != column)) fail(std::format("{}: expected at {}:{}", d.text(), line, column), where);
            if (!hint.empty() && d.hint.find(hint) == std::string::npos) fail(std::format("{}: expected the hint `{}`", d.text(), hint), where);
            return;
        }
    std::string got;
    for (const vd::Diagnostic& d : r.diagnostics) got += "\n    " + d.text();
    fail(std::format("expected [{}] `{}`; got:{}", code, message, got.empty() ? " nothing" : got), where);
}

std::string view_of(std::string_view root, std::string_view extra = {}) {
    return std::format(R"j({{"omui": "0.1", "view": {{{}"root": {}}}}})j", extra, root);
}

constexpr std::string_view counter = R"j({
  "omui": "0.1",
  "imports": ["openmui.ui.base"],
  "view": {
    "name": "Counter",
    "signals": {"count": {"type": "int", "value": 0}},
    "root": {
      "type": "column",
      "props": {"gap": 8, "padding": 12},
      "children": [
        {"type": "text", "props": {"text": {"expr": "format('点击了 {} 次', count)"}}},
        {"type": "button", "id": "inc", "props": {"text": "加一"},
         "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}
      ]
    }
  }
}
)j";

}  // namespace

int main(int argc, char** argv) {
    Suite s{"viewdoc"};
    register_base_components();

    s.test("a document and its builder make the same tree and the same pixels (CTS S level)", [] {
        Headless doc(options()), code(options());
        auto view = load(doc, counter);
        view.mount(doc.runtime(), "main");

        auto count = signal(0);
        code.mount("main", column({.padding = 12, .gap = 8}, {
                               text({.text = fmt("点击了 {} 次", count)}),
                               button({.id = "inc", .text = "加一", .on_activated = [=] { count.update([](int c) { return c + 1; }); }}),
                           }));
        doc.step();
        code.step();
        check(tree_of(doc) == tree_of(code), "the semantic trees are equal");
        check(pixels(doc) == pixels(code), "the pixels are equal");
        check(doc.click("main/inc"));
        check(code.click("main/inc"));
        check_eq(act(doc, "main/inc", "activate"), std::int64_t{0});
        check_eq(act(code, "main/inc", "activate"), std::int64_t{0});
        doc.settle();
        code.settle();
        const auto a = tree_of(doc), b = tree_of(code);
        check(a == b, "and after a click and an agent's activation");
        check(a.size() == 3 && a[1].name == "点击了 2 次", a.size() > 1 ? a[1].name : "");
        check(pixels(doc) == pixels(code));
        check(view.signal("count") && view.signal("count")->peek() == Value(2));
    });

    s.test("diagnostics: where, what and how to fix it", [] {
        diagnoses("{\"omui\": \"0.1\",\n \"view\": {\"root\": {\"type\": \"buton\"}}}", "unknown-type", "no component is named `buton`", 2, 28,
                  "did you mean `button`?");
        diagnoses(view_of(R"j({"type": "TextBox"})j"), "unknown-type", "`TextBox`", 0, 0, "component names are lower case: `text_box`");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "props": {"txt": "x"}})j"), "unknown-property", "no property `txt`", 0, 0,
                  "did you mean `text`?");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "props": {"disabled": "yes"}})j"), "type", "`disabled` is a bool");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "props": {"variant": "huge"}})j"), "type", "`variant` is a enum", 0, 0,
                  "`primary`");
        diagnoses(view_of(R"j({"type": "text", "props": {"text": {"expr": "cout + 1"}}})j", R"j("signals": {"count": {"type": "int"}}, )j"),
                  "unknown-name", "`cout`", 0, 0, "did you mean `count`?");
        diagnoses(view_of(R"j({"type": "text", "props": {"text": {"expr": "sise(count)"}}})j", R"j("signals": {"count": {"type": "int"}}, )j"),
                  "unknown-function", "no function `sise`", 0, 0, "did you mean `size`?");
        // A syntax error points into the string, at the operator.
        diagnoses("{\"omui\": \"0.1\", \"view\": {\"signals\": {\"n\": {\"type\": \"int\"}},\n\"root\": {\"type\": \"text\", \"props\": {\"text\": {\"expr\": \"n = 1\"}}}}}",
                  "expression", "`=` is not an operator here", 2, 56, "compare with `==`");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "on": {"activated": [{"set": "total", "value": 1}]}})j"), "unknown-signal", "`total`");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "on": {"tapped": []}})j"), "unknown-event", "no event `tapped`");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "on": {"activated": [{"set": "n", "verb": "x.y"}]}})j", R"j("signals": {"n": {"type": "int"}}, )j"),
                  "operation", "exactly one of set, verb and emit");
        diagnoses(view_of(R"j({"type": "button", "id": "a b"})j"), "id", "is not an id", 0, 0, "S-8");
        diagnoses(view_of(R"j({"type": "row", "children": [{"type": "button", "id": "x"}, {"type": "button", "id": "x"}]})j"), "duplicate-id",
                  "two children are named `x`");
        diagnoses(view_of(R"j({"type": "row", "children": [{"type": "button", "props": {"text": "Go"}}]})j"), "missing-id", "has no id");
        diagnoses(view_of(R"j({"type": "column", "children": [{"for": "n", "in": "notes", "body": [{"type": "text"}]}]})j"), "unknown-model",
                  "`notes`");
        diagnoses(view_of(R"j({"type": "column", "children": [{"for": "n", "in": "notes", "body": [{"type": "text", "id": "t"}]}]})j",
                          R"j("models": {"notes": "note"}, )j"),
                  "id", "named by its row's key");
        diagnoses(view_of(R"j({"type": "column", "prop": {"gap": 4}})j"), "unknown-field", "`prop` is not a field", 0, 0, "did you mean `props`?");
        diagnoses(view_of(R"j({"type": "column", "props": {"gaps": 4}})j"), "unknown-property", "`column` has no property `gaps`", 0, 0,
                  "did you mean `gap`?");
        diagnoses(view_of(R"j({"type": "text_box", "id": "t", "props": {"placeholder": {"bind": "q"}}})j", R"j("signals": {"q": {"type": "string"}}, )j"),
                  "not-bindable", "`placeholder` is not a bindable property");
        diagnoses(view_of(R"j({"type": "text", "props": {"text": {"bind": "missing"}}})j"), "unknown-signal", "`missing`");
        diagnoses(view_of(R"j({"type": "button", "id": "b", "on": {"activated": [{"verb": "a.b"}, {"verb": "c.d"}]}})j"), "verb",
                  "an activation invokes one verb");
        diagnoses(view_of(R"j({"type": "text"})j", R"j("signals": {"n": {"type": "int", "value": "3"}}, )j"), "type", "the value of `n` is not a int");
        diagnoses(view_of(R"j({"type": "text"})j", R"j("signals": {"in": {"type": "int"}}, )j"), "name", "`in` is reserved");
        diagnoses("{\"omui\": \"0.1\", \"view\": {\"root\": {\"type\": \"text\",}}}", "json", "", 1, 50);
        diagnoses(R"j({"view": {"root": {"type": "text"}}})j", "version", "no format version");
        diagnoses(R"j({"omui": "0.1", "component": {"name": "x", "root": {"type": "button", "id": "b", "on": {"activated": [{"emit": "gone"}]}}}})j",
                  "unknown-event", "`gone` is not an event of the interface");
        diagnoses(R"j({"omui": "0.1", "component": {"name": "SearchBox", "root": {"type": "text"}}})j", "name", "lower case words", 0, 0,
                  "\"search_box\"");
        // The text of a diagnostic, as `omui check` prints it.
        const vd::ReadResult r = vd::read("{\"omui\": \"0.1\",\n \"view\": {\"root\": {\"type\": \"buton\"}}}");
        check(!r.ok() && r.errors() == 1);
        check_eq(r.diagnostics.at(0).text("a.view.omui.json"),
                 std::string("a.view.omui.json:2:28: error: no component is named `buton` (did you mean `button`?) [unknown-type]"));
        check_eq(r.diagnostics.at(0).to_value("a.view.omui.json").json(),
                 std::string(R"j({"level":"error","code":"unknown-type","file":"a.view.omui.json","pointer":"/view/root/type","line":2,"column":28,"message":"no component is named `buton`","hint":"did you mean `button`?"})j"));
        // Warnings do not stop a document.
        const vd::ReadResult w = vd::read(view_of(R"j({"type": "row", "children": [{"type": "button"}], "colour": 1})j"));
        check(w.ok() && w.errors() == 0 && w.diagnostics.size() == 2);
    });

    s.test("bindings: expressions follow signals and models; bind is two-way; tr reads the catalogue", [] {
        Headless h(options());
        auto view = vd::View::load(h.runtime(), view_of(R"j({"type": "column", "children": [
            {"type": "text_box", "id": "q", "props": {"text": {"bind": "query"}, "label": {"tr": "search"}}},
            {"type": "text", "id": "echo", "props": {"label": {"expr": "size(query) == 0 ? 'empty' : upper(query)"}}},
            {"type": "text", "id": "n", "props": {"label": {"expr": "format('{} notes', size(notes))"}}}
        ]})j", R"j("signals": {"query": {"type": "string"}}, "models": {"notes": "note"}, )j"),
                                   {.translate = [](std::string_view k) { return k == "search" ? std::string("Suchen") : std::string(k); }});
        check(view.has_value());
        view->mount(h.runtime(), "main");
        h.step();
        check_eq(name_of(h, "main/q"), std::string("Suchen"));
        check_eq(name_of(h, "main/echo"), std::string("empty"));
        check_eq(name_of(h, "main/n"), std::string("0 notes"));
        // A person types: the binding writes the signal, the expression follows.
        h.runtime().focus(h.runtime().find("main/q"));
        h.type("straße");
        h.step();
        check(view->signal("query")->peek() == Value("straße"));
        check_eq(name_of(h, "main/echo"), std::string("STRASSE"));
        // Code writes the signal: the field follows.
        view->signal("query")->set(Value("ok"));
        h.step();
        check(h.tree().node("main/q")->value == Value("ok"));
        (void)h.runtime().model("notes").push({{"a", Value(Value::Map{{"title", Value("A")}})}, {"b", Value(Value::Map{})}});
        h.step();
        check_eq(name_of(h, "main/n"), std::string("2 notes"));
    });

    s.test("if and for: regions reconciled by key, rows named by their keys", [] {
        Headless h(options({320, 400}));
        auto view = load(h, view_of(R"j({"type": "column", "children": [
            {"if": "show", "then": [{"type": "text", "id": "on", "props": {"text": "shown"}}],
                           "else": [{"type": "text", "id": "off", "props": {"text": "hidden"}}]},
            {"for": "note", "in": "notes", "where": "!only_open || !note.done",
             "body": [{"type": "text", "props": {"text": {"expr": "note.title"}}}]}
        ]})j", R"j("signals": {"show": {"type": "bool", "value": true}, "only_open": {"type": "bool"}}, "models": {"notes": "note"}, )j"));
        view.mount(h.runtime(), "main");
        Model& notes = h.runtime().model("notes");
        (void)notes.push({{"n1", Value(Value::Map{{"title", Value("Milk")}, {"done", Value(true)}})},
                          {"n2", Value(Value::Map{{"title", Value("Bread")}, {"done", Value(false)}})}});
        h.step();
        check(h.tree().node("main/on").has_value() && !h.tree().node("main/off"));
        check(h.tree().node("main/n1") && h.tree().node("main/n1")->name == "Milk");
        const NodeId bread = h.runtime().find("main/n2");
        view.signal("show")->set(Value(false));
        view.signal("only_open")->set(Value(true));
        h.settle();
        check(!h.tree().node("main/on") && h.tree().node("main/off"), "the condition flipped");
        check(!h.tree().node("main/n1") && h.tree().node("main/n2"), "the filter follows its signal");
        check(h.runtime().find("main/n2") == bread, "a row that stays is the same node");
        (void)notes.patch(1, {{"n2", Value(Value::Map{{"title", Value("Rye")}, {"done", Value(false)}})}});
        h.step();
        check_eq(name_of(h, "main/n2"), std::string("Rye"));
    });

    s.test("a loop in a list is virtualised; a filtered list gets a model of its own", [] {
        Headless h(options({320, 200}));
        auto view = load(h, view_of(R"j({"type": "list", "id": "notes", "props": {"height": 120, "estimate": 24},
            "children": [{"for": "note", "in": "notes", "where": "contains(note.title, filter)",
                          "body": [{"type": "text", "props": {"text": {"expr": "note.title"}}}]}]})j",
                                    R"j("signals": {"filter": {"type": "string"}}, "models": {"notes": "note"}, )j"));
        view.mount(h.runtime(), "main");
        std::vector<Row> rows;
        for (int i = 0; i < 500; ++i) rows.push_back({std::format("n{}", i), Value(Value::Map{{"title", Value(std::format("note {}", i))}})});
        (void)h.runtime().model("notes").push(std::move(rows));
        h.settle();
        const auto list = h.tree().node("main");
        check(list.has_value() && list->role == Role::list);
        check_eq(h.runtime().virtual_items(h.runtime().find("main")).size(), std::size_t{500});
        check(h.runtime().children(h.runtime().find("main")).size() < 30, "only the rows in view are nodes");
        check_eq(name_of(h, "main/n0"), std::string("note 0"));
        view.signal("filter")->set(Value("note 49"));
        h.settle();
        check_eq(h.runtime().virtual_items(h.runtime().find("main")).size(), std::size_t{11});  // 49, 490 … 499
        check_eq(name_of(h, "main/n49"), std::string("note 49"));
    });

    s.test("a verb on an activation is the node's verb: S-1 and S-2 hold for it", [] {
        Headless h(options());
        std::vector<std::string> deleted;
        (void)h.runtime().verbs().add({"note.delete", "Delete a note", Effect::irreversible, "delete", {{"id", "string"}}},
                                      [&](const Value& a) -> VerbResult {
                                          deleted.emplace_back(a.get("id")->as_string());
                                          return Value{};
                                      });
        auto view = load(h, view_of(R"j({"type": "column", "children": [
            {"for": "note", "in": "notes", "body": [{"type": "row", "children": [
                {"type": "text", "props": {"text": {"expr": "note.title"}}},
                {"type": "button", "id": "delete", "props": {"text": "Delete"},
                 "on": {"activated": [{"verb": "note.delete", "args": {"id": "note.key"}}]}}]}]}]})j",
                                    R"j("models": {"notes": "note"}, )j"));
        view.mount(h.runtime(), "main");
        (void)h.runtime().model("notes").push({{"a", Value(Value::Map{{"title", Value("A")}})}, {"b", Value(Value::Map{{"title", Value("B")}})}});
        h.step();
        const auto button = h.tree().node("main/b/delete");
        check(button && button->action("activate") && button->action("activate")->effect == Effect::irreversible,
              "the verb classifies the activation");
        check_eq(act(h, "main/b/delete", "activate"), std::int64_t{2});  // needs-answer
        check(deleted.empty(), "nothing ran without the answer");
        check_eq(act(h, "main/b/delete", "activate", Value(Value::Map{{"id", Value("a")}}), "delete"), std::int64_t{0});
        check(deleted == std::vector<std::string>{"b"}, "the row's own argument, not the caller's");
        check(h.click("main/a/delete"));
        h.step();
        check(deleted.size() == 1, "a click on an irreversible verb asks first (the application confirms)");
    });

    s.test("verbs and events from other operations", [] {
        Headless h(options());
        std::vector<Value> saved;
        (void)h.runtime().verbs().add({"draft.save", "Save", Effect::reversible, "", {{"text", "string"}}}, [&](const Value& a) -> VerbResult {
            saved.push_back(a);
            return Value{};
        });
        auto view = load(h, view_of(R"j({"type": "text_box", "id": "t",
            "on": {"submitted": [{"verb": "draft.save", "args": {"text": "event.text", "n": 3}}, {"emit": "done", "args": {"text": "event.text"}}]}})j"));
        std::vector<Value> done;
        view.on("done", [&](const Value& a) { done.push_back(a); });
        view.mount(h.runtime(), "main");
        h.step();
        h.runtime().focus(h.runtime().find("main"));
        h.type("hi");
        h.key(Key::enter);
        h.step();
        check(saved.size() == 1 && saved[0].get("text")->as_string() == "hi" && saved[0].get("n")->as_int() == 3);
        check(done.size() == 1 && done[0].get("text")->as_string() == "hi");
    });

    s.test("provided signals are the host's, shared by every view; events forward to the host", [] {
        Headless h(options());
        auto shared = signal(Value(3));
        const vd::LoadOptions o{.provide = [&](std::string_view n) -> std::optional<Signal<Value>> {
            if (n == "count") return shared;
            return std::nullopt;
        }};
        const std::string text = view_of(R"j({"type": "button", "id": "b", "props": {"text": {"expr": "format('{}', count)"}},
            "on": {"activated": [{"set": "count", "expr": "count + 1"}, {"emit": "clicked", "args": {"n": "count"}}]}})j",
                                         R"j("signals": {"count": {"type": "int", "provided": true}}, )j");
        auto a = vd::View::load(h.runtime(), text, o);
        auto b = vd::View::load(h.runtime(), text, o);
        check(a && b);
        if (!a || !b) return;
        std::vector<std::pair<std::string, Value>> forwarded;
        a->on_any([&](std::string_view e, const Value& v) { forwarded.emplace_back(std::string(e), v); });
        a->mount(h.runtime(), "a");
        b->mount(h.runtime(), "b");
        h.step();
        check_eq(act(h, "a", "activate"), std::int64_t{0});
        check(shared.peek() == Value(4), "the host's signal changed");
        check_eq(name_of(h, "b"), std::string("4"));  // the other view follows
        check(forwarded.size() == 1 && forwarded[0].first == "clicked" && forwarded[0].second.get("n")->as_int() == 4);
        // A provided signal the host lacks is an error at load; a value for one is a mistake.
        auto none = vd::View::load(h.runtime(), text);
        check(!none && none.error().at(0).code == "provided");
        diagnoses(view_of(R"j({"type": "text"})j", R"j("signals": {"n": {"type": "int", "provided": true, "value": 1}}, )j"), "signals",
                  "provided by the host");
    });

    s.test("slots are filled by code, before or after mounting", [] {
        Headless h(options());
        auto view = load(h, view_of(R"j({"type": "column", "children": [{"slot": "toolbar"}, {"type": "text", "id": "body", "props": {"text": "Body"}}]})j"));
        view.slot("toolbar", button({.id = "save", .text = "Save"}));
        view.mount(h.runtime(), "main");
        h.step();
        check(h.tree().node("main/save").has_value());
        view.slot("toolbar", row({.id = "tools"}, {button({.id = "open", .text = "Open"})}));
        h.step();
        check(!h.tree().node("main/save") && h.tree().node("main/tools/open"));
        check(h.tree().node("main/body").has_value());
    });

    s.test("reload: state, nodes and focus stay; errors change nothing", [] {
        Headless h(options());
        auto view = load(h, counter);
        view.mount(h.runtime(), "main");
        h.step();
        check_eq(act(h, "main/inc", "activate"), std::int64_t{0});
        check_eq(act(h, "main/inc", "activate"), std::int64_t{0});
        const NodeId inc = h.runtime().find("main/inc");
        h.runtime().focus(inc);
        std::string next(counter);
        next.replace(next.find("加一"), std::string_view("加一").size(), "+1");
        next.replace(next.find("点击了 {} 次"), std::string_view("点击了 {} 次").size(), "{} clicks");
        check(view.reload(next).has_value());
        h.step();
        const auto t = tree_of(h);
        check(t.size() == 3 && t[1].name == "2 clicks", t.size() > 1 ? t[1].name : "");
        check(h.runtime().find("main/inc") == inc && h.runtime().focused() == inc, "the button is the same node, still focused");
        check_eq(name_of(h, "main/inc"), std::string("+1"));
        // A text with an error is refused whole.
        auto bad = view.reload(R"j({"omui": "0.1", "view": {"root": {"type": "nope"}}})j");
        check(!bad && bad.error().at(0).code == "unknown-type");
        check(view.signal("count")->peek() == Value(2));
        check_eq(name_of(h, "main/inc"), std::string("+1"));
        // A signal whose type changed starts again from its new value.
        std::string retyped = next;
        retyped.replace(retyped.find(R"j("type": "int", "value": 0)j"), std::string_view(R"j("type": "int", "value": 0)j").size(),
                        R"j("type": "double", "value": 0.5)j");
        check(view.reload(retyped).has_value());
        h.step();
        check(view.signal("count")->peek() == Value(0.5));
        check_eq(tree_of(h)[1].name, std::string("0.5 clicks"));
    });

    s.test("an expression that fails shows the property's default and logs once", [] {
        Headless h(options());
        auto view = load(h, view_of(R"j({"type": "column", "children": [
            {"type": "text", "id": "t", "props": {"label": "x", "text": {"expr": "format('{}', 10 / zero)"}}},
            {"type": "progress", "id": "p", "props": {"value": {"expr": "10 / zero / 10.0"}}}]})j",
                                    R"j("signals": {"zero": {"type": "int"}}, )j"));
        set_log_history(64);
        view.mount(h.runtime(), "main");
        h.step();
        h.step();
        const auto logs = recent_logs(64, Level::warning);
        const auto n = std::ranges::count_if(logs, [](const LoggedRecord& r) { return r.category == "viewdoc" && r.message.find("division by zero") != std::string::npos; });
        check_eq(n, std::ptrdiff_t{2});
        check(h.tree().node("main/p")->value.is_null(), "progress shows its default (indeterminate)");
        view.signal("zero")->set(Value(5));
        h.step();
        check(h.tree().node("main/p")->value == Value(0.2), "and recovers");
    });

    s.test("a component document: every language and document can use it", [] {
        constexpr std::string_view search = R"j({"omui": "0.1", "component": {
            "name": "search_box",
            "interface": {
              "props": {"placeholder": {"type": "string", "default": ""},
                        "query": {"type": "string", "default": "", "bindable": true}},
              "events": {"searched": {"args": {"text": "string"}}}
            },
            "signals": {"submits": {"type": "int"}},
            "root": {"type": "row", "children": [
              {"type": "text_box", "id": "input", "props": {"text": {"bind": "query"}, "placeholder": {"expr": "placeholder"}, "label": "Search"},
               "on": {"submitted": [{"set": "submits", "expr": "submits + 1"}, {"emit": "searched", "args": ["query"]}]}},
              {"type": "text", "id": "count", "props": {"text": {"expr": "format('{}', submits)"}}}]}}})j";
        auto type = vd::define_component(search);
        check(type.has_value());
        if (!type) return;
        check((*type)->contract && (*type)->contract->prop("query") && (*type)->contract->prop("query")->bindable);
        check((*type)->contract->event("searched") && (*type)->contract->event("searched")->args.size() == 1);

        Headless h(options());
        auto query = signal(std::string("first"));
        std::vector<std::string> searched;
        auto page = load(h, view_of(R"j({"type": "column", "children": [
            {"type": "search_box", "id": "find", "props": {"placeholder": "Find…", "query": {"bind": "q"}},
             "on": {"searched": [{"set": "last", "expr": "event.text"}]}},
            {"type": "text", "id": "last", "props": {"text": {"expr": "last"}}}]})j",
                                    R"j("signals": {"q": {"type": "string", "value": "a"}, "last": {"type": "string"}}, )j"));
        page.mount(h.runtime(), "main");
        // And from C++, by name, with a signal bound two-way.
        h.mount("side", vd::make("search_box", {{"query", Prop<std::string>(query).source()}},
                                 {{"searched", [&](const Value& a) { searched.emplace_back(a.get("text")->as_string()); }}}, "side"));
        h.step();
        check(h.tree().node("main/find/row:0/input") && h.tree().node("main/find/row:0/input")->value == Value("a"));
        check(h.tree().node("side/row:0/input")->value == Value("first"));
        h.runtime().focus(h.runtime().find("main/find/row:0/input"));
        h.type("bc");
        h.key(Key::enter);
        h.step();
        check(page.signal("q")->peek() == Value("abc"), "the inner field writes back through the interface");
        check_eq(name_of(h, "main/last"), std::string("abc"));
        check_eq(name_of(h, "main/find/row:0/count"), std::string("1"));
        h.runtime().focus(h.runtime().find("side/row:0/input"));
        h.key(Key::enter);
        h.step();
        check(searched == std::vector<std::string>{"first"});
        // Redefined: the instances keep their properties and state and show the new text.
        std::string next(search);
        next.replace(next.find("format('{}', submits)"), std::string_view("format('{}', submits)").size(), "format('{} searches', submits)");
        check(vd::define_component(next).has_value());
        h.step();
        check_eq(name_of(h, "main/find/row:0/count"), std::string("1 searches"));
        check(h.tree().node("main/find/row:0/input")->value == Value("abc"));
        // A name that is taken is refused.
        auto taken = vd::define_component(R"j({"omui": "0.1", "component": {"name": "button", "root": {"type": "text"}}})j");
        check(!taken && taken.error().at(0).message.find("exists already") != std::string::npos);
    });

    s.test("the schema covers every registered component; the format is canonical and idempotent", [] {
        const Value schema = vd::view_schema();
        const Value* defs = schema.get("$defs");
        check(defs && defs->get("component.button") && defs->get("component.text_box") && defs->get("component.column"));
        const Value* button = defs->get("component.button");
        const Value* props = button->get("properties")->get("props")->get("properties");
        check(props->get("variant") && props->get("text"), "the contract's properties");
        check(defs->get("runtime_property_name")->json().find("\"width\"") != std::string::npos, "and the runtime's, once");
        check(button->get("properties")->get("on")->get("propertyNames")->json().find("\"activated\"") != std::string::npos);
        check(schema.json() == vd::view_schema().json(), "deterministic");

        const std::string messy = R"j({"view":{"root":{"children":[{"props":{"text":"a"},"type":"text"},{"on":{"activated":[{"expr":"n+1","set":"n"}]},"id":"b","type":"button"}],"type":"row"},"signals":{"n":{"value":0,"type":"int"}}},"omui":"0.1"})j";
        auto once = vd::format_document(messy);
        check(once.has_value());
        if (!once) return;
        check_eq(*once, std::string(R"j({
  "omui": "0.1",
  "view": {
    "signals": {"n": {"type": "int", "value": 0}},
    "root": {
      "type": "row",
      "children": [
        {"type": "text", "props": {"text": "a"}},
        {"type": "button", "id": "b", "on": {"activated": [{"set": "n", "expr": "n+1"}]}}
      ]
    }
  }
}
)j"));
        auto twice = vd::format_document(*once);
        check(twice && *twice == *once, "idempotent");
        check(parse_json(*once)->json().size() == parse_json(messy)->json().size(), "every value kept");
        check(!vd::format_document("{").has_value());
    });

    s.test("random documents never crash the reader or the runtime", [] {
        // splitmix64: the same documents on every platform.
        std::uint64_t seed = 0x766965776463;
        auto next = [&] {
            std::uint64_t z = (seed += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        };
        auto below = [&](std::size_t n) { return static_cast<std::size_t>(next() % n); };
        static constexpr std::array<std::string_view, 14> atoms{
            "\"count\"", "\"count + 1\"", "{\"expr\": \"count > 1\"}", "{\"bind\": \"count\"}", "[]", "{}", "null", "3", "-1.5", "true",
            "\"text\"", "{\"type\": \"button\", \"id\": \"x\"}", "{\"if\": \"count > 0\", \"then\": []}", "\"((((\""};
        const Level level = log_level();
        set_log_level(Level::error);  // the documents are wrong on purpose
        const Value base = *parse_json(counter);
        Hash h;
        int loaded = 0;
        for (int round = 0; round < 400; ++round) {
            // Replace a random value of the document with a random atom.
            Value doc = base;
            std::vector<Value*> spots;
            std::function<void(Value&)> walk = [&](Value& v) {
                spots.push_back(&v);
                if (Value::List* l = v.list())
                    for (Value& x : *l) walk(x);
                if (Value::Map* m = v.map())
                    for (auto& [k, x] : *m) walk(x);
            };
            // Each replacement may drop what the others point into: collect again.
            const int changes = 1 + static_cast<int>(below(3));
            for (int k = 0; k < changes; ++k) {
                spots.clear();
                walk(doc);
                Value* spot = spots[below(spots.size())];
                *spot = *parse_json(atoms[below(atoms.size())]);
            }
            const std::string text = doc.json();
            const vd::ReadResult r = vd::read(text);
            for (const vd::Diagnostic& d : r.diagnostics) h.str(d.code);
            if (!r.ok()) continue;
            Headless host(options({160, 120}));
            auto view = vd::View::load(host.runtime(), text);
            if (!view) continue;
            ++loaded;
            view->mount(host.runtime(), "main");
            host.step();
            (void)host.click("main/inc");
            host.step();
            h.u64(tree_of(host).size());
        }
        set_log_level(level);
        check(loaded > 20, std::format("{} loaded", loaded));
        check_eq(h.hex(), std::string("02819cb2dcd45461"));
    });

    return s.run(argc, argv);
}
