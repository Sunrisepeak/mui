// mlibs.agent:endpoint — where agents find a program, and talk to it.
//
// A program listens on a local endpoint (a Unix socket, a named pipe:
// mcpplibs.os.ipc) named after it, in one directory per user, and leaves a
// small descriptor beside it (`<name>.endpoint.omui.json`) so that what runs
// can be listed. Connections are read on background threads; requests are
// served by `pump`, which the owner calls at its safe point — between frames,
// between steps — where its state may be touched.
//
// This layer carries lines. What a line means (openas's envelope, a subject's
// protocol) is the session's: one per connection, made by the owner.
export module mlibs.agent:endpoint;

import std;

export namespace mlibs::agent {

// A line of an answer, sent to the connection that asked.
using Reply = std::function<void(std::string_view line)>;

// One connection's conversation. Made and called at the owner's safe point only.
class Session {
public:
    virtual ~Session() = default;
    // A request (one line, without its newline); `reply` may be kept and called later, at a safe point.
    virtual void request(std::string_view line, const Reply& reply) = 0;
    // Once a pump: what the session waits for (a condition to hold, a person's answer).
    virtual void poll() {}
};

struct EndpointOptions {
    std::string           name;       // the endpoint's name: the program's id
    std::filesystem::path directory;  // default: default_directory()
    // The descriptor's members beside `name` (and `port`, on the loopback).
    std::string app;             // the application's id
    std::string interface_name;  // the interface its sessions speak (`openmui.tree@0.1`)
    std::string spec;            // the specification's version
    // A session for a connection, made at its first request.
    std::function<std::unique_ptr<Session>()> session;
    // Called from a reading thread when a request arrives: an owner whose loop
    // sleeps until an event wakes it here (it must be thread-safe).
    std::function<void()> wake;
    // Also on this machine's loopback (a device's endpoint reached through `adb forward`;
    // 0: a port the system chooses), where every program on the machine reaches it: a
    // connection's first line must then be the secret written beside the descriptor
    // (<name>.secret), else it is closed.
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
    explicit Endpoint(EndpointOptions options);
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

    // Serves what arrived and polls every session; the owner calls it at its
    // safe point. Answers how many requests it served.
    std::size_t pump();
    [[nodiscard]] std::size_t connections() const;
    // Every live session, for what the owner keeps in them.
    void each_session(const std::function<void(Session&)>& visit);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// A connection to an endpoint (a command line, tests, other processes): lines out, lines in.
class Connection {
public:
    [[nodiscard]] static std::expected<Connection, std::string> connect(std::string_view name,
                                                                        const std::filesystem::path& directory = default_directory());
    // An endpoint on this machine's loopback (EndpointOptions::loopback; a device's, forwarded
    // by adb): its secret given first.
    [[nodiscard]] static std::expected<Connection, std::string> connect_loopback(std::uint16_t port, std::string_view secret);
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    ~Connection();

    // Sends one line (the newline is added).
    std::expected<void, std::string> send(std::string_view line);
    // The next whole line within `timeout`: the line; nothing in time (an empty optional); or
    // why there will be none (the endpoint closed the connection, a failure).
    [[nodiscard]] std::expected<std::optional<std::string>, std::string> receive(std::chrono::milliseconds timeout);

    struct Impl;

private:
    explicit Connection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace mlibs::agent
