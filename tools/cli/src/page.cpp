// omui connect and omui page (agent connect §4, D-95): a page in the
// person's browser cannot be reached — it can only reach out — so a bridge
// listens on 127.0.0.1 and both sides come to it. The page (the Web host's
// agent-link) says hello with the pairing code it showed, then asks for the
// next request and answers it; the person's own agent, the one they asked,
// sends its requests (the semantic protocol, openmui.tree@0.1) with `omui
// page …`, or over the bridge's local HTTP. The agent is the one acting:
// every request reaches the page as theirs and openas decides it there.
//
//   page:  POST /omui/v1/hello {code, app, page}      → {session}
//          GET  /omui/v1/next?session=…               → a request, or 204 after a while
//          POST /omui/v1/reply?session=…  {id, reply}
//   agent: GET  /omui/v1/status                        (Authorization: Bearer <token>)
//          POST /omui/v1/call {request}               → the page's reply
//
// The agent's side needs the token the bridge wrote to the per-user
// directory (bridge.json): a page in a browser cannot read it, so no page can
// act as the agent.
module openmui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import openmui.core;
import openmui.agent;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;
namespace os = mcpplibs::os;

namespace {

constexpr std::uint16_t first_port = 47110;
constexpr int ports = 4;
constexpr std::string_view version = "0.1.0";

// ── HTTP, as much of it as the bridge speaks ────────────────────────────

struct Http {
    std::string method, path, query, origin, authorization, body;
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<Http> read_http(os::ipc::connection& c) {
    std::string buf;
    std::size_t head_end = std::string::npos;
    while ((head_end = buf.find("\r\n\r\n")) == std::string::npos) {
        auto got = c.read_for(std::chrono::seconds(10));
        if (!got || !*got || (*got)->empty()) return std::nullopt;
        buf += **got;
        if (buf.size() > 64 * 1024) return std::nullopt;
    }
    Http h;
    std::istringstream head(buf.substr(0, head_end));
    std::string line;
    std::getline(head, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream first(line);
        std::string target;
        first >> h.method >> target;
        const auto q = target.find('?');
        h.path = target.substr(0, q);
        if (q != std::string::npos) h.query = target.substr(q + 1);
    }
    std::size_t length = 0;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (key == "content-length") std::from_chars(value.data(), value.data() + value.size(), length);
        else if (key == "origin") h.origin = value;
        else if (key == "authorization") h.authorization = value;
    }
    if (length > 8 * 1024 * 1024) return std::nullopt;
    h.body = buf.substr(head_end + 4);
    while (h.body.size() < length) {
        auto got = c.read_for(std::chrono::seconds(10));
        if (!got || !*got || (*got)->empty()) return std::nullopt;
        h.body += **got;
    }
    h.body.resize(length);
    return h;
}

std::string query_value(std::string_view query, std::string_view key) {
    for (std::size_t at = 0; at < query.size();) {
        const std::size_t end = std::min(query.find('&', at), query.size());
        const std::string_view kv = query.substr(at, end - at);
        if (kv.starts_with(key) && kv.size() > key.size() && kv[key.size()] == '=') return std::string(kv.substr(key.size() + 1));
        at = end + 1;
    }
    return {};
}

void respond(os::ipc::connection& c, int status, std::string_view body, std::string_view origin, std::string_view type = "application/json") {
    const std::string_view reason = status == 200 ? "OK" : status == 204 ? "No Content" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized"
                                  : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 504 ? "Gateway Timeout" : "Error";
    std::string out = std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n", status,
                                  reason, type, body.size());
    // The page reads the bridge's answers: from its own origin only (and a page on a public site may reach the loopback, asked).
    if (!origin.empty())
        out += std::format("Access-Control-Allow-Origin: {}\r\nVary: Origin\r\nAccess-Control-Allow-Methods: GET, POST\r\n"
                           "Access-Control-Allow-Headers: content-type\r\nAccess-Control-Allow-Private-Network: true\r\n",
                           origin);
    out += "\r\n";
    out += body;
    (void)c.write(out);
    c.shutdown_write();
}

