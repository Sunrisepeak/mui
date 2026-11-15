module omui.showcase.glade.sim;

import std;
import mlibs.hash;
import :glade_snapshot;

namespace glade {

namespace {

std::uint32_t stored(const matter::Program& program, std::uint32_t channel, double value) {
    return static_cast<std::uint32_t>(std::clamp(std::ldexp(value, static_cast<int>(program.channels()[channel].fraction)), 0.0, 4294967295.0));
}
double unit(const matter::Program& program, std::uint32_t channel, std::uint32_t value) {
    return std::ldexp(static_cast<double>(value), -static_cast<int>(program.channels()[channel].fraction));
}

// The carried thing nearest a place (by its box), within `reach`.
std::optional<rigid3::BodyId> nearest(const State& state, Vec3 at, float reach) {
    std::optional<rigid3::BodyId> found;
    float best = reach * reach;
    for (const auto& [name, body] : state.names) {
        if (!state.objects->material(body)) continue;
        const auto box = state.world->bounds(body);
        if (!box) continue;
        const float dx = std::max({box->low.x - at.x, 0.0f, at.x - box->high.x}), dy = std::max({box->low.y - at.y, 0.0f, at.y - box->high.y}),
                    dz = std::max({box->low.z - at.z, 0.0f, at.z - box->high.z});
        const float squared = dx * dx + dy * dy + dz * dz;
        if (squared <= best) best = squared, found = body;
    }
    return found;
}

std::string said_who(const State& state, matter::Who who) {
    if (who.carrier == rigid3::bodies_carrier)
        if (const auto name = name_of(state, rigid3::body_of(who))) return std::string(*name);
    if (who.carrier == particles::particles_carrier) return std::format("a drop ({})", who.id);
    return "the air";
}

}  // namespace

std::shared_ptr<const matter::Program> program() {
    static const std::shared_ptr<const matter::Program> made = [] {
        auto pack = matter::read_pack(glade_snapshot, "glade.matter.omui.json");
        if (!pack) return std::shared_ptr<const matter::Program>();
        const std::array<matter::Definition, 1> packs{std::move(*pack)};
        auto prepared = matter::standard_with(packs);
        return prepared ? std::make_shared<const matter::Program>(std::move(*prepared)) : std::shared_ptr<const matter::Program>();
    }();
    return made;
}

Level level::glade() {
    Level out{.name = "glade", .wind = {0, 0, 0}, .stream_from = 0, .stream_to = 2};
    auto& things = out.things;
    // The ground: the two banks and the stream's bed between them, the stream itself a body of water in it.
    things.push_back({.name = "near bank", .at = {-5, -0.5f, 0}, .half = {5, 0.5f, 4}, .material = "stone", .carried = false});
    things.push_back({.name = "far bank", .at = {7, -0.5f, 0}, .half = {5, 0.5f, 4}, .material = "stone", .carried = false});
    things.push_back({.name = "stream bed", .at = {1, -0.9f, 0}, .half = {1, 0.1f, 4}, .material = "stone", .carried = false});
    things.push_back({.name = "stream", .at = {1, -0.45f, 0}, .half = {1, 0.35f, 4}, .material = "water"});
    // The footbridge: planks across, end to end, resting on both banks.
    for (int plank = 0; plank < 5; ++plank)
        things.push_back({.name = std::format("plank {}", plank + 1), .at = {-0.3f + 0.651f * static_cast<float>(plank), 0.05f, 0}, .half = {0.325f, 0.05f, 0.4f}, .material = "wood"});
    // Grass: a strip on each bank from the bridge, tufts end to end along x.
    for (int tuft = 0; tuft < 10; ++tuft)
        things.push_back({.name = std::format("near grass {}", tuft + 1), .at = {-0.75f - 0.251f * static_cast<float>(tuft), 0.15f, 0}, .half = {0.125f, 0.15f, 0.1f}, .material = "grass"});
    for (int tuft = 0; tuft < 10; ++tuft)
        things.push_back({.name = std::format("far grass {}", tuft + 1), .at = {2.755f + 0.251f * static_cast<float>(tuft), 0.15f, 0}, .half = {0.125f, 0.15f, 0.1f}, .material = "grass"});
    // The crate in the long grass at its end, resting on the last two tufts.
    things.push_back({.name = "crate", .at = {5.0f, 0.71f, 0}, .half = {0.4f, 0.4f, 0.4f}, .material = "wood", .fixed = false});
    // The campfire, 3 m along the near bank from the grass: logs in a ring of stones.
    for (int log = 0; log < 3; ++log)
        things.push_back({.name = std::format("log {}", log + 1), .at = {-3.0f + 0.201f * static_cast<float>(log), 0.1f, -3}, .half = {0.1f, 0.1f, 0.4f}, .material = "wood"});
    for (int stone = 0; stone < 4; ++stone) {
        const float side = stone % 2 ? 1.0f : -1.0f;
        things.push_back({.name = std::format("stone {}", stone + 1), .at = stone < 2 ? Vec3{-2.8f + side * 0.55f, 0.1f, -3} : Vec3{-2.8f, 0.1f, -3 + side * 0.6f},
                          .half = {0.1f, 0.1f, 0.1f}, .material = "stone"});
    }
    // The ice, a block 1.5 m from the fire.
    things.push_back({.name = "ice", .at = {-1.2f, 0.25f, -3}, .half = {0.25f, 0.25f, 0.25f}, .material = "ice", .fixed = false});
    return out;
}

State start(const Level& level) {
    State state;
    state.program = program();
    const matter::Program& matter = *state.program;
    state.world = mlibs::simulation::Branching<rigid3::World>(std::move(*rigid3::World::make({})));
    state.objects.emplace(std::move(*rigid3::Objects::make(matter, {.ambient = *matter.material("air")})));
    state.air.emplace(std::move(*field::Field::make(matter, {.air = *matter.material("air"), .wind = level.wind})));
    state.cloud.emplace(std::move(*particles::Cloud::make(state.program, {.ambient = *matter.material("air"), .touching = true})));
    state.wind = level.wind, state.stream_from = level.stream_from, state.stream_to = level.stream_to;
    for (const Thing& thing : level.things) (void)drop(state, thing);
    state.done.clear();
    return state;
}

bool drop(State& state, const Thing& thing) {
    const matter::Program& matter = *state.program;
    const auto material = matter.material(thing.material);
    if (!material || body(state, thing.name)) return false;
    rigid3::Material mechanics = rigid3::material_of(matter, *material);
    if (thing.density > 0) mechanics.density = thing.density;
    const auto made = state.world->add({.kind = thing.fixed ? rigid3::Kind::fixed : rigid3::Kind::moving, .at = thing.at}, rigid3::Shape::box(thing.half), mechanics);
    if (!made) return false;
    if (thing.carried) (void)state.objects->add(*made, *material), state.names.push_back({thing.name, *made});
    else state.ground.push_back(*made), state.names.push_back({thing.name, *made});
    state.done.push_back(std::format("{:.2f} s: put down {} ({})", static_cast<double>(state.steps) / steps_per_second, thing.name, thing.material));
    return true;
}

void step(State& state) {
    const matter::Program& matter = *state.program;
    const std::uint32_t temperature = *matter.channel("temperature");
    // The torches held: what each is on kept at its heat; one let go is gone.
    std::erase_if(state.torches, [&](const Held& held) { return held.until <= state.steps; });
    for (const Held& held : state.torches)
        if (const auto now = state.objects->get(held.body, temperature)) (void)state.objects->set(held.body, temperature, std::max(*now, stored(matter, temperature, held.kelvin)));
    if (state.raining) state.objects->rain(*state.world, 1);
    (void)state.scene.advance(*state.world, *state.objects, *state.cloud, *state.air);
    ++state.steps;
}

void run(State& state, int steps) {
    for (int count = 0; count < steps; ++count) step(state);
}

bool torch(State& state, Vec3 at, float seconds, float kelvin) {
    const auto found = nearest(state, at, 0.6f);
    if (!found) return false;
    state.torches.push_back({*found, state.steps + static_cast<std::int64_t>(std::lround(seconds * steps_per_second)), kelvin});
    state.done.push_back(std::format("{:.2f} s: a torch to {} for {:.1f} s", static_cast<double>(state.steps) / steps_per_second, *name_of(state, *found), seconds));
    return true;
}

void pour(State& state, Vec3 at, int drops) {
    // A bucket's water let go above the place: layers of nine drops, 2 cm apart, falling.
    std::vector<particles::Spawn> spawns;
    const auto water = *state.program->material("water");
    for (int drop = 0; drop < drops; ++drop) {
        const int layer = drop / 9, place = drop % 9;
        spawns.push_back({.at = {at.x - 0.021f + 0.021f * static_cast<float>(place % 3), at.y + 0.5f + 0.022f * static_cast<float>(layer), at.z - 0.021f + 0.021f * static_cast<float>(place / 3)},
                          .velocity = {0, -1, 0}, .life = 120, .material = water});
    }
    state.cloud->emit(spawns);
    state.done.push_back(std::format("{:.2f} s: water poured ({} drops) at ({:.1f}, {:.1f}, {:.1f})", static_cast<double>(state.steps) / steps_per_second, drops, at.x, at.y, at.z));
}

bool push(State& state, std::string_view name, Vec3 impulse) {
    const auto found = body(state, name);
    if (!found) return false;
    state.world->push(*found, impulse);
    state.done.push_back(std::format("{:.2f} s: pushed {}", static_cast<double>(state.steps) / steps_per_second, name));
    return true;
}

void wind(State& state, Vec3 far) {
    state.wind = far;
    state.air->set_wind(far);
    state.done.push_back(std::format("{:.2f} s: the wind ({:.1f}, {:.1f}, {:.1f}) m/s", static_cast<double>(state.steps) / steps_per_second, far.x, far.y, far.z));
}

void rain(State& state, bool on) {
    state.raining = on;
    state.done.push_back(std::format("{:.2f} s: rain {}", static_cast<double>(state.steps) / steps_per_second, on ? "on" : "off"));
}

std::optional<std::string> strike(State& state) {
    const auto struck = state.objects->lightning(*state.world);
    std::optional<std::string> out;
    if (struck)
        if (const auto name = name_of(state, *struck)) out = std::string(*name);
    state.done.push_back(std::format("{:.2f} s: lightning ({})", static_cast<double>(state.steps) / steps_per_second, out.value_or("struck nothing")));
    return out;
}

std::optional<rigid3::BodyId> body(const State& state, std::string_view name) {
    for (const auto& [thing, made] : state.names)
        if (thing == name) return made;
    return std::nullopt;
}

std::optional<std::string_view> name_of(const State& state, rigid3::BodyId made) {
    for (const auto& [thing, one] : state.names)
        if (one == made) return std::string_view(thing);
    return std::nullopt;
}

std::vector<Seen> observe(const State& state) {
    std::vector<Seen> out;
    for (const auto& [name, made] : state.names)
        if (const auto one = seen(state, name)) out.push_back(*one);
    return out;
}

std::optional<Seen> seen(const State& state, std::string_view name) {
    const matter::Program& matter = *state.program;
    const auto made = body(state, name);
    if (!made) return std::nullopt;
    const auto pose = state.world->pose(*made);
    Seen out{.name = std::string(name), .at = pose ? pose->centre : Vec3{}};
    const auto material = state.objects->material(*made);
    if (!material) return out;  // the ground: no matter carried
    out.material = matter.materials()[*material].name;
    const std::uint32_t temperature = *matter.channel("temperature"), burning = *matter.channel("burning"), wetness = *matter.channel("wetness");
    out.kelvin = static_cast<float>(unit(matter, temperature, *state.objects->get(*made, temperature)));
    out.burning = *state.objects->get(*made, burning) > 0;
    out.wetness = static_cast<float>(unit(matter, wetness, *state.objects->get(*made, wetness)));
    out.gone = out.material == "ash" || out.material == "smoke";
    return out;
}

std::vector<matter::Explained> chain(const State& state, std::string_view name) {
    const auto made = body(state, name);
    if (!made) return {};
    // Burning: why it is burning (not what was last done to it); else why it is what it is.
    const auto burning = state.program->channel("burning");
    const bool alight = burning && state.objects->get(*made, *burning).value_or(0) > 0;
    return rigid3::Scene::explain(rigid3::who(*made), *state.objects, *state.cloud, 32, alight ? burning : std::nullopt);
}

std::string explain(const State& state, std::string_view name) {
    const matter::Program& matter = *state.program;
    const auto law = [&](std::uint32_t index) -> std::string_view { return index < matter.laws().size() ? std::string_view(matter.laws()[index].name) : std::string_view("—"); };
    std::string out;
    for (const matter::Explained& link : chain(state, name)) {
        const double seconds = static_cast<double>(link.cause.step) / steps_per_second;
        out += std::format("{} at {:.2f} s: {}", said_who(state, link.who), seconds, link.cause.kind == matter::CauseKind::applied ? std::string_view("done from outside")
                                                                               : link.cause.kind == matter::CauseKind::struck ? std::string_view("struck by lightning") : law(link.cause.law));
        if (link.cause.from) out += std::format(", the most in by `{}` from {}", law(link.cause.inflow), said_who(state, link.cause.from));
        else if (link.cause.through) out += std::format(", the most in by `{}` through the air", law(link.cause.inflow));
        out += "\n";
    }
    return out;
}

std::uint64_t checksum(const State& state) {
    mlibs::hash::Fnv1a hash;
    hash.u64(state.world->state_hash()).u64(state.objects->state_hash()).u64(state.air->state_hash()).u64(state.cloud->fingerprint());
    hash.u64(static_cast<std::uint64_t>(state.steps)).u64(state.raining ? 1 : 0);
    for (const float part : {state.wind.x, state.wind.y, state.wind.z}) hash.le(std::bit_cast<std::uint32_t>(part), 4);
    for (const Held& held : state.torches) hash.le(held.body.value, 4).u64(static_cast<std::uint64_t>(held.until));
    return hash.value();
}

std::vector<Task> tasks() {
    std::vector<Task> out;
    out.push_back({.name = "across",
                   .goal = "set the crate on the far bank alight without crossing the stream",
                   .done = [](const State& state) {
                       const auto crate = seen(state, "crate");
                       return crate && (crate->burning || crate->material == "ash");
                   },
                   .may_act_at = [](Vec3 at) { return at.x < -0.2f; }});
    out.push_back({.name = "campfire",
                   .goal = "light the campfire after the rain",
                   .done = [](const State& state) {
                       for (int log = 1; log <= 3; ++log)
                           if (const auto one = seen(state, std::format("log {}", log)); one && one->burning) return true;
                       return false;
                   },
                   .may_act_at = [](Vec3) { return true; }});
    out.push_back({.name = "ice",
                   .goal = "melt the block of ice",
                   .done = [](const State& state) {
                       const auto ice = seen(state, "ice");
                       return ice && ice->material != "ice";
                   },
                   .may_act_at = [](Vec3 at) { return at.x < -0.2f; }});
    return out;
}

}  // namespace glade
