// A playtest by an agent, no interface: Quay at the runtime, played through the agent kernel's requests a person's
// tier allows (world.observe, world.act, world.step, world.branch) — a tenth of a second's reaction, ten actions a
// second, shown only what the watchkeeper sees and hears. It never reads where anything is.
//
// It fights what it sees: it turns onto a warden by the bearing it is shown, counting the turn it already asked for
// that has not shown yet (its reaction is late by 12 steps), and fires when on it. A warden's shot it hears and does
// not see, it turns toward. Otherwise it goes for the beacon's light: every half second it tries ways — the
// beacon's bearing and 30°, 60°, 90° either side — each on a branch a second and a half ahead, and takes the one
// that ends nearest the beacon (and least hurt). Given the harbour, it lights the beacon; given the harbour with a
// container dropped across the breakwater, it stops and says where and why.
import std;
import mlibs.agent;
import mlibs.json;
import xengine.playtest;
import omui.showcase.quay.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace playtest = xengine::playtest;

namespace {

constexpr double turn_rate = 2.6 * 180 / 3.14159265358979323846, look_rate = 1.8 * 180 / 3.14159265358979323846;  // degrees a second at a full axis
constexpr int decide_every = 6, reaction = 12, plan_every = 30, try_steps = 90;

struct Bearing {
    double bearing = 0, elevation = 0, distance = 0;
    double across = 0;  // the beacon's: to its mast's foot, over the ground
};
struct Heard {
    std::string what;
    double      bearing = 0, level = 0, ago = 0;
};
struct View {
    std::int64_t         frame = 0;
    std::string          outcome, why;
    std::int64_t         health = 0, rounds = 0;
    bool                 struck = false;
    double               lighting = 0, tide = 0;
    Bearing              beacon;
    std::vector<Bearing> wardens;
    std::vector<Heard>   heard;
    std::vector<double>  depth;  // from −60° every 10°
};
Bearing bearing_of(const mlibs::json::Node& node) {
    return {node["bearing"].number().value_or(0), node["elevation"].number().value_or(0), node["distance"].number().value_or(0), node["across"].number().value_or(0)};
}
View read(const mlibs::json::Node& data) {
    const auto view = data["view"];
    View seen;
    seen.frame = view["frame"].integer().value_or(0);
    seen.outcome = std::string(view["outcome"].text().value_or("")), seen.why = std::string(view["why"].text().value_or(""));
    seen.tide = view["tide"].number().value_or(0);
    const auto keeper = view["keeper"];
    seen.health = keeper["health"].integer().value_or(0), seen.rounds = keeper["rounds"].integer().value_or(0);
    seen.struck = keeper["struck"].boolean() == true, seen.lighting = keeper["lighting"].number().value_or(0);
    seen.beacon = bearing_of(view["beacon"]);
    for (const auto& warden : view["wardens"].items()) seen.wardens.push_back(bearing_of(warden));
    for (const auto& sound : view["heard"].items())
        seen.heard.push_back({std::string(sound["what"].text().value_or("")), sound["bearing"].number().value_or(0), sound["level"].number().value_or(0), sound["ago"].number().value_or(0)});
    for (const auto& metres : view["depth"]["metres"].items()) seen.depth.push_back(metres.number().value_or(0));
    return seen;
}

double wrapped(double degrees) {
    while (degrees > 180) degrees -= 360;
    while (degrees < -180) degrees += 360;
    return degrees;
}

struct Report {
    bool        won = false;
    std::string why;
    std::int64_t frames = 0, health = 0, rounds = 0;
    int         tried = 0;  // branches tried
};

class Player {
public:
    explicit Player(playtest::Hands& hands) : hands_(&hands) {}

