// openmui.inspector:connection — an application's agent endpoint, off the UI
// thread.
//
// The Inspector talks to the application it inspects over the same protocol
// as any agent (openmui.tree@0.1). A request runs on the task pool, one at a
// time per connection, and its records come back through the Inspector's UI
// queue, so its own frames never wait for another process.
export module openmui.inspector:connection;

import std;
import openmui.core;
import openmui.view;
import openmui.agent;

export namespace omui::inspector {

class Connection {
public:
    using Reply = std::function<void(std::vector<Value> records)>;

    Connection(Runtime& ui, std::filesystem::path directory);
    // Waits for the requests in flight; their replies are dropped.
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // The applications that listen in the directory.
    [[nodiscard]] std::vector<agent::EndpointInfo> applications() const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

    std::expected<void, std::string> open(std::string_view app);
    void close();
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::string app() const;

    // Sends `request`; `reply` runs on the UI thread with its records (an
    // error record when not connected or when the application does not answer).
    void call(Value request, Reply reply, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    [[nodiscard]] std::size_t in_flight() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// The data record of a reply (nullptr: none), and whether it succeeded.
[[nodiscard]] const Value* data_of(const std::vector<Value>& records) noexcept;
[[nodiscard]] bool succeeded(const std::vector<Value>& records) noexcept;
[[nodiscard]] std::string error_of(const std::vector<Value>& records);

}  // namespace omui::inspector
