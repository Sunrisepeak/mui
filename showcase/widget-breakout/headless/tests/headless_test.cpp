// The breakout on its own: no omui in this program. The game is found by name
// at an endpoint of the agent kernel and played by an agent as a person plays
// — what it is shown, the same actions, a tenth of a second's reaction, ten
// actions a second — and the same play gives the same bits.
import std;
import mlibs.agent;
import mlibs.json;
import mlibs.testing;
import xengine.dynamics;
import omui.showcase.widget_breakout.headless;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace bo = omui::breakout;

namespace {

// Eight columns, four rows of 120 × 30 bricks, the top row two hits each (the simulation's own test's wall).
bo::Level wall_of_bricks() {
    bo::Level level;
    std::uint32_t id = 0;
    for (std::uint32_t row = 0; row < 4; ++row)
        for (int column = 0; column < 8; ++column)
            level.bricks.push_back({xengine::dynamics::Box{120.0 + column * 130, 100.0 + row * 40, 120, 30}, row == 0 ? bo::Kind::card : bo::Kind::button,
                                    row == 0 ? 2 : 1, id++, row});
    return level;
}

// A game at an endpoint, and a connection to it: requests answered while this loop — the owner's — pumps.
struct Table {
    bo::Headless      game;
    agent::Endpoint   endpoint;
    std::optional<agent::Connection> connection;

    Table(std::string name, const std::filesystem::path& directory)
        : game(bo::start(wall_of_bricks())),
          endpoint({.name = std::move(name), .directory = directory, .app = "org.openmui.showcase.widget-breakout", .interface_name = "xengine.world@0.1",
                    .session = [this] { return std::make_unique<agent::SubjectSession>(std::vector<agent::Subject*>{&game}); }}) {}

    [[nodiscard]] bool open() {
        auto started = endpoint.start();
        if (!started) {
            if (started.error().find("not supported") != std::string::npos) std::println("    skipped: local endpoints are not supported here ({})", started.error());
            else expect(false) << started.error();
            return false;
        }
        auto connected = agent::Connection::connect(endpoint.name(), endpoint.directory());
        expect(fatal(connected.has_value())) << (connected ? "" : connected.error());
        connection.emplace(std::move(*connected));
        return true;
    }
    // One request: its data (the first record), once its result has come.
    [[nodiscard]] std::optional<mlibs::json::Document> call(std::string_view line) {
        expect(fatal(connection->send(line).has_value()));
        std::optional<mlibs::json::Document> data;
        for (int turn = 0; turn < 20000; ++turn) {
            endpoint.pump();
            auto got = connection->receive(std::chrono::milliseconds(1));
            if (!got) return std::nullopt;
            if (!*got) continue;
            auto record = mlibs::json::Document::parse(**got, "record");
            expect(fatal(record.has_value())) << **got;
            if (record->root()["kind"].text() == "result") return data;
            if (!data) data.emplace(std::move(*record));
        }
        return std::nullopt;
    }
};

// A player's turn, decided on what it was shown: the paddle under the ball that comes down, the ball launched when it waits.
std::string decide(const mlibs::json::Node& seen, bool& holding_launch) {
    const auto view = seen["view"];
    const double paddle = view["paddle"]["x"].number().value_or(0);
    double target = paddle;
    bool waiting = false;
    double lowest = -1;
    for (const auto& ball : view["balls"].items()) {
        waiting = waiting || ball["waiting"].boolean() == true;
        const double y = ball["y"].number().value_or(0);
        if (y > lowest) {
            lowest = y;
            // Where it will be when the reaction has passed: a tenth of a second on.
            target = ball["x"].number().value_or(paddle) + 12 * ball["vx"].number().value_or(0);
        }
    }
    const int axis = target > paddle + 12 ? 1 : (target < paddle - 12 ? -1 : 0);
    // A press is a change: held this turn when it waits and was not held the turn before.
    holding_launch = waiting && !holding_launch;
    return std::format(R"({{"op":"world.act","seen":{},"axis":[{}],"buttons":[{}]}})", seen["frame"].integer().value_or(0), axis, holding_launch ? "0" : "");
}

struct Played {
    std::string  hash;
    std::int64_t frame = 0, score = 0, standing = 0;
    int          refused = 0;
};
// Ten turns a second of the game's time, for at most `seconds`.
Played play(Table& table, int seconds) {
    Played out;
    bool holding_launch = false;
    for (int turn = 0; turn < seconds * 10; ++turn) {
        auto seen = table.call(R"({"op":"world.observe"})");
        expect(fatal(seen.has_value()));
        const auto data = seen->root()["data"];
        if (data["over"].boolean() == true) break;
        auto acted = table.call(decide(data, holding_launch));
        out.refused += !acted.has_value() || acted->root()["kind"].text() == "error";
        (void)table.call(R"({"op":"world.step","steps":12})");
    }
    auto last = table.call(R"({"op":"world.observe"})");
    expect(fatal(last.has_value()));
    const auto data = last->root()["data"];
    out.hash = data["hash"].text().value_or("");
    out.frame = data["frame"].integer().value_or(0);
    out.score = data["view"]["score"].integer().value_or(0);
    out.standing = data["view"]["standing"].integer().value_or(0);
    return out;
}

}  // namespace

