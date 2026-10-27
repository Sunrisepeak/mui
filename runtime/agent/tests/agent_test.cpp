// The agent endpoint: the protocol's operations and errors, waits served
// between frames, and the same conversation over a real local endpoint.
import std;
import openmui.agent;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace agent = omui::agent;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
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

std::string_view kind(const Value& v) { return v.get("kind") ? v.get("kind")->as_string() : ""; }
std::int64_t exit_of(const std::vector<Value>& r) {
    return r.empty() || kind(r.back()) != "result" ? -100 : r.back().get("exitCode")->as_int(-100);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"agent"};

    s.test("describe, snapshot, find", [] {
        Notes app;
        agent::Protocol p(app.rt, {.app = "org.example.notes"});
        auto d = talk(p, R"({"op":"describe"})");
        check(exit_of(d) == 0 && d[0].get("data")->get("interface")->as_string() == "openmui.tree@0.1");
        auto snap = talk(p, R"({"op":"snapshot","interactive":true})");
        check(exit_of(snap) == 0);
        const Value* nodes = snap[0].get("data")->get("nodes");
        check(nodes && nodes->list()->size() == 2, snap[0].json());
        auto found = talk(p, R"({"op":"find","q":{"role":"button","name":"Delete*"}})");
        check(exit_of(found) == 0);
        const Value& n = found[0].get("data")->get("nodes")->list()->front();
        check(n.get("id")->as_string() == "notes/delete");
        const Value& a = n.get("actions")->list()->front();
        check(a.get("effect")->as_string() == "irreversible" && a.get("answer")->as_string() == "yes");
        auto missing = talk(p, R"({"op":"snapshot","root":"notes/nothing"})");
        check(exit_of(missing) == 1);
    });

    s.test("act: needs-answer, then with the answer (S-1, S-2)", [] {
        Notes app;
        agent::Protocol p(app.rt);
        auto first = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"})");
        check(exit_of(first) == 2, first.empty() ? "" : first.back().json());
        check(kind(first[0]) == "error" && first[0].get("code")->as_string() == "needs-answer");
        check(app.deleted == 0);
        auto second = talk(p, R"({"op":"act","node":"notes/delete","action":"activate","answer":"yes","actor":"agent:planner;via=person:alice"})");
        check(exit_of(second) == 0 && app.deleted == 1);
        auto j = app.rt.action_journal();
        check(j.back().actor == "agent:planner;via=person:alice");
        auto anonymous = talk(p, R"({"op":"act","node":"notes/save","action":"activate"})");
        check(exit_of(anonymous) == 0 && app.rt.action_journal().back().actor == "agent:anonymous");
        auto bad_actor = talk(p, R"({"op":"act","node":"notes/save","action":"activate","actor":"nobody"})");
        check(exit_of(bad_actor) == 1);
    });

    s.test("invoke: an application verb, its answer asked for as an action's (S-1)", [] {
        Notes app;
        int cleared = 0;
        check(app.rt.verbs().add({"notes.clear", "Removes every note.", Effect::irreversible, "yes", {{"keep", "i64"}}},
                                 [&](const Value& args) -> VerbResult {
                                     ++cleared;
                                     return Value(args.get("keep") ? args.get("keep")->as_int() : 0);
                                 }).has_value());
        agent::Protocol p(app.rt);
        auto asked = talk(p, R"({"op":"invoke","verb":"notes.clear","args":{"keep":2}})");
        check(exit_of(asked) == 2 && cleared == 0, "needs its answer");
        auto done = talk(p, R"({"op":"invoke","verb":"notes.clear","args":{"keep":2},"answer":"yes","actor":"agent:tidy"})");
        check(exit_of(done) == 0 && cleared == 1 && done[0].get("data")->as_int() == 2, done.empty() ? "" : done.front().json());
        check(exit_of(talk(p, R"({"op":"invoke","verb":"notes.nothing"})")) == 1, "an unknown verb");
        check(exit_of(talk(p, R"({"op":"invoke"})")) == 1, "no verb");
    });

    s.test("wait: answered when the condition holds, between frames", [] {
        Notes app;
        agent::Protocol p(app.rt);
        std::vector<Value> got;
        p.request(R"({"op":"wait","q":{"id":"notes/status"},"settled":true,"re":7})",
                  [&](std::string_view l) { got.push_back(parse_json(l).value()); });
        check(got.empty() && p.pending() == 1, "deferred");
        (void)talk(p, R"({"op":"act","node":"notes/save","action":"activate"})");
        app.rt.frame({seconds(0.0), {}});
        p.poll();
        check(got.empty(), "the status is still entering");
        app.rt.frame({seconds(0.3), {}});
        p.poll();
        check(exit_of(got) == 0 && p.pending() == 0);
        check(got.front().get("re")->as_int() == 7, "records carry the request's re");
        auto now = talk(p, R"({"op":"wait","q":{"id":"notes/status"}})");
        check(exit_of(now) == 0, "already true: answered at once");
        std::vector<Value> late;
        p.request(R"({"op":"wait","q":{"id":"notes/never"},"timeout":0})", [&](std::string_view l) { late.push_back(parse_json(l).value()); });
        p.poll();
        check(exit_of(late) == 3 && late[0].get("code")->as_string() == "timeout");
    });

    s.test("changes per client, and errors that teach", [] {
        Notes app;
        agent::Protocol p(app.rt);
        auto first = talk(p, R"({"op":"changes"})");
        check(exit_of(first) == 0 && first[0].get("data")->get("added")->list()->size() >= 3);
        auto none = talk(p, R"({"op":"changes"})");
        const Value* added = none[0].get("data")->get("added");
        check(!added || added->list()->empty(), "nothing new for this client");
        check(exit_of(talk(p, "not json")) == 1);
        auto unknown = talk(p, R"({"op":"click"})");
        check(exit_of(unknown) == 1 && unknown[0].get("hint")->as_string().find("snapshot") != std::string_view::npos);
        check(exit_of(talk(p, R"({"op":"find"})")) == 1);
        check(exit_of(talk(p, R"({"op":"find","q":{"role":"knob"}})")) == 1);
        check(exit_of(talk(p, R"({"op":"capture"})")) == 1, "no renderer: refused");
        agent::Protocol with_capture(app.rt, {.capture = [](Rect) -> std::expected<std::vector<std::uint8_t>, std::string> {
                                                   return std::vector<std::uint8_t>{'P', 'N', 'G'};
                                               }});
        auto cap = talk(with_capture, R"({"op":"capture","node":"notes/save"})");
        check(exit_of(cap) == 0 && cap[0].get("data")->get("base64")->as_string() == "UE5H");
    });

    s.test("development: the application's clock, paused, stepped and sought (F4)", [] {
        Notes app;
        agent::Protocol p(app.rt);
        app.rt.frame({seconds(1), {}});
        auto shown = talk(p, R"({"op":"clock"})");
        check(exit_of(shown) == 0 && !shown[0].get("data")->get("controlled")->as_bool() && shown[0].get("data")->get("now")->as_double() == 1.0);
        check(exit_of(talk(p, R"({"op":"clock","action":"pause"})")) == 0);
        app.rt.frame({seconds(2), {}});
        auto paused = talk(p, R"({"op":"clock"})");
        check(paused[0].get("data")->get("now")->as_double() == 1.0 && paused[0].get("data")->get("host")->as_double() == 2.0, paused[0].json());
        auto step = talk(p, R"({"op":"clock","action":"step","seconds":0.5})");
        check(exit_of(step) == 0 && step[0].get("data")->get("pending")->as_bool());
        app.rt.frame({seconds(2), {}});
        check(app.rt.now() == seconds(1.5));
        check(exit_of(talk(p, R"({"op":"clock","action":"seek","seconds":0.25})")) == 0);
        app.rt.frame({seconds(2), {}});
        check(app.rt.now() == seconds(0.25));
        check(exit_of(talk(p, R"({"op":"clock","action":"rate","rate":-1})")) == 1);
        check(exit_of(talk(p, R"({"op":"clock","action":"warp"})")) == 1);
        check(exit_of(talk(p, R"({"op":"clock","action":"release"})")) == 0 && !app.rt.time_state().controlled);
    });

    s.test("development: the frame pipeline, read and changed while it runs (D-66)", [] {
        Notes app;
        agent::Protocol p(app.rt);
        app.rt.frame({seconds(0), {}});
        auto show = talk(p, R"({"op":"pipeline"})");
        check(exit_of(show) == 0);
        const Value* d = show[0].get("data");
        check(d && d->get("name")->as_string() == "runtime.h2" && d->get("stages")->list()->size() == 8);
        check((*d->get("stages")->list())[4].get("plugin")->as_string() == "openmui.view.layout");
        check(d->get("toml")->as_string().contains("[[stage]]") && d->get("graph")->as_string().contains("writes display"));
        check(std::ranges::any_of(*d->get("plugins")->list(), [](const Value& x) { return x.get("name")->as_string() == "openmui.view.debug"; }));
        auto rm = talk(p, R"({"op":"pipeline","action":"remove","stage":"debug"})");
        check(exit_of(rm) == 0 && rm[0].get("data")->get("stages")->list()->size() == 7);
        auto back = talk(p, R"({"op":"pipeline","action":"insert","stage":"debug","plugin":"openmui.view.debug","after":["paint"]})");
        check(exit_of(back) == 0 && back[0].get("data")->get("stages")->list()->size() == 8);
        auto bad = talk(p, R"({"op":"pipeline","action":"replace","stage":"paint","plugin":"nonesuch"})");
        check(exit_of(bad) == 1 && bad[0].json().contains("no plugin `nonesuch`"), bad[0].json());
        check(exit_of(talk(p, R"({"op":"pipeline","action":"explode"})")) == 1);
        app.rt.frame({seconds(0.1), {}});
        check(app.rt.frame_pipeline().order().size() == 8, "still whole");
    });

    s.test("development: inspect, stats, profile, logs, debug", [] {
        Notes app;
        agent::Protocol p(app.rt);
        for (int i = 0; i < 5; ++i) app.rt.frame({seconds(i * 0.016), {}});
        auto in = talk(p, R"({"op":"inspect","node":"notes/save"})");
        check(exit_of(in) == 0 && in[0].get("data")->get("type")->as_string() == "box");
        check(in[0].get("data")->get("handlers")->list()->size() == 1);
        auto st = talk(p, R"({"op":"stats"})");
        check(exit_of(st) == 0 && st[0].get("data")->get("frames")->as_int() >= 5 && st[0].get("data")->get("p95_ms"));
        auto hist = talk(p, R"({"op":"stats","history":3})");
        check(hist[0].get("data")->get("history") && hist[0].get("data")->get("history")->list()->size() == 3 &&
              (*hist[0].get("data")->get("history")->list())[2].get("total"));
        check(exit_of(talk(p, R"({"op":"profile","action":"start"})")) == 0);
        for (int i = 0; i < 3; ++i) {
            app.saved.set(i % 2 == 0);
            app.rt.frame({seconds(1 + i * 0.016), {}});
        }
        auto pr = talk(p, R"({"op":"profile","action":"stop","chrome":true})");
        check(exit_of(pr) == 0);
        const Value* summary = pr[0].get("data")->get("summary");
        check(summary && !summary->list()->empty());
        bool frames = false;
        for (const Value& x : *summary->list()) frames = frames || (x.get("name")->as_string() == "frame" && x.get("count")->as_int() == 3);
        check(frames, pr[0].json().substr(0, 300));
        check(pr[0].get("data")->get("chrome")->as_string().starts_with("{\"displayTimeUnit\""));
        check(exit_of(talk(p, R"({"op":"profile","action":"pause"})")) == 1);
        set_log_sink([](void*, const LogRecord&) {}, nullptr);
        constexpr Log notes_log{"notes"};
        notes_log.warn("disk {} full", 97);
        auto lg = talk(p, R"({"op":"logs","level":"warning","category":"notes"})");
        check(exit_of(lg) == 0 && lg[0].get("data")->get("records")->list()->back().get("message")->as_string() == "disk 97 full");
        set_log_sink(nullptr, nullptr);
        auto dbg = talk(p, R"({"op":"debug","bounds":true,"hud":true,"log":"layout=debug,*=info"})");
        check(exit_of(dbg) == 0 && app.rt.debug().bounds && app.rt.debug().hud && log_enabled(Level::debug, "layout"));
        check(exit_of(talk(p, R"({"op":"debug","log":"layout=loud"})")) == 1);
        set_log_level(Level::info);
        app.rt.set_debug({});
    });

    s.test("over a local endpoint: list, connect, call, wait across frames", [] {
        Notes app;
        auto dir = scratch_dir("agent");
        agent::Endpoint ep(app.rt, {.name = "notes-test", .directory = dir, .protocol = {.app = "org.example.notes"}});
        auto started = ep.start();
        if (!started && started.error().find("not supported") != std::string::npos) {
            // Wine lacks AF_UNIX on its drives; Windows 10 and later, Linux and macOS have local endpoints.
            std::println("    skipped: local endpoints are not supported here ({})", started.error());
            return;
        }
        check(started.has_value(), started ? "" : started.error());
        if (!started) return;
        auto listed = agent::list(dir);
        check(listed.size() == 1 && listed[0].name == "notes-test" && listed[0].app == "org.example.notes");

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
        check(connect_error.empty(), connect_error);
        check(exit_of(found) == 0 && found[0].get("data")->get("nodes")->list()->size() == 1);
        check(exit_of(acted) == 0 && app.saved.peek());
        check(exit_of(waited) == 0);
        for (std::size_t i = 0; i < more.size(); ++i) check(exit_of(more[i]) == 0, std::format("request {} on one connection: {}", i, more[i].empty() ? "" : more[i][0].json().substr(0, 120)));
        ep.stop();
        check(agent::list(dir).empty(), "the descriptor goes with the endpoint");
        auto gone = agent::Client::connect("notes-test", dir);
        check(!gone.has_value());
    });

    s.test("listing endpoints in a directory with names that are not ASCII", [] {
        const auto dir = scratch_dir("agent-list") / std::filesystem::path(u8"代理 目录");
        std::filesystem::create_directories(dir);
        { std::ofstream(dir / std::filesystem::path(u8"说明.txt")) << "not an endpoint"; }
        { std::ofstream(dir / "org.example.notes.endpoint.omui.json") << R"({"name":"org.example.notes","app":"org.example.notes","interface":"openmui.tree@0.1"})"; }
        auto listed = agent::list(dir);
        check(listed.size() == 1 && listed[0].name == "org.example.notes", std::format("{} endpoints", listed.size()));
    });

    return s.run(argc, argv);
}
