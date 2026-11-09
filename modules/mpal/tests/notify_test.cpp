// mpal's notifications, spoken as the freedesktop service hears them: a call
// laid out by the D-Bus specification's alignment, the EXTERNAL
// authentication, Hello, then Notify with every argument where it belongs —
// against a bus of the test's own (a local socket), so it runs anywhere; and
// the person's answers coming back as the service's signals.
import std;
import mpal;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace dbus = mpal::dbus;

namespace {

using dbus::Value;

Value field(std::uint8_t code, std::string signature, Value value) {
    return Value{std::vector<Value>{Value{code}, Value{dbus::Variant{std::move(signature), std::make_shared<Value>(std::move(value))}}}};
}

// A message as a bus sends it: its kind and serial, header fields, a body by signature.
std::string message_bytes(dbus::Kind kind, std::uint32_t serial, std::vector<Value> fields, std::string_view signature, std::span<const Value> body_values) {
    const auto body = dbus::marshal(signature, body_values).value();
    if (!signature.empty()) fields.push_back(field(8, "g", Value::of(std::string(signature))));
    std::string header{'l', static_cast<char>(kind), '\0', '\1'};
    for (const std::uint32_t word : {static_cast<std::uint32_t>(body.size()), serial})
        for (int index = 0; index < 4; ++index) header.push_back(static_cast<char>((word >> (8 * index)) & 0xFF));
    header += dbus::marshal("a(yv)", std::array{Value{std::move(fields)}}, header.size()).value();
    while (header.size() % 8 != 0) header.push_back('\0');
    return header + body;
}

// A reply: REPLY_SERIAL (5, u).
std::string reply_bytes(std::uint32_t serial, std::uint32_t reply_to, std::string_view signature, std::span<const Value> body_values) {
    return message_bytes(dbus::Kind::reply, serial, {field(5, "u", Value{reply_to})}, signature, body_values);
}

// One of the notification service's signals: PATH (1, o), INTERFACE (2, s), MEMBER (3, s).
std::string signal_bytes(std::uint32_t serial, std::string member, std::string_view signature, std::span<const Value> body_values) {
    return message_bytes(dbus::Kind::signal, serial,
                         {field(1, "o", Value::of("/org/freedesktop/Notifications")), field(2, "s", Value::of("org.freedesktop.Notifications")),
                          field(3, "s", Value::of(std::move(member)))},
                         signature, body_values);
}

// A bus of the test's own: answers the authentication, then hands each message
// to `answer`, which writes what the bus says back, until it has handled `count`;
// open until the case ends it (end()).
struct FakeBus {
    std::filesystem::path                       dir = std::filesystem::temp_directory_path() / std::format("mpal-bus-{}", std::random_device{}());
    std::optional<mcpplibs::os::ipc::listener>  listener;
    std::vector<dbus::Message>                  heard;
    std::vector<std::string>                    said;
    std::binary_semaphore                       ended{0};
    std::jthread                                thread;

