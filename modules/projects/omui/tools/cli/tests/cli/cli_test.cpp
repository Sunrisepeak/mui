// `omui agent` against a running application: a headless H3 session on this
// thread, the command line on another, talking over the real endpoint.
import std;
import omui.cli;
import omui.host.headless;
import omui.ui.base;
import mlibs.testing;
import mcpplibs.mtest;
import omui.core;
import mcpplibs.os;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

struct Result {
    int         code = -1;
    std::string out, err;
};

Result omui_cmd(std::vector<std::string> args) {
    args.insert(args.begin(), "omui");
    std::ostringstream out, err;
    Result r;
    r.code = cli::run(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

// Runs commands on a thread while the application runs frames here.
std::vector<Result> with_app(Headless& h, std::vector<std::vector<std::string>> commands) {
    std::vector<Result> results;
    std::atomic<bool> done{false};
    std::jthread t([&] {
        for (auto& c : commands) results.push_back(omui_cmd(c));
        done = true;
    });
    for (int i = 0; i < 5000 && !done; ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
    return results;
}

// One HTTP exchange with a bridge on the loopback, as a page (or a stranger) makes it: the status and the body.
std::pair<int, std::string> http(std::uint16_t port, std::string_view method, std::string_view path, std::string_view body = {},
                                 std::string_view headers = {}) {
    auto c = mcpplibs::os::net::connect_loopback(port);
    if (!c) return {0, c.error().message()};
    (void)c->write(std::format("{} {} HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n{}Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                               method, path, headers, body.size(), body));
    std::string got;
    for (;;) {
        auto chunk = c->read_for(std::chrono::seconds(30));
        if (!chunk || !*chunk || (*chunk)->empty()) break;
        got += **chunk;
    }
    int status = 0;
    if (got.size() > 12) std::from_chars(got.data() + 9, got.data() + 12, status);
    const auto at = got.find("\r\n\r\n");
    return {status, at == std::string::npos ? std::string() : got.substr(at + 4)};
}

// The repository: the nearest directory above this file with the workspace's `.xlings.json` and `modules/`.
std::filesystem::path repository() {
    auto at = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path();
    while (at.has_parent_path() && at != at.parent_path() && !(std::filesystem::exists(at / ".xlings.json") && std::filesystem::exists(at / "modules")))
        at = at.parent_path();
    return at;
}

}  // namespace

static UTest cases{"cli", [] {

    "requests from arguments: an operation's words are its catalog's parameters, sent as the protocol's request"_test = [] {
        std::vector<Value> sent;
        const cli::Transport recorded = [&](const Value& request, std::chrono::milliseconds) {
            sent.push_back(request);
            return std::vector<Value>{Value{}.set("kind", "result").set("exitCode", 0)};
        };
        // The request the words make, as it went out (null: none went, the words were refused).
        const auto request = [&](std::vector<std::string> words) {
            sent.clear();
            std::ostringstream out, err;
            (void)cli::operate_running(std::move(words), recorded, out, err);
            return sent.empty() ? Value{} : sent.front();
        };
        const auto text = [](const Value& request, std::string_view key) { return request.get(key) ? request.get(key)->json() : std::string("(none)"); };
        const Value find = request({"find", R"({"role":"button","name":"Del*"})"});
        expect(eq(text(find, "op"), std::string(R"("find")")) && eq(text(find, "q"), std::string(R"({"role":"button","name":"Del*"})")) && !find.get("actor"))
            << find.json() << " (a read says no one)";
        const Value act = request({"act", "notes/delete", "activate", "--answer", "yes"});
        expect(eq(text(act, "node"), std::string(R"("notes/delete")")) && eq(text(act, "action"), std::string(R"("activate")")) &&
               eq(text(act, "answer"), std::string(R"("yes")")) && eq(text(act, "actor"), std::string(R"("agent:omui-cli")")))
            << act.json();
        const Value dry = request({"act", "main/dark", "toggle", "--dry-run", "--actor", "agent:planner"});
        expect(eq(text(dry, "dry_run"), std::string("true")) && eq(text(dry, "actor"), std::string(R"("agent:planner")")) && eq(text(dry, "action"), std::string(R"("toggle")")))
            << dry.json();
        const Value transaction = request({"transaction", R"([{"node":"main/a","action":"toggle"}])", "--label", "both"});
        expect(eq(text(transaction, "steps"), std::string(R"([{"node":"main/a","action":"toggle"}])")) && eq(text(transaction, "label"), std::string(R"("both")")))
            << transaction.json();
        const Value clock = request({"clock", "rate", "0.5"});
        expect(eq(text(clock, "action"), std::string(R"("rate")")) && eq(text(clock, "rate"), std::string("0.5"))) << clock.json();
        // A verb of Live Design: `invoke`, its arguments beside what the request itself carries.
        const Value verb = request({"design.set", "main/title", R"({"text":"Hi"})", "--answer", "yes"});
        expect(eq(text(verb, "op"), std::string(R"("invoke")")) && eq(text(verb, "verb"), std::string(R"("design.set")")) && eq(text(verb, "answer"), std::string(R"("yes")")) &&
               verb.get("args") && eq(text(*verb.get("args"), "node"), std::string(R"("main/title")")) && eq(text(*verb.get("args"), "props"), std::string(R"({"text":"Hi"})")))
            << verb.json();
        // What the catalog needs, asked for before anything is sent; a name it has not, said so.
        expect(request({"act", "main/dark"}).get("op") == nullptr) << "act needs its action";
        expect(request({"frobnicate"}).get("op") == nullptr);
        expect(omui_cmd({"agent", "find", "nobody-listens", R"({"role":"button"})", "--dir", "/nonexistent/openmui"}).code == 1);
        expect(omui_cmd({"agent", "snapshot"}).code == 2) << "which application?";
        expect(omui_cmd({"agent", "act", "--help"}).code == 0 && omui_cmd({"agent", "act", "--help"}).out.contains(R"("name":"ui.act")"));
        // A name no catalog has: not found, with the nearest names.
        const auto unknown = omui_cmd({"frobnicate"});
        expect(unknown.code == 1 && unknown.out.contains("not-found")) << unknown.out << unknown.err;
        // What holds the terminal is a person's command line's: an agent's entry is refused, saying so.
        const Value refused = cli::run_for_agent("omui.dev", Value{});
        expect(refused.get("exit")->as_int() == 1 && std::string(refused.get("err")->as_string()).contains("command line")) << refused.json();
        expect(omui_cmd({"dev", "--help"}).code == 0 && omui_cmd({"dev", "--help"}).out.contains(R"("name":"omui.dev")"));
    };

    "omui agent ls / snapshot / find / act / wait / capture against a running app"_test = [] {
        auto dir = scratch_dir("cli");
        HeadlessOptions o;
        o.level = HeadlessLevel::session;
        o.size = {320, 160};
        o.app = "notes-cli";
        o.endpoint_directory = dir;
        Headless h(o);
        int deleted = 0;
        (void)h.runtime().verbs().add({"notes.delete", "Delete every note", Effect::irreversible, "yes", {}},
                                      [&](const Value&) -> VerbResult {
                                          ++deleted;
                                          return Value{};
                                      });
        h.mount("notes", column({.padding = 8, .gap = 4}, {
                                    text({.id = "title", .text = "Notes"}),
                                    box({.id = "delete", .width = 80, .height = 24, .background = "danger", .label = "Delete all"})
                                        .prop("verb", Source{Value("notes.delete"), {}, {}}),
                                }));
        h.step();
        if (!h.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        const std::string d = dir.string();
        auto png = dir / "delete.png";
        auto r = with_app(h, {
                                 {"agent", "ls", "--dir", d},
                                 {"agent", "snapshot", "notes-cli", "--interactive", "--dir", d},
                                 {"agent", "find", "notes-cli", R"({"name":"Delete*"})", "--dir", d},
                                 {"agent", "act", "notes-cli", "notes/delete", "activate", "--actor", "agent:planner;via=person:alice", "--dir", d},
                                 {"agent", "act", "notes-cli", "notes/delete", "activate", "--answer", "yes", "--dir", d},
                                 {"agent", "wait", "notes-cli", "--settled", "--timeout", "2", "--dir", d},
                                 {"agent", "capture", "notes-cli", "--node", "notes/delete", "--out", png.string(), "--dir", d},
                                 // The delete ends what can be undone (S-12); a dry run says so, and changes nothing.
                                 {"agent", "undo", "notes-cli", "--dry-run", "--dir", d},
                                 {"agent", "undo", "notes-cli", "--dir", d},
                                 {"agent", "history", "notes-cli", "--dir", d},
                                 {"agent", "transaction", "notes-cli", R"([{"node":"notes/delete","action":"activate","answer":"yes"}])", "--dir", d},
                                 {"agent", "capture", "notes-cli", "--format", "svg", "--out", (dir / "window.svg").string(), "--dir", d},
                             });
        expect(r.size() == 12);
        expect(r[0].code == 0 && r[0].out.starts_with("notes-cli\tnotes-cli\topenmui.tree@0.1")) << r[0].out + r[0].err;
        expect(r[1].code == 0 && r[1].out.find("notes/delete") != std::string::npos && r[1].out.find("notes/title") == std::string::npos);
        expect(r[2].code == 0 && r[2].out.find(R"("effect":"irreversible")") != std::string::npos) << r[2].out;
        expect(r[3].code == 2 && r[3].out.find("needs-answer") != std::string::npos) << std::format("{} {}", r[3].code, r[3].out);
        expect(r[4].code == 0 && deleted == 1) << r[4].out + r[4].err;
        expect(r[5].code == 0) << r[5].out + r[5].err;
        expect(r[6].code == 0 && std::filesystem::exists(png) && std::filesystem::file_size(png) > 50) << r[6].out + r[6].err;
        {
            std::ifstream in(dir / "window.svg", std::ios::binary);
            const std::string svg{std::istreambuf_iterator<char>(in), {}};
            expect(r[11].code == 0 && r[11].out.contains(R"("islands":)") && svg.starts_with("<svg") && svg.contains(R"(data-node="notes/delete")")) << r[11].out + r[11].err;
        }
        expect(r[7].code == 0 && r[7].out.find(R"("outcome":"refused")") != std::string::npos) << r[7].out + r[7].err;
        expect(r[8].code == 1 && r[8].out.find("cannot be taken back") != std::string::npos) << r[8].out + r[8].err;
        expect(r[9].code == 0 && r[9].out.find(R"("barrier":true)") != std::string::npos) << r[9].out + r[9].err;
        expect(r[10].code == 1 && r[10].out.find("S-13") != std::string::npos && deleted == 1) << r[10].out + r[10].err;
        auto journal = h.runtime().action_journal();
        expect(std::ranges::count(journal, std::string("agent:planner;via=person:alice"), &ActionEntry::actor) == 1);
        // The transaction was refused before its irreversible step ran: the
        // journal ends with the undo that was refused before it.
        expect(!journal.empty() && journal.back().action == "undo" && journal.back().actor == "agent:omui-cli") << (journal.empty() ? "nothing journalled" : journal.back().action + " by " + journal.back().actor);
    };

    "omui agent sweep: every action performed, undone as it was; an answer only asked; a dead control fails it"_test = [] {
        auto dir = scratch_dir("cli-sweep");
        HeadlessOptions o;
        o.level = HeadlessLevel::session;
        o.size = {360, 240};
        o.app = "sweep-cli";
        o.endpoint_directory = dir;
        Headless h(o);
        int deleted = 0;
        (void)h.runtime().verbs().add({"notes.delete", "Delete every note", Effect::irreversible, "yes", {}}, [&](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        // A verb that always refuses: a control nothing can make work.
        (void)h.runtime().verbs().add({"notes.broken", "Never works", Effect::reversible, {}, {}},
                                      [](const Value&) -> VerbResult { return std::unexpected(std::string("it never works")); });
        int person_actions = 0;
        auto agree = signal(false);
        auto level = signal(0.37);  // off its steps: an undo puts it back as it was, not snapped
        auto mount = [&](bool broken) {
            std::vector<Element> items{
                checkbox({.id = "agree", .checked = agree, .label = "I agree"}),
                slider({.id = "level", .label = "Level", .step = 0.1, .value = level, .width = 160}),
                box({.id = "delete", .width = 80, .height = 24, .background = "danger", .label = "Delete all"}).prop("verb", Source{Value("notes.delete"), {}, {}}),
                button({.id = "off", .disabled = true, .text = "Off"}),
                button({.id = "person", .text = "Only the person", .on_activated = [&] { ++person_actions; }}).prop("person", Source{Value(true), {}, {}}),
            };
            if (broken) items.push_back(box({.id = "broken", .width = 80, .height = 24, .label = "Broken"}).prop("verb", Source{Value("notes.broken"), {}, {}}));
            h.mount("main", column({.padding = 8, .gap = 4}, std::move(items)));
            h.step();
        };
        mount(false);
        if (!h.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        const std::string d = dir.string();
        auto r = with_app(h, {{"agent", "sweep", "sweep-cli", "--dir", d}});
        expect(r.size() == 1 && r[0].code == 0) << (r.empty() ? "" : r[0].out + r[0].err);
        auto verdict = [&](const Result& x, std::string_view node, std::string_view action) {
            for (auto line : x.out | std::views::split('\n')) {
                const std::string_view l(line.begin(), line.end());
                if (l.contains(std::format(R"("node":"{}")", node)) && l.contains(std::format(R"("action":"{}")", action))) {
                    const auto at = l.find(R"("verdict":")");
                    if (at != std::string_view::npos) return std::string(l.substr(at + 11, l.find('"', at + 11) - at - 11));
                }
            }
            return std::string("(none)");
        };
        expect(eq(verdict(r[0], "main/agree", "toggle"), std::string("ok")));
        expect(eq(verdict(r[0], "main/level", "decrement"), std::string("ok")));
        expect(eq(verdict(r[0], "main/delete", "activate"), std::string("asks")));
        expect(eq(verdict(r[0], "main/person", "activate"), std::string("person")));
        expect(person_actions == 0 && r[0].out.contains(R"("person":1)")) << "the protected control was refused without running";
        expect(deleted == 0) << "an irreversible action is only asked";
        expect(!agree.peek() && level.peek() == 0.37) << std::format("as they were: {} {}", agree.peek(), level.peek());
        // A control that cannot work fails the sweep.
        mount(true);
        r = with_app(h, {{"agent", "sweep", "sweep-cli", "--dir", d}});
        expect(r.size() == 1 && r[0].code == 1) << (r.empty() ? "" : r[0].out + r[0].err);
        expect(eq(verdict(r[0], "main/broken", "activate"), std::string("failed")));
        expect(r[0].out.contains(R"("failed":1)")) << r[0].out;
        expect(person_actions == 0) << "a dead ordinary control still fails; a person-only control still does not run";
    };

    "omui check: located diagnostics, as records"_test = [] {
        const auto dir = scratch_dir("omui-check");
        auto write = [&](std::string_view name, std::string_view text) {
            std::ofstream(dir / name, std::ios::binary) << text;
            return utf8(dir / name);
        };
        const std::string good = write("good.view.xdef.json", R"({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "hi"}}}})");
        const std::string bad = write("bad.view.xdef.json", "{\"omui\": \"0.1\",\n \"view\": {\"root\": {\"type\": \"buton\"}}}");
        const std::string component = write("search.view.xdef.json", R"({"omui": "0.1", "component": {"name": "search_field",
            "interface": {"props": {"query": {"type": "string", "bindable": true}}},
            "root": {"type": "text_box", "id": "input", "props": {"text": {"bind": "query"}}}}})");
        const std::string user = write("page.view.xdef.json", R"({"omui": "0.1", "view": {"root": {"type": "search_field", "id": "find"}}})");
        // Records a line each (the subject `omui`'s answer): a diagnostic's data record, located.
        const auto data_of = [](const std::string& text) {
            std::vector<Value> found;
            std::istringstream lines(text);
            for (std::string line; std::getline(lines, line);)
                if (auto value = parse_json(line); value && value->get("kind") && value->get("kind")->as_string() == "data") found.push_back(*value->get("data"));
            return found;
        };
        auto r = omui_cmd({"check", good});
        expect(eq(r.code, 0) && data_of(r.out).empty()) << r.out + r.err;
        r = omui_cmd({"check", good, bad});
        expect(eq(r.code, 1));
        const auto found = data_of(r.out);
        expect(fatal(eq(found.size(), std::size_t{1}))) << r.out;
        expect(found[0].get("message")->as_string().contains("no component is named `buton`") && found[0].get("hint")->as_string() == "did you mean `button`?" && found[0].get("line")->as_int() == 2 &&
               found[0].get("column")->as_int() == 28 && found[0].get("pointer")->as_string() == "/view/root/type")
            << found[0].json();
        r = omui_cmd({"check", "--json", bad});
        expect(r.code == 1 && eq(data_of(r.out).size(), std::size_t{1})) << "a switch it does not take, then a document: " << r.out;
        // A document that uses another document's component, given in either order.
        r = omui_cmd({"check", user, component});
        expect(r.code == 0) << r.out + r.err;
    };

    "omui fmt rewrites to the canonical format; --check only reports"_test = [] {
        const auto dir = scratch_dir("omui-fmt");
        const auto file = dir / "a.view.xdef.json";
        std::ofstream(file, std::ios::binary) << R"({"view":{"root":{"type":"text"}},"omui":"0.1"})";
        auto r = omui_cmd({"fmt", "--check", utf8(file)});
        expect(r.code == 1 && r.out.contains(R"("canonical":false)") && r.out.contains("a.view.xdef.json")) << r.out;
        expect(eq(omui_cmd({"fmt", utf8(file)}).code, 0));
        std::ifstream in(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        expect(eq(text, std::string("{\n  \"omui\": \"0.1\",\n  \"view\": {\n    \"root\": {\"type\": \"text\"}\n  }\n}\n")));
        expect(eq(omui_cmd({"fmt", "--check", utf8(file)}).code, 0));
    };

    "omui schema is the committed spec/schema/view.schema.json"_test = [] {
        const auto committed = repository() / "spec" / "schema" / "view.schema.json";
        std::ifstream in(committed, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto r = omui_cmd({"schema"});
        expect(eq(r.code, 0));
        // Its data record is the schema; the committed file is it in the canonical format.
        const auto record = parse_json(r.out.substr(0, r.out.find('\n')));
        const auto file = parse_json(text);
        expect(record && file && record->get("data") && record->get("data")->json() == file->json()) << "regenerate it: omui schema --out spec/schema/view.schema.json";
    };

    "omui connect and omui page: a page pairs with the code, the agent's requests reach it as the agent's, its answers come back; strangers refused"_test = [] {
        const auto dir = scratch_dir("omui-bridge");
        // Before any bridge: --wait waits for one to come (an agent starts `omui connect` and waits at once), then says none came.
        const auto before = std::chrono::steady_clock::now();
        auto none = omui_cmd({"--dir", omui::utf8(dir), "page", "status", "--wait", "--timeout", "0.5"});
        expect(none.code == 1 && std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(400)) << none.out + none.err;
        std::atomic<std::uint16_t> port{0};
        std::mutex heard_lock;
        std::string heard;  // what `paired` heard (omui connect prints the page's description then)
        std::jthread bridge([&](std::stop_token stop) {
            auto r = cli::run_bridge({.code = "ABCD-EFGH", .name = "Claude Code", .any_port = true, .dir = dir,
                                      .paired = [&](std::string_view page, std::string_view app) {
                                          std::scoped_lock g(heard_lock);
                                          heard = std::format("{} {}", page, app);
                                      }},
                                     stop, [&](std::uint16_t p) { port = p; });
            expect(r.has_value()) << (r ? "" : r.error());
        });
        for (int i = 0; i < 500 && !port; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        expect(port != 0) << "listening";
        if (!port) return;
        const std::string agent_dir = omui::utf8(dir);
        // Not paired yet: the agent is told so.
        auto status = omui_cmd({"--dir", agent_dir, "page", "status"});
        expect(status.code == 3) << status.out + status.err;
        // A wrong code is refused; the right one, the dash or the case aside, pairs.
        expect(eq(http(port, "POST", "/omui/v1/hello", R"({"code":"ZZZZ-ZZZZ","app":"x"})", "Origin: https://example.org\r\n").first, 403));
        auto hello = http(port, "POST", "/omui/v1/hello", R"({"code":"abcdefgh","app":"org.example.notes","page":"https://example.org/app"})",
                          "Origin: https://example.org\r\n");
        expect(hello.first == 200) << hello.second;
        auto session = parse_json(hello.second);
        const std::string s_id(session && session->get("session") ? session->get("session")->as_string() : "");
        expect(!s_id.empty()) << "a session";
        {
            std::scoped_lock g(heard_lock);
            expect(eq(heard, std::string("https://example.org/app org.example.notes"))) << "the bridge says which page paired";
        }
        status = omui_cmd({"--dir", agent_dir, "page", "status", "--wait", "--timeout", "5"});
        expect(status.code == 0 && status.out.contains("\"paired\":true") && status.out.contains("org.example.notes")) << status.out + status.err;
        // The page: takes the next request and answers it, as its window.omui would.
        std::string seen;
        std::jthread the_page([&] {
            auto job = http(port, "GET", "/omui/v1/next?session=" + s_id);
            seen = job.second;
            auto j = parse_json(job.second);
            const std::int64_t id = j && j->get("id") ? j->get("id")->as_int() : 0;
            const std::string reply = std::format(R"({{"id":{},"reply":{{"exitCode":0,"data":{{"nodes":[]}},"errors":[],"records":[{{"kind":"data","data":{{"nodes":[]}},"re":1}},{{"kind":"result","exitCode":0,"re":1}}]}}}})", id);
            (void)http(port, "POST", "/omui/v1/reply?session=" + s_id, reply);
        });
        auto snap = omui_cmd({"--dir", agent_dir, "page", "snapshot", "--interactive"});
        the_page.join();
        expect(snap.code == 0) << snap.err;
        expect(snap.out.contains("\"kind\":\"data\"") && !snap.out.contains("\"re\"")) << snap.out;
        expect(seen.contains("\"op\":\"snapshot\"") && seen.contains("\"actor\":\"agent:claude-code\"") && seen.contains("\"interactive\":true")) << std::format("the request as the page took it: {}", seen);
        // Strangers: the agent's side from a page (an Origin), or without the token, is refused; a page with a wrong session too.
        expect(eq(http(port, "POST", "/omui/v1/call", R"({"op":"snapshot"})", "Origin: https://evil.example\r\n").first, 401));
        expect(eq(http(port, "GET", "/omui/v1/status").first, 401));
        expect(eq(http(port, "GET", "/omui/v1/next?session=nope", {}, "Origin: https://example.org\r\n").first, 403));
        bridge.request_stop();
        bridge.join();
        expect(!std::filesystem::exists(dir / "bridge.json")) << "the bridge says it is gone";
    };
}};
