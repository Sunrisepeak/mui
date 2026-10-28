// `omui agent` against a running application: a headless H3 session on this
// thread, the command line on another, talking over the real endpoint.
import std;
import openmui.cli;
import mcpplibs.cmdline;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
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

}  // namespace

int main(int argc, char** argv) {
    Suite s{"cli"};

    s.test("requests from arguments", [] {
        auto a = cli::app();
        auto p = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "find", "notes", R"({"role":"button"})", "--name", "Del*"});
        check(p.has_value());
        auto r = cli::agent_request(p->subcommand()->get());
        check(r.has_value() && *r == R"({"op":"find","q":{"role":"button","name":"Del*"}})", r.value_or(r.error()));
        auto act = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "act", "notes", "notes/delete", "activate", "--answer", "yes"});
        auto ra = cli::agent_request(act->subcommand()->get());
        check(ra && *ra == R"({"op":"act","node":"notes/delete","action":"activate","answer":"yes","actor":"agent:omui-cli"})", ra.value_or(ra.error()));
        auto wait = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "wait", "notes"});
        check(!cli::agent_request(wait->subcommand()->get()).has_value(), "wait for what?");
        auto bad = a.parse_from(mcpplibs::cmdline::Argv{"omui", "agent", "act", "notes", "x", "y", "--args", "{oops"});
        check(!cli::agent_request(bad->subcommand()->get()).has_value());
        auto b64 = cli::base64_decode("UE5HAA==");
        check(b64 && b64->size() == 4 && (*b64)[0] == 'P' && (*b64)[3] == 0);
        check(omui_cmd({"agent", "find", "nobody-listens", "--dir", "/nonexistent/openmui"}).code == 1);
        check(omui_cmd({"frobnicate"}).code == 2);
    });

    s.test("omui agent ls / snapshot / find / act / wait / capture against a running app", [] {
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
                             });
        check(r.size() == 7);
        check(r[0].code == 0 && r[0].out.starts_with("notes-cli\tnotes-cli\topenmui.tree@0.1"), r[0].out + r[0].err);
        check(r[1].code == 0 && r[1].out.find("notes/delete") != std::string::npos && r[1].out.find("notes/title") == std::string::npos);
        check(r[2].code == 0 && r[2].out.find(R"("effect":"irreversible")") != std::string::npos, r[2].out);
        check(r[3].code == 2 && r[3].out.find("needs-answer") != std::string::npos, std::format("{} {}", r[3].code, r[3].out));
        check(r[4].code == 0 && deleted == 1, r[4].out + r[4].err);
        check(r[5].code == 0, r[5].out + r[5].err);
        check(r[6].code == 0 && std::filesystem::exists(png) && std::filesystem::file_size(png) > 50, r[6].out + r[6].err);
        auto journal = h.runtime().action_journal();
        check(journal.size() >= 2 && journal[journal.size() - 2].actor == "agent:planner;via=person:alice");
    });

    s.test("omui check: located diagnostics, as text and as JSON", [] {
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
        check_eq(r.code, 0);
        check(r.out.empty() && r.err.find("0 errors, 0 warnings in 1 file") != std::string::npos, r.err);
        r = omui_cmd({"check", good, bad});
        check_eq(r.code, 1);
        check_eq(r.out, bad + ":2:28: error: no component is named `buton` (did you mean `button`?) [unknown-type]\n");
        r = omui_cmd({"check", "--json", bad});
        check(r.code == 1 && parse_json(r.out).has_value() && parse_json(r.out)->get("pointer")->as_string() == "/view/root/type", r.out);
        // A document that uses another document's component, given in either order.
        r = omui_cmd({"check", user, component});
        check(r.code == 0, r.out + r.err);
    });

    s.test("omui fmt rewrites to the canonical format; --check only reports", [] {
        const auto dir = scratch_dir("omui-fmt");
        const auto file = dir / "a.view.omui.json";
        std::ofstream(file, std::ios::binary) << R"({"view":{"root":{"type":"text"}},"omui":"0.1"})";
        auto r = omui_cmd({"fmt", "--check", utf8(file)});
        check(r.code == 1 && r.out == utf8(file) + "\n", r.out);
        check_eq(omui_cmd({"fmt", utf8(file)}).code, 0);
        std::ifstream in(file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        check_eq(text, std::string("{\n  \"omui\": \"0.1\",\n  \"view\": {\n    \"root\": {\"type\": \"text\"}\n  }\n}\n"));
        check_eq(omui_cmd({"fmt", "--check", utf8(file)}).code, 0);
    });

    s.test("omui schema is the committed spec/schema/view.schema.json", [] {
        const auto committed = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() /
                               "spec" / "schema" / "view.schema.json";
        std::ifstream in(committed, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto r = omui_cmd({"schema"});
        check_eq(r.code, 0);
        check(!text.empty() && r.out == text, "regenerate it: omui schema --out spec/schema/view.schema.json");
    });

    return s.run(argc, argv);
}
