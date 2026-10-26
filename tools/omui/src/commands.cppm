// openmui.cli — the commands of `omui`, as functions over parsed arguments,
// so tests run them without a process. Each writes its output (envelope
// records as JSON lines, or tables) to `out` and answers the exit code,
// which for agent operations is the envelope's (0 done, 1 failed,
// 2 needs an answer, 3 not ready) — the openas convention.
export module openmui.cli;

import std;
import mcpplibs.cmdline;

export namespace omui::cli {

// The whole command line: `omui <command> …`.
[[nodiscard]] mcpplibs::cmdline::App app();

// Runs a command line (argv[0] included); writes to `out` and `err`.
int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err);

// The request an `omui agent <op>` command sends, from its arguments
// (exposed for tests); an error message when the arguments do not make one.
[[nodiscard]] std::expected<std::string, std::string> agent_request(const mcpplibs::cmdline::ParsedArgs& op);

[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> base64_decode(std::string_view text);

}  // namespace omui::cli
