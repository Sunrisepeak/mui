// The agent endpoint: the protocol's operations and errors, waits served
// between frames, and the same conversation over a real local endpoint.
import std;
import omui.agent;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
using mlibs::log::LogRecord;
using mlibs::log::log_enabled;
using mlibs::log::set_log_level;
using mlibs::log::set_log_sink;
using mlibs::log::Level;
}

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;
namespace agent = omui::agent;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

struct Notes {
    Runtime      rt;
    Signal<bool> saved{false};
    int          deleted = 0;

    Notes() : rt([] {
          RuntimeOptions o;
          o.fonts = &fonts();
          o.size = {320, 200};
          return o;
      }()) {
        (void)rt.verbs().add({"notes.delete", "Delete every note", Effect::irreversible, "yes", {}}, [this](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        rt.mount("notes", column({.padding = 8, .gap = 4}, {
                                     box({.id = "save", .width = 60, .height = 20, .label = "Save",
                                          .on = {{"press", [this](const Value&) { saved.set(true); }}}}),
                                     box({.id = "delete", .width = 60, .height = 20, .label = "Delete all"})
                                         .prop("verb", Source{Value("notes.delete"), {}, {}}),
                                     when([s = saved] { return s.get(); },
                                          [] {
                                              return text({.id = "status", .text = "Saved",
                                                           .animate = {{"enter", Transition::tween(seconds(0.2))}}});
                                          }),
                                 }));
        rt.frame({seconds(0), {}});
    }
};

std::vector<Value> talk(agent::Protocol& p, std::string_view line) {
    std::vector<Value> out;
    p.request(line, [&](std::string_view l) { out.push_back(parse_json(l).value()); });
    return out;
}

int exit_of_env(const Envelope& e) { return e.exit_code().value_or(-100); }

std::string_view record_kind(const Value& v) { return v.get("kind") ? v.get("kind")->as_string() : ""; }
std::int64_t exit_of(const std::vector<Value>& r) {
    return r.empty() || record_kind(r.back()) != "result" ? -100 : r.back().get("exitCode")->as_int(-100);
}

// A counter that gives its inverse (S-12) and previews (S-13): increment is
// taken back by set_value with the value it replaced.
struct Count {
    static constexpr std::string_view name = "count";
    static constexpr Role             role = Role::spin_button;
    std::int64_t value = 0;
    Services*    services = nullptr;
    NodeId       node;

    void mount(Services& s, NodeId n) {
        services = &s;
        node = n;
    }
    void semantics(SemanticsContext&, spec::SemanticNode& n) const {
        n.name = "count";
        n.value = Value(value);
        n.actions.push_back({"increment", Effect::reversible, {}, {}, {}});
        n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"value", "i64"}}});
    }
    std::expected<std::int64_t, std::string> target(const ActContext& ctx, std::string_view a) const {
        if (a == "increment") return value + 1;
        if (value < 0) return std::unexpected("broken");
        const std::int64_t v = ctx.args.get("value") ? ctx.args.get("value")->as_int() : 0;
        if (v > 9) return std::unexpected("at most 9");
        return v;
    }
    ActResult act(ActContext& ctx, std::string_view a) {
        auto to = target(ctx, a);
        if (!to) return ActResult::fail(to.error());
        const std::int64_t old = value;
        value = *to;
        services->invalidate_semantics(node);
        return ActResult::done(Value(value)).undo_by("set_value", Value{}.set("value", old));
    }
    ActResult preview(ActContext& ctx, std::string_view a) const {
        auto to = target(ctx, a);
        if (!to) return ActResult::fail(to.error());
        return ActResult::done(Value(*to)).undo_by("set_value", Value{}.set("value", value));
    }
};

Element count(std::string id) {
    Element e;
    e.type = &component_type<Count>();
    e.id = std::move(id);
    return e;
}

}  // namespace

