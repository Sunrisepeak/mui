// mlibs.agent's endpoint with no interface and no game behind it: a session
// that answers lines, found by name, served at the owner's own safe point.
import std;
import mlibs.agent;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

// Answers each line with two: what it heard, then how many it has heard.
class Echo final : public agent::Session {
public:
    explicit Echo(int& polls) : polls_(polls) {}
    void request(std::string_view line, const agent::Reply& reply) override {
        reply(std::format("heard {}", line));
        reply(std::format("count {}", ++heard_));
    }
    void poll() override { ++polls_; }

private:
    int  heard_ = 0;
    int& polls_;
};

bool unsupported(const std::expected<void, std::string>& started) {
    if (started || started.error().find("not supported") == std::string::npos) return false;
    std::println("    skipped: local endpoints are not supported here ({})", started.error());
    return true;
}

// The next line, the endpoint pumped meanwhile (the owner's safe point is this loop).
std::optional<std::string> next(agent::Endpoint& endpoint, agent::Connection& connection) {
    for (int turn = 0; turn < 2000; ++turn) {
        endpoint.pump();
        auto got = connection.receive(std::chrono::milliseconds(1));  // the shortest wait still reads
        if (!got) return std::nullopt;
        if (*got) return **got;
    }
    return std::nullopt;
}

}  // namespace

UTest cases{"agent.endpoint", [] {
    "a program found by name answers lines at its own safe point, a session a connection"_test = [] {
        const auto directory = scratch_dir("agent-kernel");
        int polls = 0;
        agent::Endpoint endpoint({.name = "world-test", .directory = directory, .app = "org.example.world", .interface_name = "example.world@1",
                                  .spec = "0.1", .session = [&] { return std::make_unique<Echo>(polls); }});
        auto started = endpoint.start();
        if (unsupported(started)) return;
        expect(fatal(started.has_value())) << (started ? "" : started.error());
        expect(endpoint.listening() && endpoint.name() == "world-test");

        const auto found = agent::list(directory);
        expect(fatal(found.size() == 1u));
        expect(found[0].name == "world-test" && found[0].app == "org.example.world" && found[0].interface_name == "example.world@1");

        auto first = agent::Connection::connect("world-test", directory);
        auto second = agent::Connection::connect("world-test", directory);
        expect(fatal(first.has_value() && second.has_value()));
        expect(first->send("one").has_value() && second->send("alpha").has_value() && first->send("two").has_value());
        expect(next(endpoint, *first) == "heard one");
        expect(next(endpoint, *first) == "count 1");
        expect(next(endpoint, *first) == "heard two");
        expect(next(endpoint, *first) == "count 2") << "a connection keeps its session";
        expect(next(endpoint, *second) == "heard alpha");
        expect(next(endpoint, *second) == "count 1") << "another connection has a session of its own";
        expect(polls > 0) << "sessions are polled each pump";
        int sessions = 0;
        endpoint.each_session([&](agent::Session&) { ++sessions; });
        expect(eq(sessions, 2));

        endpoint.stop();
        expect(!endpoint.listening());
        expect(agent::list(directory).empty()) << "the descriptor goes with the endpoint";
        expect(!agent::Connection::connect("world-test", directory).has_value());
    };

    "on the loopback too: the secret first, else the connection is closed unanswered"_test = [] {
        const auto directory = scratch_dir("agent-kernel-loopback");
        int polls = 0;
        agent::Endpoint endpoint({.name = "world-loopback", .directory = directory, .app = "org.example.world",
                                  .session = [&] { return std::make_unique<Echo>(polls); }, .loopback = 0});
        auto started = endpoint.start();
        if (unsupported(started)) return;
        expect(fatal(started.has_value())) << (started ? "" : started.error());
        const auto port = endpoint.loopback_port();
        expect(fatal(port.has_value() && *port > 0));
        expect(endpoint.secret().size() == 32u) << "a secret for the loopback";
        expect(std::filesystem::exists(directory / "world-loopback.secret"));

        auto trusted = agent::Connection::connect_loopback(*port, endpoint.secret());
        expect(fatal(trusted.has_value())) << (trusted ? "" : trusted.error());
        expect(trusted->send("hello").has_value());
        expect(next(endpoint, *trusted) == "heard hello");

        auto stranger = agent::Connection::connect_loopback(*port, "not the secret");
        if (stranger) {
            (void)stranger->send("hello");
            expect(!next(endpoint, *stranger).has_value()) << "no answer without the secret";
        }
        endpoint.stop();
        expect(!std::filesystem::exists(directory / "world-loopback.secret")) << "the secret goes with the endpoint";
    };

    "a name of its own: the application's id, else a default"_test = [] {
        agent::Endpoint named({.app = "org.example.notes"});
        expect(named.name() == "org.example.notes");
        agent::Endpoint bare({});
        expect(bare.name() == "openmui-app");
    };
}};
