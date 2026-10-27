// openmui.mcp — an MCP server for any running openmui application.
//
// The Model Context Protocol (2025-06-18) over a transport of JSON-RPC 2.0
// messages, one a line (stdio). The server offers tools an agent calls:
//
//   ui_apps      the applications running (their agent endpoints)
//   ui_snapshot  an application's semantic tree, as an outline
//   ui_find      nodes by role, name, state, place
//   ui_act       an action of a node (activate, toggle, set_value …)
//   ui_invoke    an application verb (notes.new …)
//   ui_wait      until nodes appear, go, or the interface settles
//   ui_capture   pixels of the window or a node (an image)
//
// Each speaks openmui.tree@0.1 to the application's endpoint (openmui.agent).
// An action the application marks irreversible or external needs an answer
// (S-1): the server asks the person — by an elicitation when the client can
// show one — and acts again only with their yes, the actor recorded as
// `agent:<client>;via=person:user`; a client without elicitation is told to
// ask the user. The agent never answers for the person.
export module openmui.mcp;

import std;
import openmui.core;
import openmui.agent;

export namespace omui::mcp {

// The protocol versions served, newest first (a client asking for one of
// them gets it; otherwise the newest).
inline constexpr std::array<std::string_view, 3> protocol_versions{"2025-06-18", "2025-03-26", "2024-11-05"};

struct ServerOptions {
    std::string           app;        // the application a call names none of ("": the one running)
    std::filesystem::path directory;  // where endpoints are (empty: agent::default_directory())
    // The actor's kind:name; the client's own name (initialize's clientInfo)
    // when empty: `agent:claude-code`.
    std::string           actor;
    std::chrono::milliseconds timeout{30'000};  // a request to an application
    // Every tool call and its outcome, as the server sees them (a log a
    // cockpit shows, or a test reads); called on the serving thread.
    std::function<void(const Value& entry)> journal;
};

// What carries the messages: one JSON-RPC message a line, either way.
class Transport {
public:
    virtual ~Transport() = default;
    // The next line; nothing at the end.
    [[nodiscard]] virtual std::optional<std::string> read() = 0;
    virtual void write(std::string_view line) = 0;
};

// This program's standard input and output (bytes: no line-end translation).
class StdioTransport final : public Transport {
public:
    [[nodiscard]] std::optional<std::string> read() override;
    void write(std::string_view line) override;

private:
    std::string buffer_;
    bool        ended_ = false;
};

class Server {
public:
    explicit Server(ServerOptions options = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Serves until the transport ends.
    void serve(Transport& transport);
    // One message: a request is answered, a notification taken, a response
    // matched to the request the server made.
    void handle(std::string_view line, Transport& transport);

    // tools/list's tools: name, title, description, input schema, hints.
    [[nodiscard]] static Value::List tools();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The text an agent reads of semantic nodes (snapshot or find's): one line a
// node, indented under its parent — `id role "name" = value [states]
// {action, action (irreversible)}`.
[[nodiscard]] std::string outline(const Value::List& nodes);

}  // namespace omui::mcp
