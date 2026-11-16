// mlibs.agent:host — the tool kernel's entries (T0, RC3.8, D-203): one session, reached the same way from a
// command line, from an MCP client and over the agent protocol itself.
//
// Every entry makes the same request and sends it to the same session, so an operation decides, runs, records
// and answers the same whichever way it was asked: the command line `world.observe --budget 400` is the request
// {"op":"world.observe","budget":400}; MCP's tools/call of `world_observe` with {"budget":400} is that request too.
// What an entry adds is only its framing — records printed a line each and the result's exit code; or JSON-RPC
// 2.0, one message a line, each tool's schema the catalog's own (`<product>.ops.json`'s `tools`).
//
//   tool subjects                          who is here (as the protocol's `subjects`)
//   tool describe world                    a subject briefly; `describe world.act` one operation whole
//   tool find "set something on fire"      what answers an intent
//   tool world.act --at '[3,4]' --name fire
//   tool --request '{"op":"world.act","at":[3,4]}'
export module mlibs.agent:host;

import std;
import :endpoint;

export namespace mlibs::agent {

// The request a command line's arguments make: an operation's name and its arguments as `--name value` (a value
// that reads as JSON is that value — a number, true, an array; else it is text; `--name` alone is true; a name given
// again gathers its values into an array: `--at 0 --at 1` is "at":[0,1]), or one
// request whole after `--request`. The session's own: `subjects`, `describe <subject or operation>`, `find <words>`,
// `log`. Why the arguments make none, else.
[[nodiscard]] std::expected<std::string, std::string> request_from(std::span<const std::string_view> arguments);

// A request's answer, whole: its records as sent, and the result's exit code.
struct Answered {
    std::vector<std::string> records;
    int                      exit_code = 0;
    bool                     finished = false;  // a result came (else the session did not answer in time)
};
// A request sent and its answer taken: the session pumped until the result comes, for at most `wait`.
[[nodiscard]] Answered answer_to(Session& session, std::string_view request, std::chrono::milliseconds wait = std::chrono::seconds(30));

// The command line's entry: the request the arguments make, sent; its records printed one a line on `out`; the
// result's exit code returned (2: the arguments make no request, said on `errors` with the way to write one).
int command_line(Session& session, std::span<const std::string_view> arguments, std::ostream& out, std::ostream& errors);

// What an MCP client is told the server is.
struct McpServer {
    std::string name = "mlibs-agent", version = "0.1.0";
    std::string instructions;  // empty: how to begin (subjects, describe, find), the session's own
};
// An operation's name as an MCP tool's (letters, digits, `_` and `-`: `world.act` → `world_act`).
[[nodiscard]] std::string tool_name(std::string_view operation);
// One JSON-RPC message (a line) answered: initialize, ping, tools/list (each operation of the session's subjects
// a tool, with its catalog's schema, and the session's own four: subjects, describe, find, log), tools/call (its
// request sent to the session; its data the result's content, an error or a failed result `isError`). Empty for a
// notification.
[[nodiscard]] std::string mcp_reply(Session& session, std::string_view message, const McpServer& server = {});
// The MCP entry over a stream (stdio): every line answered until the input ends; 0.
int serve_mcp(Session& session, std::istream& in, std::ostream& out, const McpServer& server = {});

}  // namespace mlibs::agent
