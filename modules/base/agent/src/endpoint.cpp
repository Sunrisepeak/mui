module mlibs.agent;

import std;
import mlibs.log;
import mlibs.json;
import mcpplibs.os;

namespace mlibs::agent {

namespace ipc = mcpplibs::os::ipc;

namespace {

constexpr std::string_view descriptor_suffix = ".endpoint.omui.json";

// A path as UTF-8 text (never through the code page: a Chinese folder on an English system).
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// A shared connection: its reader thread and the replies of the serving thread both use it.
struct Link {
    ipc::connection   conn;
    std::atomic<bool> open{true};
    bool              proven = true;  // a loopback connection: proven by its first line, the secret
};

struct Incoming {
    std::shared_ptr<Link> link;
    std::string           line;
};

}  // namespace

std::filesystem::path default_directory() {
    auto base = mcpplibs::os::dirs::runtime();
    return (base ? *base : mcpplibs::os::dirs::temp()) / "openmui";
}

std::vector<EndpointInfo> list(const std::filesystem::path& directory) {
    std::vector<EndpointInfo> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        const std::string file = utf8(entry.path().filename());
        if (!file.ends_with(descriptor_suffix)) continue;
        auto text = mcpplibs::os::fs::read(entry.path());
        if (!text) continue;
        auto document = mlibs::json::Document::parse(*text, file);
        if (!document) continue;
        const auto root = document->root();
        EndpointInfo info;
        info.name = root["name"].text().value_or("");
        info.app = root["app"].text().value_or("");
        info.interface_name = root["interface"].text().value_or("");
        info.directory = directory;
        if (!info.name.empty()) out.push_back(std::move(info));
    }
    std::ranges::sort(out, {}, &EndpointInfo::name);
    return out;
}

struct Endpoint::Impl {
    EndpointOptions                                     options;
    std::filesystem::path                               directory;
    ipc::listener                                       listener;
    std::jthread                                        acceptor;
    std::optional<mcpplibs::os::net::loopback_listener> loopback;
    std::jthread                                        loopback_acceptor;
    std::string                                         secret;
    mutable std::mutex                                  lock;
    std::vector<std::jthread>                           readers;
    std::vector<std::shared_ptr<Link>>                  links;
    std::deque<Incoming>                                inbox;
    std::map<const Link*, std::unique_ptr<Session>>     sessions;
    bool                                                running = false;

    explicit Impl(EndpointOptions given) : options(std::move(given)) {}

    void read_loop(std::stop_token stop, std::shared_ptr<Link> link) {
        std::string buffer;
        while (!stop.stop_requested() && link->open) {
            auto got = link->conn.read_for(std::chrono::milliseconds(100));
            if (!got) break;                  // closed or failed
            if (!*got) continue;              // nothing yet
            if (got->value().empty()) break;  // end of stream
            buffer += **got;
            for (auto nl = buffer.find('\n'); nl != std::string::npos; nl = buffer.find('\n')) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (!link->proven) {
                    // A loopback connection's first line: the secret, or it goes.
                    if (line != secret) {
                        link->open = false;
                        break;
                    }
                    link->proven = true;
                    continue;
                }
                {
                    std::scoped_lock guard(lock);
                    inbox.push_back({link, std::move(line)});
                }
                if (options.wake) options.wake();
            }
        }
        link->open = false;
    }

    void take(ipc::connection connection, bool proven) {
        auto link = std::make_shared<Link>();
        link->conn = std::move(connection);
        link->proven = proven;
        std::scoped_lock guard(lock);
        links.push_back(link);
        readers.emplace_back([this, link](std::stop_token stop) { read_loop(stop, link); });
    }

    void accept_loopback(std::stop_token stop) {
        while (!stop.stop_requested()) {
            auto connection = loopback->accept();
            if (stop.stop_requested()) break;  // the connection stop() made to wake this
            if (!connection) continue;
            take(std::move(*connection), false);
        }
    }

    void accept_loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            auto connection = listener.accept_for(std::chrono::milliseconds(100));
            if (!connection) break;
            if (!*connection) continue;
            take(std::move(**connection), true);
        }
    }
};