UTest cases{"breakout.headless", [] {
    "the game is found by name and says what it is: a world, what it can do, what a player is shown"_test = [] {
        const auto directory = scratch_dir("breakout-headless");
        Table table("breakout", directory);
        if (!table.open()) return;
        const auto found = agent::list(directory);
        expect(fatal(found.size() == 1u));
        expect(found[0].name == "breakout" && found[0].interface_name == "xengine.world@0.1");

        auto who = table.call(R"({"op":"subjects"})");
        expect(fatal(who.has_value()));
        const auto subject = who->root()["data"]["subjects"][0];
        expect(subject["name"].text() == "world" && subject["capabilities"]["preview"].boolean() == true);

        auto seen = table.call(R"({"op":"world.observe"})");
        expect(fatal(seen.has_value()));
        const auto view = seen->root()["data"]["view"];
        expect(seen->root()["data"]["tier"].text() == "human");
        expect(eq(view["bricks"].size(), std::size_t{32}) && view["standing"].integer() == 32);
        expect(eq(view["balls"].size(), std::size_t{1}) && view["balls"][0]["waiting"].boolean() == true) << "a ball on the paddle, waiting";
        expect(!view["rng"]) << "a player as a person is not shown the generator";
        table.endpoint.stop();
    };

    "an agent plays it as a person does — bricks broken, a score — and the same play is the same bits"_test = [] {
        const auto directory = scratch_dir("breakout-headless-play");
        Table first("breakout-one", directory);
        if (!first.open()) return;
        const Played one = play(first, 40);
        std::println("    played to step {}: score {}, {} of 32 bricks standing, {} actions refused, state {}", one.frame, one.score, one.standing, one.refused, one.hash);
        expect(one.score > 0 && one.standing < 32) << "the ball was launched and bricks were broken";
        expect(eq(one.refused, 0)) << "ten turns a second is within the tier";
        first.endpoint.stop();

        Table second("breakout-two", directory);
        if (!second.open()) return;
        const Played two = play(second, 40);
        expect(two.hash == one.hash && eq(two.frame, one.frame) && eq(two.score, one.score)) << "the same requests at the same steps: the same game";
        second.endpoint.stop();
    };

    "what would be, asked before acting: the game itself does not move"_test = [] {
        const auto directory = scratch_dir("breakout-headless-branch");
        Table table("breakout-branch", directory);
        if (!table.open()) return;
        const std::uint64_t before = table.game.state_hash();
        auto future = table.call(R"({"op":"world.branch","steps":240,"buttons":[0]})");
        expect(fatal(future.has_value()));
        const auto data = future->root()["data"];
        expect(data["frame"].integer() == 240);
        expect(data["view"]["balls"][0]["waiting"].boolean() == false) << "in that future the ball was launched";
        expect(eq(table.game.state_hash(), before) && eq(table.game.frame(), std::int64_t{0})) << "here it still waits";
        table.endpoint.stop();
    };
}};
