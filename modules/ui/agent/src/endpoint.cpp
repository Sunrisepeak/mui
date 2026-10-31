module openmui.agent;

import std;
import openmui.core;
import openmui.view;
import mcpplibs.os;

namespace omui::agent {

namespace ipc = mcpplibs::os::ipc;

namespace {

constexpr std::string_view descriptor_suffix = ".endpoint.omui.json";

// A shared connection: its reader thread and the replies of the UI thread both use it.
struct Link {
    ipc::connection   conn;
    std::atomic<bool> open{true};
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
        auto v = parse_json(*text);
        if (!v) continue;
        EndpointInfo info;
        info.name = std::string(v->get("name") ? v->get("name")->as_string() : "");
        info.app = std::string(v->get("app") ? v->get("app")->as_string() : "");
        info.interface_name = std::string(v->get("interface") ? v->get("interface")->as_string() : "");
        info.directory = directory;
        if (!info.name.empty()) out.push_back(std::move(info));
    }
    std::ranges::sort(out, {}, &EndpointInfo::name);
    return out;
}

struct Endpoint::Impl {
    Runtime&                                     runtime;
    EndpointOptions                              options;
    std::string                                  name;
    std::filesystem::path                        directory;
    ipc::listener                                listener;
    std::jthread                                 acceptor;
    mutable std::mutex                           lock;
    std::vector<std::jthread>                    readers;
    std::vector<std::shared_ptr<Link>>           links;
    std::deque<Incoming>                         inbox;
    std::map<const Link*, std::unique_ptr<Protocol>> protocols;
    bool                                         running = false;

    Impl(Runtime& r, EndpointOptions o) : runtime(r), options(std::move(o)) {}

    void read_loop(std::stop_token stop, std::shared_ptr<Link> link) {
        std::string buffer;
        while (!stop.stop_requested() && link->open) {
            auto got = link->conn.read_for(std::chrono::milliseconds(100));
            if (!got) break;            // closed or failed
            if (!*got) continue;        // nothing yet
            if (got->value().empty()) break;  // end of stream
            buffer += **got;
            for (auto nl = buffer.find('\n'); nl != std::string::npos; nl = buffer.find('\n')) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                {
                    std::scoped_lock g(lock);
                    inbox.push_back({link, std::move(line)});
                }
                if (options.wake) options.wake();
            }
        }
        link->open = false;
    }

    void accept_loop(std::stop_token stop) {
        while (!stop.stop_requested()) {
            auto c = listener.accept_for(std::chrono::milliseconds(100));
            if (!c) break;
            if (!*c) continue;
            auto link = std::make_shared<Link>();
            link->conn = std::move(**c);
            std::scoped_lock g(lock);
            links.push_back(link);
            readers.emplace_back([this, link](std::stop_token s) { read_loop(s, link); });
        }
    }
};

Endpoint::Endpoint(Runtime& runtime, EndpointOptions options) : impl_(std::make_unique<Impl>(runtime, std::move(options))) {
    Impl& d = *impl_;
    d.name = !d.options.name.empty() ? d.options.name : (!d.options.protocol.app.empty() ? d.options.protocol.app : "openmui-app");
    d.directory = d.options.directory.empty() ? default_directory() : d.options.directory;
}

Endpoint::~Endpoint() { stop(); }

std::expected<void, std::string> Endpoint::start() {
    Impl& d = *impl_;
    if (d.running) return {};
    std::error_code ec;
    std::filesystem::create_directories(d.directory, ec);
    auto l = ipc::listener::listen(d.directory, d.name);
    if (!l) return std::unexpected(std::format("listening as '{}' in {}: {}", d.name, utf8(d.directory), l.error().message()));
    d.listener = std::move(*l);
    Value desc;
    desc.set("name", d.name).set("app", d.options.protocol.app).set("interface", interface_name).set("spec", spec::version);
    if (auto w = mcpplibs::os::fs::write_atomic(d.directory / (d.name + std::string(descriptor_suffix)), desc.json()); !w)
        warn("the endpoint descriptor was not written: {}", w.error().message());
    d.acceptor = std::jthread([&d](std::stop_token s) { d.accept_loop(s); });
    d.running = true;
    return {};
}