Endpoint::Endpoint(EndpointOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {
    Impl& d = *impl_;
    if (d.options.name.empty()) d.options.name = !d.options.app.empty() ? d.options.app : "openmui-app";
    d.directory = d.options.directory.empty() ? default_directory() : d.options.directory;
}

Endpoint::~Endpoint() { stop(); }

std::expected<void, std::string> Endpoint::start() {
    Impl& d = *impl_;
    if (d.running) return {};
    std::error_code ec;
    std::filesystem::create_directories(d.directory, ec);
    auto listening = ipc::listener::listen(d.directory, d.options.name);
    if (!listening) return std::unexpected(std::format("listening as '{}' in {}: {}", d.options.name, utf8(d.directory), listening.error().message()));
    d.listener = std::move(*listening);
    std::string descriptor = std::format("{{\"name\":{},\"app\":{},\"interface\":{},\"spec\":{}", json_text(d.options.name), json_text(d.options.app),
                                         json_text(d.options.interface_name), json_text(d.options.spec));
    if (d.options.loopback) {
        auto secret = mcpplibs::os::random::hex(16);
        if (!secret) return std::unexpected(std::format("a secret for the loopback: {}", secret.error().message()));
        auto opened = mcpplibs::os::net::loopback_listener::listen(*d.options.loopback);
        if (!opened) return std::unexpected(std::format("listening on the loopback: {}", opened.error().message()));
        d.secret = *secret;
        d.loopback.emplace(std::move(*opened));
        if (auto written = mcpplibs::os::fs::write_atomic(d.directory / (d.options.name + ".secret"), d.secret); !written)
            return std::unexpected(std::format("the loopback's secret was not written: {}", written.error().message()));
        descriptor += std::format(",\"port\":{}", d.loopback->port());
        d.loopback_acceptor = std::jthread([&d](std::stop_token stop) { d.accept_loopback(stop); });
    }
    descriptor += '}';
    if (auto written = mcpplibs::os::fs::write_atomic(d.directory / (d.options.name + std::string(descriptor_suffix)), descriptor); !written)
        mlibs::log::warn("the endpoint descriptor was not written: {}", written.error().message());
    d.acceptor = std::jthread([&d](std::stop_token stop) { d.accept_loop(stop); });
    d.running = true;
    return {};
}

void Endpoint::stop() {
    Impl& d = *impl_;
    if (!d.running) return;
    d.acceptor.request_stop();
    if (d.acceptor.joinable()) d.acceptor.join();
    if (d.loopback) {
        // Its accept blocks: a connection of its own wakes it to see it is asked to stop.
        d.loopback_acceptor.request_stop();
        (void)mcpplibs::os::net::connect_loopback(d.loopback->port());
        if (d.loopback_acceptor.joinable()) d.loopback_acceptor.join();
        d.loopback->close();
        d.loopback.reset();
    }
    std::vector<std::jthread> readers;
    {
        std::scoped_lock guard(d.lock);
        for (auto& link : d.links) link->open = false;
        readers = std::move(d.readers);
    }
    for (auto& reader : readers) reader.request_stop();
    readers.clear();  // joins
    for (auto& link : d.links) link->conn.close();
    d.links.clear();
    d.sessions.clear();
    d.inbox.clear();
    d.listener.close();
    std::error_code ec;
    std::filesystem::remove(d.directory / (d.options.name + std::string(descriptor_suffix)), ec);
    std::filesystem::remove(d.directory / (d.options.name + ".secret"), ec);
    d.running = false;
}

bool Endpoint::listening() const noexcept { return impl_->running; }
const std::string& Endpoint::name() const noexcept { return impl_->options.name; }
const std::filesystem::path& Endpoint::directory() const noexcept { return impl_->directory; }
std::optional<std::uint16_t> Endpoint::loopback_port() const noexcept {
    return impl_->loopback ? std::optional<std::uint16_t>(impl_->loopback->port()) : std::nullopt;
}
const std::string& Endpoint::secret() const noexcept { return impl_->secret; }

std::size_t Endpoint::connections() const {
    std::scoped_lock guard(impl_->lock);
    return static_cast<std::size_t>(std::ranges::count_if(impl_->links, [](const auto& link) { return link->open.load(); }));
}

void Endpoint::each_session(const std::function<void(Session&)>& visit) {
    for (auto& [link, session] : impl_->sessions)
        if (session) visit(*session);
}

std::size_t Endpoint::pump() {
    Impl& d = *impl_;
    std::deque<Incoming> work;
    {
        std::scoped_lock guard(d.lock);
        work.swap(d.inbox);
    }
    for (Incoming& incoming : work) {
        auto& session = d.sessions[incoming.link.get()];
        if (!session && d.options.session) session = d.options.session();
        if (!session) continue;  // an endpoint nobody answers at
        std::weak_ptr<Link> weak = incoming.link;
        session->request(incoming.line, [weak](std::string_view line) {
            auto link = weak.lock();
            if (!link || !link->open) return;
            std::string out(line);
            out += '\n';
            if (!link->conn.write(out)) link->open = false;
        });
    }
    for (auto& [link, session] : d.sessions)
        if (session) session->poll();
    // Forget connections that closed.
    {
        std::scoped_lock guard(d.lock);
        for (auto it = d.links.begin(); it != d.links.end();) {
            if (!(*it)->open && !std::ranges::any_of(d.inbox, [&](const Incoming& waiting) { return waiting.link == *it; })) {
                d.sessions.erase(it->get());
                it = d.links.erase(it);
            } else {
                ++it;
            }
        }
    }
    return work.size();
}

// ── a connection to an endpoint ──────────────────────────────────────────

struct Connection::Impl {
    ipc::connection conn;
    std::string     buffer;
};

Connection::Connection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;
Connection::~Connection() = default;

std::expected<Connection, std::string> Connection::connect(std::string_view name, const std::filesystem::path& directory) {
    auto opened = ipc::connection::connect(directory, name);
    if (!opened) return std::unexpected(std::format("no endpoint '{}' in {}: {}", name, utf8(directory), opened.error().message()));
    auto impl = std::make_unique<Impl>();
    impl->conn = std::move(*opened);
    return Connection(std::move(impl));
}

std::expected<Connection, std::string> Connection::connect_loopback(std::uint16_t port, std::string_view secret) {
    auto opened = mcpplibs::os::net::connect_loopback(port);
    if (!opened) return std::unexpected(std::format("no endpoint on the loopback at {}: {}", port, opened.error().message()));
    if (auto written = opened->write(std::format("{}\n", secret)); !written)
        return std::unexpected(std::format("the endpoint at {}: {}", port, written.error().message()));
    auto impl = std::make_unique<Impl>();
    impl->conn = std::move(*opened);
    return Connection(std::move(impl));
}

std::expected<void, std::string> Connection::send(std::string_view line) {
    std::string out(line);
    out += '\n';
    if (auto written = impl_->conn.write(out); !written) return std::unexpected(written.error().message());
    return {};
}

std::expected<std::optional<std::string>, std::string> Connection::receive(std::chrono::milliseconds timeout) {
    Impl& d = *impl_;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (const auto nl = d.buffer.find('\n'); nl != std::string::npos) {
            std::string line = d.buffer.substr(0, nl);
            d.buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return std::optional<std::string>{};
        auto got = d.conn.read_for(std::min(left, std::chrono::milliseconds(200)));
        if (!got) return std::unexpected(got.error().message());
        if (*got) {
            if ((*got)->empty()) return std::unexpected(std::string("the endpoint closed the connection"));
            d.buffer += **got;
        }
    }
}

}  // namespace mlibs::agent
