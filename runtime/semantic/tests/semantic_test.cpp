// The semantic tree: compact snapshots, paging, deltas, queries, actions by
// id, wait conditions, the spec's checks; recording a session and replaying
// it to the same tree.
import std;
import openmui.semantic;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace sem = omui::semantic;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
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

struct App {
    Signal<int>  count{0};
    Signal<bool> toast{false};
    std::vector<std::string> log;

    View view() {
        auto button = [this](std::string id, std::string label) {
            return box({.id = id, .width = 60, .height = 24, .label = label,
                        .on = {{"press", [this, id](const Value&) { log.push_back(id); }}}});
        };
        return column({.padding = 8, .gap = 4}, {
            text({.id = "title", .text = "Notes", .style = "title"}),
            text({.id = "count", .text = derive([c = count] { return std::format("{} notes", c.get()); })}),
            row({.gap = 4}, {
                button("add", "Add note").on("press", [c = count](const Value&) { c.set(c.peek() + 1); }),
                button("delete", "Delete all").prop("verb", Source{Value("notes.clear"), {}, {}}),
            }),
            when([t = toast] { return t.get(); },
                 [] {
                     return box({.id = "toast", .height = 20, .label = "Saved",
                                 .animate = {{"enter", Transition::tween(seconds(0.2))}}});
                 }),
        });
    }
};

