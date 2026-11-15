module omui.showcase.glade.headless;

import std;
import xengine.input;

namespace glade {

namespace {

// The valley the game is: its ground gives the hand's height over a place.
const Level& valley() {
    static const Level made = level::glade();
    return made;
}

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char letter : text) {
        switch (letter) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(letter) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(letter));
                else out += letter;
        }
    }
    return out + "\"";
}

constexpr std::array verb_names{"torch", "pour", "drop", "push", "wind", "rain", "strike"};

// The goals, made once: the predicates are the simulation's.
const std::vector<Task>& goals() {
    static const std::vector<Task> made = tasks();
    return made;
}

float axis_of(const xengine::Actions& hand, int axis) { return static_cast<float>(hand.axis(axis)); }

}  // namespace

Vec3 aimed(std::int16_t pointer_x, std::int16_t pointer_y) {
    const float x = aim_x0 + static_cast<float>(pointer_x) / 32767.0f * aim_width, z = aim_z0 + static_cast<float>(pointer_y) / 32767.0f * aim_depth;
    return {x, valley().ground.height(x, z) + 0.1f, z};
}

std::array<double, 2> pointer_at(Vec3 place) noexcept {
    return {std::clamp((place.x - aim_x0) / aim_width, 0.0f, 1.0f), std::clamp((place.z - aim_z0) / aim_depth, 0.0f, 1.0f)};
}

Play::State begin() { return {start(valley())}; }

void Play::step(State& state, const Input& input) {
    const xengine::Actions& hand = input.hand;
    const std::uint32_t pressed = hand.held & ~state.held, released = state.held & ~hand.held;
    const bool aiming = hand.pointer_x >= 0 && hand.pointer_y >= 0;
    const Vec3 at = aiming ? aimed(hand.pointer_x, hand.pointer_y) : Vec3{0, 0, 0};
    const auto went_down = [&](int button) { return ((pressed >> button) & 1u) != 0; };
    glade::State& valley_state = state.valley;
    if ((released >> torch_button) & 1u) let_go(valley_state);
    // A torch held is a torch until it is let go (an hour is as long as a hand holds one).
    if (went_down(torch_button) && aiming) (void)torch(valley_state, at, 3600);
    if (went_down(pour_button) && aiming) pour(valley_state, at, bucket);
    if (went_down(push_button) && aiming) {
        const Vec3 way{axis_of(hand, push_x_axis), 0, axis_of(hand, push_z_axis)};
        if (const auto thing = nearest_to(valley_state, at, 1.0f); thing && (way.x != 0 || way.z != 0))
            (void)push(valley_state, std::string(*thing), Vec3{way.x * push_impulse, 0, way.z * push_impulse});
    }
    if (went_down(rain_button)) rain(valley_state, !valley_state.raining);
    if (went_down(strike_button)) (void)strike(valley_state);
    if (went_down(rod_button) && aiming) {
        int count = 1;
        while (body(valley_state, std::format("rod {}", count))) ++count;
        (void)drop(valley_state, {.name = std::format("rod {}", count), .at = {at.x, at.y + 0.9f, at.z}, .half = {0.03f, 1.0f, 0.03f}, .material = "iron"});
    }
    // The wind blows as the axes are held: the valley's own wind and the hand's.
    const Vec3 wind_now{valley().wind.x + axis_of(hand, wind_x_axis) * wind_speed, valley().wind.y, valley().wind.z + axis_of(hand, wind_z_axis) * wind_speed};
    if (wind_now.x != valley_state.wind.x || wind_now.y != valley_state.wind.y || wind_now.z != valley_state.wind.z) wind(valley_state, wind_now);
    state.held = hand.held;
    glade::step(valley_state);
}

std::uint64_t Play::checksum(const State& state) {
    // The buttons held are state too: what a press is depends on them.
    return glade::checksum(state.valley) ^ (static_cast<std::uint64_t>(state.held) * 0x9e3779b97f4a7c15ull);
}

bool Play::over(const State& state) {
    return std::ranges::all_of(goals(), [&](const Task& goal) { return goal.done(state.valley); });
}

std::string Play::observe(const State& state, bool full) {
    const glade::State& valley_state = state.valley;
    std::string out = std::format(R"({{"step":{},"seconds":{:.2f},"raining":{},"wind":[{:.2f},{:.2f},{:.2f}],"things":[)", valley_state.steps,
                                  static_cast<double>(valley_state.steps) / steps_per_second, valley_state.raining, valley_state.wind.x, valley_state.wind.y,
                                  valley_state.wind.z);
    const auto seen_now = glade::observe(valley_state);
    for (std::size_t index = 0; index < seen_now.size(); ++index) {
        const Seen& thing = seen_now[index];
        out += std::format(R"({}{{"name":{},"material":{},"at":[{:.2f},{:.2f},{:.2f}],"kelvin":{:.0f},"burning":{},"wetness":{:.0f},"gone":{},"open":{}}})",
                           index ? "," : "", json_text(thing.name), json_text(thing.material), thing.at.x, thing.at.y, thing.at.z, thing.kelvin, thing.burning,
                           thing.wetness, thing.gone, thing.open);
    }
    out += R"(],"goals":[)";
    for (std::size_t index = 0; index < goals().size(); ++index) {
        const Task& goal = goals()[index];
        std::string verbs;
        for (const Verb verb : goal.verbs) verbs += std::format("{}{}", verbs.empty() ? "" : ",", json_text(verb_names[static_cast<std::size_t>(verb)]));
        std::string about;
        for (const std::string& name : goal.about) about += std::format("{}{}", about.empty() ? "" : ",", json_text(name));
        out += std::format(R"({}{{"name":{},"goal":{},"about":[{}],"verbs":[{}],"where":{},"x_below":{},"done":{}}})", index ? "," : "", json_text(goal.name),
                           json_text(goal.goal), about, verbs, json_text(goal.where), goal.x_below ? std::format("{:.2f}", *goal.x_below) : std::string("null"),
                           goal.done(valley_state));
    }
    out += "]";
    if (full) {
        // Why each thing that is no longer as it began is what it became.
        const Level& began = valley();
        out += std::format(R"(,"hash":"{:016x}","why":{{)", checksum(state));
        bool first = true;
        for (const Seen& thing : seen_now) {
            const auto start_thing = std::ranges::find(began.things, thing.name, &Thing::name);
            const bool changed = thing.burning || thing.gone || (start_thing != began.things.end() && start_thing->material != thing.material);
            if (!changed) continue;
            out += std::format("{}{}:{}", first ? "" : ",", json_text(thing.name), json_text(explain(valley_state, thing.name)));
            first = false;
        }
        out += "}";
    }
    return out + "}";
}

}  // namespace glade
