// openmui.cli — the commands of `omui`, as functions over parsed arguments,
// so tests run them without a process. Each writes its output (envelope
// records as JSON lines, or tables) to `out` and answers the exit code,
// which for agent operations is the envelope's (0 done, 1 failed,
// 2 needs an answer, 3 not ready) — the openas convention.
export module omui.cli;

import std;
import mcpplibs.cmdline;
import omui.viewdoc;

export namespace omui::cli {

// The whole command line: `omui <command> …`.
[[nodiscard]] mcpplibs::cmdline::App app();

// Runs a command line (argv[0] included); writes to `out` and `err`.
int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err);

// The request an `omui agent <op>` command sends, from its arguments
// (exposed for tests); an error message when the arguments do not make one.
// `first`: where the operation's own positional arguments begin (1 for `omui agent <op> <app>`, 0 for `omui page <op>`).
[[nodiscard]] std::expected<std::string, std::string> agent_request(const mcpplibs::cmdline::ParsedArgs& op, std::size_t first = 1);

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

}  // namespace omui::cli

// The document commands (documents.cpp), for run().
namespace omui::cli {

[[nodiscard]] mcpplibs::cmdline::App check_command();
[[nodiscard]] mcpplibs::cmdline::App fmt_command();
[[nodiscard]] mcpplibs::cmdline::App schema_command();
int check(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);
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
int check_project(bool json, std::ostream& out, std::ostream& err);
int fmt(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);
int schema(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);
// The official components, registered once (documents and views use them).
void register_components();

// omui connect and omui page (page.cpp, agent connect §4): the bridge a page pairs with, and the paired page operated.
[[nodiscard]] mcpplibs::cmdline::App connect_command();
[[nodiscard]] mcpplibs::cmdline::App page_command(mcpplibs::cmdline::App operations);
int connect(const mcpplibs::cmdline::ParsedArgs& p, std::optional<std::filesystem::path> dir, std::ostream& out, std::ostream& err);
int page(const mcpplibs::cmdline::ParsedArgs& p, std::optional<std::filesystem::path> dir, std::ostream& out, std::ostream& err);

// omui agent sweep and omui page sweep (sweep.cpp): every action of every
// operable node performed through `call` (one request, its records).
int sweep(const std::function<std::vector<Value>(const Value&)>& call, const mcpplibs::cmdline::ParsedArgs& a, std::ostream& out, std::ostream& err);

// omui video (video.cpp).
[[nodiscard]] mcpplibs::cmdline::App video_command();
int video(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);

// omui audio (audio.cpp): offline automation audition and visual inspection.
[[nodiscard]] mcpplibs::cmdline::App audio_command();
int audio(const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);

// omui new, dev, run, build, test, doctor (project.cpp, D-150): a project over mcpp.
inline constexpr std::array<std::string_view, 6> project_commands{"new", "dev", "run", "build", "test", "doctor"};
[[nodiscard]] mcpplibs::cmdline::App project_command(std::string_view name);
int project(std::string_view name, const mcpplibs::cmdline::ParsedArgs& p, std::ostream& out, std::ostream& err);

}  // namespace omui::cli
