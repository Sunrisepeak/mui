// A playtest by an agent, no interface: Kiln at the runtime, played through the agent kernel's requests a
// person's tier allows (world.observe, world.branch, world.act, world.step). The player tries the way forward
// by branching — walking, jumping, swimming — and, blocked, tries each spell it can cast at a few aims, each
// over its own time (a fire takes seconds to open wood), keeping what clears the way and hurts least. It
// finishes the first cavern; given the same cavern with a mistake planted (a second wall of stone, still one
// bomb), it stops and says where and why.
import std;
import mlibs.agent;
import mlibs.json;
import omui.showcase.kiln.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

struct Answered {
    std::vector<mlibs::json::Document> records;
    [[nodiscard]] mlibs::json::Node data() const { return records.front().root()["data"]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        out.records.push_back(std::move(*parsed));
    });
    expect(fatal(!out.records.empty()));
    return out;
}

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

struct Report {
    bool won = false;
    std::string why;
    std::vector<std::string> log;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    // Said as it happens (a playtest is long; what it does should be seen while it does it).
    void note(std::string line) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::println("  [{:6.1f} s] {}", seconds, line);
        std::fflush(nullptr);
        log.push_back(std::move(line));
    }
};

// The player. `width` and `height`: the field's, for a pointer as a share of it.
Report playtest(agent::SubjectSession& session, double width, double height) {
    Report report;
    const auto act = [&](std::string_view what) { (void)ask(session, std::format(R"({{"op":"world.act",{}}})", what)); };
    const auto step = [&](int steps) { (void)ask(session, std::format(R"({{"op":"world.step","steps":{}}})", steps)); };
    const auto branch = [&](int steps, std::string_view what) { return read(ask(session, std::format(R"({{"op":"world.branch","steps":{},{}}})", steps, what)).data()); };
    const auto seen_now = [&] { return read(ask(session, R"({"op":"world.observe"})").data()); };
    // Each spell's time to work, and where to aim: ahead at her middle, ahead low, down ahead, and further down ahead.
    const std::array<std::pair<std::string_view, int>, 3> spells{{{"fire", 700}, {"water", 240}, {"bomb", 120}}};
    const std::array<std::pair<double, double>, 5> aims{{{5, 0}, {5, 3}, {8, 7}, {12, 6}, {17, 6}}};
    int waits = 0;
    double reached = 0;  // the furthest she has been: ground regained after backing off is not progress
    for (int turn = 0; turn < 160; ++turn) {
        const Seen seen = seen_now();
        if (seen.outcome == "won") { report.won = true; report.why = std::format("reached the exit at frame {}", seen.frame); return report; }
        if (seen.outcome == "lost") { report.why = std::format("lost at x {:.1f} (frame {})", seen.x, seen.frame); return report; }
        // 0 hurt: first out of harm's way, and mend — in water, up out of it (backing off there is drowning).
        if (seen.health < 60 && seen.swimming) {
            act(R"("axis":[1],"buttons":[0])");
            step(60);
            report.note(std::format("frame {}: hurt in the water ({}), up and out", seen.frame, seen.health));
            continue;
        }
        if (seen.health < 60) {
            act(R"("axis":[-1])");
            step(30);
            act(R"("axis":[0])");
            step(300);
            report.note(std::format("frame {}: hurt ({}), back off and mend", seen.frame, seen.health));
            continue;
        }
        // 1 forward, a short way at a time (so she stops short of what would hurt her and sees it): walking, or holding
        // jump (which swims too); the one that gets furthest without much harm.
        std::optional<std::string_view> best;
        double furthest = seen.x + 2;
        bool harmful = false;  // the way is open but hurts: a hazard, not an obstacle
        for (const std::string_view move : {std::string_view(R"("axis":[1])"), std::string_view(R"("axis":[1],"buttons":[0])")}) {
            const Seen future = branch(40, move);
            if (future.outcome == "lost" || future.health < seen.health - 10 || future.health < 40) {
                harmful = harmful || future.x > seen.x + 2;
                continue;
            }
            // And further the same way: a step that is safe itself but leads where she cannot stay (onto a crust
            // over lava, into a fire) is not taken.
            if (future.outcome != "won" && !seen.swimming && !future.swimming) {
                const Seen beyond = branch(120, move);
                if (beyond.outcome == "lost" || beyond.health < seen.health - 25) {
                    harmful = harmful || future.x > seen.x + 2;
                    continue;
                }
            }
            if (future.outcome == "won" || future.x > furthest) furthest = future.outcome == "won" ? 1e9 : future.x, best = move;
        }
        if (best) {
            act(*best);
            step(40);
            if (furthest > reached + 1) {
                report.note(std::format("frame {}: forward to {:.1f}", seen.frame, furthest));
                reached = furthest, waits = 0;
            }
            continue;
        }
        // 2 blocked: each spell that suits what she sees ahead, at each aim, over its own time; the first that clears the way.
        // 2a the way is open but would hurt her, and it is not lava (which water answers): back off and let it
        // cool — what burned goes out, hot air rises away.
        if (harmful && !ahead(seen, static_cast<std::int64_t>(seen.x), static_cast<std::int64_t>(seen.y), "l") && waits++ < 12) {
            act(R"("axis":[-1])");
            step(30);
            act(R"("axis":[0])");
            step(180);
            report.note(std::format("frame {}: the way ahead of {:.1f} hurts; back off and let it cool", seen.frame, seen.x));
            continue;
        }
        struct Try { std::string_view spell; std::pair<double, double> aim; int window = 0; int left = 0; };
        std::optional<Try> chosen;
        const std::int64_t blocked_x = static_cast<std::int64_t>(seen.x), blocked_y = static_cast<std::int64_t>(seen.y);
        const int before = in_the_way(seen, blocked_x, blocked_y);
        std::string tried;  // what each try came to, for the report
        // A few steps back first, for real, and stand: a fire or a blast beside her would hurt her, and from
        // there she still sees what was in her way. Every aim is at the place she was blocked.
        act(R"("axis":[-1])");
        step(20);
        act(R"("axis":[0])");
        step(10);
        const auto aimed = [&](std::pair<double, double> aim) {
            return std::format(R"("buttons":[1],"axis":[0],"pointer":[{:.4f},{:.4f}])", std::clamp((static_cast<double>(blocked_x) + aim.first) / width, 0.0, 1.0),
                               std::clamp((static_cast<double>(blocked_y) - 3 + aim.second) / height, 0.0, 1.0));
        };
        for (const auto& [spell, window] : spells) {
            if (spell == "bomb" && seen.bombs == 0) continue;
            // As a person would: fire for what burns, water for lava and flames, a bomb for what is solid.
            if (!ahead(seen, blocked_x, blocked_y, spell == "fire" ? "wgo" : spell == "water" ? "l*" : "#wgs")) continue;
            // Choose the spell for real (a press of `next` a frame) — reversible, and needed to branch it.
            for (int press = 0; press < 3 && seen_now().spell != spell; ++press) {
                act(R"("buttons":[2])");
                step(13);
                act(R"("buttons":[])");
                step(13);
            }
            const Seen ready = seen_now();
            if (ready.spell != spell) continue;
            for (const auto& aim : aims) {
                const Seen future = branch(window, aimed(aim));
                const int left = in_the_way(future, blocked_x, blocked_y);
                tried += std::format(" {}({:+},{:+}): {} left, health {}{};", spell, aim.first, aim.second, left, future.health, future.outcome == "lost" ? ", lost" : "");
                if (future.outcome == "lost" || future.health < 50) continue;
                if (left + 3 <= before) {
                    chosen = Try{spell, aim, window, left};  // the first that clears the way: no need to try the rest
                    break;
                }
            }
            if (chosen) break;  // the cheapest kind that works, in the order fire, water, bomb
        }
        if (chosen) {
            for (int press = 0; press < 3 && seen_now().spell != chosen->spell; ++press) act(R"("buttons":[2])"), step(13), act(R"("buttons":[])"), step(13);
            act(aimed(chosen->aim));
            step(2);
            act(R"("axis":[0])");
            step(chosen->window);
            report.note(std::format("frame {}: {} at ({:+}, {:+}), in the way {} → {}", seen.frame, chosen->spell, chosen->aim.first, chosen->aim.second, before, chosen->left));
            waits = 0;
            continue;
        }
        // 3 nothing helps now: step back out of harm and wait (mana comes back, what burns cools, she mends), a
        // few times; then say so.
        report.note(std::format("frame {}: blocked at {:.1f}, {} in the way, {} mana, {} bombs; tried{}", seen.frame, seen.x, before, seen.mana, seen.bombs, tried.empty() ? " nothing" : tried));
        if (waits++ < 5) {
            step(240);
            continue;
        }
        report.why = std::format("stuck at x {:.1f}, frame {}: {} cells in the way ahead; {} bombs, {} mana; walking, jumping and every spell at every aim leave the way shut",
                                 seen.x, seen.frame, before, seen.bombs, seen.mana);
        return report;
    }
    report.why = "out of turns";
    return report;
}

}  // namespace

UTest cases{"kiln.playtest", [] {
    "an agent playing as a person, by branching, finishes the first cavern"_test = [] {
        kiln::Standalone game(*kiln::start(kiln::level::first()), {.keep = 600, .most_steps = 1200});
        agent::SubjectSession session({&game});
        const Report report = playtest(session, 128, 64);
        std::println("{}", report.why);
        expect(report.won) << report.why;
    };

    "given the cavern with a mistake planted, it stops at the second wall and says why: no bomb left, and nothing else opens stone"_test = [] {
        kiln::Standalone game(*kiln::start(kiln::level::flawed()), {.keep = 600, .most_steps = 1200});
        agent::SubjectSession session({&game});
        const Report report = playtest(session, 128, 64);
        std::println("{}", report.why);
        expect(!report.won);
        expect(report.why.starts_with("stuck at x")) << report.why;
        expect(report.why.contains("0 bombs")) << report.why;
        const auto at = report.why.find("stuck at x ");
        const double x = std::stod(report.why.substr(at + 11));
        expect(x > 100.0 && x < 113.0) << std::format("before the second wall (column 113): {}", x);
    };
}};