    template <class Answer>
    FakeBus(int count, Answer answer) {
        std::filesystem::create_directories(dir);
        if (auto listening = mcpplibs::os::ipc::listener::listen(dir, "bus")) listener.emplace(std::move(*listening));
        if (!listener) return;
        thread = std::jthread([this, count, answer] {
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
            for (int answered = 0; answered < count;) {
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
                (void)connection.write(answer(read));
                ++answered;
            }
            (void)ended.try_acquire_for(std::chrono::seconds(5));
        });
    }
    void end() {
        if (!thread.joinable()) return;
        ended.release();
        thread.join();
    }
    ~FakeBus() {
        end();
        std::filesystem::remove_all(dir);
    }
};

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
        std::uint32_t serial = 100;
        std::optional<FakeBus> bus;
        bus.emplace(2, [&serial](const dbus::Message& read) {
            if (read.member == "Hello") return reply_bytes(++serial, read.serial, "s", std::array{Value::of(":1.7")});
            return reply_bytes(++serial, read.serial, "u", std::array{Value{std::uint32_t{42}}});
        });
        auto connected = dbus::Bus::at(bus->dir, "bus");
        expect(connected.has_value()) << (connected ? std::string() : connected.error().text);
        if (connected) {
            expect(connected->name() == ":1.7") << "Hello's unique name";
            const auto id = mpal::notify_on(*connected, {.application = "Notes", .title = "Saved", .body = "notes.md",
                                                         .actions = {{"open", "Open"}}, .shown = std::chrono::milliseconds(5000)});
            expect(id && *id == 42u) << (id ? std::string() : id.error().text);
        }
        bus->end();
        const auto said = bus->said;
        const auto heard = bus->heard;
        expect(eq(said.size(), std::size_t{3}) && said[0] == std::string("\0AUTH EXTERNAL", 14) && said[1] == "DATA" && said[2] == "BEGIN");
        expect(eq(heard.size(), std::size_t{2}));
        if (heard.size() == 2) {
            const dbus::Message& notify = heard[1];
            expect(notify.member == "Notify" && notify.interface == "org.freedesktop.Notifications" && notify.signature == "susssasa{sv}i");
            expect(eq(notify.body.size(), std::size_t{8}));
            if (notify.body.size() == 8) {
                expect(std::get<std::string>(notify.body[0].data) == "Notes" && std::get<std::string>(notify.body[3].data) == "Saved" &&
                       std::get<std::string>(notify.body[4].data) == "notes.md");
                expect(eq(std::get<std::vector<Value>>(notify.body[5].data).size(), std::size_t{2})) << "the action's key and label";
                expect(eq(std::get<std::int32_t>(notify.body[7].data), 5000));
            }
        }
    };

    "the person's answers: an action chosen, dismissed, closed by the application; another application's not heard"_test = [] {
        std::uint32_t serial = 100;
        std::optional<FakeBus> bus;
        bus.emplace(5, [&serial](const dbus::Message& read) {
            if (read.member == "Hello") return reply_bytes(++serial, read.serial, "s", std::array{Value::of(":1.7")});
            if (read.member == "AddMatch") return reply_bytes(++serial, read.serial, "", {});
            if (read.member == "Notify") {
                const std::uint32_t id = std::get<std::string>(read.body[3].data) == "Saved" ? 42 : 43;
                // Another application's notification closing arrives before the reply: kept, then passed over.
                std::string out = signal_bytes(++serial, "NotificationClosed", "uu", std::array{Value{std::uint32_t{7}}, Value{std::uint32_t{2}}});
                out += reply_bytes(++serial, read.serial, "u", std::array{Value{id}});
                if (id == 42) {
                    out += signal_bytes(++serial, "ActionInvoked", "us", std::array{Value{std::uint32_t{42}}, Value::of("open")});
                    out += signal_bytes(++serial, "NotificationClosed", "uu", std::array{Value{std::uint32_t{42}}, Value{std::uint32_t{2}}});
                }
                return out;
            }
            // CloseNotification: done, and the service says so.
            const auto id = std::get<std::uint32_t>(read.body[0].data);
            return reply_bytes(++serial, read.serial, "", {}) +
                   signal_bytes(++serial, "NotificationClosed", "uu", std::array{Value{id}, Value{std::uint32_t{3}}});
        });
        auto connected = dbus::Bus::at(bus->dir, "bus");
        expect(connected.has_value());
        if (!connected) return;
        auto notes = mpal::Notifier::on(std::move(*connected));
        expect(notes.has_value()) << (notes ? std::string() : notes.error().text);
        if (!notes) return;
        const auto saved = notes->show({.application = "Notes", .title = "Saved", .actions = {{"open", "Open"}}});
        expect(saved && *saved == 42u);
        const auto chosen = notes->next(std::chrono::milliseconds(2000));
        expect(chosen && *chosen && (*chosen)->id == 42u && (*chosen)->kind == mpal::Answer::Kind::chosen && (*chosen)->key == "open")
            << "the action chosen, by its key; another application's closing passed over";
        const auto dismissed = notes->next(std::chrono::milliseconds(2000));
        expect(dismissed && *dismissed && (*dismissed)->id == 42u && (*dismissed)->kind == mpal::Answer::Kind::dismissed);
        const auto synced = notes->show({.application = "Notes", .title = "Synced"});
        expect(synced && *synced == 43u);
        expect(notes->close(43).has_value());
        const auto closed = notes->next(std::chrono::milliseconds(2000));
        expect(closed && *closed && (*closed)->id == 43u && (*closed)->kind == mpal::Answer::Kind::closed) << "taken back";
        const auto quiet = notes->next(std::chrono::milliseconds(50));
        expect(quiet && !*quiet) << "nothing more";
        bus->end();
        expect(bus->heard.size() == 5 && bus->heard[1].member == "AddMatch" &&
               std::get<std::string>(bus->heard[1].body[0].data).contains("interface='org.freedesktop.Notifications'"));
    };

    "against a real bus (dbus-daemon): the match rule asked of it, the service's signals it routes heard"_test = [] {
        namespace os = mcpplibs::os;
        const auto daemon = os::env::find_executable("dbus-daemon");
        const auto sender = os::env::find_executable("dbus-send");
        if (!daemon || !sender) skip("no dbus-daemon here");
        const auto dir = std::filesystem::temp_directory_path() / std::format("mpal-real-bus-{}", std::random_device{}());
        std::filesystem::create_directories(dir);
        const std::string socket = os::fs::utf8(dir / "bus");
        // A session bus of the test's own: anyone on it may send and listen.
        (void)os::fs::write_atomic(dir / "session.conf", std::format(R"(<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:path={}</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>)", socket));
        os::process::spawn_options options;
        options.program = *daemon;
        options.args = {std::format("--config-file={}", os::fs::utf8(dir / "session.conf")), "--nofork", "--print-address"};
        options.out = os::process::stdio::pipe;
        options.err = os::process::stdio::null;
        options.bound_lifetime = true;
        auto bus_process = os::process::process::spawn(options);
        expect(fatal(bus_process.has_value()));
        (void)bus_process->read_output_for(std::chrono::milliseconds(5000));  // its address: it listens
        std::optional<dbus::Bus> connected;
        for (int tries = 0; tries < 50 && !connected; ++tries) {
            if (auto bus = dbus::Bus::at(dir, "bus")) connected.emplace(std::move(*bus));
            else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        expect(fatal(connected.has_value())) << "connected to dbus-daemon";
        expect(connected->name().starts_with(":")) << "the unique name the daemon gave";
        auto notes = mpal::Notifier::listen(std::move(*connected));
        expect(fatal(notes.has_value())) << (notes ? std::string() : notes.error().text);
        const auto send = [&](std::vector<std::string> arguments) {
            os::process::spawn_options said;
            said.program = *sender;
            said.args = {std::format("--bus=unix:path={}", socket), "--type=signal", "/org/freedesktop/Notifications"};
            said.args.insert(said.args.end(), arguments.begin(), arguments.end());
            said.out = said.err = os::process::stdio::null;
            const auto ran = os::process::run(said);
            expect(ran.has_value()) << "dbus-send";
        };
        send({"org.freedesktop.Notifications.ActionInvoked", "uint32:42", "string:open"});
        const auto chosen = notes->next(std::chrono::milliseconds(3000));
        expect(chosen && *chosen && (*chosen)->id == 42u && (*chosen)->kind == mpal::Answer::Kind::chosen && (*chosen)->key == "open");
        send({"org.freedesktop.Notifications.NotificationClosed", "uint32:42", "uint32:1"});
        const auto expired = notes->next(std::chrono::milliseconds(3000));
        expect(expired && *expired && (*expired)->kind == mpal::Answer::Kind::expired);
        send({"org.example.Other.Said", "uint32:42", "uint32:1"});
        const auto other = notes->next(std::chrono::milliseconds(300));
        expect(other && !*other) << "another interface's signal: not asked for, not routed";
        (void)bus_process->kill();
        std::filesystem::remove_all(dir);
    };

    "where there is no session bus: unavailable, said"_test = [] {
        auto none = dbus::Bus::at(std::filesystem::temp_directory_path() / "mpal-no-bus-here", "bus");
        expect(!none && none.error().code == mpal::Error::Code::unavailable);
    };
}};
