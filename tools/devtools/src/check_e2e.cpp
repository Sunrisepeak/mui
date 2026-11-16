// The end-to-end steps (moved from tools/ci/tools-e2e.sh and agent-e2e.sh): command lines as an Agent uses them,
// each held to its exit status and its JSON answer.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

namespace {

// The programs of a build, by file name; or the first one it did not report.
std::expected<std::map<std::string, std::string>, std::string> programs(const fs::path& root, std::vector<std::string> args,
                                                                         std::initializer_list<std::string_view> wanted) {
    const auto built = run::build(root, std::move(args));
    if (!built) return std::unexpected(built.error());
    std::map<std::string, std::string> out;
    for (const auto& artifact : *built)
        if (artifact.kind == "bin") out[mcpplibs::os::fs::utf8(artifact.path.filename())] = mcpplibs::os::fs::utf8(artifact.path);
    for (const auto name : wanted)
        if (!out.contains(std::string(name))) return std::unexpected(std::format("the build reported no {}", name));
    return out;
}

fs::path scratch(std::string_view stem) { return fs::temp_directory_path() / std::format("{}-{}", stem, std::random_device{}()); }

// A program serving agents from `directory`, ended however the step ends; ready once its endpoint is there.
class Served {
public:
    Served(const fs::path& program, std::vector<std::string> args, const fs::path& log) {
        namespace process = mcpplibs::os::process;
        process::spawn_options options;
        options.program = program;
        options.args = std::move(args);
        options.in = process::stdio::null;
        options.out = process::stdio::pipe;
        options.err = process::stdio::output;
        auto started = process::process::spawn(options);
        if (!started) return;
        running_ = std::make_unique<process::process>(std::move(*started));
        logger_ = std::jthread([this, log] {
            std::ofstream file(log, std::ios::binary);
            for (;;) {
                auto chunk = running_->read_output();
                if (!chunk || chunk->empty()) break;
                file << *chunk << std::flush;
            }
        });
    }
    ~Served() {
        if (!running_) return;
        running_->kill();
        static_cast<void>(running_->wait());
    }
    [[nodiscard]] bool started() const { return running_ != nullptr; }

