// A playtest by an agent, no interface: Kiln at the runtime, played through the agent kernel's requests a
// person's tier allows (world.observe, world.branch, world.act, world.step). The player is the engine's
// (`xengine.playtest`): it tries the way forward by branching and, blocked, each tool at each aim over its own
// time. What is Kiln's is the table below — how its view is read, walking and jumping as its ways forward, its
// three spells and where to aim them, what stands in the way. It finishes the first cavern; given the same
// cavern with a mistake planted (a second wall of stone, still one bomb), it stops and says where and why.
import std;
import mlibs.agent;
import mlibs.json;
import xengine.playtest;
import omui.showcase.kiln.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace playtest = xengine::playtest;

namespace {

// What the player reads of a view.
struct Seen {
    double x = 0, y = 0;
    bool swimming = false;
    std::int64_t health = 0, mana = 0, bombs = 0, frame = 0;
    std::string spell, outcome;
    std::int64_t left = 0, top = 0;  // the field's cell at the window's corner
    std::vector<std::string> rows;   // a screen's worth of cells around her
};
Seen read(const mlibs::json::Node& data) {
    const auto view = data["view"];
    Seen seen;
    seen.frame = data["frame"].integer().value_or(0);
    seen.outcome = std::string(view["outcome"].text().value_or(""));
    const auto cat = view["cat"];
    seen.x = cat["x"].number().value_or(0), seen.y = cat["y"].number().value_or(0);
    seen.swimming = cat["swimming"].boolean() == true;
    seen.health = cat["health"].integer().value_or(0), seen.mana = cat["mana"].integer().value_or(0), seen.bombs = cat["bombs"].integer().value_or(0);
    seen.spell = std::string(cat["spell"].text().value_or(""));
    seen.left = view["around"]["left"].integer().value_or(0), seen.top = view["around"]["top"].integer().value_or(0);
    for (const auto& row : view["around"]["rows"].items()) seen.rows.emplace_back(row.text().value_or(""));
    return seen;
}
// What stands in the way of a place (where she was blocked, in the field's cells), as a view shows it: what is
// solid at her height a few cells ahead of it (and what burns there), and lava ahead and below (twice). Out of the
// view: solid.
int in_the_way(const Seen& seen, std::int64_t x, std::int64_t y) {
    int count = 0;
    const auto at = [&](std::int64_t column, std::int64_t row) {
        const std::int64_t c = column - seen.left, r = row - seen.top;
        return r >= 0 && r < static_cast<std::int64_t>(seen.rows.size()) && c >= 0 && c < static_cast<std::int64_t>(seen.rows[static_cast<std::size_t>(r)].size())
                   ? seen.rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]
                   : '#';
    };
    for (std::int64_t row = y - 5; row <= y - 1; ++row)
        for (std::int64_t column = x + 2; column <= x + 10; ++column) count += std::string_view("#wgs").contains(at(column, row));
    for (std::int64_t row = y; row <= y + 4; ++row)
        for (std::int64_t column = x + 2; column <= x + 14; ++column) count += at(column, row) == 'l' ? 2 : at(column, row) == '*' ? 1 : 0;
    return count;
}

// Whether any of `letters` shows ahead of a place: at her height and below, up to 14 cells on.
bool ahead(const Seen& seen, std::int64_t x, std::int64_t y, std::string_view letters) {
    for (std::int64_t row = y - 6; row <= y + 5; ++row)
        for (std::int64_t column = x + 1; column <= x + 14; ++column) {
            const std::int64_t c = column - seen.left, r = row - seen.top;
            if (r >= 0 && r < static_cast<std::int64_t>(seen.rows.size()) && c >= 0 && c < static_cast<std::int64_t>(seen.rows[static_cast<std::size_t>(r)].size()) &&
                letters.contains(seen.rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]))
                return true;
        }
    return false;
}

// Kiln, as the player needs it. `width` and `height`: the field's, for a pointer as a share of it.
struct Kiln {
    using View = Seen;
    double width = 128, height = 64;