    Report play(std::int64_t most) {
        Report report;
        double nearest = 1e9;
        std::int64_t nearer_at = 0;
        while (hands_->ok()) {
            const View view = read(hands_->observe().root()["data"]);
            if (view.outcome != "playing") {
                report.won = view.outcome == "won", report.why = view.why;
                report.frames = view.frame, report.health = view.health, report.rounds = view.rounds;
                break;
            }
            if (view.frame >= most) {
                report.why = "out of time", report.frames = view.frame;
                break;
            }
            // Coming no nearer the beacon for six seconds: say where and why.
            if (view.beacon.across < nearest - 0.5) nearest = view.beacon.across, nearer_at = view.frame;
            if (view.frame - nearer_at > 6 * quay::steps_per_second && view.wardens.empty()) {
                report.why = stuck(view), report.frames = view.frame, report.health = view.health, report.rounds = view.rounds;
                break;
            }
            decide(view, report);
            hands_->step(decide_every);
        }
        report.tried = tried_;
        if (!hands_->ok()) report.why = std::format("the game refused: {}", hands_->problem());
        return report;
    }

private:
    // The turn (degrees, to the right) and look (up) still to come of what was asked: the steps from `frame` until
    // an action asked now would take effect.
    [[nodiscard]] std::pair<double, double> coming(std::int64_t frame) const {
        double turn = 0, look = 0;
        for (std::int64_t step = frame; step < frame + reaction - 1; ++step) {
            const auto held = std::ranges::find_last_if(asked_, [&](const Asked& asked) { return asked.from <= step; });
            if (held.empty()) continue;
            turn += held.front().turn * turn_rate / quay::steps_per_second, look += held.front().look * look_rate / quay::steps_per_second;
        }
        return {turn, look};
    }
    void act(const View& view, double turn, double look, double walk, std::initializer_list<int> buttons) {
        std::string held;
        for (const int button : buttons) held += std::format("{}{}", held.empty() ? "" : ",", button);
        const auto answer = hands_->act(std::format(R"("seen":{},"axis":[0,{:.4f},{:.4f},{:.4f}],"buttons":[{}])", view.frame, walk, turn, look, held));
        // The step it is first held in: the frame it shows from, less one.
        const std::int64_t from = answer.root()["data"]["effective"].integer().value_or(view.frame + reaction) - 1;
        asked_.push_back({from, turn, look});
        if (asked_.size() > 8) asked_.erase(asked_.begin());
    }
    // Onto a bearing: the axis that closes what is left of it, after what is coming, by the time the next action
    // takes over. What is left after this one, returned.
    [[nodiscard]] std::pair<double, double> onto(const View& view, double bearing, double elevation, double& turn, double& look) const {
        const auto [turning, looking] = coming(view.frame);
        const double left = wrapped(bearing - turning), above = elevation - looking;
        turn = std::clamp(left / (turn_rate * decide_every / quay::steps_per_second), -1.0, 1.0);
        look = std::clamp(above / (look_rate * decide_every / quay::steps_per_second), -1.0, 1.0);
        return {left, above};
    }

    void decide(const View& view, Report&) {
        double turn = 0, look = 0;
        // A warden in view: the nearest; fire when on it.
        if (!view.wardens.empty()) {
            const Bearing& warden = view.wardens.front();
            const auto [left, above] = onto(view, warden.bearing, warden.elevation, turn, look);
            const double within = std::max(0.5, std::atan2(0.3, warden.distance) * 180 / 3.14159265358979323846);
            const bool on = std::abs(left) < within && std::abs(above) < within * 2;
            return act(view, turn, look, 0, on ? std::initializer_list<int>{quay::fire_button} : std::initializer_list<int>{});
        }
        // A warden's shot heard and not seen: turn toward it.
        for (const Heard& sound : view.heard)
            if (sound.what == "warden shot") {
                (void)onto(view, sound.bearing, 0, turn, look);
                return act(view, turn, look, 0, {});
            }
        // At the beacon: work it.
        if (view.beacon.across < 2.0) return act(view, 0, 0, 0, {quay::use_button});
        // The way to the beacon: tried on branches every half second, kept between.
        if (view.frame >= planned_) {
            planned_ = view.frame + plan_every;
            way_ = best_way(view);
        }
        const auto [left, above] = onto(view, view.beacon.bearing + way_, view.beacon.elevation - std::atan2(quay::beacon_height - quay::eye_height, view.beacon.across) * 180 / 3.14159265358979323846, turn, look);
        (void)above;
        act(view, turn, look, std::abs(left) < 35 ? 1.0 : 0.0, {});
    }

    // Of the ways tried, the one whose branch ends nearest the beacon, least hurt. A way: degrees off the beacon's bearing.
    [[nodiscard]] double best_way(const View& view) {
        static constexpr std::array<double, 7> ways{0, -30, 30, -60, 60, -90, 90};
        double best = way_, best_score = 1e9;
        for (const double way : ways) {
            const double heading = wrapped(view.beacon.bearing + way);
            const int turning = std::max(reaction, static_cast<int>(std::ceil(std::abs(heading) / turn_rate * quay::steps_per_second)));
            const double axis = std::clamp(heading / (turn_rate * turning / quay::steps_per_second), -1.0, 1.0);
            const auto future = hands_->branch(try_steps, std::format(R"("axis":[0,1,{:.4f},0])", axis), turning, R"("axis":[0,1,0,0])");
            ++tried_;
            if (!hands_->ok()) return way_;
            const View ahead = read(future.root()["data"]);
            const double score = ahead.beacon.across + 0.2 * static_cast<double>(view.health - ahead.health) + (ahead.outcome == "lost" ? 1000 : 0);
            if (score < best_score - 0.05) best_score = score, best = way;
        }
        return best;
    }

