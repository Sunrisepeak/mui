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

    return s.run(argc, argv);
}
