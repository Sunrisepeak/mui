// `omui agent` against a running application: a headless H3 session on this
// thread, the command line on another, talking over the real endpoint.
import std;
import omui.cli;
import mcpplibs.cmdline;
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

}  // namespace

UTest cases{"cli", [] {

    "requests from arguments"_test = [] {
        auto a = cli::app();
        auto p = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "find", "notes", R"({"role":"button"})", "--name", "Del*"});
        expect(p.has_value());
        auto r = cli::agent_request(p->subcommand()->get());
        expect(r.has_value() && *r == R"({"op":"find","q":{"role":"button","name":"Del*"}})") << r.value_or(r.error());
        auto act = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "act", "notes", "notes/delete", "activate", "--answer", "yes"});
        auto ra = cli::agent_request(act->subcommand()->get());
        expect(ra && *ra == R"({"op":"act","node":"notes/delete","action":"activate","answer":"yes","actor":"agent:omui-cli"})") << ra.value_or(ra.error());
        auto dry = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "act", "notes", "main/dark", "toggle", "--dry-run"});
        auto rd = cli::agent_request(dry->subcommand()->get());
        expect(rd && *rd == R"({"op":"act","node":"main/dark","action":"toggle","actor":"agent:omui-cli","dry_run":true})") << rd.value_or(rd.error());
        auto undo = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "undo", "notes", "--scope", "main/panel"});
        auto ru = cli::agent_request(undo->subcommand()->get());
        expect(ru && *ru == R"({"op":"undo","scope":"main/panel","actor":"agent:omui-cli"})") << ru.value_or(ru.error());
        auto txn = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "transaction", "notes", R"([{"node":"main/a","action":"toggle"}])", "--label",
                                                         "both", "--dry-run"});
        auto rt = cli::agent_request(txn->subcommand()->get());
        expect(rt && *rt == R"({"op":"transaction","steps":[{"node":"main/a","action":"toggle"}],"label":"both","actor":"agent:omui-cli","dry_run":true})") << rt.value_or(rt.error());
        auto not_list = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "transaction", "notes", R"({"node":"main/a"})"});
        expect(!cli::agent_request(not_list->subcommand()->get()).has_value()) << "the steps are a list";
        auto wait = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "wait", "notes"});
        expect(!cli::agent_request(wait->subcommand()->get()).has_value()) << "wait for what?";
        auto bad = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "act", "notes", "x", "y", "--args", "{oops"});
        expect(!cli::agent_request(bad->subcommand()->get()).has_value());
        expect(omui_cmd({"agent", "find", "nobody-listens", "--dir", "/nonexistent/openmui"}).code == 1);
        expect(omui_cmd({"frobnicate"}).code == 2);
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
                                 {"agent", "find", "notes-cli", "--name", "Delete*", "--dir", d},
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

    "omui check: located diagnostics, as text and as JSON"_test = [] {
        const auto dir = scratch_dir("omui-check");
        auto write = [&](std::string_view name, std::string_view text) {
            std::ofstream(dir / name, std::ios::binary) << text;
            return utf8(dir / name);
        };
        const std::string good = write("good.view.omui.json", R"({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "hi"}}}})");
        const std::string bad = write("bad.view.omui.json", "{\"omui\": \"0.1\",\n \"view\": {\"root\": {\"type\": \"buton\"}}}");
        const std::string component = write("search.view.omui.json", R"({"omui": "0.1", "component": {"name": "search_field",
            "interface": {"props": {"query": {"type": "string", "bindable": true}}},
            "root": {"type": "text_box", "id": "input", "props": {"text": {"bind": "query"}}}}})");
        const std::string user = write("page.view.omui.json", R"({"omui": "0.1", "view": {"root": {"type": "search_field", "id": "find"}}})");
        auto r = omui_cmd({"check", good});
        expect(eq(r.code, 0));
        expect(r.out.empty() && r.err.find("0 errors, 0 warnings in 1 file") != std::string::npos) << r.err;
        r = omui_cmd({"check", good, bad});
        expect(eq(r.code, 1));
        expect(eq(r.out, bad + ":2:28: error: no component is named `buton` (did you mean `button`?) [unknown-type]\n"));
        r = omui_cmd({"check", "--json", bad});
        expect(r.code == 1 && parse_json(r.out).has_value() && parse_json(r.out)->get("pointer")->as_string() == "/view/root/type") << r.out;
        // A document that uses another document's component, given in either order.
        r = omui_cmd({"check", user, component});
        expect(r.code == 0) << r.out + r.err;
    };

    "omui fmt rewrites to the canonical format; --check only reports"_test = [] {
        const auto dir = scratch_dir("omui-fmt");
        const auto file = dir / "a.view.omui.json";
        std::ofstream(file, std::ios::binary) << R"({"view":{"root":{"type":"text"}},"omui":"0.1"})";
        auto r = omui_cmd({"fmt", "--check", utf8(file)});
        expect(r.code == 1 && r.out == utf8(file) + "\n") << r.out;
        expect(eq(omui_cmd({"fmt", utf8(file)}).code, 0));
        std::ifstream in(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        expect(eq(text, std::string("{\n  \"omui\": \"0.1\",\n  \"view\": {\n    \"root\": {\"type\": \"text\"}\n  }\n}\n")));
        expect(eq(omui_cmd({"fmt", "--check", utf8(file)}).code, 0));
    };

    "omui schema is the committed spec/schema/view.schema.json"_test = [] {
        const auto committed = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() /
                               "spec" / "schema" / "view.schema.json";
        std::ifstream in(committed, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto r = omui_cmd({"schema"});
        expect(eq(r.code, 0));
        expect(!text.empty() && r.out == text) << "regenerate it: omui schema --out spec/schema/view.schema.json";
    };

    "omui video: a description and a view rendered to files, a still, a probe, the timing; problems located"_test = [] {
        const auto dir = scratch_dir("omui-video");
        // As its author writes it: a TypeScript data module the command evaluates.
        std::ofstream(dir / "card.video.omui.ts", std::ios::binary)
            << "import { defineVideo } from \"omui/define\";\n\nexport default defineVideo({ width: 64, height: 36, fps: 30, seconds: 1, from: { card: \"sync\" }, audio: { beeps: true } });\n";
        auto r = omui_cmd({"video", "render", utf8(dir / "card.video.omui.ts"), "--out", utf8(dir / "out" / "card.y4m")});
        expect(r.code == 0) << r.out + r.err;
        auto v = parse_json(r.out);
        expect(v && v->get("frames")->as_int() == 30 && v->get("samples")->as_int() == 48000 && v->get("files")->list()->size() == 2) << r.out;
        r = omui_cmd({"video", "probe", utf8(dir / "out" / "card.y4m")});
        v = parse_json(r.out);
        expect(r.code == 0 && v && v->get("frames")->as_int() == 30 && v->get("fps")->as_string() == "30") << r.out;
        r = omui_cmd({"video", "probe", utf8(dir / "out" / "card.wav")});
        v = parse_json(r.out);
        expect(r.code == 0 && v && v->get("samples")->as_int() == 48000) << r.out;
        // WebM: VP9 and Opus, read back.
        r = omui_cmd({"video", "render", utf8(dir / "card.video.omui.ts"), "--out", utf8(dir / "out" / "card.webm"), "--speed", "8"});
        expect(r.code == 0) << r.out + r.err;
        r = omui_cmd({"video", "probe", utf8(dir / "out" / "card.webm")});
        v = parse_json(r.out);
        expect(r.code == 0 && v && v->get("frames")->as_int() == 30 && v->get("samples")->as_int() == 48000 && v->get("video")->as_string() == "V_VP9" &&
                  v->get("audio")->as_string() == "A_OPUS" && v->get("bt709")->as_bool()) << r.out;
        r = omui_cmd({"video", "still", utf8(dir / "card.video.omui.ts"), "--at", "0.5", "--out", utf8(dir / "f15.png")});
        v = parse_json(r.out);
        expect(r.code == 0 && v && v->get("frame")->as_int() == 15 && std::filesystem::exists(dir / "f15.png")) << r.out + r.err;

        // A view on the headless host's clock, the same pixels twice.
        std::ofstream(dir / "hello.view.omui.json", std::ios::binary) << R"({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "openmui"}}}})";
        std::array<std::string, 2> hashes;
        for (int k = 0; k < 2; ++k) {
            r = omui_cmd({"video", "render", utf8(dir / "hello.view.omui.json"), "--size", "160x90", "--frames", "3", "--out", utf8(dir / std::format("view{}/", k))});
            v = parse_json(r.out);
            expect(r.code == 0 && v && v->get("files")->list()->size() == 3) << r.out + r.err;
            if (v && v->get("files")->list()->size() == 3) hashes[static_cast<std::size_t>(k)] = std::string(v->get("files")->list()->at(2).get("hash")->as_string());
        }
        expect(!hashes[0].empty() && hashes[0] == hashes[1]);

        r = omui_cmd({"video", "timing", "--fps", "30000/1001", "--frames", "5"});
        v = parse_json(r.out);
        expect(r.code == 0 && v && v->get("samples")->as_int() == 8008) << r.out;
        std::ofstream(dir / "bad.video.omui.json", std::ios::binary) << "{\"video\": {\n  \"width\": 64,\n  \"height\": 36,\n  \"fsp\": 30,\n  \"seconds\": 1\n}}";
        r = omui_cmd({"video", "check", utf8(dir / "bad.video.omui.json")});
        v = parse_json(r.out);
        expect(r.code == 1 && v && v->get("problems")->list()->at(0).get("line")->as_int() == 4) << r.out;
        expect(eq(omui_cmd({"video", "render", utf8(dir / "card.video.omui.ts")}).code, 2));
    };

    "omui connect and omui page: a page pairs with the code, the agent's requests reach it as the agent's, its answers come back; strangers refused"_test = [] {
        const auto dir = scratch_dir("omui-bridge");
        // Before any bridge: --wait waits for one to come (an agent starts `omui connect` and waits at once), then says none came.
        const auto before = std::chrono::steady_clock::now();
        auto none = omui_cmd({"--dir", omui::utf8(dir), "page", "status", "--wait", "--timeout", "0.5"});
        expect(none.code == 1 && std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(400)) << none.out + none.err;
        std::atomic<std::uint16_t> port{0};
        std::jthread bridge([&](std::stop_token stop) {
            auto r = cli::run_bridge({.code = "ABCD-EFGH", .name = "Claude Code", .any_port = true, .dir = dir}, stop, [&](std::uint16_t p) { port = p; });
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