    static bool ready(const fs::path& endpoint) {
        for (int look = 0; look < 100; ++look) {
            if (fs::exists(endpoint)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

private:
    std::unique_ptr<mcpplibs::os::process::process> running_;
    std::jthread logger_;
};

std::string bytes_of(const fs::path& file) { return mcpplibs::os::fs::read(file).value_or(""); }

}  // namespace

int tools_e2e(const fs::path& root, const std::vector<std::string>& profile_args) {
    // The products' commands as an Agent uses them (convergence plan §10.3): each operation of a catalogue through its
    // real command line — hanim's animation tool, xrender's command (the GPU ones on the row's device: lavapipe or
    // Metal, as omui-devtools gpu sets) and the timeline's (timelines edited and taken back to the very bytes).
    std::vector<std::string> args{"-p", "hanim.studio", "-p", "xrender.tool", "-p", "hanim.timeline.tool", "-p", "omui.gen"};
    args.insert(args.end(), profile_args.begin(), profile_args.end());
    const auto built = programs(root, std::move(args), {"hanim", "xrender", "hanim-timeline", "omui-gen"});
    if (!built) return failed(built.error());
    const auto& bin = *built;
    const fs::path dir = scratch("omui-tools-e2e");
    fs::create_directories(dir);
    const Removed removed{dir};
    const auto at = [&](std::string_view name) { return mcpplibs::os::fs::utf8(dir / name); };
    Expect expect(root);

    const std::string document = "modules/projects/hanim/studio/examples/bounce.animation.omui.json";
    const std::string hanim = bin.at("hanim");
    expect.status(0, hanim, {"document.check", "--file", document}) && expect.has(R"("markers":\[\{"name":"landed")");
    expect.status(0, hanim, {"document.sample", "--file", document, "--at", "0", "--at", "1.0"}) && expect.has(R"("id":"ball")");
    expect.status(0, hanim, {"document.render", "--file", document, "--out", at("bounce"), "--frames", "4"}) &&
        expect.holds(fs::file_size(dir / "bounce-003.png") > 0 && fs::file_size(dir / "bounce.strip.png") > 0 &&
                         fs::file_size(dir / "bounce.report.omui.json") > 0,
                     "hanim document.render wrote its frames, strip and report");
    std::ofstream(dir / "bad.animation.omui.json")
        << R"({"version": 1, "objects": [{"id": "a", "shape": {"circle": 5}, "fill": "#fff"}], "program": {"do": "twirl", "object": "a"}})" << '\n';
    expect.status(1, hanim, {"document.check", "--file", at("bad.animation.omui.json")}) && expect.has(R"("where":"/program/do")");
    expect.status(1, hanim, {"document.render", "--file", document}) && expect.has(R"("code":"invalid-argument")");
    // A command by its old name is no operation: the way out names the one meant.
    expect.status(1, hanim, {"render"}) && expect.has(R"("code":"not-found")") && expect.has(R"("operation":"document.render")");
    expect.status(2, hanim, {"--file", document});

    const std::string effects = "modules/projects/xrender/effect/tests/data", xrender = bin.at("xrender");
    expect.status(0, xrender, {"device.describe"});
    expect.status(0, xrender, {"effect.check", "--files", effects + "/scanlines.effect.omui.json"}) && expect.has(R"("name":"strength")");
    expect.status(1, xrender, {"effect.check", "--files", effects + "/greedy.effect.omui.json"}) && expect.has("binds resources of its own");
    expect.status(0, xrender, {"effect.preview", "--file", effects + "/scanlines.effect.omui.json", "--out", at("scan.png"), "--param", "strength=0.7"}) &&
        expect.holds(fs::exists(dir / "scan.png") && fs::file_size(dir / "scan.png") > 0, "the preview was written");
    expect.status(0, xrender, {"conformance.run", "--report", at("render.cts.omui.json")}) && expect.has(R"("failed":\[\])");
    expect.status(1, xrender, {"frobnicate"}) && expect.has(R"("code":"not-found")");
    expect.status(2, xrender, {"--report", "x"});

    const fs::path timeline = dir / "short.timeline.omui.json";
    const std::string tl = bin.at("hanim-timeline"), file = mcpplibs::os::fs::utf8(timeline);
    expect.status(0, bin.at("omui-gen"), {"snapshot", "showcase/promo-video/short.timeline.omui.ts", "--out", file});  // the JSON the tool works on
    const std::string before = bytes_of(timeline);
    expect.status(0, tl, {"timeline.check", "--file", file}) && expect.has(R"("length":"900f")");
    expect.status(0, tl, {"timeline.at", "--file", file, "--time", "1s"}) && expect.has(R"("clip":"hook")");
    expect.status(0, tl, {"timeline.edit", "--file", file, "--edit", "split", "--track", "0", "--clip", "0", "--at", "15f", "--dry_run"}) &&
        expect.holds(bytes_of(timeline) == before, "a dry run leaves the file as it was");
    expect.status(0, tl, {"timeline.edit", "--file", file, "--edit", "split", "--track", "0", "--clip", "0", "--at", "15f"}) && expect.holds(bytes_of(timeline) != before, "the edit changed the file");
    expect.status(0, tl, {"timeline.check", "--file", file});
    expect.status(1, tl, {"timeline.edit", "--file", file, "--edit", "split", "--track", "0", "--clip", "0", "--at", "99999s"});
    expect.status(0, tl, {"timeline.undo", "--file", file}) && expect.holds(bytes_of(timeline) == before, "undo took the file back to the very bytes");
    expect.status(1, tl, {"timeline.undo", "--file", file});
    return expect.result("tools e2e: ok");
}

int agent_e2e(const fs::path& root, const std::vector<std::string>& profile_args) {
    // M1 acceptance ② (implementation plan §19.2): omui agent snapshot / find / act / wait operate example 07 running as
    // its own process, and an irreversible action without its answer ends in needs-answer (exit 2); then the
    // development operations against the same process, the MCP server over real pipes, and an application's verbs as
    // MCP tools from its generated listing.
    std::vector<std::string> args{"-p", "omui.example.headless-and-agent", "-p", "omui.cli", "-p", "omui.inspector"};
    args.insert(args.end(), profile_args.begin(), profile_args.end());
    const auto built = programs(root, std::move(args), {"notes-agent", "omui", "omui-inspector"});
    if (!built) return failed(built.error());
    const auto& bin = *built;
    const fs::path dir = scratch("omui-agent-e2e");
    fs::create_directories(dir);
    const Removed removed{dir};
    const std::string agents = mcpplibs::os::fs::utf8(dir), omui = bin.at("omui");

    // Dual-user, whatever the build profile: a dev build otherwise starts in development mode, where a trusted
    // session's declared operations run without asking (D-155), and the needs-answer path (S-10, S-11) is checked here.
    const Served app(bin.at("notes-agent"), {"--serve", "--agent-dir", agents, "--agent-mode", "dual-user"}, dir / "app.log");
    const std::string id = "org.openmui.examples.notes";
    if (!app.started() || !Served::ready(dir / (id + ".endpoint.omui.json"))) return failed("example 07 did not serve (its log: app.log)");
    Expect expect(root);
    const auto agent = [&](int wanted, std::vector<std::string> words) {
        words.insert(words.begin(), "agent");
        words.insert(words.end(), {"--dir", agents});
        return expect.status(wanted, omui, std::move(words));
    };
    agent(0, {"profile", id, "start"});
    agent(0, {"ls"}) && expect.has("(^|\\n)org\\.openmui\\.examples\\.notes");
    agent(0, {"snapshot", id, "--interactive"});
    agent(0, {"find", id, R"({"role":"button","name":"Delete*"})"}) && expect.has(R"("effect":"irreversible")");
    agent(0, {"act", id, "main/compose/draft", "set_value", "--args", R"({"value":"From the shell"})"});
    agent(0, {"act", id, "main/compose/add", "activate"});
    agent(0, {"wait", id, "--q", R"({"name":"3 notes"})", "--timeout", "5"});
    agent(2, {"act", id, "main/delete-all", "activate", "--actor", "agent:cleaner;via=person:alice"}) && expect.has("needs-answer");
    agent(0, {"find", id, R"({"name":"3 notes"})"});
    agent(0, {"act", id, "main/delete-all", "activate", "--answer", "delete"});
    agent(0, {"wait", id, "--q", R"({"name":"0 notes"})", "--settled", "--timeout", "5"});
    agent(0, {"capture", id, "--out", mcpplibs::os::fs::utf8(dir / "after.png")}) &&
        expect.holds(fs::exists(dir / "after.png") && fs::file_size(dir / "after.png") > 0, "the capture was written");

    // Development: what the running application is doing.
    agent(0, {"inspect", id, "main/compose/add"}) && expect.has(R"("type":"button")") && expect.has(R"("source":"[^"]*notes)");
    agent(0, {"stats", id}) && expect.has(R"("p95_ms")");
    agent(0, {"debug", id, "--bounds", "--hud", "--slow-frame-ms", "0.001", "--log", "frame=debug,*=info"});
    agent(0, {"act", id, "main/compose/draft", "set_value", "--args", R"({"value":"traced"})"});
    agent(0, {"wait", id, "--settled", "--timeout", "5"});
    agent(0, {"capture", id, "--out", mcpplibs::os::fs::utf8(dir / "debug.png")});
    agent(0, {"logs", id, "--category", "frame", "--limit", "5"}) && expect.has(R"("category":"frame")");
    agent(0, {"debug", id, "--bounds", "false", "--hud", "false"});
    agent(0, {"profile", id, "stop", "--chrome", "--out", mcpplibs::os::fs::utf8(dir / "run.trace.json")});
    const std::string trace = bytes_of(dir / "run.trace.json");
    expect.holds(trace.contains(R"("traceEvents")") && trace.contains(R"("name":"layout")"), "the profile is a Chrome trace with the layout stage");
    agent(0, {"profile", id, "start"});
    agent(0, {"profile", id, "stop"}) && expect.has(R"("summary")");

    // The application's time, controlled from outside (F4).
    agent(0, {"clock", id, "pause"});
    agent(0, {"clock", id, "step", "--seconds", "0.5"});
    agent(0, {"clock", id, "seek", "--seconds", "0"});
    agent(0, {"clock", id, "rate", "0.25"});
    agent(1, {"clock", id, "rate", "nope"});
    agent(0, {"clock", id, "release"}) && expect.has(R"("controlled":false)");

    // The frame's pipeline, read and changed in the running process (D-66).
    agent(0, {"pipeline", id}) && expect.has(R"("openmui\.view\.layout")") && expect.has(R"("stages"|"stage")");
    agent(0, {"pipeline", id, "remove", "--stage", "debug"});
    agent(0, {"pipeline", id, "insert", "--stage", "debug", "--plugin", "openmui.view.debug", "--after", R"(["paint"])"});
    agent(1, {"pipeline", id, "replace", "--stage", "paint", "--plugin", "nonesuch"});
    agent(0, {"wait", id, "--settled", "--timeout", "5"});

    // The MCP server over real pipes, as Claude Code runs it: the tools listed, a node found, an irreversible action
    // refused to a client that cannot ask its user, a reversible one done.
    const std::string requests = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ui_find","arguments":{"name":"Delete*"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"ui_act","arguments":{"node":"main/delete-all","action":"activate"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"ui_act","arguments":{"node":"main/compose/draft","action":"set_value","args":{"value":"from MCP"}}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"ui_snapshot","arguments":{"root":"main/compose"}}}
)";
    const auto responses = [](const std::string& text) {
        std::size_t count = 0;
        for (std::size_t at = text.find(R"("jsonrpc":"2.0")"); at != std::string::npos; at = text.find(R"("jsonrpc":"2.0")", at + 1)) ++count;
        return count;
    };
    expect.status(0, omui, {"mcp", "--dir", agents}, {.input = requests}) && expect.holds(responses(expect.last()) == 6, "six responses from omui mcp") &&
        expect.has(R"("protocolVersion":"2025-06-18")") && expect.has(R"("name":"ui_capture")") && expect.has(R"("id":3.*main/delete-all)") &&
        expect.has(R"("id":4.*needs the user's confirmation.*"isError":true)") && expect.has(R"("id":5.*done: set_value on main/compose/draft)") &&
        expect.has(R"("id":6.*from MCP)");

    // An application's verbs as MCP tools from its generated listing (§10.3): the Inspector's Reaction lab, headless,
    // driven by cellular_* tools over real pipes — paint, advance, read back — with the listing's hints.
    const Served lab(bin.at("omui-inspector"), {"--cellular", "--headless", "--serve", "--agent-dir", agents}, dir / "lab.log");
    const std::string lab_id = "org.openmui.inspector.cellular";
    if (!lab.started() || !Served::ready(dir / (lab_id + ".endpoint.omui.json"))) return failed("the Reaction lab did not serve (its log: lab.log)");
    const std::string lab_requests = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e"}}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"cellular_snapshot","arguments":{}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"cellular_paint","arguments":{"x":10,"y":10,"radius":1,"brush":"spark"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"cellular_advance","arguments":{"count":3}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"cellular_paint","arguments":{"x":10,"y":10,"radius":99,"brush":"spark"}}}
)";
    expect.status(0, omui, {"mcp", "--dir", agents, "--app", lab_id, "--ops", "tools/inspector/src/catalog/inspector.ops.json"}, {.input = lab_requests}) &&
        expect.holds(responses(expect.last()) == 6, "six responses from omui mcp --ops") && expect.has(R"("name":"cellular_paint","title":"cellular\.paint")") &&
        expect.has(R"("name":"cellular_snapshot"[^}]*"inputSchema".*"readOnlyHint":true)") && expect.has(R"("id":3.*"tick":0)") &&
        expect.has(R"("id":4.*done: cellular\.paint)") && expect.has(R"("id":5.*done: cellular\.advance.*"tick":3)") &&
        expect.has(R"("id":6.*radius 0\.\.8.*"isError":true)");
    return expect.result("agent e2e: ok");
}

}  // namespace omui::devtools::check
