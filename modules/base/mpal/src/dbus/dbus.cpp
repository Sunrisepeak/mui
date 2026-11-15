// D-Bus spoken over a local connection (mpal:dbus).
module mpal;

import std;
import mcpplibs.os;

namespace mpal::dbus {

namespace {

// Where a single complete type ends in a signature (from `at`), or npos.
std::size_t type_end(std::string_view signature, std::size_t at) {
    if (at >= signature.size()) return std::string_view::npos;
    const char code = signature[at];
    if (code == 'a') return type_end(signature, at + 1);
    if (code == '(' || code == '{') {
        const char close = code == '(' ? ')' : '}';
        std::size_t next = at + 1;
        while (next < signature.size() && signature[next] != close) {
            next = type_end(signature, next);
            if (next == std::string_view::npos) return next;
        }
        return next < signature.size() ? next + 1 : std::string_view::npos;
    }
    return std::string_view("ybnqiuxtdsogv").contains(code) ? at + 1 : std::string_view::npos;
}

std::size_t alignment(char code) {
    switch (code) {
    case 'n': case 'q': return 2;
    case 'b': case 'i': case 'u': case 's': case 'o': case 'a': return 4;
    case 'x': case 't': case 'd': case '(': case '{': return 8;
    default: return 1;  // y g v
    }
}

struct Writer {
    std::string bytes;
    std::size_t offset;  // where `bytes` begins in the message
    void pad(std::size_t align) {
        while ((offset + bytes.size()) % align != 0) bytes.push_back('\0');
    }
    template <class T>
    void raw(T value) {
        pad(sizeof(T));
        auto bits = std::bit_cast<std::array<char, sizeof(T)>>(value);  // the machine's order: little-endian (the header says so)
        if constexpr (std::endian::native == std::endian::big) std::ranges::reverse(bits);
        bytes.append(bits.data(), bits.size());
    }
    void text(std::string_view value) {
        raw(static_cast<std::uint32_t>(value.size()));
        bytes.append(value);
        bytes.push_back('\0');
    }
    void signature(std::string_view value) {
        bytes.push_back(static_cast<char>(value.size()));
        bytes.append(value);
        bytes.push_back('\0');
    }

