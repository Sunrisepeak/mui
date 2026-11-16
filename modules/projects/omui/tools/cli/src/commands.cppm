// openmui.cli — the commands of `omui`, as functions over parsed arguments,
// so tests run them without a process. Each writes its output (envelope
// records as JSON lines, or tables) to `out` and answers the exit code,
// which for agent operations is the envelope's (0 done, 1 failed,
// 2 needs an answer, 3 not ready) — the openas convention.
export module omui.cli;

// The commands an agent may run through MCP (cli.ops.xdef.ts).
export import :catalog;

import std;
import mlibs.project;
export import :omui_profile;  // omui's profiles as data (omui.profile.xdef.ts)
export import mlibs.agent;
import omui.core;
import omui.viewdoc;

export namespace omui::cli {


// Runs a command line (argv[0] included); writes to `out` and `err`.
int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err);

// A running application reached — omui agent: its endpoint; omui page: the bridge to the paired page — a request
// sent and its records taken, waiting at most `limit`.
using Transport = std::function<std::vector<Value>(const Value& request, std::chrono::milliseconds limit)>;
// `omui agent <op> <app> …` (the app taken out) and `omui page <op> …` (running.cpp): the operation's words through the
// tool kernel's command line over the protocol's catalogs (ui.*, design.*, generate.*, task.*); `--out FILE` keeps a
// picture, its vectors or a trace the answer carries in that file. The exit code is the answer's.
int operate_running(std::vector<std::string> words, const Transport& send, std::ostream& out, std::ostream& err);
// `call`: one request of the protocol's as it is (the first word, a JSON object), or why the words make none.
[[nodiscard]] std::expected<Value, std::string> raw_request(std::span<const std::string> words);
// The records an application answered, a line each as it sent them (without `re`); the result's exit code.
int print_records(std::vector<Value> records, std::ostream& out);

// A bridge (omui connect, agent connect §4) run until `stop`: the pairing
// code, who the agent is, the origin it serves (empty: any), the port
// (0: the first free of 47110–47113; any_port: one the system chooses) and
// where its bridge.json goes (the per-user directory unless given). `ready`
// hears the port once it listens; answers it when stopped.
struct BridgeStart {
    std::string                          code, name, origin;
    std::uint16_t                        port = 0;
    bool                                 any_port = false;
    std::optional<std::filesystem::path> dir;
};
std::expected<std::uint16_t, std::string> run_bridge(const BridgeStart& start, std::stop_token stop, const std::function<void(std::uint16_t)>& ready);


// A command of the command line, run for an agent (MCP's omui_* tools, the
// omui-cli listing): its exit code and what it wrote, its tools' output
// caught (standard output is the protocol's).
[[nodiscard]] Value run_for_agent(std::string_view command, const Value& args);
}  // namespace omui::cli

// The document commands (documents.cpp), for run().
namespace omui::cli {

// A data file as the JSON its reader takes (D-139): a TypeScript data module
// (`*.xdef.ts`) evaluated, anything else read as it is (project.cpp).
[[nodiscard]] std::expected<std::string, std::string> data_text(const std::filesystem::path& file);

// A diagnostic and the file it is about: what `omui check` prints, whichever check found it.
struct Found {
    std::string                   file;
    omui::viewdoc::Diagnostic     diagnostic;
};
// The view documents' diagnostics (component documents defined first); nothing when one cannot be read.
[[nodiscard]] std::optional<std::vector<Found>> check_documents(const std::vector<std::string>& files, std::ostream& err);
// Prints them (text, or a JSON record each), the count to `err`; 1 when one is an error.
int report(const std::vector<Found>& found, std::size_t files, bool json, std::ostream& out, std::ostream& err);
// `omui check` with no files: the project here (typecheck.cpp).
int check_project(bool json, bool portable, std::ostream& out, std::ostream& err);
// omui.describe: the project's API (typecheck.cpp), also written as llms.txt to `llms` when given.
int describe(const std::optional<std::string>& llms, std::ostream& out, std::ostream& err);
// omui.fmt and omui.schema: view documents in the canonical format; their JSON Schema.
int fmt(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err);
int schema(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err);
// The official components, registered once (documents and views use them).
void register_components();

// omui connect and omui page (page.cpp, agent connect §4): the bridge a page pairs with, and the paired page operated.
int connect(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err);
// omui mcp (agent.cpp): the MCP server on this process's standard streams, until they end.
int serve_mcp(const mlibs::agent::Arguments& a, std::ostream& err);
int page(std::vector<std::string> words, std::optional<std::filesystem::path> dir, std::ostream& out, std::ostream& err);

// omui agent sweep and omui page sweep (sweep.cpp): every action of every
// operable node performed through `call` (one request, its records): under `root` (empty: all), as `actor`.
struct Sweeping {
    std::string root, actor = "agent:omui-sweep";
};
[[nodiscard]] Sweeping sweeping(std::span<const std::string> words);  // --root ID, --actor ACTOR
int sweep(const std::function<std::vector<Value>(const Value&)>& call, const Sweeping& how, std::ostream& out, std::ostream& err);

// omui video (video.cpp).
// omui.video.render, .still, .check, .probe, .timing (`name`: the last part).
int video(std::string_view name, const mlibs::agent::Arguments& p, std::ostream& out, std::ostream& err);

// omui audio (audio.cpp): offline automation audition and visual inspection.
int audio(const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err);

// The project's commands (project.cpp, D-150, RC3.9): a project over mcpp. Its catalog's operations (cli.ops.xdef.ts)
// are the subject `omui` — `omui build --release` is omui.build through the tool kernel's command line, and `omui mcp`
// serves them as tools; dev, run, connect and mcp hold a window or the terminal (`terminal`): a person's command line runs them.
// One of the catalog's operations (omui.build …), its records to `out` and what went wrong to `err`; the exit code.
int operation(std::string_view op, const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err);
// omui's profiles over the tool kernel's project model (profiles.cpp): general, workspace, app, extension.
std::vector<mlibs::project::Profile> profiles();
export class Project final : public mlibs::agent::Subject {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "omui"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "omui.cli@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return ops::catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return ops::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // its state is the project's files
    void operate(std::string_view op, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;
    // The command line's own streams: what holds the terminal writes there as it goes (none: this process's).
    std::ostream* live_out = nullptr;
    std::ostream* live_errors = nullptr;
};
// omui design sync (design.cpp): Live Design's history (.omui/design) carried into git as design/<branch>.
int design_sync(const std::filesystem::path& app, bool json, std::ostream& out, std::ostream& err);

}  // namespace omui::cli

namespace omui::cli {

// While an agent runs a command (run_for_agent), the tools it starts write to
// what the command writes, not to this process's terminal.
bool& tools_caught();

}  // namespace omui::cli
