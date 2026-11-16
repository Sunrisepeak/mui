module omui.devtools.cdp;

import std;
import mcpplibs.os;
import nlohmann.json;
import mlibs.base64;
import mlibs.hash;
import mlibs.http;

namespace omui::devtools::cdp {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using Clock = std::chrono::steady_clock;

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& letter : out)
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return out;
}

std::uint32_t random_word() {
    static std::mutex lock;
    static std::mt19937 generator{std::random_device{}()};
    const std::scoped_lock hold(lock);
    return generator();
}

}  // namespace

// --- what Pages serves ---

bool compressed_kind(std::string_view extension) {
    return std::ranges::contains(std::array<std::string_view, 6>{".html", ".js", ".wasm", ".ttf",
                                                                 ".json", ".md"},
                                 extension);
}

bool accepts_gzip(std::string_view accept_encoding) {
    static const std::regex word(R"(\bgzip\b)");
    return std::regex_search(accept_encoding.begin(), accept_encoding.end(), word);
}

std::string_view pages_type(std::string_view extension) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 8> types{
        {{".html", "text/html"},
         {".js", "text/javascript"},
         {".wasm", "application/wasm"},
         {".ttf", "font/ttf"},
         {".json", "application/json"},
         {".md", "text/markdown"},
         {".webp", "image/webp"},
         {".png", "image/png"}}};
    for (const auto& [known, type] : types)
        if (known == extension)
            return type;
    return "application/octet-stream";
}

std::string resolved_path(std::string_view target) {
    const std::size_t cut = target.find_first_of("?#");
    const std::string decoded = mlibs::http::decoded(target.substr(0, cut));
    std::vector<std::string> parts;
    for (auto part : std::views::split(std::string_view(decoded), '/')) {
        const std::string_view piece(part.begin(), part.end());
        if (piece.empty() || piece == ".")
            continue;
        if (piece == "..") {
            if (!parts.empty())
                parts.pop_back();
            continue;
        }
        parts.emplace_back(piece);
    }
    std::string out;
    for (const auto& part : parts)
        out += (out.empty() ? "" : "/") + part;
    return out;
}

// --- WebSocket ---