    // Why it cannot go on: how far the beacon is, and what the eye meets on its side.
    [[nodiscard]] static std::string stuck(const View& view) {
        int from = 0, to = 0;
        bool found = false;
        double nearest = 1e9;
        for (std::size_t sample = 0; sample < view.depth.size(); ++sample) {
            const int angle = quay::depth_from + static_cast<int>(sample) * quay::depth_every;
            if (std::abs(angle - view.beacon.bearing) > 45 || view.depth[sample] > 3) continue;
            if (!found) from = angle, found = true;
            to = angle, nearest = std::min(nearest, view.depth[sample]);
        }
        if (!found) return std::format("stuck {:.1f} m from the beacon (bearing {:.0f}°): no way tried came nearer for six seconds, and nothing in view stands in the way", view.beacon.across, view.beacon.bearing);
        return std::format("stuck {:.1f} m from the beacon (bearing {:.0f}°): no way tried came nearer for six seconds — something solid {:.1f} m ahead across {:+}° to {:+}°, "
                           "the beacon's side: the way to the beacon is closed",
                           view.beacon.across, view.beacon.bearing, nearest, from, to);
    }

    struct Asked {
        std::int64_t from;
        double       turn, look;
    };
    playtest::Hands*   hands_;
    std::vector<Asked> asked_;
    std::int64_t       planned_ = 0;
    double             way_ = 0;
    int                tried_ = 0;
};

Report play(const quay::Level& level) {
    quay::Standalone game(quay::start(level), {.keep = 600, .most_steps = 600});
    agent::SubjectSession session({&game});
    playtest::Hands hands(session);
    return Player(hands).play(static_cast<std::int64_t>(level.tide) * quay::steps_per_second);
}

}  // namespace

UTest cases{"quay.playtest", [] {
    "a person is shown what the watchkeeper sees and hears: the wardens in view, the beacon's light, nothing of where they are"_test = [] {
        const quay::State state = quay::start(quay::level::harbour());
        const auto human = mlibs::json::Document::parse(quay::observe(state, false), "human");
        const auto full = mlibs::json::Document::parse(quay::observe(state, true), "full");
        expect(fatal(human.has_value() && full.has_value()));
        expect(!human->root()["at"] && !human->root()["all"] && !human->root()["hash"]) << "no places at a person's tier";
        expect(full->root()["hash"] && eq(full->root()["all"].items().size(), std::size_t{4}));
        expect(eq(human->root()["depth"]["metres"].items().size(), std::size_t{13}));
        const double beacon = human->root()["beacon"]["across"].number().value_or(0);
        expect(gt(beacon, 76.0) && lt(beacon, 78.0)) << "77 m down the quay";
        expect(lt(std::abs(human->root()["beacon"]["bearing"].number().value_or(99)), 1.0)) << "straight ahead";
    };

    "an agent playing as a person — shown what the watchkeeper sees and hears, branching to choose its way — lights the beacon"_test = [] {
        const Report report = play(quay::level::harbour());
        std::println("{} at frame {} ({:.1f} s): {}; health {}, {} rounds left, {} ways tried on branches", report.won ? "won" : "not won", report.frames,
                     static_cast<double>(report.frames) / quay::steps_per_second, report.why, report.health, report.rounds, report.tried);
        expect(report.won) << report.why;
    };

    "given the harbour with a container dropped across the breakwater, it stops before it and says the way is closed"_test = [] {
        const Report report = play(quay::level::flawed());
        std::println("{}", report.why);
        expect(!report.won);
        expect(report.why.starts_with("stuck")) << report.why;
        expect(report.why.contains("the way to the beacon is closed")) << report.why;
        const auto at = report.why.find("stuck ");
        const double distance = std::stod(report.why.substr(at + 6));
        expect(distance > 9.0 && distance < 13.0) << std::format("before the container (its face 10.2 m short of the beacon): {}", distance);
    };
}};
