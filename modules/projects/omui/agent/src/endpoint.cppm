// omui.agent:endpoint — where agents find an application, and talk to it.
//
// The endpoint itself is the agent kernel's (mlibs.agent, D-173): a local
// connection named after the application, a descriptor beside it
// (`<name>.endpoint.omui.json`) so `omui agent ls` can list what runs, lines
// read on background threads and served by `pump`. This is the interface's
// use of it: each connection's session speaks openmui.tree@0.1 (the
// protocol) to this application's runtime, on the UI thread, between frames,
// where the runtime may be touched.
export module omui.agent:endpoint;

import std;
import omui.core;
import omui.view;
import :protocol;

export namespace omui::agent {

struct EndpointOptions {
    std::string           name;       // default: the app id, else "openmui-app"
    std::filesystem::path directory;  // default: default_directory()
    ProtocolOptions       protocol;
    // Called from a reading thread when a request arrives: a host whose loop
    // sleeps until an event wakes it here (it must be thread-safe).
    std::function<void()> wake;
    // Also on this machine's loopback (a device's endpoint reached through `adb forward`;
    // 0: a port the system chooses), where every program on the machine reaches it: a
    // connection's first line must then be the secret written beside the descriptor
    // (<name>.secret, readable by this program's user alone), else it is closed.
    std::optional<std::uint16_t> loopback;
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
    // On the loopback (EndpointOptions::loopback): its port and the secret a connection gives first.
    [[nodiscard]] std::optional<std::uint16_t> loopback_port() const noexcept;
    [[nodiscard]] const std::string& secret() const noexcept;

    // Serves what arrived and checks pending waits; call once per frame on
    // the UI thread. Answers how many requests it served.
    std::size_t pump();
    // The next frame's input from the connections' `input` requests
    // (ProtocolOptions::input), one batch a frame; the host passes it to
    // the frame it runs next.
    [[nodiscard]] std::optional<AgentInput> take_input();
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
    // An endpoint on this machine's loopback (EndpointOptions::loopback; a device's, forwarded
    // by adb): its secret given first.
    [[nodiscard]] static std::expected<Client, std::string> connect_loopback(std::uint16_t port, std::string_view secret);
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
