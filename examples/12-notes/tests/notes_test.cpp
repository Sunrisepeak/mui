// The notes application as its user and an agent use it: three ways of
// writing in one program (the TypeScript views and logic, the macros' script, a view document
// in the TypeScript view's slot, the C++ search), sharing only signals,
// models, verbs and events. Searching as you type, the toolbar's macros, a
// title the macros read from the text, a task ticked in the preview kept in the
// note, a new note; an agent's verbs; pinned pixels.
import std;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;
import omui.ui.markdown;
import omui.native.fuzzy;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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

UTest cases{"example.notes", [] {
    register_base_components();
    register_markdown_component();
    expect(natives::fuzzy::define().has_value()) << "the native module fuzzy";

    "three ways of writing, one tool: search, macros, titles, tasks, a new note, an agent's verbs, pixels"_test = [] {
        auto m = read_manifest(here() / "mcpp.toml");
        expect(m.has_value()) << (m ? "" : m.error());
        if (!m) return;
        expect(m->scripts.size() == 2 && m->views.size() == 1) << "the scripts and the view document, from the manifest";
        Headless h(HeadlessOptions{.size = m->size});
        player::Player p(h.runtime(), *m, {lang::js::typescript()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        settle(h, p);
        expect(p.problems().empty()) << (p.problems().empty() ? "" : p.problems()[0]);

        // The view document is in the TypeScript view's slot, not beside it.
        const auto help = h.tree().find({.name = "How it works"});
        expect(help.size() == 1 && help[0].id.starts_with("main/side/")) << (help.empty() ? "none" : help[0].id);
        expect(!h.tree().node("help").has_value()) << "the help panel is not a view of its own";
        expect(listed(h) == std::vector<std::string>{"Groceries", "Reading", "Ideas"});
        expect(eq(name_of(h, "main/side/found"), std::string("3 shown")));
        expect(editor(h).starts_with("# Groceries")) << editor(h);
        expect(eq(tasks(h), std::string("-x-")));
        const std::string first = pixels(h);
        std::println("example.notes pixels {}", first);
        expect(h.save_png(scratch_dir("example-notes") / "notes.png").has_value());

        // Searching as you type: C++ ranks on a worker; the list follows.
        expect(eq(act(h, "main/side/search", "set_value", Value{}.set("value", "middle")), std::int64_t{0}));
        settle(h, p);
        expect(listed(h) == std::vector<std::string>{"Reading"}) << std::format("{}", listed(h));
        expect(eq(name_of(h, "main/side/found"), std::string("1 shown")));
        expect(eq(act(h, "main/side/search", "set_value", Value{}.set("value", "")), std::int64_t{0}));
        settle(h, p);
        expect(eq(listed(h).size(), std::size_t{3}));

        // Opening a note; the toolbar's macros are the macros' script's.
        expect(eq(act(h, "main/side/list/n3", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(editor(h).starts_with("# Ideas")) << editor(h);
        expect(eq(act(h, "main/page/tools/task", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(editor(h).ends_with("three ways of writing.\n- [ ] ")) << editor(h);
        expect(eq(tasks(h), std::string("-")));
        expect(eq(act(h, "main/page/tools/today", "activate"), std::int64_t{0}));
        settle(h, p);
        static const std::regex day(R"(\n\n## \d{4}-\d{2}-\d{2}\n$)");
        expect(std::regex_search(editor(h), day)) << editor(h);

        // An edit: the title is the macros' to read from the text.
        expect(eq(act(h, "main/page/panes/editor", "set_value", Value{}.set("value", "# Plans\n\n- [ ] Write the README")), std::int64_t{0}));
        settle(h, p);
        expect(listed(h) == std::vector<std::string>{"Groceries", "Reading", "Plans"}) << std::format("{}", listed(h));

        // A task ticked in the preview is kept in the note.
        expect(eq(act(h, "main/side/list/n1", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/page/panes/scroll/preview", "toggle_task", Value{}.set("index", 0)), std::int64_t{0}));
        settle(h, p);
        expect(eq(tasks(h), std::string("xx-")));
        expect(eq(act(h, "main/side/list/n2", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(eq(act(h, "main/side/list/n1", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(editor(h).contains("- [x] Milk\n- [x] Bread")) << editor(h);

        // A new note.
        expect(eq(act(h, "main/side/new", "activate"), std::int64_t{0}));
        settle(h, p);
        expect(h.tree().node("main/side/list/n4").has_value()) << "the new note is listed";
        expect(eq(editor(h), std::string("# Untitled\n\n")));

        // An agent's verbs: the application's and the macros'.
        const Envelope found = verb(h, "notes.search", Value{}.set("query", "middlemarch"));
        expect(eq(found.exit_code().value_or(-1), 0));
        settle(h, p);
        expect(listed(h) == std::vector<std::string>{"Reading"}) << std::format("{}", listed(h));
        const Envelope title = verb(h, "macro.title", Value{}.set("text", "no heading\nhere"));
        expect(title.exit_code() == 0 && title.ndjson().contains(R"("no heading")")) << title.ndjson();
        expect(eq(verb(h, "notes.open", Value{}.set("key", "n9")).exit_code().value_or(-1), 1));

        expect(eq(first, std::string("25f626398be76161")));  // the help says the macros are TypeScript (D-138)
    };
}};
