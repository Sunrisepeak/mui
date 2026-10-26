// openmui.agent:endpoint — where agents find an application, and talk to it.
//
// Every application listens on a local endpoint (a Unix socket, a named pipe:
// mcpplibs.os.ipc) named after it, in one directory per user, and leaves a
// small descriptor beside it (`<name>.endpoint.omui.json`) so `omui agent ls`
// can list what runs. Connections are read on background threads; requests
// are served on the UI thread by `pump`, between frames, where the runtime
// may be touched.
export module openmui.agent:endpoint;

import std;
import openmui.core;
import openmui.view;
import :protocol;

export namespace omui::agent {

struct EndpointOptions {
    std::string           name;       // default: the app id, else "openmui-app"
    std::filesystem::path directory;  // default: default_directory()
    ProtocolOptions       protocol;
};

// The per-user directory endpoints live in: the runtime directory (XDG),
// else a temporary one, plus "openmui".
[[nodiscard]] std::filesystem::path default_directory();

struct EndpointInfo {
    std::string           name;
    std::string           app;
    std::string           interface_name;
    std::filesystem::path directory;
};

// The endpoints described in a directory (live or left over: connect to know).
[[nodiscard]] std::vector<EndpointInfo> list(const std::filesystem::path& directory = default_directory());

class Endpoint {
public:
    Endpoint(Runtime& runtime, EndpointOptions options = {});
    ~Endpoint();
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    // Starts listening (and writes the descriptor).
    std::expected<void, std::string> start();
    void stop();
    [[nodiscard]] bool listening() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

    // Serves what arrived and checks pending waits; call once per frame on
    // the UI thread. Answers how many requests it served.
    std::size_t pump();
    [[nodiscard]] std::size_t connections() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// A connection to an endpoint (the command line, tests, other processes).
class Client {
public:
    [[nodiscard]] static std::expected<Client, std::string> connect(std::string_view name,
                                                                    const std::filesystem::path& directory = default_directory());
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    ~Client();

    // Sends a request and returns its records, up to and including the
    // result (or an error record of its own after `timeout`).
    [[nodiscard]] std::vector<Value> call(const Value& request, std::chrono::milliseconds timeout = std::chrono::seconds(30));

    struct Impl;

private:
    explicit Client(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::agent