// Equal in a time that does not depend on where they differ.
bool same_secret(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// A pairing code as the page shows it: letters and digits, upper case, the dash ignored.
std::string code_of(std::string_view text) {
    std::string out;
    for (char c : text)
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// ── the bridge ──────────────────────────────────────────────────────────

struct Bridge {
    std::mutex              m;
    std::condition_variable changed;
    std::string             code, token, agent_name, only_origin;
    std::string             session, origin, app, page;  // the page paired with, once it is
    int                     refused = 0;                 // hellos with a wrong code
    std::deque<std::pair<std::uint64_t, std::string>> waiting;   // requests the page has not taken
    std::map<std::uint64_t, std::string>              replies;   // by request
    std::uint64_t           next = 1;
    std::chrono::steady_clock::time_point seen{};        // the page last asked
    bool                    stopping = false;

    [[nodiscard]] bool paired() const { return !session.empty(); }
    [[nodiscard]] Value status() const {
        Value v;
        v.set("bridge", std::string(version)).set("paired", paired()).set("agent", agent_name);
        if (paired()) v.set("page", page).set("app", app).set("origin", origin);
        return v;
    }
};

void serve(Bridge& b, os::ipc::connection c) {
    auto req = read_http(c);
    if (!req) return;
    const Http& h = *req;
    if (h.method == "OPTIONS") {  // a preflight
        respond(c, 204, "", h.origin);
        return;
    }
    // The agent's side: the token, and no browser's origin.
    const bool agent_side = h.path == "/omui/v1/call" || h.path == "/omui/v1/status";
    if (agent_side) {
        if (!h.origin.empty() || !h.authorization.starts_with("Bearer ") || !same_secret(h.authorization.substr(7), b.token)) {
            respond(c, 401, R"({"error":"the agent's side needs the token in bridge.json"})", "");
            return;
        }
        if (h.path == "/omui/v1/status") {
            std::scoped_lock g(b.m);
            respond(c, 200, b.status().json(), "");
            return;
        }
        auto request = parse_json(h.body);
        if (!request || !request->map()) {
            respond(c, 400, R"({"error":"a request is a JSON object"})", "");
            return;
        }
        // Every request is the agent's: its actor, whatever it names.
        request->set("actor", std::format("agent:{}", b.agent_name.empty() ? std::string("local") : b.agent_name));
        std::unique_lock g(b.m);
        if (!b.paired()) {
            respond(c, 503, R"({"error":"no page is paired yet: omui page status --wait"})", "");
            return;
        }
        const std::uint64_t id = b.next++;
        b.waiting.emplace_back(id, request->json());
        b.changed.notify_all();
        double limit = 30;
        if (const Value* t = request->get("timeout")) limit = t->as_double() + 5;
        if (!b.changed.wait_for(g, std::chrono::duration<double>(limit), [&] { return b.replies.contains(id) || b.stopping; })) {
            std::erase_if(b.waiting, [&](const auto& w) { return w.first == id; });
            respond(c, 504, R"({"error":"the page did not answer: is its tab still open?"})", "");
            return;
        }
        const std::string reply = std::move(b.replies[id]);
        b.replies.erase(id);
        g.unlock();
        respond(c, 200, reply, "");
        return;
    }
    // The page's side.
    if (!b.only_origin.empty() && h.origin != b.only_origin) {
        respond(c, 403, R"({"error":"not the origin this bridge serves"})", h.origin);
        return;
    }
    if (h.path == "/omui/v1/hello" && h.method == "POST") {
        auto hello = parse_json(h.body);
        std::scoped_lock g(b.m);
        if (b.refused >= 10) {
            respond(c, 403, R"({"error":"too many wrong codes: start the bridge again"})", h.origin);
            return;
        }
        const std::string given = code_of(hello && hello->get("code") ? hello->get("code")->as_string() : "");
        if (!same_secret(given, b.code)) {
            ++b.refused;
            respond(c, 403, R"({"error":"not the pairing code this bridge was started with"})", h.origin);
            return;
        }
        auto token = os::random::hex(24);
        b.session = token ? *token : std::string("s");
        b.origin = h.origin;
        b.app = std::string(hello->get("app") ? hello->get("app")->as_string() : "");
        b.page = std::string(hello->get("page") ? hello->get("page")->as_string() : "");
        b.seen = std::chrono::steady_clock::now();
        b.changed.notify_all();
        respond(c, 200, Value{}.set("session", b.session).set("agent", b.agent_name).set("bridge", std::string(version)).json(), h.origin);
        return;
    }
    const std::string session = query_value(h.query, "session");
    {
        std::scoped_lock g(b.m);
        if (!b.paired() || !same_secret(session, b.session)) {
            respond(c, 403, R"({"error":"not this bridge's session"})", h.origin);
            return;
        }
        b.seen = std::chrono::steady_clock::now();
    }
    if (h.path == "/omui/v1/next" && h.method == "GET") {
        std::unique_lock g(b.m);
        b.changed.wait_for(g, std::chrono::seconds(20), [&] { return !b.waiting.empty() || b.stopping; });
        if (b.waiting.empty()) {
            g.unlock();
            respond(c, 204, "", h.origin);
            return;
        }
        auto [id, request] = std::move(b.waiting.front());
        b.waiting.pop_front();
        g.unlock();
        respond(c, 200, std::format(R"({{"id":{},"request":{}}})", id, request), h.origin);
        return;
    }
    if (h.path == "/omui/v1/reply" && h.method == "POST") {
        auto reply = parse_json(h.body);
        if (!reply || !reply->get("id") || !reply->get("reply")) {
            respond(c, 400, R"({"error":"a reply is {id, reply}"})", h.origin);
            return;
        }
        std::scoped_lock g(b.m);
        b.replies[static_cast<std::uint64_t>(reply->get("id")->as_int())] = reply->get("reply")->json();
        b.changed.notify_all();
        respond(c, 200, "{}", h.origin);
        return;
    }
    respond(c, 404, R"({"error":"no such path"})", h.origin);
}

std::filesystem::path state_file(const std::optional<std::filesystem::path>& dir) {
    return (dir ? *dir : agent::default_directory()) / "bridge.json";
}

struct Found {
    std::uint16_t port = 0;
    std::string   token;
};
std::expected<Found, std::string> find_bridge(const std::optional<std::filesystem::path>& dir) {
    Found f;
    auto text = mcpplibs::os::fs::read(state_file(dir));
    if (!text) return std::unexpected(std::format("no bridge is running ({} is not there): start one with `omui connect --code CODE`", utf8(state_file(dir))));
    auto v = parse_json(*text);
    if (!v || !v->get("port") || !v->get("token")) return std::unexpected("bridge.json is not the bridge's");
    f.port = static_cast<std::uint16_t>(v->get("port")->as_int());
    f.token = std::string(v->get("token")->as_string());
    return f;
}

// One HTTP exchange with the bridge, as the agent.
std::expected<std::string, std::string> ask_bridge(const Found& f, std::string_view method, std::string_view path, std::string_view body,
                                                   std::chrono::seconds limit) {
    auto c = os::net::connect_loopback(f.port);
    if (!c) return std::unexpected(std::format("the bridge at 127.0.0.1:{} does not answer: {}", f.port, c.error().message()));
    const std::string req = std::format("{} {} HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer {}\r\nContent-Type: application/json\r\n"
                                        "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                                        method, path, f.token, body.size(), body);
    if (auto w = c->write(req); !w) return std::unexpected(w.error().message());
    std::string got;
    const auto until = std::chrono::steady_clock::now() + limit;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return std::unexpected("the bridge did not answer in time");
        auto chunk = c->read_for(left);
        if (!chunk) return std::unexpected(chunk.error().message());
        if (!*chunk) continue;
        if ((*chunk)->empty()) break;
        got += **chunk;
    }
    const auto body_at = got.find("\r\n\r\n");
    if (body_at == std::string::npos) return std::unexpected("the bridge's answer is not HTTP");
    int status = 0;
    std::from_chars(got.data() + 9, got.data() + std::min<std::size_t>(got.size(), 12), status);
    std::string answer = got.substr(body_at + 4);
    if (status != 200) {
        auto v = parse_json(answer);
        return std::unexpected(v && v->get("error") ? std::string(v->get("error")->as_string()) : std::format("the bridge answered {}", status));
    }
    return answer;
}

}  // namespace

cmd::App connect_command() {
    cmd::App a{"connect"};
    (void)a.description("Start the bridge a page in your browser pairs with, so your agent operates it with `omui page …` (it runs until stopped)");
    a.option("code").takes_value().value_name("CODE").help("the pairing code the page shows (in the prompt it gave you)");
    a.option("port").takes_value().value_name("N").help("the port on 127.0.0.1 (default: the first free of 47110–47113)");
    a.option("name").takes_value().value_name("AGENT").help("who you are, as the page shows it (Claude Code, Codex …)");
    a.option("origin").takes_value().value_name("URL").help("serve only a page of this origin (https://example.org)");
    return a;
}

cmd::App page_command(cmd::App ops) {
    (void)ops.description("Operate the page paired with the bridge (omui connect), through its semantic tree: the same operations as `omui agent`");
    cmd::App status{"status"};
    (void)status.description("Whether a page is paired: the page, its application, its origin");
    status.option("wait").help("until a page pairs");
    status.option("timeout").takes_value().value_name("SECONDS").help("how long --wait waits (default 600)");
    (void)ops.subcommand(std::move(status));
    return ops;
}

std::expected<std::uint16_t, std::string> run_bridge(const BridgeStart& start, std::stop_token stop, const std::function<void(std::uint16_t)>& ready) {
    Bridge b;
    b.code = code_of(start.code);
    if (b.code.size() < 6) return std::unexpected("the pairing code is the page's (letters and digits)");
    b.agent_name = start.name.empty() ? std::string("local") : start.name;
    b.only_origin = start.origin;
    auto token = os::random::hex(24);
    if (!token) return std::unexpected(std::format("no random source: {}", token.error().message()));
    b.token = *token;
    std::optional<os::net::loopback_listener> listener;
    std::uint16_t port = 0;
    if (start.port || start.any_port) {
        auto l = os::net::loopback_listener::listen(start.port);
        if (!l) return std::unexpected(std::format("127.0.0.1:{} is taken: {}", start.port, l.error().message()));
        listener.emplace(std::move(*l));
        port = listener->port();
    } else {
        for (int i = 0; i < ports && !listener; ++i)
            if (auto l = os::net::loopback_listener::listen(static_cast<std::uint16_t>(first_port + i))) {
                port = static_cast<std::uint16_t>(first_port + i);
                listener.emplace(std::move(*l));
            }
        if (!listener) return std::unexpected(std::format("ports {}–{} are all taken", first_port, first_port + ports - 1));
    }
    const std::filesystem::path state = state_file(start.dir);
    std::error_code ec;
    std::filesystem::create_directories(state.parent_path(), ec);
    if (auto w = os::fs::write_atomic(state, Value{}.set("port", static_cast<std::int64_t>(port)).set("token", b.token).json()); !w) return std::unexpected(w.error().message());
    // Stopped: the waits woken, and the accept by a connection of its own (closing a listener does not wake one everywhere).
    std::stop_callback on_stop(stop, [&] {
        {
            std::scoped_lock g(b.m);
            b.stopping = true;
        }
        b.changed.notify_all();
        (void)os::net::connect_loopback(port);
    });
    if (ready) ready(port);
    // A thread a connection (a page's request may wait 20 s for the agent's); the finished ones joined as new ones come.
    struct Serving {
        std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
        std::jthread                      thread;
    };
    std::vector<Serving> serving;
    while (!stop.stop_requested()) {
        auto c = listener->accept();
        if (!c) {
            if (stop.stop_requested()) break;
            continue;
        }
        std::erase_if(serving, [](const Serving& s) { return s.done->load(); });
        Serving s;
        s.thread = std::jthread([&b, done = s.done, conn = std::move(*c)]() mutable {
            serve(b, std::move(conn));
            done->store(true);
        });
        serving.push_back(std::move(s));
    }
    serving.clear();  // joined: each answers and ends
    std::filesystem::remove(state, ec);
    return port;
}

int connect(const cmd::ParsedArgs& p, std::optional<std::filesystem::path> dir, std::ostream& out, std::ostream& err) {
    BridgeStart start{.code = p.value("code").value_or(""), .name = p.value("name").value_or(""), .origin = p.value("origin").value_or(""), .dir = dir};
    if (auto given = p.value("port")) std::from_chars(given->data(), given->data() + given->size(), start.port);
    std::stop_source never;
    auto ran = run_bridge(start, never.get_token(), [&](std::uint16_t port) {
        out << Value{}.set("kind", "data").set("data", Value{}.set("bridge", std::format("127.0.0.1:{}", port)).set("waiting_for", "the page")).json() << '\n';
        out.flush();
    });
    if (!ran) {
        err << "omui connect: " << ran.error() << '\n';
        return 1;
    }
    return 0;
}

int page(const cmd::ParsedArgs& p, std::optional<std::filesystem::path> dir, std::ostream& out, std::ostream& err) {
    if (!p.has_subcommand()) {
        err << "usage: omui page <status|describe|snapshot|changes|find|act|undo|redo|history|transaction|wait|capture|call> …\n";
        return 2;
    }
    auto bridge = find_bridge(dir);
    if (!bridge) {
        err << bridge.error() << '\n';
        return 1;
    }
    const cmd::ParsedArgs& sub = p.subcommand()->get();
    if (p.subcommand_name() == "status") {
        double limit = 600;
        if (auto t = sub.value("timeout")) std::from_chars(t->data(), t->data() + t->size(), limit);
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(limit);
        for (;;) {
            auto s = ask_bridge(*bridge, "GET", "/omui/v1/status", "", std::chrono::seconds(10));
            if (!s) {
                err << s.error() << '\n';
                return 1;
            }
            auto v = parse_json(*s);
            const bool paired = v && v->get("paired") && v->get("paired")->as_bool();
            if (paired || !sub.is_flag_set("wait") || std::chrono::steady_clock::now() >= until) {
                out << Value{}.set("kind", "data").set("data", v ? *v : Value{}).json() << '\n';
                out << Value{}.set("kind", "result").set("exitCode", paired ? 0 : 3).json() << '\n';
                return paired ? 0 : 3;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    auto request = agent_request(p, 0);
    if (!request) {
        err << "omui page " << p.subcommand_name() << ": " << request.error() << '\n';
        return 1;
    }
    auto parsed = parse_json(*request);
    std::chrono::seconds limit(40);
    if (const Value* t = parsed->get("timeout")) limit = std::chrono::seconds(static_cast<std::int64_t>(t->as_double()) + 10);
    auto reply = ask_bridge(*bridge, "POST", "/omui/v1/call", *request, limit);
    if (!reply) {
        err << reply.error() << '\n';
        return 1;
    }
    // The page's answer: {exitCode, data, errors, records}; its records, a line each, as `omui agent` writes them.
    auto v = parse_json(*reply);
    if (!v) {
        err << "the page's answer is not JSON\n";
        return 1;
    }
    int code = static_cast<int>(v->get("exitCode") ? v->get("exitCode")->as_int(1) : 1);
    if (const Value* records = v->get("records"); records && records->list())
        for (Value r : *records->list()) {
            if (r.map()) std::erase_if(*r.map(), [](const auto& kv) { return kv.first == "re"; });
            out << r.json() << '\n';
        }
    else
        out << v->json() << '\n';
    return code;
}

}  // namespace omui::cli