namespace websocket {

std::string frame(Opcode opcode, std::string_view payload, std::array<std::uint8_t, 4> mask,
                  bool fin) {
    std::string out;
    out += static_cast<char>((fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(opcode));
    const std::uint64_t size = payload.size();
    if (size < 126) {
        out += static_cast<char>(0x80 | size);
    } else if (size <= 0xffff) {
        out += static_cast<char>(0x80 | 126);
        out += static_cast<char>(size >> 8);
        out += static_cast<char>(size & 0xff);
    } else {
        out += static_cast<char>(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8)
            out += static_cast<char>((size >> shift) & 0xff);
    }
    for (const std::uint8_t byte : mask)
        out += static_cast<char>(byte);
    const std::size_t start = out.size();
    out += payload;
    for (std::size_t index = 0; index < payload.size(); ++index)
        out[start + index] = static_cast<char>(out[start + index] ^ mask[index % 4]);
    return out;
}

std::expected<std::optional<Frame>, std::string> parse(std::string_view arrived, std::size_t most) {
    if (arrived.size() < 2)
        return std::optional<Frame>{};
    const auto byte = [&](std::size_t index) { return static_cast<std::uint8_t>(arrived[index]); };
    Frame out;
    out.fin = (byte(0) & 0x80) != 0;
    const std::uint8_t code = byte(0) & 0x0f;
    if (!std::ranges::contains(std::array<std::uint8_t, 6>{0, 1, 2, 8, 9, 10}, code))
        return std::unexpected(std::format("a reserved opcode {}", code));
    out.opcode = static_cast<Opcode>(code);
    const bool masked = (byte(1) & 0x80) != 0;
    std::uint64_t size = byte(1) & 0x7f;
    std::size_t at = 2;
    if (size == 126) {
        if (arrived.size() < 4)
            return std::optional<Frame>{};
        size = (std::uint64_t{byte(2)} << 8) | byte(3);
        at = 4;
    } else if (size == 127) {
        if (arrived.size() < 10)
            return std::optional<Frame>{};
        size = 0;
        for (std::size_t index = 2; index < 10; ++index)
            size = (size << 8) | byte(index);
        at = 10;
    }
    if (size > most)
        return std::unexpected(std::format("a frame of {} bytes (at most {})", size, most));
    std::array<std::uint8_t, 4> mask{};
    if (masked) {
        if (arrived.size() < at + 4)
            return std::optional<Frame>{};
        for (std::size_t index = 0; index < 4; ++index)
            mask[index] = byte(at + index);
        at += 4;
    }
    if (arrived.size() < at + size)
        return std::optional<Frame>{};
    out.payload = std::string(arrived.substr(at, size));
    if (masked)
        for (std::size_t index = 0; index < out.payload.size(); ++index)
            out.payload[index] = static_cast<char>(out.payload[index] ^ mask[index % 4]);
    out.size = at + size;
    return std::optional<Frame>{std::move(out)};
}

std::string accept(std::string_view key) {
    const auto digest =
        mlibs::hash::sha1(std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return mlibs::base64::encode(digest);
}

std::expected<std::optional<std::pair<Opcode, std::string>>, std::string>
Assembler::add(Frame frame) {
    if (static_cast<std::uint8_t>(frame.opcode) >= 8)
        return std::optional<std::pair<Opcode, std::string>>{
            std::pair{frame.opcode, std::move(frame.payload)}};
    if (frame.opcode == Opcode::continuation) {
        if (!started_)
            return std::unexpected("a continuation with no message started");
        pending_ += frame.payload;
    } else {
        if (started_)
            return std::unexpected("a new message before the last one ended");
        started_ = frame.opcode;
        pending_ = std::move(frame.payload);
    }
    if (!frame.fin)
        return std::optional<std::pair<Opcode, std::string>>{};
    std::pair<Opcode, std::string> out{*started_, std::move(pending_)};
    started_.reset();
    pending_.clear();
    return std::optional<std::pair<Opcode, std::string>>{std::move(out)};
}

}  // namespace websocket

// --- HTTP over the loopback ---

std::expected<std::string, std::string> get(std::uint16_t port, std::string_view target,
                                            std::chrono::milliseconds timeout) {
    auto connection = os::net::connect_loopback(port);
    if (!connection)
        return std::unexpected(std::format("127.0.0.1:{}: {}", port, connection.error().message()));
    if (const auto sent =
            connection->write(mlibs::http::request("GET", target, std::format("127.0.0.1:{}", port),
                                                   {{"Connection", "close"}}));
        !sent)
        return std::unexpected(std::format("127.0.0.1:{}: {}", port, sent.error().message()));
    std::string arrived;
    std::optional<mlibs::http::Response> head;
    const auto end = Clock::now() + timeout;
    for (;;) {
        if (!head) {
            auto parsed = mlibs::http::parse_response(arrived);
            if (!parsed)
                return std::unexpected(parsed.error());
            head = std::move(*parsed);
        }
        if (head && head->body_length && arrived.size() >= head->head_size + *head->body_length)
            break;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now());
        if (left.count() <= 0)
            return std::unexpected(
                std::format("GET {}: no whole answer in {} ms", target, timeout.count()));
        auto chunk = connection->read_for(left);
        if (!chunk)
            return std::unexpected(std::format("GET {}: {}", target, chunk.error().message()));
        if (!*chunk)
            continue;
        if ((*chunk)->empty())
            break;  // the end
        arrived += **chunk;
    }
    if (!head)
        return std::unexpected(std::format("GET {}: no answer", target));
    if (head->status < 200 || head->status >= 300)
        return std::unexpected(std::format("GET {}: {}", target, head->status));
    std::string body = arrived.substr(std::min(head->head_size, arrived.size()));
    if (head->body_length)
        body.resize(std::min(body.size(), *head->body_length));
    return body;
}

std::expected<std::string, std::string> get(std::string_view url,
                                            std::chrono::milliseconds timeout) {
    static const std::regex form(R"(^https?://(127\.0\.0\.1|localhost):(\d+)(/[^#]*)?)");
    std::match_results<std::string_view::const_iterator> found;
    if (!std::regex_search(url.begin(), url.end(), found, form))
        return std::unexpected(std::format("not a loopback URL: {}", url));
    int port = 0;
    const std::string digits = found[2].str();
    std::from_chars(digits.data(), digits.data() + digits.size(), port);
    const std::string target = found[3].matched ? found[3].str() : "/";
    return get(static_cast<std::uint16_t>(port), target, timeout);
}

// --- the server ---

std::string_view Arrived::header(std::string_view name) const {
    for (const auto& [key, value] : headers)
        if (key == name)
            return value;
    return {};
}

struct Server::State {
    os::net::loopback_listener listener;
    std::uint16_t port = 0;
    Handler handler;
    std::atomic<bool> stopping{false};
    std::mutex lock;
    std::map<std::size_t, os::ipc::connection*> open;  // each connection while its thread serves it
    std::size_t next = 0;
    std::vector<std::jthread> threads;
    std::jthread accepting;
    bool closed = false;