    std::optional<Error> put(std::string_view type, const Value& value) {
        const char code = type.front();
        const auto wrong = [&] { return Error{Error::Code::failed, std::format("a value that is not a `{}`", type)}; };
        switch (code) {
        case 'y': if (auto* v = std::get_if<std::uint8_t>(&value.data)) { bytes.push_back(static_cast<char>(*v)); return {}; } return wrong();
        case 'b': if (auto* v = std::get_if<bool>(&value.data)) { raw(static_cast<std::uint32_t>(*v ? 1 : 0)); return {}; } return wrong();
        case 'i': if (auto* v = std::get_if<std::int32_t>(&value.data)) { raw(*v); return {}; } return wrong();
        case 'u': if (auto* v = std::get_if<std::uint32_t>(&value.data)) { raw(*v); return {}; } return wrong();
        case 'x': if (auto* v = std::get_if<std::int64_t>(&value.data)) { raw(*v); return {}; } return wrong();
        case 't': if (auto* v = std::get_if<std::uint64_t>(&value.data)) { raw(*v); return {}; } return wrong();
        case 'd': if (auto* v = std::get_if<double>(&value.data)) { raw(*v); return {}; } return wrong();
        case 's': case 'o': if (auto* v = std::get_if<std::string>(&value.data)) { text(*v); return {}; } return wrong();
        case 'g': if (auto* v = std::get_if<std::string>(&value.data)) { signature(*v); return {}; } return wrong();
        case 'v': {
            auto* v = std::get_if<Variant>(&value.data);
            if (!v || !v->second) return wrong();
            signature(v->first);
            return put(v->first, *v->second);
        }
        case 'a': {
            auto* items = std::get_if<std::vector<Value>>(&value.data);
            if (!items) return wrong();
            const std::string_view element = type.substr(1);
            raw(std::uint32_t{0});
            const std::size_t length_at = bytes.size() - 4;
            pad(alignment(element.front()));
            const std::size_t first = bytes.size();
            for (const Value& item : *items)
                if (auto problem = put(element, item)) return problem;
            const auto length = static_cast<std::uint32_t>(bytes.size() - first);
            auto bits = std::bit_cast<std::array<char, 4>>(length);
            if constexpr (std::endian::native == std::endian::big) std::ranges::reverse(bits);
            std::ranges::copy(bits, bytes.begin() + static_cast<std::ptrdiff_t>(length_at));
            return {};
        }
        case '(': case '{': {
            auto* fields = std::get_if<std::vector<Value>>(&value.data);
            if (!fields) return wrong();
            pad(8);
            std::size_t at = 1, index = 0;
            while (type[at] != ')' && type[at] != '}') {
                const std::size_t end = type_end(type, at);
                if (index >= fields->size()) return wrong();
                if (auto problem = put(type.substr(at, end - at), (*fields)[index++])) return problem;
                at = end;
            }
            return {};
        }
        default: return Error{Error::Code::failed, std::format("no D-Bus type `{}`", code)};
        }
    }
};

struct Reader {
    std::string_view bytes;
    std::size_t      at = 0, offset = 0;
    bool             failed = false;
    bool skip_to(std::size_t align) {
        while ((offset + at) % align != 0) ++at;
        if (at > bytes.size()) failed = true;
        return !failed;
    }
    template <class T>
    T raw() {
        if (!skip_to(sizeof(T)) || bytes.size() - at < sizeof(T)) {
            failed = true;
            return T{};
        }
        std::array<char, sizeof(T)> bits{};
        std::copy_n(bytes.data() + at, sizeof(T), bits.begin());
        if constexpr (std::endian::native == std::endian::big) std::ranges::reverse(bits);
        at += sizeof(T);
        return std::bit_cast<T>(bits);
    }
    std::string text() {
        const auto size = raw<std::uint32_t>();
        if (failed || bytes.size() - at < size + 1) {
            failed = true;
            return {};
        }
        std::string out(bytes.substr(at, size));
        at += size + 1;
        return out;
    }
    std::string signature() {
        if (at >= bytes.size()) {
            failed = true;
            return {};
        }
        const auto size = static_cast<std::uint8_t>(bytes[at++]);
        if (bytes.size() - at < size + 1u) {
            failed = true;
            return {};
        }
        std::string out(bytes.substr(at, size));
        at += size + 1;
        return out;
    }
    Value get(std::string_view type, int depth = 0) {
        if (depth > 32) {
            failed = true;
            return {};
        }
        switch (type.front()) {
        case 'y': if (at < bytes.size()) return {static_cast<std::uint8_t>(bytes[at++])}; failed = true; return {};
        case 'b': return {raw<std::uint32_t>() != 0};
        case 'n': return {static_cast<std::int32_t>(raw<std::int16_t>())};
        case 'q': return {static_cast<std::uint32_t>(raw<std::uint16_t>())};
        case 'i': return {raw<std::int32_t>()};
        case 'u': return {raw<std::uint32_t>()};
        case 'x': return {raw<std::int64_t>()};
        case 't': return {raw<std::uint64_t>()};
        case 'd': return {raw<double>()};
        case 's': case 'o': return {text()};
        case 'g': return {signature()};
        case 'v': {
            std::string inner = signature();
            if (failed || type_end(inner, 0) != inner.size()) {
                failed = true;
                return {};
            }
            auto held = std::make_shared<Value>(get(inner, depth + 1));
            return {Variant{std::move(inner), std::move(held)}};
        }
        case 'a': {
            const auto length = raw<std::uint32_t>();
            const std::string_view element = type.substr(1);
            skip_to(alignment(element.front()));
            const std::size_t end = at + length;
            std::vector<Value> items;
            if (failed || end > bytes.size()) {
                failed = true;
                return {};
            }
            while (!failed && at < end) items.push_back(get(element, depth + 1));
            return {std::move(items)};
        }
        case '(': case '{': {
            skip_to(8);
            std::vector<Value> fields;
            std::size_t position = 1;
            while (!failed && type[position] != ')' && type[position] != '}') {
                const std::size_t next = type_end(type, position);
                fields.push_back(get(type.substr(position, next - position), depth + 1));
                position = next;
            }
            return {std::move(fields)};
        }
        default: failed = true; return {};
        }
    }
};

const std::string* text_of(const Value& value) { return std::get_if<std::string>(&value.data); }

}  // namespace

std::expected<std::string, Error> marshal(std::string_view signature, std::span<const Value> values, std::size_t offset) {
    Writer out{{}, offset};
    std::size_t at = 0, index = 0;
    while (at < signature.size()) {
        const std::size_t end = type_end(signature, at);
        if (end == std::string_view::npos) return failure(Error::Code::failed, std::format("`{}` is not a D-Bus signature", signature));
        if (index >= values.size()) return failure(Error::Code::failed, std::format("`{}` wants more than {} values", signature, values.size()));
        if (auto problem = out.put(signature.substr(at, end - at), values[index++])) return std::unexpected(*problem);
        at = end;
    }
    return std::move(out.bytes);
}

std::expected<std::vector<Value>, Error> unmarshal(std::string_view signature, std::string_view bytes, std::size_t offset) {
    Reader in{bytes, 0, offset};
    std::vector<Value> out;
    std::size_t at = 0;
    while (at < signature.size() && !in.failed) {
        const std::size_t end = type_end(signature, at);
        if (end == std::string_view::npos) return failure(Error::Code::failed, std::format("`{}` is not a D-Bus signature", signature));
        out.push_back(in.get(signature.substr(at, end - at)));
        at = end;
    }
    if (in.failed) return failure(Error::Code::failed, "a message body shorter than its signature says");
    return out;
}

std::string message_bytes(const Call& call, std::uint32_t serial) {
    // The header fields, as a(yv): path 1 (o), interface 2 (s), member 3 (s), destination 6 (s), signature 8 (g).
    const auto field = [](std::uint8_t code, std::string signature, std::string text) {
        return Value{std::vector<Value>{Value{code}, Value{Variant{std::move(signature), std::make_shared<Value>(Value{std::move(text)})}}}};
    };
    std::vector<Value> fields{field(1, "o", call.path), field(3, "s", call.member)};
    if (!call.interface.empty()) fields.push_back(field(2, "s", call.interface));
    if (!call.destination.empty()) fields.push_back(field(6, "s", call.destination));
    if (!call.signature.empty()) fields.push_back(field(8, "g", call.signature));
    const auto body = marshal(call.signature, call.arguments, 0).value_or(std::string());
    std::string header;
    header.push_back('l');
    header.push_back(static_cast<char>(Kind::call));
    header.push_back('\0');  // flags
    header.push_back('\1');  // version
    auto put32 = [&](std::uint32_t value) {
        for (int index = 0; index < 4; ++index) header.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
    };
    put32(static_cast<std::uint32_t>(body.size()));
    put32(serial);
    header += marshal("a(yv)", std::array{Value{std::move(fields)}}, header.size()).value_or(std::string());
    while (header.size() % 8 != 0) header.push_back('\0');
    return header + body;
}

std::expected<std::optional<std::pair<Message, std::size_t>>, Error> read_message(std::string_view bytes) {
    if (bytes.size() < 16) return std::optional<std::pair<Message, std::size_t>>{};
    if (bytes[0] != 'l') return failure(Error::Code::failed, "a big-endian message (this reads little-endian)");
    Reader head{bytes, 4, 0};
    const auto body_length = head.raw<std::uint32_t>();
    const auto serial = head.raw<std::uint32_t>();
    const auto fields_length = head.raw<std::uint32_t>();
    std::size_t header_end = 16 + fields_length;
    header_end = (header_end + 7) / 8 * 8;
    const std::size_t total = header_end + body_length;
    if (bytes.size() < total) return std::optional<std::pair<Message, std::size_t>>{};
    Reader fields{bytes.substr(0, 16 + fields_length), 12, 0};
    const Value table = fields.get("a(yv)");
    if (fields.failed) return failure(Error::Code::failed, "a message whose header fields cannot be read");
    Message message;
    message.kind = static_cast<Kind>(bytes[1]);
    message.serial = serial;
    for (const Value& entry : std::get<std::vector<Value>>(table.data)) {
        const auto& pair = std::get<std::vector<Value>>(entry.data);
        const auto code = std::get<std::uint8_t>(pair[0].data);
        const Variant& held = std::get<Variant>(pair[1].data);
        const Value& value = *held.second;
        switch (code) {
        case 1: if (auto* text = text_of(value)) message.path = *text; break;
        case 2: if (auto* text = text_of(value)) message.interface = *text; break;
        case 3: if (auto* text = text_of(value)) message.member = *text; break;
        case 4: if (auto* text = text_of(value)) message.error_name = *text; break;
        case 5: if (auto* number = std::get_if<std::uint32_t>(&value.data)) message.reply_to = *number; break;
        case 6: if (auto* text = text_of(value)) message.destination = *text; break;
        case 7: if (auto* text = text_of(value)) message.sender = *text; break;
        case 8: if (auto* text = text_of(value)) message.signature = *text; break;
        default: break;
        }
    }
    auto body = unmarshal(message.signature, bytes.substr(header_end, body_length), header_end);
    if (!body) return std::unexpected(body.error());
    message.body = std::move(*body);
    return std::optional(std::pair{std::move(message), total});
}

std::expected<Bus, Error> Bus::session() {
    std::string address = mcpplibs::os::env::get("DBUS_SESSION_BUS_ADDRESS").value_or("");
    if (address.empty()) {
        const auto runtime = mcpplibs::os::env::get("XDG_RUNTIME_DIR");
        if (!runtime || runtime->empty()) return failure(Error::Code::unavailable, "no session bus: neither DBUS_SESSION_BUS_ADDRESS nor XDG_RUNTIME_DIR is set");
        address = std::format("unix:path={}/bus", *runtime);
    }
    // The first address that is a socket path (an abstract socket is not reachable by path).
    for (const auto entry : std::views::split(address, ';')) {
        const std::string_view one(entry.begin(), entry.end());
        const std::size_t path = one.find("path=");
        if (!one.starts_with("unix:") || path == std::string_view::npos) continue;
        std::string_view socket = one.substr(path + 5);
        socket = socket.substr(0, socket.find(','));
        const std::filesystem::path file = mcpplibs::os::fs::from_utf8(socket);
        return at(file.parent_path(), mcpplibs::os::fs::utf8(file.filename()));
    }
    return failure(Error::Code::unavailable, std::format("no session bus reachable by a path in `{}`", address));
}

std::expected<Bus, Error> Bus::at(const std::filesystem::path& directory, std::string_view name) {
    auto connected = mcpplibs::os::ipc::connection::connect(directory, name);
    if (!connected) return failure(Error::Code::unavailable, std::format("the bus at {}: {}", mcpplibs::os::fs::generic_utf8(directory / name), connected.error().message()));
    Bus bus;
    bus.connection_ = std::move(*connected);
    // EXTERNAL without data: the bus knows who connects from the connection itself.
    const auto exchange = [&](std::string_view say) -> std::expected<std::string, Error> {
        if (auto written = bus.connection_.write(say); !written) return failure(Error::Code::failed, written.error().message());
        std::string line;
        while (!line.ends_with("\r\n")) {
            auto chunk = bus.connection_.read_for(std::chrono::milliseconds(2000), 1);
            if (!chunk || !*chunk || (*chunk)->empty()) return failure(Error::Code::failed, "the bus did not answer the authentication");
            line += **chunk;
        }
        return line;
    };
    auto first = exchange(std::string_view("\0AUTH EXTERNAL\r\n", 16));
    if (!first) return std::unexpected(first.error());
    if (first->starts_with("DATA")) first = exchange("DATA\r\n");
    if (!first) return std::unexpected(first.error());
    if (!first->starts_with("OK")) return failure(Error::Code::refused, std::format("the bus refused the authentication: {}", *first));
    if (auto begun = bus.connection_.write("BEGIN\r\n"); !begun) return failure(Error::Code::failed, begun.error().message());
    auto hello = bus.call({.destination = "org.freedesktop.DBus", .path = "/org/freedesktop/DBus", .interface = "org.freedesktop.DBus", .member = "Hello"});
    if (!hello) return std::unexpected(hello.error());
    if (!hello->empty())
        if (auto* unique = text_of(hello->front())) bus.name_ = *unique;
    return bus;
}

std::expected<std::optional<Message>, Error> Bus::receive(std::chrono::steady_clock::time_point until) {
    while (true) {
        auto message = read_message(pending_);
        if (!message) return std::unexpected(message.error());
        if (*message) {
            auto [read, used] = std::move(**message);
            pending_.erase(0, used);
            return std::optional(std::move(read));
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return std::optional<Message>{};
        auto chunk = connection_.read_for(left);
        if (!chunk) return failure(Error::Code::failed, chunk.error().message());
        if (*chunk) {
            if ((*chunk)->empty()) return failure(Error::Code::failed, "the bus closed the connection");
            pending_ += **chunk;
        }
    }
}

std::expected<std::vector<Value>, Error> Bus::call(const Call& asked, std::chrono::milliseconds wait) {
    const std::uint32_t serial = ++serial_;
    if (auto written = connection_.write(message_bytes(asked, serial)); !written) return failure(Error::Code::failed, written.error().message());
    const auto until = std::chrono::steady_clock::now() + wait;
    while (true) {
        auto received = receive(until);
        if (!received) return std::unexpected(received.error());
        if (!*received) return failure(Error::Code::failed, std::format("`{}` was not answered in time", asked.member));
        Message& read = **received;
        if (read.kind == Kind::signal) {
            signals_.push_back(std::move(read));
            continue;
        }
        if (read.reply_to != serial) continue;  // another's reply
        if (read.kind == Kind::error) {
            std::string why = read.error_name;
            if (!read.body.empty())
                if (auto* text = text_of(read.body.front())) why += ": " + *text;
            return failure(Error::Code::refused, why);
        }
        return std::move(read.body);
    }
}

std::expected<void, Error> Bus::add_match(std::string_view rule) {
    auto added = call({.destination = "org.freedesktop.DBus", .path = "/org/freedesktop/DBus", .interface = "org.freedesktop.DBus",
                       .member = "AddMatch", .signature = "s", .arguments = {Value::of(std::string(rule))}});
    if (!added) return std::unexpected(added.error());
    return {};
}

std::expected<std::optional<Message>, Error> Bus::next_signal(std::chrono::milliseconds wait) {
    const auto until = std::chrono::steady_clock::now() + wait;
    while (signals_.empty()) {
        auto received = receive(until);
        if (!received) return std::unexpected(received.error());
        if (!*received) return std::optional<Message>{};
        if ((*received)->kind == Kind::signal) signals_.push_back(std::move(**received));
    }
    Message next = std::move(signals_.front());
    signals_.pop_front();
    return std::optional(std::move(next));
}

}  // namespace mpal::dbus
