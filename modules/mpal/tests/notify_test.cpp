// mpal's notifications, spoken as the freedesktop service hears them: a call
// laid out by the D-Bus specification's alignment, the EXTERNAL
// authentication, Hello, then Notify with every argument where it belongs —
// against a bus of the test's own (a local socket), so it runs anywhere.
import std;
import mpal;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace dbus = mpal::dbus;

namespace {

// A reply: kind 2, its serial, REPLY_SERIAL (5, u) and SIGNATURE (8, g) fields.
std::string reply_bytes(std::uint32_t serial, std::uint32_t reply_to, std::string_view signature, std::span<const dbus::Value> body_values) {
    const auto body = dbus::marshal(signature, body_values).value();
    using dbus::Value;
    std::vector<Value> fields{
        Value{std::vector<Value>{Value{std::uint8_t{5}}, Value{dbus::Variant{"u", std::make_shared<Value>(Value{reply_to})}}}},
        Value{std::vector<Value>{Value{std::uint8_t{8}}, Value{dbus::Variant{"g", std::make_shared<Value>(Value::of(std::string(signature)))}}}}};
    std::string header{'l', '\2', '\0', '\1'};
    for (const std::uint32_t word : {static_cast<std::uint32_t>(body.size()), serial})
        for (int index = 0; index < 4; ++index) header.push_back(static_cast<char>((word >> (8 * index)) & 0xFF));
    header += dbus::marshal("a(yv)", std::array{Value{std::move(fields)}}, header.size()).value();
    while (header.size() % 8 != 0) header.push_back('\0');
    return header + body;
}

}  // namespace

UTest cases{"mpal.notify", [] {
    "a call laid out as the specification says: header, fields 8-aligned, strings with their length and nul"_test = [] {
        const std::string bytes = dbus::message_bytes({.destination = "org.freedesktop.DBus", .path = "/org/freedesktop/DBus",
                                                       .interface = "org.freedesktop.DBus", .member = "Hello"}, 1);
        expect(bytes.substr(0, 4) == std::string_view("l\1\0\1", 4)) << "little-endian, a method call, version 1";
        expect(bytes[16] == '\1' && bytes.substr(17, 3) == std::string_view("\1o\0", 3)) << "the first field: PATH, an object path";
        expect(eq(static_cast<int>(static_cast<unsigned char>(bytes[20])), 21) && bytes.substr(24, 21) == "/org/freedesktop/DBus") << "its length, then itself";
        expect(eq(bytes.size() % 8, std::size_t{0})) << "no body: the header padded to 8";
        const auto read = dbus::read_message(bytes);
        expect(read && *read && (*read)->first.member == "Hello" && (*read)->first.destination == "org.freedesktop.DBus" && (*read)->second == bytes.size());
        // Values of every kind there and back, from an odd offset (alignment counts from the message's start).
        using dbus::Value;
        const std::array values{Value{std::uint8_t{7}}, Value{true}, Value{std::int32_t{-5}}, Value{std::uint64_t{1} << 40}, Value{2.5}, Value::of("hé"),
                                Value{std::vector<Value>{Value{std::vector<Value>{Value::of("k"), Value{dbus::Variant{"i", std::make_shared<Value>(Value{std::int32_t{9}})}}}}}}};
        const auto laid = dbus::marshal("ybitdsa{sv}", values, 3);
        expect(laid.has_value());
        const auto back = dbus::unmarshal("ybitdsa{sv}", laid.value_or(""), 3);
        expect(back && back->size() == 7 && std::get<std::int32_t>((*back)[2].data) == -5 && std::get<std::string>((*back)[5].data) == "hé");
    };

    "Notify on a bus: authenticated, said hello, every argument where the service reads it; its id answered"_test = [] {
        const auto dir = std::filesystem::temp_directory_path() / std::format("mpal-bus-{}", std::random_device{}());
        std::filesystem::create_directories(dir);
        auto listener = mcpplibs::os::ipc::listener::listen(dir, "bus");
        expect(listener.has_value());
        if (!listener) return;
        std::vector<dbus::Message> heard;
        std::vector<std::string> said;
        std::jthread bus([&] {
            auto peer = listener->accept_for(std::chrono::milliseconds(5000));
            if (!peer || !*peer) return;
            auto& connection = **peer;
            std::string pending;
            const auto line = [&]() -> std::string {
                while (!pending.contains("\r\n")) {
                    auto chunk = connection.read_for(std::chrono::milliseconds(3000));
                    if (!chunk || !*chunk || (*chunk)->empty()) return {};
                    pending += **chunk;
                }
                const auto end = pending.find("\r\n");
                std::string out = pending.substr(0, end);
                pending.erase(0, end + 2);
                return out;
            };
            said.push_back(line());
            (void)connection.write("DATA\r\n");
            said.push_back(line());
            (void)connection.write("OK 0123456789abcdef\r\n");
            said.push_back(line());
            std::uint32_t serial = 100;
            for (int answered = 0; answered < 2;) {
                auto message = dbus::read_message(pending);
                if (!message) return;
                if (!*message) {
                    auto chunk = connection.read_for(std::chrono::milliseconds(3000));
                    if (!chunk || !*chunk || (*chunk)->empty()) return;
                    pending += **chunk;
                    continue;
                }
                auto [read, used] = std::move(**message);
                pending.erase(0, used);
                heard.push_back(read);
                if (read.member == "Hello") (void)connection.write(reply_bytes(++serial, read.serial, "s", std::array{dbus::Value::of(":1.7")}));
                else (void)connection.write(reply_bytes(++serial, read.serial, "u", std::array{dbus::Value{std::uint32_t{42}}}));
                ++answered;
            }
        });
        auto connected = dbus::Bus::at(dir, "bus");
        expect(connected.has_value()) << (connected ? std::string() : connected.error().text);
        if (connected) {
            expect(connected->name() == ":1.7") << "Hello's unique name";
            const auto id = mpal::notify_on(*connected, {.application = "Notes", .title = "Saved", .body = "notes.md",
                                                         .actions = {{"open", "Open"}}, .shown = std::chrono::milliseconds(5000)});
            expect(id && *id == 42u) << (id ? std::string() : id.error().text);
        }
        bus.join();
        expect(eq(said.size(), std::size_t{3}) && said[0] == std::string("\0AUTH EXTERNAL", 14) && said[1] == "DATA" && said[2] == "BEGIN");
        expect(eq(heard.size(), std::size_t{2}));
        if (heard.size() == 2) {
            const dbus::Message& notify = heard[1];
            expect(notify.member == "Notify" && notify.interface == "org.freedesktop.Notifications" && notify.signature == "susssasa{sv}i");
            expect(eq(notify.body.size(), std::size_t{8}));
            if (notify.body.size() == 8) {
                expect(std::get<std::string>(notify.body[0].data) == "Notes" && std::get<std::string>(notify.body[3].data) == "Saved" &&
                       std::get<std::string>(notify.body[4].data) == "notes.md");
                expect(eq(std::get<std::vector<dbus::Value>>(notify.body[5].data).size(), std::size_t{2})) << "the action's key and label";
                expect(eq(std::get<std::int32_t>(notify.body[7].data), 5000));
            }
        }
        std::filesystem::remove_all(dir);
    };

    "where there is no session bus: unavailable, said"_test = [] {
        auto none = dbus::Bus::at(std::filesystem::temp_directory_path() / "mpal-no-bus-here", "bus");
        expect(!none && none.error().code == mpal::Error::Code::unavailable);
    };
}};
