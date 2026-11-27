module omui.showcase.fighting.headless;

import std;
import gongfu.data;
import mlibs.hash;

namespace fighting {

namespace {

// The work's fighters, read once from the files the build carries; a match by a pair of them, kept to step in.
struct Roster {
    gongfu::SystemDef                 system;
    std::vector<std::string_view>     names;
    std::vector<gongfu::Fighter>      fighters;
    std::vector<std::int32_t>         reach;  // each one's quickest ground strike's: within this far apart it lands
    std::vector<std::string>          problems;
    std::map<std::pair<std::int32_t, std::int32_t>, gongfu::Match> matches;  // one thread steps them
};

Roster& roster() {
    static Roster made = [] {
        Roster out;
        out.system.round_frames = 99 * 60;
        for (const files::FighterFiles& files : files::fighters) {
            auto fighter = gongfu::data::read_fighter(files.fighter, std::format("{}.fighter.gongfu.toml", files.name), files.boxes, std::format("{}.boxes.gongfu.toml", files.name));
            if (!fighter) {
                for (const auto& problem : fighter.error()) out.problems.push_back(problem.text());
                continue;
            }
            out.names.push_back(files.name);
            const gongfu::ai::Kit kit = gongfu::ai::read_kit(*fighter);
            out.reach.push_back(kit.pokes.empty() ? 0 : kit.pokes.front().reach);
            out.fighters.push_back(std::move(*fighter));
        }
        return out;
    }();
    return made;
}

gongfu::Match& match_of(const State& state) {
    Roster& known = roster();
    const auto pair = std::pair{state.left, state.right};
    auto found = known.matches.find(pair);
    if (found == known.matches.end())
        found = known.matches.try_emplace(pair, known.system, known.fighters[static_cast<std::size_t>(state.left)], known.fighters[static_cast<std::size_t>(state.right)]).first;
    found->second.restore(state.match);
    return found->second;
}

std::string_view doing(const gongfu::Fighter& fighter, const gongfu::FighterState& body) noexcept {
    if (body.mode == gongfu::Mode::move && body.move >= 0) return fighter.def.moves[static_cast<std::size_t>(body.move)].name;
    return gongfu::name(body.mode);
}

}  // namespace

std::vector<std::string_view> names() { return roster().names; }

std::expected<State, std::string> start(std::string_view left, std::string_view right, std::uint64_t seed) {
    Roster& known = roster();
    if (!known.problems.empty()) return std::unexpected(std::format("the fighters' files: {}", known.problems.front()));
    const auto place = [&](std::string_view name) -> std::expected<std::int32_t, std::string> {
        const auto found = std::ranges::find(known.names, name);
        if (found == known.names.end()) {
            std::string all;
            for (const std::string_view each : known.names) all += std::format("{}{}", all.empty() ? "" : ", ", each);
            return std::unexpected(std::format("no fighter is named `{}` ({})", name, all));
        }
        return static_cast<std::int32_t>(found - known.names.begin());
    };
    const auto at_left = place(left);
    if (!at_left) return std::unexpected(at_left.error());
    const auto at_right = place(right);
    if (!at_right) return std::unexpected(at_right.error());
    State state;
    state.left = *at_left, state.right = *at_right, state.seed = seed;
    const gongfu::Match fresh(known.system, known.fighters[static_cast<std::size_t>(state.left)], known.fighters[static_cast<std::size_t>(state.right)]);
    state.match = fresh.state();
    state.computer.emplace(known.fighters[static_cast<std::size_t>(state.right)], 1, seed);
    return state;
}

gongfu::Pad pad_of(const meng::Actions& actions) noexcept {
    constexpr std::int16_t pushed = 16384;  // half way: a stick's dead middle is no direction
    gongfu::Pad pad;
    if (actions.axes[0] <= -pushed) pad.directions |= gongfu::Pad::left;
    if (actions.axes[0] >= pushed) pad.directions |= gongfu::Pad::right;
    if (actions.axes[1] <= -pushed) pad.directions |= gongfu::Pad::up;
    if (actions.axes[1] >= pushed) pad.directions |= gongfu::Pad::down;
    pad.buttons = static_cast<std::uint16_t>(actions.held & gongfu::any_button);  // buttons 0 … 5: lp mp hp lk mk hk
    return pad;
}

void step(State& state, const Input& input) {
    if (over(state)) return;
    gongfu::Match& match = match_of(state);
    const gongfu::Pad theirs = state.computer->next(match);
    match.tick(pad_of(input.player), theirs);
    state.match = match.state();
}

std::uint64_t checksum(const State& state) noexcept {
    return mlibs::hash::Fnv1a().u64(gongfu::checksum(state.match)).u64(static_cast<std::uint64_t>(state.left)).u64(static_cast<std::uint64_t>(state.right)).u64(state.seed).value();
}

std::string observe(const State& state, bool full) {
    const Roster& known = roster();
    const gongfu::Match& match = match_of(state);
    // A view read by someone who has never seen the game says itself what its numbers are.
    std::string out =
        R"j({"legend":{"you":"the seat's fighter, side 0, on the left at the start; the computer's is side 1","x":"place along the stage, in the stage's units (the two start 400 apart); y: height above the ground","apart":"how far the two are, centre to centre","reach":"a fighter's quickest ground strike lands when the two are at most this far apart",)j"
        R"j("facing":"+1 right, -1 left: back is the other way","doing":"idle, walk_forward, walk_back, crouch, air, hitstun, blockstun, knockdown, … or a move's name (5LP: a standing light punch)",)j"
        R"j("can_act":"whether a new move or walk starts now","health":"of max_health; at 0 a knock-out","seconds_left":"the round's clock","time":"60 steps a second"},)j";
    const char* outcome = !over(state) ? "playing" : state.match.winner == 0 ? "won" : state.match.winner == 1 ? "lost" : "draw";
    const auto& left = state.match.f[0];
    const auto& right = state.match.f[1];
    out += std::format(R"("outcome":"{}","frame":{},"seconds_left":{:.2f},"apart":{:.3f},"fighters":[)", outcome, state.match.frame, state.match.timer / 60.0,
                       std::abs(right.pos.x.to_double() - left.pos.x.to_double()));
    for (std::int32_t side = 0; side < 2; ++side) {
        const auto& body = state.match.f[static_cast<std::size_t>(side)];
        const gongfu::Fighter& fighter = match.fighter(side);
        const std::int32_t chosen = side == 0 ? state.left : state.right;
        out += std::format(R"({}{{"side":{},"name":"{}","health":{},"max_health":{},"meter":{},"x":{:.3f},"y":{:.3f},"facing":{},"doing":"{}","can_act":{},"combo":{},"reach":{}}})", side == 0 ? "" : ",",
                           side, known.names[static_cast<std::size_t>(chosen)], body.health, fighter.def.health, body.meter, body.pos.x.to_double(), body.pos.y.to_double(), body.facing,
                           doing(fighter, body), match.actionable(side), body.combo, known.reach[static_cast<std::size_t>(chosen)]);
    }
    out += "]";
    if (full) {
        out += R"(,"hidden":[)";
        for (std::int32_t side = 0; side < 2; ++side) {
            const auto& body = state.match.f[static_cast<std::size_t>(side)];
            out += std::format(R"({}{{"vx":{:.4f},"vy":{:.4f},"stun":{},"move_frame":{},"posture":{}}})", side == 0 ? "" : ",", body.vel.x.to_double(), body.vel.y.to_double(), body.stun,
                               body.move_frame, body.posture);
        }
        out += "]";
    }
    return out + "}";
}

}  // namespace fighting