InputEvent pointer(PointerKind kind, float x, float y) {
    PointerEvent e;
    e.kind = kind;
    e.position = {x, y};
    e.button = MouseButton::left;
    return e;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"semantic"};

    s.test("compact snapshots lift wrappers; ids stay paths", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame();
        sem::Tree tree(rt);
        auto snap = tree.snapshot();
        check(snap.nodes.front().id == "app" && snap.nodes.front().role == Role::application);
        std::vector<std::string> ids;
        for (const auto& n : snap.nodes) ids.push_back(n.id);
        check(std::ranges::find(ids, "main/row:0") == ids.end(), "a nameless row is lifted, its children keep their paths");
        check(std::ranges::find(ids, "main/row:0/add") != ids.end());
        auto main = tree.node("main");
        check(main.has_value());
        auto app_node = snap.nodes.front();
        check(app_node.children == std::vector<std::string>{"main/title", "main/count", "main/row:0/add", "main/row:0/delete"},
              std::format("{}", app_node.children));
        // The full tree keeps every node.
        sem::Tree full(rt, {.compact = false});
        check(full.snapshot().nodes.size() == rt.node_count());
        // Subtree, depth, interactive and paging.
        auto inter = tree.snapshot({.interactive = true});
        check(inter.nodes.size() == 2 && inter.nodes[0].id == "main/row:0/add");
        auto page = tree.snapshot({.offset = 1, .limit = 2});
        check(page.total == 5 && page.nodes.size() == 2 && page.nodes[0].id == "main/title");
        auto shallow = tree.snapshot({.depth = 0});
        check(shallow.nodes.size() == 1 && shallow.nodes[0].children.size() == 4);
        auto v = snap.to_value();
        check(v.get("nodes") && v.get("nodes")->list()->size() == 5);
    });

    s.test("queries: role, name glob, state, place, interactive", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame();
        sem::Tree tree(rt);
        auto q = sem::Query::parse(parse_json(R"({"name":"Delete*"})").value());
        check(q.has_value());
        auto found = tree.find(*q);
        check(found.size() == 1 && found[0].id == "main/row:0/delete");
        check(found[0].action("activate") && found[0].action("activate")->verb == "notes.clear");
        check(tree.find({.role = Role::text}).size() == 2);
        check(tree.find({.name = "? notes"}).size() == 1, "? is one character");
        check(tree.find({.states = {State::focusable}}).size() == 2);
        check(tree.find({.at = Point{20, 60}}).size() >= 1);
        check(tree.find({.within = Rect{0, 0, 320, 30}, .interactive = true}).empty());
        check(tree.find({.under = "main", .limit = 1}).size() == 1);
        check(sem::glob("删除*", "删除全部") && !sem::glob("删除?", "删除全部") && sem::glob("删除??", "删除全部"));
        auto bad = sem::Query::parse(parse_json(R"({"role":"buton"})").value());
        check(!bad && bad.error().find("buton") != std::string::npos);
        auto unknown = sem::Query::parse(parse_json(R"({"colour":"red"})").value());
        check(!unknown && unknown.error().find("colour") != std::string::npos);
        auto rt_q = sem::Query::parse(q->to_value());
        check(rt_q && rt_q->name == "Delete*");
    });

    s.test("changes answer only the difference", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame();
        sem::Tree tree(rt);
        auto first = tree.changes();
        check(first.added.size() == 5 && first.updated.empty() && first.removed.empty());
        check(tree.changes().empty(), "nothing happened");
        app.count.set(3);
        rt.frame();
        auto d = tree.changes();
        check(d.added.empty() && d.removed.empty() && d.updated.size() == 1 && d.updated[0].id == "main/count");
        check(d.updated[0].name == "3 notes");
        app.toast.set(true);
        rt.frame({seconds(0), {}});
        auto t = tree.changes();
        check(t.added.size() == 1 && t.added[0].id == "main/toast");
        app.toast.set(false);
        rt.frame({seconds(1), {}});
        check(tree.changes().removed == std::vector<std::string>{"main/toast"});
    });

    s.test("actions by id, and conditions to wait for", [] {
        Runtime rt(options());
        App app;
        rt.mount("main", app.view());
        rt.frame({seconds(0), {}});
        sem::Tree tree(rt);
        auto e = tree.act("main/row:0/add", "activate");
        check(e.exit_code() == std::optional<int>(0));
        check(app.count.peek() == 1);
        auto none = tree.act("main/nothing", "activate");
        check(none.exit_code() == std::optional<int>(1));
        check(none.records().front().body.get("code")->as_string() == "not-found");
        auto c = sem::Condition::parse(parse_json(R"({"q":{"id":"main/toast"},"settled":true})").value());
        check(c.has_value());
        check(!tree.check(*c).satisfied);
        app.toast.set(true);
        rt.frame({seconds(0), {}});
        auto w = tree.check(*c);
        check(!w.satisfied && w.why.find("animating") != std::string::npos, w.why);
        rt.frame({seconds(0.3), {}});
        rt.frame({seconds(0.3), {}});
        check(tree.check(*c).satisfied);
        auto gone = sem::Condition::parse(parse_json(R"({"q":{"id":"main/toast"},"absent":true})").value());
        check(!tree.check(*gone).satisfied);
        check(!sem::Condition::parse(parse_json(R"({"absent":true})").value()).has_value());
    });

    s.test("the spec's checks over the tree", [] {
        Runtime rt(options());
        rt.mount("main", column({}, {
                                        box({.id = "nameless", .width = 10, .height = 10, .on = {{"press", [](const Value&) {}}}}),
                                        box({.id = "ok", .width = 10, .height = 10, .label = "OK", .on = {{"press", [](const Value&) {}}}}),
                                    }));
        rt.frame();
        sem::Tree tree(rt);
        auto v = tree.violations();
        check(v.size() == 1 && v[0].clause == "S-6" && v[0].id == "main/nameless", std::format("{}", v.size()));
    });

    s.test("a recorded session replays to the same tree", [] {
        // Everything that happens is driven by recorded input and actions:
        // the second note shows the toast.
        auto mount = [](Runtime& rt, App& app) {
            rt.mount("main", app.view());
            rt.on(rt.find("main/row:0/add"), "press", [&app](const Value&) {
                if (app.count.peek() >= 2) app.toast.set(true);
            });
        };
        record::Session session;
        std::uint64_t end = 0;
        {
            Runtime rt(options());
            App app;
            mount(rt, app);
            sem::Tree tree(rt);
            sem::Recorder rec(tree, session);
            rec.frame({seconds(0), {}});
            const Point c = rt.bounds(rt.find("main/row:0/add")).center();
            rec.frame({seconds(0.1), {pointer(PointerKind::down, c.x, c.y), pointer(PointerKind::up, c.x, c.y)}});
            rec.checkpoint("after a click");
            Invocation agent;
            agent.actor = {"agent", "qa", {}};
            rec.act("main/row:0/add", "activate", agent);
            rec.act("main/row:0/delete", "activate", agent);  // no such verb: an error, recorded as such
            rec.frame({seconds(0.2), {}});
            rec.checkpoint("toast entering");
            rec.frame({seconds(0.5), {}});
            rec.checkpoint();
            end = tree.digest();
            check(app.count.peek() == 2 && app.toast.peek(), std::format("count {}", app.count.peek()));
        }
        check(session.of("frame").size() == 4 && session.of("act").size() == 2 && session.of("check").size() == 3);
        check(session.header.width == 320 && session.header.clock == "virtual");
        auto back = record::Session::parse(session.ndjson());
        check(back.has_value());
        {
            Runtime rt(options());
            App app;
            mount(rt, app);
            sem::Tree tree(rt);
            auto r = sem::replay(tree, *back);
            check(r.ok(), std::format("{}", r.mismatches));
            check(r.frames == 4 && r.actions == 2 && r.checkpoints == 3);
            check(r.digest == end);
        }
        {
            // Up to a time: seeking a bug to the moment before it.
            Runtime rt(options());
            App app;
            mount(rt, app);
            sem::Tree tree(rt);
            sem::Player player(tree, *back);
            auto& r = player.advance_to(0.05);
            check(r.ok() && r.frames == 1 && r.actions == 0 && r.checkpoints == 0);
            check(app.count.peek() == 0);
            // Then on: the click, its checkpoint and the agent's two actions at 0.1.
            player.advance_to(0.15);
            check(r.ok() && r.frames == 2 && r.actions == 2 && r.checkpoints == 1);
            check(app.count.peek() == 2 && !player.done());
            player.finish();
            check(player.done() && r.ok() && r.digest == end);
        }
        {
            // A different interface does not agree with the recording, and says where.
            Runtime rt(options());
            rt.mount("main", column({}, {text({.text = "something else"})}));
            sem::Tree tree(rt);
            auto r = sem::replay(tree, *back);
            check(!r.ok() && r.mismatches.front().find("after a click") != std::string::npos, std::format("{}", r.mismatches));
        }
    });

    return s.run(argc, argv);
}