    void serve(os::ipc::connection connection) {
        std::size_t key = 0;
        {
            const std::scoped_lock hold(lock);
            key = next++;
            open[key] = &connection;
        }
        std::string arrived;
        while (!stopping) {
            std::optional<mlibs::http::Request> request;
            bool ended = false;
            while (!request && !stopping) {
                auto parsed = mlibs::http::parse(arrived);
                if (!parsed) {
                    ended = true;
                    break;
                }
                if (*parsed) {
                    request = std::move(*parsed);
                    break;
                }
                auto chunk = connection.read_for(std::chrono::milliseconds(250));
                if (!chunk || (*chunk && (*chunk)->empty())) {
                    ended = true;
                    break;
                }
                if (*chunk)
                    arrived += **chunk;
            }
            if (ended || !request)
                break;
            while (arrived.size() < request->head_size + request->body_length && !stopping) {
                auto chunk = connection.read_for(std::chrono::milliseconds(250));
                if (!chunk || (*chunk && (*chunk)->empty())) {
                    ended = true;
                    break;
                }
                if (*chunk)
                    arrived += **chunk;
            }
            if (ended || stopping)
                break;
            Arrived given{request->method, request->path, request->query, request->headers,
                          arrived.substr(request->head_size, request->body_length)};
            arrived.erase(0, request->head_size + request->body_length);
            const bool keep = lower(given.header("connection")) != "close";
            Reply reply = handler(given);
            if (!connection.write(mlibs::http::response(reply.status, reply.body,
                                                        {.type = reply.type,
                                                         .headers = reply.headers,
                                                         .keep_alive = keep})))
                break;
            if (!keep) {
                connection.shutdown_write();
                break;
            }
        }
        const std::scoped_lock hold(lock);
        open.erase(key);
    }
};

Server::Server(std::unique_ptr<State> state) : state_(std::move(state)) {}

std::expected<std::unique_ptr<Server>, std::string> Server::start(Handler handler) {
    auto state = std::make_unique<State>();
    auto listener = os::net::loopback_listener::listen(0);
    if (!listener)
        return std::unexpected(
            std::format("no server on the loopback: {}", listener.error().message()));
    state->listener = std::move(*listener);
    state->port = state->listener.port();
    state->handler = std::move(handler);
    State* shared = state.get();
    state->accepting = std::jthread([shared] {
        while (!shared->stopping) {
            auto accepted = shared->listener.accept();
            if (!accepted || shared->stopping)
                break;
            const std::scoped_lock hold(shared->lock);
            shared->threads
                .emplace_back([shared](os::ipc::connection
                                           connection) { shared->serve(std::move(connection)); },
                              std::move(*accepted));
        }
    });
    return std::make_unique<Server>(std::move(state));
}

std::string Server::url() const {
    return std::format("http://127.0.0.1:{}/", state_->port);
}
std::uint16_t Server::port() const {
    return state_->port;
}

void Server::close() {
    if (!state_ || state_->closed)
        return;
    state_->closed = true;
    state_->stopping = true;
    static_cast<void>(os::net::connect_loopback(state_->port));  // wakes the accept
    if (state_->accepting.joinable())
        state_->accepting.join();
    {
        const std::scoped_lock hold(state_->lock);
        for (auto& [_, connection] : state_->open)
            connection->shutdown();
    }
    std::vector<std::jthread> threads;
    {
        const std::scoped_lock hold(state_->lock);
        threads = std::move(state_->threads);
    }
    for (auto& thread : threads)
        if (thread.joinable())
            thread.join();
    state_->listener.close();
}

Server::~Server() {
    close();
}

Handler pages(fs::path directory) {
    struct Zipped {
        std::mutex lock;
        std::map<fs::path, std::shared_ptr<const std::string>> files;
    };
    auto zipped = std::make_shared<Zipped>();
    // Every file Pages compresses, compressed before the first request (several at once): a page's
    // timings then measure the page, not this server's deflate.
    {
        std::vector<fs::path> files;
        std::error_code error;
        for (const auto& entry : fs::recursive_directory_iterator(directory, error))
            if (entry.is_regular_file(error) &&
                compressed_kind(os::fs::utf8(entry.path().extension())))
                files.push_back(entry.path());
        std::atomic<std::size_t> next{0};
        std::vector<std::jthread> workers;
        const std::size_t count =
            std::clamp<std::size_t>(std::thread::hardware_concurrency() / 2, 1, 8);
        for (std::size_t worker = 0; worker < count; ++worker)
            workers.emplace_back([&] {
                for (std::size_t index = next++; index < files.size(); index = next++) {
                    auto packed = std::make_shared<const std::string>(
                        gzip(mcpplibs::os::fs::read(files[index]).value_or(""), 6));
                    const std::scoped_lock hold(zipped->lock);
                    zipped->files.emplace(files[index], std::move(packed));
                }
            });
    }
    return [directory = std::move(directory), zipped](const Arrived& request) -> Reply {
        const std::string relative = resolved_path(request.path);
        fs::path file = directory / os::fs::from_utf8(relative);
        std::error_code error;
        if (fs::is_directory(file, error))
            file /= "index.html";
        if (!fs::is_regular_file(file, error))
            return {.status = 404, .body = "", .type = ""};
        const std::string extension = os::fs::utf8(file.extension());
        Reply reply{.status = 200,
                    .body = "",
                    .type = std::string(pages_type(extension)),
                    .headers = {{"cache-control", "max-age=600"}}};
        if (compressed_kind(extension) && accepts_gzip(request.header("accept-encoding"))) {
            std::shared_ptr<const std::string> packed;
            {
                const std::scoped_lock hold(zipped->lock);
                if (const auto found = zipped->files.find(file); found != zipped->files.end())
                    packed = found->second;
            }
            if (!packed) {
                packed = std::make_shared<const std::string>(gzip(mcpplibs::os::fs::read(file).value_or(""), 6));
                const std::scoped_lock hold(zipped->lock);
                zipped->files.emplace(file, packed);
            }
            reply.body = *packed;
            reply.headers.emplace_back("content-encoding", "gzip");
        } else {
            reply.body = mcpplibs::os::fs::read(file).value_or("");
        }
        return reply;
    };
}

// --- the browser ---

std::chrono::milliseconds deadline() {
    if (const auto given = os::env::get("OMUI_CDP_TIMEOUT_S"); given && !given->empty()) {
        double seconds = 0;
        const auto [end, failed] =
            std::from_chars(given->data(), given->data() + given->size(), seconds);
        if (failed == std::errc{} && seconds > 0)
            return std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
    }
    return std::chrono::seconds(120);
}

struct Browser::State {
    os::process::process process;
    fs::path profile;
    bool own_profile = false;
    os::ipc::connection socket;
    std::jthread reader;
    std::mutex lock;
    std::condition_variable arrived;
    std::deque<json> inbox;
    bool ended = false;
    std::string end_reason;
    int next_id = 0;
    std::set<int> awaited;
    std::map<int, json> answers;  // for a call further out than the one waiting
    std::optional<std::string> stalled;
    std::vector<Listener> listeners;
    std::string version;
    bool closed = false;