    [[nodiscard]] Seen read(const mlibs::json::Node& data) const { return ::read(data); }
    [[nodiscard]] playtest::Vitals vitals(const Seen& seen) const {
        return {.progress = seen.x, .health = static_cast<double>(seen.health), .frame = seen.frame, .won = seen.outcome == "won", .lost = seen.outcome == "lost", .afloat = seen.swimming};
    }
    // Walking, or holding jump (which swims too).
    [[nodiscard]] std::span<const std::string_view> ways() const {
        static constexpr std::array<std::string_view, 2> ways{R"("axis":[1])", R"("axis":[1],"buttons":[0])"};
        return ways;
    }
    [[nodiscard]] std::string_view back() const { return R"("axis":[-1])"; }
    [[nodiscard]] std::string_view stay() const { return R"("axis":[0])"; }
    [[nodiscard]] std::string_view surface() const { return R"("axis":[1],"buttons":[0])"; }
    // What hurts ahead passes unless it is lava (which water answers): what burned goes out, hot air rises away.
    [[nodiscard]] bool passes(const Seen& seen) const { return !ahead(seen, static_cast<std::int64_t>(seen.x), static_cast<std::int64_t>(seen.y), "l"); }
    [[nodiscard]] int in_the_way(const Seen& seen, const Seen& blocked) const { return ::in_the_way(seen, static_cast<std::int64_t>(blocked.x), static_cast<std::int64_t>(blocked.y)); }
    // Each spell's time to work (a fire takes seconds to open wood), cheapest first.
    [[nodiscard]] std::span<const playtest::Tool> tools() const {
        static constexpr std::array<playtest::Tool, 3> spells{{{"fire", 700}, {"water", 240}, {"bomb", 120}}};
        return spells;
    }
    // As a person would: fire for what burns, water for lava and flames, a bomb for what is solid.
    [[nodiscard]] bool suits(const playtest::Tool& spell, const Seen& seen) const {
        if (spell.name == "bomb" && seen.bombs == 0) return false;
        return ahead(seen, static_cast<std::int64_t>(seen.x), static_cast<std::int64_t>(seen.y), spell.name == "fire" ? "wgo" : spell.name == "water" ? "l*" : "#wgs");
    }
    // Choose the spell for real (a press of `next` a frame) — reversible, and needed to branch it.
    [[nodiscard]] bool ready(const playtest::Tool& spell, playtest::Hands& hands) const {
        const auto chosen = [&] { return ::read(hands.observe().root()["data"]).spell; };
        for (int press = 0; press < 3 && chosen() != spell.name; ++press) {
            hands.act(R"("buttons":[2])");
            hands.step(13);
            hands.act(R"("buttons":[])");
            hands.step(13);
        }
        return chosen() == spell.name;
    }
    // Where to aim: ahead at her middle, ahead low, down ahead, and further down ahead.
    static constexpr std::array<std::pair<double, double>, 5> places{{{5, 0}, {5, 3}, {8, 7}, {12, 6}, {17, 6}}};
    [[nodiscard]] std::size_t aims() const { return places.size(); }
    [[nodiscard]] std::string use(const playtest::Tool&, std::size_t aim, const Seen& blocked) const {
        return std::format(R"("buttons":[1],"axis":[0],"pointer":[{:.4f},{:.4f}])", std::clamp((static_cast<double>(static_cast<std::int64_t>(blocked.x)) + places[aim].first) / width, 0.0, 1.0),
                           std::clamp((static_cast<double>(static_cast<std::int64_t>(blocked.y)) - 3 + places[aim].second) / height, 0.0, 1.0));
    }
    [[nodiscard]] std::string aim(std::size_t aim) const { return std::format("({:+},{:+})", places[aim].first, places[aim].second); }
    [[nodiscard]] std::string where(const Seen& seen) const { return std::format("x {:.1f}", seen.x); }
    [[nodiscard]] std::string has(const Seen& seen) const { return std::format("{} bombs, {} mana", seen.bombs, seen.mana); }
};

}  // namespace

UTest cases{"kiln.playtest", [] {
    "an agent playing as a person, by branching, finishes the first cavern"_test = [] {
        kiln::Standalone game(*kiln::start(kiln::level::first()), {.keep = 600, .most_steps = 1200});
        agent::SubjectSession session({&game});
        playtest::Hands hands(session);
        const playtest::Report report = playtest::play(hands, Kiln{});
        std::println("{}", report.why);
        expect(report.won) << report.why;
    };

    "given the cavern with a mistake planted, it stops at the second wall and says why: no bomb left, and nothing else opens stone"_test = [] {
        kiln::Standalone game(*kiln::start(kiln::level::flawed()), {.keep = 600, .most_steps = 1200});
        agent::SubjectSession session({&game});
        playtest::Hands hands(session);
        const playtest::Report report = playtest::play(hands, Kiln{});
        std::println("{}", report.why);
        expect(!report.won);
        expect(report.why.starts_with("stuck at x")) << report.why;
        expect(report.why.contains("0 bombs")) << report.why;
        const auto at = report.why.find("stuck at x ");
        const double x = std::stod(report.why.substr(at + 11));
        expect(x > 100.0 && x < 113.0) << std::format("before the second wall (column 113): {}", x);
    };
}};
