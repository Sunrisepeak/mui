module omui.agent;

import std;
import omui.core;
import omui.view;
import mlibs.agent;

namespace omui::agent {

namespace kernel = mlibs::agent;

std::filesystem::path default_directory() { return kernel::default_directory(); }

std::vector<EndpointInfo> list(const std::filesystem::path& directory) {
    std::vector<EndpointInfo> out;
    for (auto& found : kernel::list(directory))
        out.push_back({std::move(found.name), std::move(found.app), std::move(found.interface_name), std::move(found.directory)});
    return out;
}

// The interface's session on a connection: openmui.tree@0.1 (the protocol), spoken over the kernel's lines.
struct Endpoint::Impl {
    // A connection's session: openmui.tree@0.1 (the protocol) and the kernel's requests for the process's
    // subjects (ProcessSession), over the kernel's lines.
    class TreeSession final : public kernel::Session {
    public:
        TreeSession(Impl& owner, Runtime& runtime, const ProtocolOptions& options) : owner_(owner), session_(runtime, options) {
            owner_.live.push_back(&session_.protocol());
            session_.keep_facts_in(owner_.facts);  // one book for the endpoint: `log` reads what any connection asked
        }
        ~TreeSession() override { std::erase(owner_.live, &session_.protocol()); }
        void request(std::string_view line, const kernel::Reply& reply) override { session_.request(line, reply); }
        void poll() override { session_.poll(); }

    private:
        Impl&          owner_;
        ProcessSession session_;
    };

    Runtime&               runtime;
    EndpointOptions        options;
    std::vector<Protocol*> live;  // the sessions' protocols, for the input they hold; outlives the endpoint below
    std::shared_ptr<kernel::Facts> facts = std::make_shared<kernel::Facts>();  // what was asked of the application's subjects, by any connection
    kernel::Endpoint       endpoint;

    Impl(Runtime& given, EndpointOptions asked)
        : runtime(given), options(std::move(asked)),
          endpoint({.name = options.name,
                    .directory = options.directory,
                    .app = options.protocol.app,
                    .interface_name = std::string(interface_name),
                    .spec = std::string(spec::version),
                    .session = [this] { return std::unique_ptr<kernel::Session>(std::make_unique<TreeSession>(*this, runtime, options.protocol)); },
                    .wake = options.wake,
                    .loopback = options.loopback}) {}
};

Endpoint::Endpoint(Runtime& runtime, EndpointOptions options) : impl_(std::make_unique<Impl>(runtime, std::move(options))) {}
Endpoint::~Endpoint() { stop(); }

std::expected<void, std::string> Endpoint::start() { return impl_->endpoint.start(); }
void Endpoint::stop() { impl_->endpoint.stop(); }
bool Endpoint::listening() const noexcept { return impl_->endpoint.listening(); }
const std::string& Endpoint::name() const noexcept { return impl_->endpoint.name(); }
const std::filesystem::path& Endpoint::directory() const noexcept { return impl_->endpoint.directory(); }
std::optional<std::uint16_t> Endpoint::loopback_port() const noexcept { return impl_->endpoint.loopback_port(); }
const std::string& Endpoint::secret() const noexcept { return impl_->endpoint.secret(); }
std::size_t Endpoint::connections() const { return impl_->endpoint.connections(); }
std::size_t Endpoint::pump() { return impl_->endpoint.pump(); }

std::optional<AgentInput> Endpoint::take_input() {
    for (Protocol* protocol : impl_->live)
        if (auto batch = protocol->take_input()) return batch;
    return std::nullopt;
}

// ── client ─────────────────────────────────────────────────────────────

struct Client::Impl {
    kernel::Connection connection;
    std::uint64_t      next = 1;
};

Client::Client(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
Client::~Client() = default;

std::expected<Client, std::string> Client::connect(std::string_view name, const std::filesystem::path& directory) {
    auto opened = kernel::Connection::connect(name, directory);
    if (!opened) return std::unexpected(opened.error());
    return Client(std::make_unique<Impl>(std::move(*opened)));
}

std::expected<Client, std::string> Client::connect_loopback(std::uint16_t port, std::string_view secret) {
    auto opened = kernel::Connection::connect_loopback(port, secret);
    if (!opened) return std::unexpected(opened.error());
    return Client(std::make_unique<Impl>(std::move(*opened)));
}

std::vector<Value> Client::call(const Value& request, std::chrono::milliseconds timeout) {
    Impl& d = *impl_;
    std::vector<Value> out;
    Value req = request;
    const std::int64_t re = static_cast<std::int64_t>(d.next++);
    req.set("re", re);
    auto fail = [&](std::string what) {
        Value e;
        e.set("kind", "error").set("code", spec::error_code::internal).set("text", std::move(what));
        out.push_back(std::move(e));
        out.push_back(Value{}.set("kind", "result").set("exitCode", spec::exit_code::failure));
        return out;
    };
    if (auto sent = d.connection.send(req.json()); !sent) return fail(sent.error());
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            Value e;
            e.set("kind", "error").set("code", spec::error_code::timeout).set("text", "no answer in time");
            out.push_back(std::move(e));
            out.push_back(Value{}.set("kind", "result").set("exitCode", spec::exit_code::not_ready));
            return out;
        }
        auto line = d.connection.receive(left);
        if (!line) return fail(line.error());
        if (!*line) continue;  // nothing in time: the deadline above ends it
        auto v = parse_json(**line);
        if (!v) continue;
        const Value* r = v->get("re");
        if (r && r->as_int(-1) != re) continue;  // another request's
        const bool last = v->get("kind") && v->get("kind")->as_string() == "result";
        out.push_back(std::move(*v));
        if (last) return out;
    }
}

}  // namespace omui::agent
