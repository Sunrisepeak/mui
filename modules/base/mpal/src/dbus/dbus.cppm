// mpal:dbus — D-Bus spoken over a local connection (the specification's wire
// format, little-endian): values marshalled by signature with their
// alignment, the EXTERNAL authentication (the bus reads who connects from the
// connection itself), a method call and its reply, and the signals a service
// sends (asked for by a match rule). Enough for a service's conversation; no
// libdbus, no sd-bus.
export module mpal:dbus;

import std;
import mcpplibs.os;
import :error;

export namespace mpal::dbus {

// A value as D-Bus carries it. A variant holds one value with its signature.
struct Value;
using Variant = std::pair<std::string, std::shared_ptr<Value>>;  // signature, value
struct Value {
    std::variant<std::monostate, std::uint8_t, bool, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t, double, std::string,
                 std::vector<Value>, Variant>
        data;
    static Value of(std::string text) { return {std::move(text)}; }
};

// The bytes of `values` laid out by `signature` from `offset` (where the body begins: alignment counts from the message's start).
[[nodiscard]] std::expected<std::string, Error> marshal(std::string_view signature, std::span<const Value> values, std::size_t offset = 0);
// The values `signature` describes, read from `bytes` (alignment from `offset`).
[[nodiscard]] std::expected<std::vector<Value>, Error> unmarshal(std::string_view signature, std::string_view bytes, std::size_t offset = 0);

struct Call {
    std::string destination, path, interface, member, signature;
    std::vector<Value> arguments;
};

enum class Kind : std::uint8_t { call = 1, reply = 2, error = 3, signal = 4 };

struct Message {
    Kind               kind = Kind::call;
    std::uint32_t      serial = 0, reply_to = 0;
    std::string        path, interface, member, error_name, destination, sender, signature;
    std::vector<Value> body;
};

[[nodiscard]] std::string message_bytes(const Call& call, std::uint32_t serial);
// One whole message at the front of `bytes`: it and how many bytes it took;
// none yet while more is needed.
[[nodiscard]] std::expected<std::optional<std::pair<Message, std::size_t>>, Error> read_message(std::string_view bytes);

// A connection to a bus, authenticated and said hello to.
class Bus {
public:
    // The session bus ($DBUS_SESSION_BUS_ADDRESS, else $XDG_RUNTIME_DIR/bus).
    [[nodiscard]] static std::expected<Bus, Error> session();
    // A bus at a local socket's directory and name (a test's own).
    [[nodiscard]] static std::expected<Bus, Error> at(const std::filesystem::path& directory, std::string_view name);

    // A method call and its reply's body (an error reply is the error).
    [[nodiscard]] std::expected<std::vector<Value>, Error> call(const Call& call, std::chrono::milliseconds wait = std::chrono::milliseconds(2000));
    // Asks the bus for the signals `rule` matches (AddMatch: "type='signal',interface='…'").
    [[nodiscard]] std::expected<void, Error> add_match(std::string_view rule);
    // The next signal, waiting at most `wait`; none when none came. Signals that
    // arrived while a call waited for its reply are kept for this, in order.
    [[nodiscard]] std::expected<std::optional<Message>, Error> next_signal(std::chrono::milliseconds wait);
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    // One message read, waiting at most until `until`; none when the time ran out.
    [[nodiscard]] std::expected<std::optional<Message>, Error> receive(std::chrono::steady_clock::time_point until);

    mcpplibs::os::ipc::connection connection_;
    std::deque<Message>           signals_;
    std::string                   pending_;
    std::uint32_t                 serial_ = 0;
    std::string                   name_;  // the unique name Hello gave
};

}  // namespace mpal::dbus