    void send(std::string_view text) {
        const std::uint32_t word = random_word();
        const std::array<std::uint8_t, 4> mask{static_cast<std::uint8_t>(word),
                                               static_cast<std::uint8_t>(word >> 8),
                                               static_cast<std::uint8_t>(word >> 16),
                                               static_cast<std::uint8_t>(word >> 24)};
        static_cast<void>(socket.write(websocket::frame(websocket::Opcode::text, text, mask)));
    }

    void read_all(std::string arrived_bytes) {
        websocket::Assembler messages;
        const auto finish = [&](std::string reason) {
            const std::scoped_lock hold(lock);
            ended = true;
            end_reason = std::move(reason);
            arrived.notify_all();
        };
        for (;;) {
            for (;;) {
                auto parsed = websocket::parse(arrived_bytes);
                if (!parsed)
                    return finish(parsed.error());
                if (!*parsed)
                    break;
                const std::size_t used = (*parsed)->size;
                auto message = messages.add(std::move(**parsed));
                arrived_bytes.erase(0, used);
                if (!message)
                    return finish(message.error());
                if (!*message)
                    continue;
                auto& [opcode, payload] = **message;
                if (opcode == websocket::Opcode::ping) {
                    const std::uint32_t word = random_word();
                    static_cast<void>(
                        socket.write(websocket::frame(websocket::Opcode::pong, payload,
                                                      {static_cast<std::uint8_t>(word),
                                                       static_cast<std::uint8_t>(word >> 8),
                                                       static_cast<std::uint8_t>(word >> 16),
                                                       static_cast<std::uint8_t>(word >> 24)})));
                    continue;
                }
                if (opcode == websocket::Opcode::close)
                    return finish("the browser closed the connection");
                if (opcode != websocket::Opcode::text)
                    continue;
                auto value = json::parse(payload, nullptr, false);
                if (value.is_discarded())
                    continue;
                const std::scoped_lock hold(lock);
                inbox.push_back(std::move(value));
                arrived.notify_all();
            }
            auto chunk = socket.read();
            if (!chunk)
                return finish(
                    std::format("the connection to the browser: {}", chunk.error().message()));
            if (chunk->empty())
                return finish("the connection to the browser ended");
            arrived_bytes += *chunk;
        }
    }