UTest cases{"agent", [] {

    "describe, snapshot, find"_test = [] {
        Notes app;
        agent::Protocol p(app.rt, {.app = "org.example.notes"});
        auto d = talk(p, R"({"op":"describe"})");
        expect(exit_of(d) == 0 && d[0].get("data")->get("interface")->as_string() == "openmui.tree@0.1");
        auto snap = talk(p, R"({"op":"snapshot","interactive":true})");
        expect(exit_of(snap) == 0);
        const Value* nodes = snap[0].get("data")->get("nodes");
        expect(nodes && nodes->list()->size() == 2) << snap[0].json();
        auto found = talk(p, R"({"op":"find","q":{"role":"button","name":"Delete*"}})");
        expect(exit_of(found) == 0);
        const Value& n = found[0].get("data")->get("nodes")->list()->front();
        expect(n.get("id")->as_string() == "notes/delete");
        const Value& a = n.get("actions")->list()->front();
        expect(a.get("effect")->as_string() == "irreversible" && a.get("answer")->as_string() == "yes");
        auto missing = talk(p, R"({"op":"snapshot","root":"notes/nothing"})");
        expect(exit_of(missing) == 1);
    };

    "act: needs-answer, then with the answer (S-1, S-2)"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        auto first = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"})");
        expect(exit_of(first) == 2) << (first.empty() ? "" : first.back().json());
        expect(record_kind(first[0]) == "error" && first[0].get("code")->as_string() == "needs-answer");
        expect(app.deleted == 0);
        auto second = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","answer":"yes","actor":"agent:planner;via=person:alice"})");
        expect(exit_of(second) == 0 && app.deleted == 1);
        auto j = app.rt.action_journal();
        expect(j.back().actor == "agent:planner;via=person:alice");
        auto anonymous = talk(p, R"({"op":"act","node":"notes/save","action":"activate"})");
        expect(exit_of(anonymous) == 0 && app.rt.action_journal().back().actor == "agent:anonymous");
        auto bad_actor = talk(p, R"({"op":"act","node":"notes/save","action":"activate","actor":"nobody"})");
        expect(exit_of(bad_actor) == 1);
    };

    "invoke: an application verb, its answer asked for as an action's (S-1)"_test = [] {
        Notes app;
        int cleared = 0;
        expect(app.rt.verbs().add({"notes.clear", "Removes every note.", Effect::irreversible, "yes", {{"keep", "i64"}}},
                                 [&](const Value& args) -> VerbResult {
                                     ++cleared;
                                     return Value(args.get("keep") ? args.get("keep")->as_int() : 0);
                                 }).has_value());
        agent::Protocol p(app.rt);
        auto asked = talk(p, R"({"op":"invoke","verb":"notes.clear","args":{"keep":2}})");
        expect(exit_of(asked) == 2 && cleared == 0) << "needs its answer";
        auto done = talk(p, R"({"op":"invoke","verb":"notes.clear","args":{"keep":2},"answer":"yes","actor":"agent:tidy"})");
        expect(exit_of(done) == 0 && cleared == 1 && done[0].get("data")->as_int() == 2) << (done.empty() ? "" : done.front().json());
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"notes.nothing"})")) == 1) << "an unknown verb";
        expect(exit_of(talk(p, R"({"op":"invoke"})")) == 1) << "no verb";
    };

    "the person's own (S-10): marked in the tree, refused over the bus whatever the actor, done by the person's input"_test = [] {
        Notes app;
        int allowed = 0;
        VerbDesc allow{"cockpit.allow", "Allow what the agent asked", Effect::reversible, "", {}};
        allow.person = true;
        expect(app.rt.verbs().add(allow, [&](const Value&) -> VerbResult {
                      ++allowed;
                      return Value{};
                  }).has_value());
        int pressed = 0;
        app.rt.mount("cockpit", row({.gap = 4}, {
                                        box({.id = "allow", .width = 60, .height = 20, .label = "Allow"}).prop("verb", Source{Value("cockpit.allow"), {}, {}}),
                                        box({.id = "deny", .width = 60, .height = 20, .label = "Deny", .on = {{"press", [&](const Value&) { ++pressed; }}}})
                                            .prop("person", Source{Value(true), {}, {}}),
                                    }));
        app.rt.frame({seconds(0.1), {}});
        agent::Protocol p(app.rt);
        auto tree = talk(p, R"({"op":"find","q":{"name":"Allow"}})");
        expect(!tree.empty() && tree.front().json().contains(R"("verb":"cockpit.allow","person":true)")) << (tree.empty() ? "" : tree.front().json());
        // Over the bus, as an agent or claiming to be the person: refused, nothing done.
        for (std::string_view line : {R"({"op":"act","node":"cockpit/allow","action":"activate","actor":"agent:sly"})",
                                      R"({"op":"act","node":"cockpit/allow","action":"activate","actor":"person:user;via=service:input"})",
                                      R"({"op":"invoke","verb":"cockpit.allow","actor":"person:user"})",
                                      R"({"op":"act","node":"cockpit/deny","action":"activate"})"}) {
            auto r = talk(p, line);
            expect(exit_of(r) == 1 && !r.empty() && r.front().json().contains("refused") && r.front().json().contains("S-10")) << std::string(line);
        }
        expect(eq(allowed, 0));
        expect(eq(pressed, 0));
        // The person's own input does it.
        const auto at = app.rt.bounds(app.rt.find("cockpit/allow"));
        const Point c{at.x + at.width / 2, at.y + at.height / 2};
        app.rt.frame({seconds(0.2), {PointerEvent{.kind = PointerKind::down, .position = c, .button = MouseButton::left, .buttons = 1},
                                     PointerEvent{.kind = PointerKind::up, .position = c, .button = MouseButton::left}}});
        expect(eq(allowed, 1));
    };

    "input: keys pressed together — a chord, down in one frame, held, up in one"_test = [] {
        Notes app;
        agent::Protocol p(app.rt, {.input = true});
        expect(exit_of(talk(p, R"({"op":"input","events":[{"type":"keys","keys":[]}]})")) == 1) << "no keys";
        expect(exit_of(talk(p, R"({"op":"input","events":[{"type":"keys","keys":["U","Nope"]}]})")) == 1) << "not a key";
        p.request(R"({"op":"input","events":[{"type":"keys","keys":["U","J"],"hold":3}]})", [](std::string_view) {});
        std::vector<std::vector<InputEvent>> frames;
        while (auto batch = p.take_input()) frames.push_back(std::move(batch->events));
        expect(eq(frames.size(), std::size_t{4}));
        const auto keys = [&](std::size_t f, KeyKind kind) {
            return frames[f].size() == 2 && std::ranges::all_of(frames[f], [&](const InputEvent& e) {
                       const auto* k = std::get_if<KeyEvent>(&e);
                       return k && k->kind == kind;
                   });
        };
        expect(keys(0, KeyKind::down) && frames[1].empty() && frames[2].empty() && keys(3, KeyKind::up)) << "down together, held 3 frames, up together";
    };

    "input: refused where it is off; a click as a device gives it, what it hit and did"_test = [] {
        Notes app;
        agent::Protocol closed(app.rt);
        auto refused = talk(closed, R"({"op":"input","events":[{"type":"click","node":"notes/save"}]})");
        expect(exit_of(refused) == 1 && refused.front().json().contains("--no-agent-input")) << (refused.empty() ? "" : refused.front().json());
        expect(!closed.take_input()) << "nothing queued";
        agent::Protocol p(app.rt, {.input = true});
        expect(exit_of(talk(p, R"({"op":"input","events":[{"type":"tap","x":1,"y":1}]})")) == 1) << "an unknown event";
        expect(exit_of(talk(p, R"({"op":"input","events":[{"type":"click","node":"notes/nothing"}]})")) == 1) << "no such node";
        std::vector<Value> out;
        p.request(R"({"op":"input","re":7,"events":[{"type":"click","node":"notes/save"},{"type":"wait","frames":2}]})",
                  [&](std::string_view l) { out.push_back(parse_json(l).value()); });
        expect(out.empty() && p.pending() == 1) << "answered after its frames";
        double t = 0.1;
        int frames = 0;
        while (auto batch = p.take_input()) {
            app.rt.set_input_actor(batch->actor);
            app.rt.frame({seconds(t += 1.0 / 60), std::move(batch->events)});
            app.rt.set_input_actor(std::nullopt);
            ++frames;
            p.poll();
        }
        p.poll();
        expect(eq(frames, 4));
        expect(app.saved.peek()) << "the click reached the button";
        expect(exit_of(out) == 0 && out.front().get("re")->as_int() == 7) << (out.empty() ? "" : out.front().json());
        const Value* d = out.front().get("data");
        expect(d->get("frames")->as_int() == 4) << (d->json());
        const Value& hit = d->get("pressed")->list()->front();
        expect(hit.get("semantic")->as_string() == "notes/save" && hit.get("role")->as_string() == "button") << hit.json();
        const Value& act = d->get("actions")->list()->front();
        expect(act.get("node")->as_string() == "notes/save" && act.get("action")->as_string() == "activate") << (d->json());
        expect(act.get("actor")->as_string() == "agent:anonymous;via=service:input") << act.json();
        // What the input does is the agent's, decided as its requests are: an
        // irreversible action asks for its answer, the person's own is refused.
        auto deliver = [&](std::string_view line) {
            out.clear();
            p.request(line, [&](std::string_view l) { out.push_back(parse_json(l).value()); });
            while (auto batch = p.take_input()) {
                app.rt.set_input_actor(batch->actor);
                app.rt.frame({seconds(t += 1.0 / 60), std::move(batch->events)});
                app.rt.set_input_actor(std::nullopt);
                p.poll();
            }
            p.poll();
            expect(exit_of(out) == 0 && out.front().get("data")->get("actions")->list()->size() == 1) << (out.empty() ? "" : out.front().json());
            return out.empty() ? Value{} : out.front().get("data")->get("actions")->list()->front();
        };
        const Value asked = deliver(R"({"op":"input","actor":"agent:sly","events":[{"type":"click","node":"notes/delete"}]})");
        expect(asked.get("exitCode")->as_int() == 2 && app.deleted == 0) << asked.json();
        app.rt.mount("cockpit", row({}, {box({.id = "allow", .width = 60, .height = 20, .label = "Allow",
                                               .on = {{"press", [&](const Value&) { app.saved.set(false); }}}})
                                              .prop("person", Source{Value(true), {}, {}})}));
        app.rt.frame({seconds(t += 1.0 / 60), {}});
        const Value refused_own = deliver(R"({"op":"input","actor":"agent:sly","events":[{"type":"click","node":"cockpit/allow"}]})");
        expect(refused_own.get("exitCode")->as_int() == 1 && refused_own.get("actor")->as_string() == "agent:sly;via=service:input" && app.saved.peek()) << refused_own.json();
        expect(p.pending() == 0);
    };

    "grants under the cap (S-11, openas §5): beyond the cap refused, within a grant no answer asked"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        AgentAccess& access = agent_access();
        access.give(openas::parse_grant("agent:planner;via=person:alice=irreversible@notes.*").value());
        // The agent the person granted, and what it started, delete without the answer.
        auto granted = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"})");
        expect(exit_of(granted) == 0 && app.deleted == 1) << (granted.empty() ? "" : granted.front().json());
        auto helper = talk(p, R"({"op":"invoke","verb":"notes.delete","actor":"agent:reader;via=person:alice,agent:planner"})");
        expect(exit_of(helper) == 0 && app.deleted == 2) << (helper.empty() ? "" : helper.front().json());
        // Another agent, or the same name without the person behind it, is asked as before.
        expect(exit_of(talk(p, R"({"op":"act","node":"notes/delete","action":"activate","actor":"agent:tidy"})")) == 2);
        expect(exit_of(talk(p, R"({"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner"})")) == 2);
        expect(eq(app.deleted, 2));
        // A cap of read: an edit is refused even when granted; reading goes on.
        access.set_cap({openas::Effect::read});
        auto beyond = talk(p, R"({"op":"act","node":"notes/save","action":"activate","actor":"agent:planner;via=person:alice"})");
        expect(exit_of(beyond) == 1 && !beyond.empty() && beyond.front().get("code")->as_string() == "refused") << (beyond.empty() ? "" : beyond.front().json());
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"notes.delete","actor":"agent:planner;via=person:alice","answer":"yes"})")) == 1);
        expect(!app.saved.peek() && app.deleted == 2);
        expect(exit_of(talk(p, R"({"op":"snapshot"})")) == 0);
        // The program's own requests are not the bus: the cap does not apply to them.
        Invocation own;
        own.actor = {"program", "test", {}};
        expect(exit_of_env(app.rt.act(app.rt.find("notes/save"), "activate", own)) == 0);
        access.set_cap({});
        access.revoke_all();
    };

    "undo, redo, history, transactions and dry runs over the bus (S-12, S-13)"_test = [] {
        Notes app;
        app.rt.mount("counts", row({.gap = 4}, {count("a"), count("b")}));
        app.rt.frame({seconds(0.1), {}});
        agent::Protocol p(app.rt);
        auto value = [&](std::string_view id) { return app.rt.semantic_node(app.rt.find(id)).value.as_int(); };
        auto d = talk(p, R"({"op":"describe"})");
        expect(d[0].get("data")->json().contains(R"("undo","redo","transaction","preview","history")")) << d[0].json();
        // A dry run: the outcome and the value it would have; nothing else.
        const auto journal = app.rt.action_journal().size();
        auto dry = talk(p, R"({"op":"act","node":"counts/a","action":"increment","dry_run":true})");
        expect(exit_of(dry) == 0) << (dry.empty() ? "" : dry.front().json());
        const Value* dd = dry[0].get("data");
        expect(dd->get("outcome")->as_string() == "run" && dd->get("value")->get("after")->as_int() == 1 && dd->get("undoable")->as_bool()) << dry[0].json();
        auto ask = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","dry_run":true})");
        expect(exit_of(ask) == 0 && ask[0].get("data")->get("outcome")->as_string() == "needs-answer") << ask[0].json();
        auto verb = talk(p, R"({"op":"invoke","verb":"notes.delete","answer":"yes","dry_run":true})");
        expect(exit_of(verb) == 0 && verb[0].get("data")->get("outcome")->as_string() == "run" && app.deleted == 0) << verb[0].json();
        expect(value("counts/a") == 0 && app.rt.action_journal().size() == journal) << "no trace";
        // Done, then undone and redone; the history says what.
        expect(exit_of(talk(p, R"({"op":"act","node":"counts/a","action":"increment","actor":"agent:planner"})")) == 0);
        expect(exit_of(talk(p, R"({"op":"act","node":"counts/a","action":"increment","actor":"agent:planner"})")) == 0);
        auto h = talk(p, R"({"op":"history"})");
        expect(h[0].get("data")->get("undo")->list()->size() == 2 && h[0].get("data")->get("scope")->as_string() == "app") << h[0].json();
        auto u = talk(p, R"({"op":"undo","actor":"agent:planner"})");
        expect(exit_of(u) == 0 && value("counts/a") == 1 && u[0].get("data")->get("undone")->as_string() == "increment on 'counts/a'") << u[0].json();
        expect(exit_of(talk(p, R"({"op":"redo"})")) == 0 && value("counts/a") == 2);
        expect(app.rt.action_journal().back().action == "redo" && app.rt.action_journal().back().actor == "agent:anonymous");
        // A transaction that fails midway is taken back.
        auto t = talk(p, R"({"op":"transaction","label":"both","steps":[{"node":"counts/a","action":"increment"},{"node":"counts/b","action":"set_value","args":{"value":5}},{"node":"counts/b","action":"set_value","args":{"value":12}}]})");
        expect(exit_of(t) == 1 && t[0].get("code")->as_string() == "refused" && t[0].get("rolled_back")->as_int() == 2 && t[0].get("step")->as_int() == 2) << (t.empty() ? "" : t[0].json());
        expect(value("counts/a") == 2 && value("counts/b") == 0) << "rolled back";
        // An irreversible step is refused before it asks: no answer would let it be taken back.
        auto needs = talk(p, R"({"op":"transaction","steps":[{"node":"counts/a","action":"increment"},{"node":"notes/delete","action":"activate"}]})");
        expect(exit_of(needs) == 1 && needs[0].json().contains("S-13") && value("counts/a") == 2) << (needs.empty() ? "" : needs[0].json());
        // Planned first, then done: one step of history.
        constexpr std::string_view good = R"({"op":"transaction","label":"both","steps":[{"node":"counts/a","action":"increment"},{"node":"counts/b","action":"set_value","args":{"value":5}}]})";
        std::string plan(good);
        plan.insert(plan.size() - 1, R"(,"dry_run":true)");
        auto pl = talk(p, plan);
        expect(exit_of(pl) == 0 && pl[0].get("data")->get("outcome")->as_string() == "run" && pl[0].get("data")->get("steps")->list()->size() == 2 &&
                  value("counts/b") == 0) << pl[0].json();
        auto ok = talk(p, good);
        expect(exit_of(ok) == 0 && value("counts/a") == 3 && value("counts/b") == 5) << (ok.empty() ? "" : ok[0].json());
        expect(exit_of(talk(p, R"({"op":"undo"})")) == 0 && value("counts/a") == 2 && value("counts/b") == 0) << "undone as one";
        // An irreversible verb ends what can be undone.
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"notes.delete","answer":"yes"})")) == 0 && app.deleted == 1);
        auto refused = talk(p, R"({"op":"undo"})");
        expect(exit_of(refused) == 1 && refused[0].json().contains("cannot be taken back") && value("counts/a") == 2) << refused[0].json();
        auto plan_undo = talk(p, R"({"op":"undo","dry_run":true})");
        expect(exit_of(plan_undo) == 0 && plan_undo[0].get("data")->get("outcome")->as_string() == "refused") << plan_undo[0].json();
        // A plan with a step that cannot be tried says the whole would fail, not run.
        auto doomed = talk(p, R"({"op":"transaction","dry_run":true,"steps":[{"node":"counts/a","action":"increment"},{"node":"notes/nothing","action":"activate"}]})");
        expect(exit_of(doomed) == 0 && doomed[0].get("data")->get("outcome")->as_string() == "fail") << doomed[0].json();
        // A step with no known way back (a button whose handler keeps no history), then a failing one: the whole is
        // refused before anything is done — never done and then said to be "taken back".
        const bool saved = app.saved.peek();
        auto honest = talk(p, R"({"op":"transaction","steps":[{"node":"notes/save","action":"activate"},{"node":"notes/nothing","action":"activate"}]})");
        expect(exit_of(honest) == 1 && honest[0].json().contains("no known way back") && app.saved.peek() == saved) << honest[0].json();
        expect(exit_of(talk(p, R"({"op":"transaction","steps":[]})")) == 1) << "a transaction needs steps";
        expect(exit_of(talk(p, R"({"op":"history","scope":"nowhere"})")) == 1);
    };

    "wait: answered when the condition holds, between frames"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        std::vector<Value> got;
        p.request(R"({"op":"wait","q":{"id":"notes/status"},"settled":true,"re":7})",
                  [&](std::string_view l) { got.push_back(parse_json(l).value()); });
        expect(got.empty() && p.pending() == 1) << "deferred";
        (void)talk(p, R"({"op":"act","node":"notes/save","action":"activate"})");
        app.rt.frame({seconds(0.0), {}});
        p.poll();
        expect(got.empty()) << "the status is still entering";
        app.rt.frame({seconds(0.3), {}});
        p.poll();
        expect(exit_of(got) == 0 && p.pending() == 0);
        expect(got.front().get("re")->as_int() == 7) << "records carry the request's re";
        auto now = talk(p, R"({"op":"wait","q":{"id":"notes/status"}})");
        expect(exit_of(now) == 0) << "already true: answered at once";
        std::vector<Value> late;
        p.request(R"({"op":"wait","q":{"id":"notes/never"},"timeout":0})", [&](std::string_view l) { late.push_back(parse_json(l).value()); });
        p.poll();
        expect(exit_of(late) == 3 && late[0].get("code")->as_string() == "timeout");
    };

    "changes per client, and errors that teach"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        auto first = talk(p, R"({"op":"changes"})");
        expect(exit_of(first) == 0 && first[0].get("data")->get("added")->list()->size() >= 3);
        auto none = talk(p, R"({"op":"changes"})");
        const Value* added = none[0].get("data")->get("added");
        expect(!added || added->list()->empty()) << "nothing new for this client";
        expect(exit_of(talk(p, "not json")) == 1);
        auto unknown = talk(p, R"({"op":"click"})");
        expect(exit_of(unknown) == 1 && unknown[0].get("hint")->as_string().find("snapshot") != std::string_view::npos);
        expect(exit_of(talk(p, R"({"op":"find"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"find","q":{"role":"knob"}})")) == 1);
        expect(exit_of(talk(p, R"({"op":"capture"})")) == 1) << "no renderer: refused";
        agent::Protocol with_capture(app.rt, {.capture = [](Rect) -> std::expected<std::vector<std::uint8_t>, std::string> {
                                                   return std::vector<std::uint8_t>{'P', 'N', 'G'};
                                               }});
        auto cap = talk(with_capture, R"({"op":"capture","node":"notes/save"})");
        expect(exit_of(cap) == 0 && cap[0].get("data")->get("base64")->as_string() == "UE5H");
        // A part of the window, to look closer: clipped to what is captured; outside, refused.
        Rect asked{};
        agent::Protocol closer(app.rt, {.capture = [&](Rect r) -> std::expected<std::vector<std::uint8_t>, std::string> {
                                            asked = r;
                                            return std::vector<std::uint8_t>{'P', 'N', 'G'};
                                        }});
        expect(exit_of(talk(closer, R"({"op":"capture","region":[10,20,30,40]})")) == 0 && asked == Rect{10, 20, 30, 40}) << "a region";
        const Size win = app.rt.size();
        expect(exit_of(talk(closer, std::format(R"({{"op":"capture","region":[{},{},100,100]}})", win.width - 50, win.height - 50))) == 0 &&
                  asked == Rect{win.width - 50, win.height - 50, 50, 50}) << "clipped to the window";
        expect(exit_of(talk(closer, R"({"op":"capture","region":[-500,-500,10,10]})")) == 1) << "outside: refused";
        expect(exit_of(talk(closer, R"({"op":"capture","region":[1,2]})")) == 1) << "four numbers";
    };

    "development: the application's clock, paused, stepped and sought (F4)"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        app.rt.frame({seconds(1), {}});
        auto shown = talk(p, R"({"op":"clock"})");
        expect(exit_of(shown) == 0 && !shown[0].get("data")->get("controlled")->as_bool() && shown[0].get("data")->get("now")->as_double() == 1.0);
        expect(exit_of(talk(p, R"({"op":"clock","action":"pause"})")) == 0);
        app.rt.frame({seconds(2), {}});
        auto paused = talk(p, R"({"op":"clock"})");
        expect(paused[0].get("data")->get("now")->as_double() == 1.0 && paused[0].get("data")->get("host")->as_double() == 2.0) << paused[0].json();
        auto step = talk(p, R"({"op":"clock","action":"step","seconds":0.5})");
        expect(exit_of(step) == 0 && step[0].get("data")->get("pending")->as_bool());
        app.rt.frame({seconds(2), {}});
        expect(app.rt.now() == seconds(1.5));
        expect(exit_of(talk(p, R"({"op":"clock","action":"seek","seconds":0.25})")) == 0);
        app.rt.frame({seconds(2), {}});
        expect(app.rt.now() == seconds(0.25));
        expect(exit_of(talk(p, R"({"op":"clock","action":"rate","rate":-1})")) == 1);
        expect(exit_of(talk(p, R"({"op":"clock","action":"warp"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"clock","action":"release"})")) == 0 && !app.rt.time_state().controlled);
    };

    "development: the frame pipeline, read and changed while it runs (D-66)"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        app.rt.frame({seconds(0), {}});
        auto show = talk(p, R"({"op":"pipeline"})");
        expect(exit_of(show) == 0);
        const Value* d = show[0].get("data");
        expect(d && d->get("name")->as_string() == "runtime.h2" && d->get("stages")->list()->size() == 9);  // … paint, motion, debug
        expect((*d->get("stages")->list())[4].get("plugin")->as_string() == "openmui.view.layout");
        expect(d->get("toml")->as_string().contains("[[stage]]") && d->get("graph")->as_string().contains("writes display"));
        expect(std::ranges::any_of(*d->get("plugins")->list(), [](const Value& x) { return x.get("name")->as_string() == "openmui.view.debug"; }));
        auto rm = talk(p, R"({"op":"pipeline","action":"remove","stage":"debug"})");
        expect(exit_of(rm) == 0 && rm[0].get("data")->get("stages")->list()->size() == 8);
        auto back = talk(p, R"({"op":"pipeline","action":"insert","stage":"debug","plugin":"openmui.view.debug","after":["paint"]})");
        expect(exit_of(back) == 0 && back[0].get("data")->get("stages")->list()->size() == 9);
        auto bad = talk(p, R"({"op":"pipeline","action":"replace","stage":"paint","plugin":"nonesuch"})");
        expect(exit_of(bad) == 1 && bad[0].json().contains("no plugin `nonesuch`")) << bad[0].json();
        expect(exit_of(talk(p, R"({"op":"pipeline","action":"explode"})")) == 1);
        app.rt.frame({seconds(0.1), {}});
        expect(app.rt.frame_pipeline().order().size() == 9) << "still whole";
    };

    "development: inspect, stats, profile, logs, debug"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        for (int i = 0; i < 5; ++i) app.rt.frame({seconds(i * 0.016), {}});
        auto in = talk(p, R"({"op":"inspect","node":"notes/save"})");
        expect(exit_of(in) == 0 && in[0].get("data")->get("type")->as_string() == "box");
        expect(in[0].get("data")->get("handlers")->list()->size() == 1);
        auto st = talk(p, R"({"op":"stats"})");
        expect(exit_of(st) == 0 && st[0].get("data")->get("frames")->as_int() >= 5 && st[0].get("data")->get("p95_ms"));
        auto hist = talk(p, R"({"op":"stats","history":3})");
        expect(hist[0].get("data")->get("history") && hist[0].get("data")->get("history")->list()->size() == 3 &&
              (*hist[0].get("data")->get("history")->list())[2].get("total"));
        expect(exit_of(talk(p, R"({"op":"profile","action":"start"})")) == 0);
        for (int i = 0; i < 3; ++i) {
            app.saved.set(i % 2 == 0);
            app.rt.frame({seconds(1 + i * 0.016), {}});
        }
        auto pr = talk(p, R"({"op":"profile","action":"stop","chrome":true})");
        expect(exit_of(pr) == 0);
        const Value* summary = pr[0].get("data")->get("summary");
        expect(summary && !summary->list()->empty());
        bool frames = false;
        for (const Value& x : *summary->list()) frames = frames || (x.get("name")->as_string() == "frame" && x.get("count")->as_int() == 3);
        expect(frames) << pr[0].json().substr(0, 300);
        expect(pr[0].get("data")->get("chrome")->as_string().starts_with("{\"displayTimeUnit\""));
        expect(exit_of(talk(p, R"({"op":"profile","action":"pause"})")) == 1);
        set_log_sink([](void*, const LogRecord&) {}, nullptr);
        constexpr Log notes_log{"notes"};
        notes_log.warn("disk {} full", 97);
        auto lg = talk(p, R"({"op":"logs","level":"warning","category":"notes"})");
        expect(exit_of(lg) == 0 && lg[0].get("data")->get("records")->list()->back().get("message")->as_string() == "disk 97 full");
        set_log_sink(nullptr, nullptr);
        auto dbg = talk(p, R"({"op":"debug","bounds":true,"hud":true,"log":"layout=debug,*=info"})");
        expect(exit_of(dbg) == 0 && app.rt.debug().bounds && app.rt.debug().hud && log_enabled(Level::debug, "layout"));
        expect(exit_of(talk(p, R"({"op":"debug","log":"layout=loud"})")) == 1);
        set_log_level(Level::info);
        app.rt.set_debug({});
    };

    "over a local endpoint: list, connect, call, wait across frames"_test = [] {
        Notes app;
        auto dir = scratch_dir("agent");
        agent::Endpoint ep(app.rt, {.name = "notes-test", .directory = dir, .protocol = {.app = "org.example.notes"}});
        auto started = ep.start();
        if (!started && started.error().find("not supported") != std::string::npos) {
            // Wine lacks AF_UNIX on its drives; Windows 10 and later, Linux and macOS have local endpoints.
            std::println("    skipped: local endpoints are not supported here ({})", started.error());
            return;
        }
        expect(started.has_value()) << (started ? "" : started.error());
        if (!started) return;
        auto listed = agent::list(dir);
        expect(listed.size() == 1 && listed[0].name == "notes-test" && listed[0].app == "org.example.notes");

        std::atomic<bool> done{false};
        std::vector<Value> found, acted, waited;
        std::vector<std::vector<Value>> more;  // the Inspector's polling, on the same connection
        std::string connect_error;
        std::jthread client([&] {
            auto c = agent::Client::connect("notes-test", dir);
            if (!c) {
                connect_error = c.error();
                done = true;
                return;
            }
            found = c->call(parse_json(R"({"op":"find","q":{"name":"Save"}})").value());
            acted = c->call(parse_json(R"({"op":"act","node":"notes/save","action":"activate","actor":"agent:test"})").value());
            waited = c->call(parse_json(R"({"op":"wait","q":{"id":"notes/status"},"settled":true})").value());
            for (const char* r : {R"({"op":"stats","history":120})", R"({"op":"clock"})", R"({"op":"pipeline"})", R"({"op":"snapshot","depth":0})",
                                  R"({"op":"snapshot"})"})
                more.push_back(c->call(parse_json(r).value(), std::chrono::seconds(5)));
            done = true;
        });
        // The application's loop: frames on its clock, requests served between them.
        double t = 0;
        for (int i = 0; i < 2000 && !done; ++i) {
            ep.pump();
            app.rt.frame({seconds(t), {}});
            t += 1.0 / 60;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.join();
        expect(connect_error.empty()) << connect_error;
        expect(exit_of(found) == 0 && found[0].get("data")->get("nodes")->list()->size() == 1);
        expect(exit_of(acted) == 0 && app.saved.peek());
        expect(exit_of(waited) == 0);
        for (std::size_t i = 0; i < more.size(); ++i) expect(exit_of(more[i]) == 0) << std::format("request {} on one connection: {}", i, more[i].empty() ? "" : more[i][0].json().substr(0, 120));
        ep.stop();
        expect(agent::list(dir).empty()) << "the descriptor goes with the endpoint";
        auto gone = agent::Client::connect("notes-test", dir);
        expect(!gone.has_value());
    };

    "listing endpoints in a directory with names that are not ASCII"_test = [] {
        const auto dir = scratch_dir("agent-list") / std::filesystem::path(u8"代理 目录");
        std::filesystem::create_directories(dir);
        { std::ofstream(dir / std::filesystem::path(u8"说明.txt")) << "not an endpoint"; }
        { std::ofstream(dir / "org.example.notes.endpoint.omui.json") << R"({"name":"org.example.notes","app":"org.example.notes","interface":"openmui.tree@0.1"})"; }
        auto listed = agent::list(dir);
        expect(listed.size() == 1 && listed[0].name == "org.example.notes") << std::format("{} endpoints", listed.size());
    };

    "timeline and explain (D-148): what a click did, as facts — a disabled control, then a click that ran"_test = [] {
        Notes app;
        app.rt.set_record_level(RecordLevel::full);
        agent::Protocol p(app.rt, {.input = true});
        const auto drive = [&](std::string_view line) {
            std::vector<Value> out;
            p.request(line, [&](std::string_view l) { out.push_back(parse_json(l).value()); });
            double t = 0.1;
            while (auto batch = p.take_input()) {
                app.rt.set_input_actor(batch->actor);
                app.rt.frame({seconds(t += 1.0 / 60), std::move(batch->events)});
                app.rt.set_input_actor(std::nullopt);
                p.poll();
            }
            p.poll();
            return out;
        };
        // The Delete button made disabled, as a bug might leave it.
        const NodeId del = app.rt.find("notes/delete");
        app.rt.set(del, "disabled", Value(true));
        app.rt.frame({seconds(0.05), {}});
        const Point at = app.rt.screen_bounds(del).center();
        (void)drive(std::format(R"({{"op":"input","events":[{{"type":"click","x":{},"y":{}}},{{"type":"wait","frames":1}}]}})", at.x, at.y));
        const auto why = talk(p, std::format(R"({{"op":"explain","at":[{},{}]}})", at.x, at.y));
        expect(eq(exit_of(why), std::int64_t{0}));
        const Value* data = why.front().get("data");
        expect(data && data->get("found")->as_bool() && data->get("summary")->as_string() == "notes/delete: disabled: a disabled control takes no input")
            << why.front().json();
        // Then the Save button: its activation, in the timeline after the first press.
        const auto first = data->get("cause")->as_int();
        (void)drive(R"({"op":"input","events":[{"type":"click","node":"notes/save"},{"type":"wait","frames":1}]})");
        const auto line = talk(p, std::format(R"({{"op":"timeline","since":{}}})", first));
        const Value* records = line.front().get("data")->get("records");
        expect(line.front().get("data")->get("level")->as_string() == "full" && records && records->list() && !records->list()->empty()) << line.front().json();
        expect(line.front().json().contains("\"kind\":\"activated\"") && line.front().json().contains("notes/save")) << line.front().json();
        expect(app.saved.peek());
        // A press nowhere recorded: said so, with the recorder's level.
        const auto none = talk(p, R"({"op":"explain","at":[300,190]})");
        expect(none.front().json().contains("\"found\":false")) << none.front().json();
        expect(eq(exit_of(talk(p, R"({"op":"explain"})")), std::int64_t{1})) << "it needs at or cause";
    };

    "report: changes — a step answers with the semantic changes it made, in one round trip (W4)"_test = [] {
        Notes app;
        agent::Protocol p(app.rt);
        const auto done = talk(p, R"({"op":"act","node":"notes/save","action":"activate","report":"changes"})");
        expect(eq(exit_of(done), std::int64_t{0}));
        const auto all = [&] {
            std::string text;
            for (const auto& r : done) text += r.json();
            return text;
        }();
        expect(all.contains("\"changes\"") && all.contains("notes/status") && all.contains("\"added\"")) << all;
        const auto plain = talk(p, R"({"op":"act","node":"notes/save","action":"activate"})");
        std::string text;
        for (const auto& r : plain) text += r.json();
        expect(!text.contains("\"changes\"")) << "only when asked";
    };

    "reload (W2): what a saved file changed — the semantic changes, where the pixels changed, the motion sampled until it settles"_test = [] {
        Notes app;
        int version = 0;
        double reloaded_at = 0;
        app.rt.set_source_reloader([&](const Value& request) -> std::expected<Value, std::string> {
            if (request.get("last")) return Value{}.set("reloaded", Value(Value::List{Value("app/main.ts")})).set("changes", Value{});
            ++version;
            reloaded_at = app.rt.time_state().shown.count();
            return Value{}.set("reloaded", Value(Value::List{Value("app/main.ts")})).set("took_ms", 1.0).set("changes", Value{});
        });
        // The window as a picture: its lower right quarter coloured by the version, its first pixel
        // moving with the time for 0.1 s after a reload (a motion the reload started), then still.
        agent::Protocol p(app.rt, {.capture = [&](Rect) -> std::expected<std::vector<std::uint8_t>, std::string> {
            mlibs::image::Image picture{.width = 8, .height = 8, .rgba = std::vector<std::uint8_t>(8 * 8 * 4, 255)};
            for (int y = 4; y < 8; ++y)
                for (int x = 4; x < 8; ++x) picture.row(y)[static_cast<std::size_t>(x) * 4] = static_cast<std::uint8_t>(40 * version);
            const double moving = std::clamp(app.rt.time_state().shown.count() - reloaded_at, 0.0, 0.1);
            picture.row(0)[1] = static_cast<std::uint8_t>(moving * 2000);
            return mlibs::image::encode_png(mlibs::image::view(picture));
        }});
        std::vector<Value> answers;
        p.request(R"({"op":"reload","every":0.025,"samples":20})", [&](std::string_view line) { answers.push_back(parse_json(line).value()); });
        expect(answers.empty()) << "the motion is sampled over frames: the answer comes after";
        double at = 0;
        for (int frame = 0; frame < 40 && answers.empty(); ++frame) {
            app.rt.frame({seconds(at += 1.0 / 60), {}});
            p.poll();
        }
        expect(exit_of(answers) == 0) << (answers.empty() ? std::string("no answer") : answers.back().json());
        if (answers.empty()) return;
        const Value& data = *answers.front().get("data");
        expect(data.get("reloaded")->list()->front().as_string() == "app/main.ts") << data.json();
        expect(!data.get("pixels")->get("changed")->is_null()) << "the picture changed where the version is";
        expect(data.get("pixels")->get("before") && data.get("pixels")->get("after")) << "that region before and after";
        const Value::List& samples = *data.get("motion")->get("samples")->list();
        const auto moving = std::ranges::count_if(samples, [](const Value& s) { return !s.get("changed")->is_null(); });
        expect(moving >= 3 && samples.size() > static_cast<std::size_t>(moving)) << data.get("motion")->json();
        expect(data.get("motion")->get("settled")->as_bool()) << "two still samples end it";
        expect(!app.rt.time_state().controlled) << "the time is the host's again";
        // What the last reload did, without reloading.
        expect(exit_of(talk(p, R"({"op":"reload","last":true})")) == 0);
    };
}};

