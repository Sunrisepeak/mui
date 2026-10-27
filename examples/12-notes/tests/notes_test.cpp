// The notes application as its user and an agent use it: four languages in
// one program (the TypeScript views and logic, Lua's macros, a view document
// in the TypeScript view's slot, the C++ search), sharing only signals,
// models, verbs and events. Searching as you type, the toolbar's macros, a
// title Lua reads from the text, a task ticked in the preview kept in the
// note, a new note; an agent's verbs; pinned pixels.
import std;
import openmui;
import openmui.player;
import openmui.entry.player;
import openmui.lang.js;
import openmui.lang.lua;
import openmui.ui.markdown;
import openmui.native.fuzzy;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

void settle(Headless& h, player::Player& p) {
    for (int i = 0; i < 4000 && !p.idle(); ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    h.settle();
}

std::int64_t act(Headless& h, std::string_view node, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = std::move(args);
    return h.tree().act(node, action, call).exit_code().value_or(-1);
}

Envelope verb(Headless& h, std::string_view name, Value args) {
    Invocation call;
    call.actor = {"agent", "test", {}};
    call.args = std::move(args);
    return h.runtime().verbs().invoke(name, call);
}

std::string name_of(Headless& h, std::string_view node) {
    auto n = h.tree().node(node);
    return n ? n->name : "<none>";
}

std::string editor(Headless& h) {
    auto n = h.tree().node("main/page/panes/editor");
    return n ? std::string(n->value.as_string()) : "<none>";
}

// The notes the list shows, in its order.
std::vector<std::string> listed(Headless& h) {
    std::vector<std::string> out;
    for (const auto& n : h.tree().snapshot({.root = "main/side/list"}).nodes)
        if (n.role == Role::button) out.push_back(n.name);
    return out;
}

// The preview's task boxes, ticked or not.
std::string tasks(Headless& h) {
    std::string out;
    for (const auto& n : h.tree().snapshot({.root = "main/page/panes/scroll/preview"}).nodes)
        if (n.role == Role::checkbox) out += n.states.has(State::checked) ? 'x' : '-';
    return out;
}

std::string pixels(Headless& h) {
    h.runtime().focus(NodeId{});
    h.settle();
    Hash hash;
    const auto& px = h.pixels();
    for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
    return hash.hex();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"example.notes"};
    register_base_components();
    register_markdown_component();
    check(natives::fuzzy::define().has_value(), "the native module fuzzy");

    s.test("four languages, one tool: search, macros, titles, tasks, a new note, an agent's verbs, pixels", [] {
        auto m = read_manifest(here() / "mcpp.toml");
        check(m.has_value(), m ? "" : m.error());
        if (!m) return;
        check(m->scripts.size() == 2 && m->views.size() == 1, "the scripts and the view document, from the manifest");
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript(), lang::lua::language()});
        auto started = p.start();
        check(started.has_value(), started ? "" : started.error());
        settle(h, p);
        check(p.problems().empty(), p.problems().empty() ? "" : p.problems()[0]);

        // The view document is in the TypeScript view's slot, not beside it.
        const auto help = h.tree().find({.name = "How it works"});
        check(help.size() == 1 && help[0].id.starts_with("main/side/"), help.empty() ? "none" : help[0].id);
        check(!h.tree().node("help").has_value(), "the help panel is not a view of its own");
        check(listed(h) == std::vector<std::string>{"Groceries", "Reading", "Ideas"});
        check_eq(name_of(h, "main/side/found"), std::string("3 shown"));
        check(editor(h).starts_with("# Groceries"), editor(h));
        check_eq(tasks(h), std::string("-x-"));
        const std::string first = pixels(h);
        std::println("example.notes pixels {}", first);
        check(h.save_png(scratch_dir("example-notes") / "notes.png").has_value());

        // Searching as you type: C++ ranks on a worker; the list follows.
        check_eq(act(h, "main/side/search", "set_value", Value{}.set("value", "middle")), std::int64_t{0});
        settle(h, p);
        check(listed(h) == std::vector<std::string>{"Reading"}, std::format("{}", listed(h)));
        check_eq(name_of(h, "main/side/found"), std::string("1 shown"));
        check_eq(act(h, "main/side/search", "set_value", Value{}.set("value", "")), std::int64_t{0});
        settle(h, p);
        check_eq(listed(h).size(), std::size_t{3});

        // Opening a note; the toolbar's macros are Lua's.
        check_eq(act(h, "main/side/list/n3", "activate"), std::int64_t{0});
        settle(h, p);
        check(editor(h).starts_with("# Ideas"), editor(h));
        check_eq(act(h, "main/page/tools/task", "activate"), std::int64_t{0});
        settle(h, p);
        check(editor(h).ends_with("four languages.\n- [ ] "), editor(h));
        check_eq(tasks(h), std::string("-"));
        check_eq(act(h, "main/page/tools/today", "activate"), std::int64_t{0});
        settle(h, p);
        static const std::regex day(R"(\n\n## \d{4}-\d{2}-\d{2}\n$)");
        check(std::regex_search(editor(h), day), editor(h));

        // An edit: the title is Lua's to read from the text.
        check_eq(act(h, "main/page/panes/editor", "set_value", Value{}.set("value", "# Plans\n\n- [ ] Write the README")), std::int64_t{0});
        settle(h, p);
        check(listed(h) == std::vector<std::string>{"Groceries", "Reading", "Plans"}, std::format("{}", listed(h)));

        // A task ticked in the preview is kept in the note.
        check_eq(act(h, "main/side/list/n1", "activate"), std::int64_t{0});
        settle(h, p);
        check_eq(act(h, "main/page/panes/scroll/preview", "toggle_task", Value{}.set("index", 0)), std::int64_t{0});
        settle(h, p);
        check_eq(tasks(h), std::string("xx-"));
        check_eq(act(h, "main/side/list/n2", "activate"), std::int64_t{0});
        settle(h, p);
        check_eq(act(h, "main/side/list/n1", "activate"), std::int64_t{0});
        settle(h, p);
        check(editor(h).contains("- [x] Milk\n- [x] Bread"), editor(h));

        // A new note.
        check_eq(act(h, "main/side/new", "activate"), std::int64_t{0});
        settle(h, p);
        check(h.tree().node("main/side/list/n4").has_value(), "the new note is listed");
        check_eq(editor(h), std::string("# Untitled\n\n"));

        // An agent's verbs: the application's and Lua's.
        const Envelope found = verb(h, "notes.search", Value{}.set("query", "middlemarch"));
        check_eq(found.exit_code().value_or(-1), 0);
        settle(h, p);
        check(listed(h) == std::vector<std::string>{"Reading"}, std::format("{}", listed(h)));
        const Envelope title = verb(h, "macro.title", Value{}.set("text", "no heading\nhere"));
        check(title.exit_code() == 0 && title.ndjson().contains(R"("no heading")"), title.ndjson());
        check_eq(verb(h, "notes.open", Value{}.set("key", "n9")).exit_code().value_or(-1), 1);

        check_eq(first, std::string("10994fc62b920972"));
    });

    return s.run(argc, argv);
}