    // One message delivered: an event to the listeners, an answer kept for its call. False when
    // none came by `until`.
    bool deliver_one(Clock::time_point until, int waiting_for, std::optional<json>& mine) {
        std::unique_lock hold(lock);
        if (!arrived.wait_until(hold, until, [&] { return !inbox.empty() || ended; }))
            return false;
        if (inbox.empty())
            return false;  // ended
        json message = std::move(inbox.front());
        inbox.pop_front();
        if (message.contains("id") && message["id"].is_number_integer()) {
            const int id = message["id"].get<int>();
            if (id == waiting_for)
                mine = std::move(message);
            else if (awaited.contains(id))
                answers[id] = std::move(message);
            return true;
        }
        if (message.contains("method") && message["method"].is_string()) {
            const auto listening = listeners;
            hold.unlock();
            const std::string method = message["method"].get<std::string>();
            const json& params = message.contains("params") ? message["params"] : json::object();
            for (const auto& listener : listening)
                listener(method, params);
        }
        return true;
    }
};

Browser::Browser(std::unique_ptr<State> state) : state_(std::move(state)) {}

std::expected<json, std::string> Browser::call(std::string_view method, json params) {
    auto& state = *state_;
    if (state.stalled)
        return std::unexpected(std::format("the page stopped answering earlier ({}); {} not sent",
                                           *state.stalled, method));
    std::string what(method);
    if (params.is_object() && params.contains("expression") && params["expression"].is_string()) {
        const std::string expression = params["expression"].get<std::string>();
        what += " " + expression.substr(0, 160);
    }
    int id = 0;
    {
        const std::scoped_lock hold(state.lock);
        if (state.ended)
            return std::unexpected(std::format("{}: {}", what, state.end_reason));
        id = ++state.next_id;
        state.awaited.insert(id);
    }
    state.send(json{{"id", id}, {"method", std::string(method)}, {"params", std::move(params)}}
                   .dump(-1, ' ', false, json::error_handler_t::replace));
    const auto limit = deadline();
    const auto until = Clock::now() + limit;
    std::optional<json> mine;
    for (;;) {
        {
            const std::scoped_lock hold(state.lock);
            if (const auto found = state.answers.find(id); found != state.answers.end()) {
                mine = std::move(found->second);
                state.answers.erase(found);
            }
        }
        if (mine)
            break;
        if (!state.deliver_one(until, id, mine)) {
            const std::scoped_lock hold(state.lock);
            state.awaited.erase(id);
            if (state.ended && !mine)
                return std::unexpected(std::format("{}: {}", what, state.end_reason));
            if (!mine) {
                state.stalled = what;
                return std::unexpected(
                    std::format("{}: no answer in {} s", what, limit.count() / 1000.0));
            }
        }
        if (mine)
            break;
    }
    const std::scoped_lock hold(state.lock);
    state.awaited.erase(id);
    return std::move(*mine);
}

void Browser::post(std::string_view method, json params) {
    auto& state = *state_;
    int id = 0;
    {
        const std::scoped_lock hold(state.lock);
        if (state.ended)
            return;
        id = ++state.next_id;
    }
    state.send(json{{"id", id}, {"method", std::string(method)}, {"params", std::move(params)}}
                   .dump(-1, ' ', false, json::error_handler_t::replace));
}

std::expected<json, std::string> Browser::value(std::string_view expression) {
    auto answer = call("Runtime.evaluate", {{"expression", std::string(expression)},
                                            {"awaitPromise", true},
                                            {"returnByValue", true}});
    if (!answer)
        return std::unexpected(answer.error());
    return dig(*answer, "result", "result", "value");
}

void Browser::on(Listener listener) {
    const std::scoped_lock hold(state_->lock);
    state_->listeners.push_back(std::move(listener));
}

void Browser::pump(std::chrono::milliseconds duration) {
    const auto until = Clock::now() + duration;
    std::optional<json> none;
    while (Clock::now() < until) {
        if (!state_->deliver_one(until, -1, none)) {
            const std::scoped_lock hold(state_->lock);
            if (!state_->ended)
                continue;
            break;
        }
    }
    const auto left = until - Clock::now();
    if (left.count() > 0)
        std::this_thread::sleep_for(left);
}

const std::string& Browser::version() const {
    return state_->version;
}

void Browser::close() {
    if (!state_ || state_->closed)
        return;
    auto& state = *state_;
    state.closed = true;
    static_cast<void>(
        state.socket.write(websocket::frame(websocket::Opcode::close, "", {1, 2, 3, 4})));
    state.process.terminate();
    const auto ended = state.process.wait_for(std::chrono::seconds(5));
    if (!ended || !*ended) {
        state.process.kill();
        static_cast<void>(state.process.wait());
    }
    state.socket.shutdown();
    if (state.reader.joinable())
        state.reader.join();
    if (state.own_profile) {
        // Its helpers may still be writing as it goes: retry, and leave a profile that will not go
        // rather than fail.
        std::error_code error;
        for (int attempt = 0; attempt < 10 && fs::exists(state.profile, error); ++attempt) {
            fs::remove_all(state.profile, error);
            if (fs::exists(state.profile, error))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

Browser::~Browser() {
    close();
}

std::expected<std::unique_ptr<Browser>, std::string> Browser::launch(const fs::path& chrome,
                                                                     const Launch& launch) {
    auto state = std::make_unique<State>();
    std::optional<std::string> given;
    for (const auto& flag : launch.flags)
        if (flag.starts_with("--user-data-dir="))
            given = flag.substr(std::string_view("--user-data-dir=").size());
    std::error_code error;
    if (given) {
        state->profile = os::fs::from_utf8(*given);
        fs::remove(state->profile / "DevToolsActivePort", error);
    } else {
        state->profile = fs::temp_directory_path() /
                         std::format("omui-chrome-{:08x}{:08x}", random_word(), random_word());
        fs::create_directories(state->profile, error);
        state->own_profile = true;
    }
    os::process::spawn_options options;
    options.program = chrome;
    options.args = {"--headless=new", "--no-sandbox", "--enable-unsafe-swiftshader",
                    "--remote-debugging-port=0"};
    if (!given)
        options.args.push_back(std::format("--user-data-dir={}", os::fs::utf8(state->profile)));
    options.args.insert(options.args.end(),
                        {"--no-first-run", "--no-default-browser-check",
                         std::format("--window-size={},{}", launch.width, launch.height)});
    options.args.insert(options.args.end(), launch.flags.begin(), launch.flags.end());
    options.args.push_back("about:blank");
    options.in = options.out = options.err = os::process::stdio::null;
    auto started = os::process::process::spawn(options);
    if (!started)
        return std::unexpected(
            std::format("{} did not start: {}", os::fs::utf8(chrome), started.error().message()));
    state->process = std::move(*started);
    const auto remove_profile = [&] {
        if (state->own_profile)
            fs::remove_all(state->profile, error);
    };

    // Chromium picks its own free port and writes it to DevToolsActivePort in its profile: no
    // guessed port to collide with, and a cold start may take as long as it takes (up to a minute)
    // while the process lives.
    std::uint16_t port = 0;
    json targets;
    std::optional<std::string> exited;
    for (int attempt = 0; attempt < 300 && targets.is_null(); ++attempt) {
        if (const auto status = state->process.try_wait(); status && *status) {
            exited = std::to_string((*status)->shell_code());
            break;
        }
        port = 0;
        if (const auto active = mcpplibs::os::fs::read(state->profile / "DevToolsActivePort")) {
            int number = 0;
            std::from_chars(active->data(), active->data() + active->size(), number);
            port = static_cast<std::uint16_t>(number);
        }
        if (port)
            if (const auto listed = get(port, "/json", std::chrono::seconds(5))) {
                auto parsed = json::parse(*listed, nullptr, false);
                if (!parsed.is_discarded() && parsed.is_array())
                    targets = std::move(parsed);
            }
        if (targets.is_null())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (targets.is_null()) {
        state->process.kill();
        static_cast<void>(state->process.wait());
        remove_profile();
        return std::unexpected(std::format("{} did not open its DevTools port{}",
                                           os::fs::utf8(chrome),
                                           exited ? " (it exited: " + *exited + ")" : " in 60 s"));
    }
    std::string socket_url;
    for (const auto& target : targets)
        if (text(at(target, "type")) == "page") {
            socket_url = text(at(target, "webSocketDebuggerUrl"));
            break;
        }
    const std::size_t path_at = socket_url.find('/', std::string_view("ws://").size());
    if (socket_url.empty() || path_at == std::string::npos) {
        state->process.kill();
        static_cast<void>(state->process.wait());
        remove_profile();
        return std::unexpected("the browser lists no page to drive");
    }
    auto connection = os::net::connect_loopback(port);
    if (!connection)
        return std::unexpected(
            std::format("the page's WebSocket: {}", connection.error().message()));
    std::string key_bytes;
    for (int index = 0; index < 4; ++index) {
        const std::uint32_t word = random_word();
        key_bytes.append(reinterpret_cast<const char*>(&word), 4);
    }
    const std::string key = mlibs::base64::encode(
        std::span(reinterpret_cast<const std::uint8_t*>(key_bytes.data()), key_bytes.size()));
    static_cast<void>(connection->write(mlibs::http::request("GET", socket_url.substr(path_at),
                                                             std::format("127.0.0.1:{}", port),
                                                             {{"Upgrade", "websocket"},
                                                              {"Connection", "Upgrade"},
                                                              {"Sec-WebSocket-Key", key},
                                                              {"Sec-WebSocket-Version", "13"}})));
    std::string arrived;
    std::optional<mlibs::http::Response> head;
    while (!head) {
        auto chunk = connection->read_for(std::chrono::seconds(30));
        if (!chunk || !*chunk || (*chunk)->empty())
            return std::unexpected("the page's WebSocket did not answer its handshake");
        arrived += **chunk;
        auto parsed = mlibs::http::parse_response(arrived);
        if (!parsed)
            return std::unexpected(parsed.error());
        head = std::move(*parsed);
    }
    if (head->status != 101 || head->header("sec-websocket-accept") != websocket::accept(key))
        return std::unexpected(
            std::format("the page's WebSocket refused the handshake ({})", head->status));
    state->socket = std::move(*connection);
    State* shared = state.get();
    state->reader =
        std::jthread([shared, rest = arrived.substr(head->head_size)] { shared->read_all(rest); });

    auto browser = std::make_unique<Browser>(std::move(state));
    if (const auto version = get(port, "/json/version")) {
        const auto parsed = json::parse(*version, nullptr, false);
        browser->state_->version = text(at(parsed, "Browser"));
    }
    for (const auto& [method, params] :
         std::vector<std::pair<std::string, json>>{{"Page.enable", json::object()},
                                                   {"Runtime.enable", json::object()},
                                                   {"Emulation.setDeviceMetricsOverride",
                                                    {{"width", launch.width},
                                                     {"height", launch.height},
                                                     {"deviceScaleFactor", 1},
                                                     {"mobile", false}}}})
        if (const auto answer = browser->call(method, params); !answer)
            return std::unexpected(answer.error());
    return browser;
}

// --- the JavaScript value helpers ---

namespace {
const json& null_value() {
    static const json none;
    return none;
}
}  // namespace

const json& at(const json& value, std::string_view key) {
    if (!value.is_object())
        return null_value();
    const auto found = value.find(key);
    return found == value.end() ? null_value() : *found;
}

const json& at(const json& value, std::size_t index) {
    if (!value.is_array() || index >= value.size())
        return null_value();
    return value[index];
}

bool truthy(const json& value) {
    if (value.is_null() || value.is_discarded())
        return false;
    if (value.is_boolean())
        return value.get<bool>();
    if (value.is_number()) {
        const double number = value.get<double>();
        return number != 0 && !std::isnan(number);
    }
    if (value.is_string())
        return !value.get_ref<const std::string&>().empty();
    return true;
}

double number(const json& value) {
    return value.is_number() ? value.get<double>() : std::numeric_limits<double>::quiet_NaN();
}

std::string text(const json& value) {
    return value.is_string() ? value.get<std::string>() : std::string{};
}

bool includes(const json& value, std::string_view item) {
    if (value.is_string())
        return value.get_ref<const std::string&>().contains(item);
    if (value.is_array())
        for (const auto& element : value)
            if (element.is_string() && element.get_ref<const std::string&>() == item)
                return true;
    return false;
}

std::string q(std::string_view text) {
    return json(std::string(text)).dump(-1, ' ', false, json::error_handler_t::replace);
}
std::string stringify(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}
std::string pretty(const json& value, int indent) {
    return value.dump(indent, ' ', false, json::error_handler_t::replace);
}

const json& find_id(const json& nodes, std::string_view id) {
    if (nodes.is_array())
        for (const auto& node : nodes)
            if (text(at(node, "id")) == id)
                return node;
    return null_value();
}

std::optional<std::array<double, 4>> box(const json& value) {
    if (!value.is_array() || value.size() != 4)
        return std::nullopt;
    std::array<double, 4> out{};
    for (std::size_t index = 0; index < 4; ++index) {
        if (!value[index].is_number())
            return std::nullopt;
        out[index] = value[index].get<double>();
        if (!std::isfinite(out[index]))
            return std::nullopt;
    }
    return out;
}

std::string js_number(double value) {
    if (std::isnan(value))
        return "NaN";
    if (value == std::floor(value) && std::abs(value) < 1e15)
        return std::format("{}", static_cast<long long>(value));
    return std::format("{}", value);
}

std::string from_base64(std::string_view base64) {
    const auto bytes = mlibs::base64::decode(base64);
    if (!bytes)
        return {};
    return std::string(bytes->begin(), bytes->end());
}

bool write_base64(const fs::path& path, std::string_view base64) {
    const auto bytes = mlibs::base64::decode(base64);
    if (!bytes)
        return false;
    std::error_code error;
    if (path.has_parent_path())
        fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes->data()),
              static_cast<std::streamsize>(bytes->size()));
    return static_cast<bool>(out);
}

std::string error_text(std::string_view message) {
    return "Error: " + std::string(message);
}

// --- a check's run ---

Script::Script(bool detail_when_falsy) : detail_when_falsy_(detail_when_falsy) {}

bool Script::check(bool ok, std::string_view what, std::optional<json> detail) {
    if (thrown_)
        return ok;
    json row = {{"ok", ok}, {"what", std::string(what)}};
    if (detail && (detail_when_falsy_ ? !detail->is_discarded() : truthy(*detail)))
        row["detail"] = std::move(*detail);
    report["checks"].push_back(std::move(row));
    return ok;
}

void Script::record(json row) {
    if (!thrown_)
        report["checks"].push_back(std::move(row));
}

void Script::raise(std::string message) {
    if (!thrown_)
        thrown_ = std::move(message);
}

std::optional<std::string> Script::take() {
    auto out = std::move(thrown_);
    thrown_.reset();
    return out;
}

bool Script::guard(std::string_view failed, const std::function<void()>& body) {
    return attempt(body,
                   [&](const std::string& message) { check(false, failed, error_text(message)); });
}

bool Script::attempt(const std::function<void()>& body,
                     const std::function<void(const std::string&)>& handler) {
    if (thrown_)
        return false;  // an error already on its way out: this block is not entered
    body();
    const auto error = take();
    if (!error)
        return true;
    handler(*error);
    return false;
}

void Script::finally(const std::function<void()>& body) {
    auto held = take();
    body();
    if (!thrown_)
        thrown_ = std::move(held);
}

json Script::call(std::string_view method, json params) {
    if (thrown_ || !page) {
        if (!page)
            raise("no browser");
        return nullptr;
    }
    auto answer = page->call(method, std::move(params));
    if (!answer) {
        raise(answer.error());
        return nullptr;
    }
    return std::move(*answer);
}

json Script::dispatch(std::string_view method, json params) {
    json answer = call(method, std::move(params));
    if (answer.is_object() && answer.contains("error"))
        raise(text(dig(answer, "error", "message")));
    return answer;
}

json Script::value(std::string_view expression) {
    if (thrown_ || !page) {
        if (!page)
            raise("no browser");
        return nullptr;
    }
    auto answer = page->value(expression);
    if (!answer) {
        raise(answer.error());
        return nullptr;
    }
    return std::move(*answer);
}

void Script::post(std::string_view method, json params) {
    if (page)
        page->post(method, std::move(params));
}

void Script::sleep(int milliseconds) {
    if (thrown_)
        return;
    if (page)
        page->pump(std::chrono::milliseconds(milliseconds));
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool Script::until_count(std::string_view expression, int count, int step_ms) {
    return until_count([&] { return truthy(value(expression)); }, count, step_ms);
}

bool Script::until_count(const std::function<bool()>& predicate, int count, int step_ms) {
    for (int index = 0; index < count && !thrown_; ++index) {
        if (predicate())
            return !thrown_;
        sleep(step_ms);
    }
    return false;
}

bool Script::until_time(const std::function<bool()>& predicate, int timeout_ms, int step_ms) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < end && !thrown_) {
        if (predicate())
            return !thrown_;
        sleep(step_ms);
    }
    return false;
}

bool Script::until_time(std::string_view expression, int timeout_ms, int step_ms) {
    return until_time([&] { return truthy(value(expression)); }, timeout_ms, step_ms);
}

std::string Script::screenshot(const fs::path& path, json params, bool required) {
    const json answer = call("Page.captureScreenshot", std::move(params));
    const std::string data = text(dig(answer, "result", "data"));
    if (data.empty()) {
        if (required)
            raise(std::format("Screenshot failed: {}", os::fs::utf8(path.stem())));
        return {};
    }
    write_base64(path, data);
    return data;
}

bool Script::all_ok(bool at_least_one) const {
    const auto& checks = report.at("checks");
    if (at_least_one && checks.empty())
        return false;
    return std::ranges::all_of(checks, [](const json& row) { return truthy(at(row, "ok")); });
}

}  // namespace omui::devtools::cdp