void Endpoint::stop() {
    Impl& d = *impl_;
    if (!d.running) return;
    d.acceptor.request_stop();
    if (d.acceptor.joinable()) d.acceptor.join();
    std::vector<std::jthread> readers;
    {
        std::scoped_lock g(d.lock);
        for (auto& l : d.links) l->open = false;
        readers = std::move(d.readers);
    }
    for (auto& r : readers) r.request_stop();
    readers.clear();  // joins
    for (auto& l : d.links) l->conn.close();
    d.links.clear();
    d.protocols.clear();
    d.inbox.clear();
    d.listener.close();
    std::error_code ec;
    std::filesystem::remove(d.directory / (d.name + std::string(descriptor_suffix)), ec);
    d.running = false;
}

bool Endpoint::listening() const noexcept { return impl_->running; }
const std::string& Endpoint::name() const noexcept { return impl_->name; }
const std::filesystem::path& Endpoint::directory() const noexcept { return impl_->directory; }

std::size_t Endpoint::connections() const {
    std::scoped_lock g(impl_->lock);
    return static_cast<std::size_t>(std::ranges::count_if(impl_->links, [](const auto& l) { return l->open.load(); }));
}

std::size_t Endpoint::pump() {
    Impl& d = *impl_;
    std::deque<Incoming> work;
    {
        std::scoped_lock g(d.lock);
        work.swap(d.inbox);
    }
    for (Incoming& in : work) {
        auto& p = d.protocols[in.link.get()];
        if (!p) p = std::make_unique<Protocol>(d.runtime, d.options.protocol);
        std::weak_ptr<Link> weak = in.link;
        p->request(in.line, [weak](std::string_view line) {
            auto link = weak.lock();
            if (!link || !link->open) return;
            std::string out(line);
            out += '\n';
            if (!link->conn.write(out)) link->open = false;
        });
    }
    for (auto& [link, p] : d.protocols) p->poll();
    // Forget connections that closed.
    {
        std::scoped_lock g(d.lock);
        for (auto it = d.links.begin(); it != d.links.end();) {
            if (!(*it)->open && !std::ranges::any_of(d.inbox, [&](const Incoming& x) { return x.link == *it; })) {
                d.protocols.erase(it->get());
                it = d.links.erase(it);
            } else {
                ++it;
            }
        }
    }
    return work.size();
}

std::optional<AgentInput> Endpoint::take_input() {
    for (auto& [link, p] : impl_->protocols)
        if (auto batch = p->take_input()) return batch;
    return std::nullopt;
}

// ── client ───────────────────────────────────────────────────────────────

struct Client::Impl {
    ipc::connection conn;
    std::string     buffer;
    std::uint64_t   next = 1;
};

Client::Client(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
Client::~Client() = default;

std::expected<Client, std::string> Client::connect(std::string_view name, const std::filesystem::path& directory) {
    auto c = ipc::connection::connect(directory, name);
    if (!c) return std::unexpected(std::format("no endpoint '{}' in {}: {}", name, utf8(directory), c.error().message()));
    auto impl = std::make_unique<Impl>();
    impl->conn = std::move(*c);
    return Client(std::move(impl));
}

std::vector<Value> Client::call(const Value& request, std::chrono::milliseconds timeout) {
    Impl& d = *impl_;
    std::vector<Value> out;
    Value req = request;
    const std::int64_t re = static_cast<std::int64_t>(d.next++);
    req.set("re", re);
    std::string line = req.json();
    line += '\n';
    auto fail = [&](std::string what) {
        Value e;
        e.set("kind", "error").set("code", spec::error_code::internal).set("text", std::move(what));
        out.push_back(std::move(e));
        out.push_back(Value{}.set("kind", "result").set("exitCode", spec::exit_code::failure));
        return out;
    };
    if (auto w = d.conn.write(line); !w) return fail(w.error().message());
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        for (auto nl = d.buffer.find('\n'); nl != std::string::npos; nl = d.buffer.find('\n')) {
            std::string l = d.buffer.substr(0, nl);
            d.buffer.erase(0, nl + 1);
            auto v = parse_json(l);
            if (!v) continue;
            const Value* r = v->get("re");
            if (r && r->as_int(-1) != re) continue;  // another request's
            const bool last = v->get("kind") && v->get("kind")->as_string() == "result";
            out.push_back(std::move(*v));
            if (last) return out;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            Value e;
            e.set("kind", "error").set("code", spec::error_code::timeout).set("text", "no answer in time");
            out.push_back(std::move(e));
            out.push_back(Value{}.set("kind", "result").set("exitCode", spec::exit_code::not_ready));
            return out;
        }
        auto got = d.conn.read_for(std::min(left, std::chrono::milliseconds(200)));
        if (!got) return fail(got.error().message());
        if (*got) {
            if ((*got)->empty()) return fail("the endpoint closed the connection");
            d.buffer += **got;
        }
    }
}

}  // namespace omui::agent
