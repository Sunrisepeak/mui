// Swarm's rules: one step. Only +, −, ×, ÷ and √ on floats (the same bits everywhere; the package is built
// with -ffp-contract=off), the random stream mlibs.math's, the checksum mlibs.hash's. No allocation in a step.
module omui.showcase.swarm.headless;

import std;
import mlibs.hash;
import mlibs.math;
import xengine.input;
import xengine.world;

namespace swarm {

namespace {

using mlibs::math::length;

constexpr float per_step = 1.0f / static_cast<float>(steps_per_second);

[[nodiscard]] float clamped(float value, float low, float high) noexcept { return value < low ? low : value > high ? high : value; }

// Where the next enemy of a wave enters: just outside the side the wave comes from, somewhere along its middle
// four fifths.
[[nodiscard]] Vec2 entry(Side from, double along) noexcept {
    const float share = 0.1f + 0.8f * static_cast<float>(along);
    switch (from) {
        case Side::east: return {arena_width + enemy_radius, arena_height * share};
        case Side::west: return {-enemy_radius, arena_height * share};
        case Side::south: return {arena_width * share, arena_height + enemy_radius};
        case Side::north: return {arena_width * share, -enemy_radius};
    }
    return {};
}

// The waves: one enemy of the wave every `spawn_steps`; the arena empty and the wave all in, a rest, then the next.
void arrive(State& state) noexcept {
    mlibs::math::SplitMix64 stream{state.random};
    if (state.to_come > 0) {
        if (state.spawn_in > 0) {
            --state.spawn_in;
        } else if (const xengine::Entity enemy = state.field.create(); enemy.valid()) {  // a full field: it waits a step
            state.field.add(enemy, Body{entry(state.from, mlibs::math::unit24(stream.next())), {}});
            state.field.add(enemy, Enemy{static_cast<std::uint32_t>(state.wave)});
            --state.to_come, ++state.enemies;
            state.spawn_in = spawn_steps;
        }
    } else if (state.enemies == 0) {
        if (state.rest > 0) {
            --state.rest;
        } else {
            ++state.wave;
            state.to_come = wave_size(state.wave);
            state.from = state.wave == 1 ? Side::east : static_cast<Side>(stream.next() % 4);
            state.spawn_in = 0;
            state.rest = rest_steps;
        }
    }
    state.random = stream.state;
}

}  // namespace

State start(std::uint64_t seed) noexcept {
    State state;
    state.random = mlibs::math::mix64(seed);
    return state;
}

void step(State& state, const Input& input) noexcept {
    if (over(state)) return;  // a game lost stays as it ended
    ++state.frame;
    const xengine::Actions& actions = input.player;

    // The player: pushed by two axes (a diagonal is no faster), kept inside the arena.
    Vec2 push{static_cast<float>(actions.axis(0)), static_cast<float>(actions.axis(1))};
    if (const float pushed = length(push); pushed > 1.0f) push = push * (1.0f / pushed);
    state.player = state.player + push * (player_speed * per_step);
    state.player.x = clamped(state.player.x, player_radius, arena_width - player_radius);
    state.player.y = clamped(state.player.y, player_radius, arena_height - player_radius);

    // The gun points where the pointer is, and stays so when the pointer is gone.
    if (actions.pointer_x >= 0 && actions.pointer_y >= 0) {
        const Vec2 place{static_cast<float>(actions.pointer_x) / 32767.0f * arena_width, static_cast<float>(actions.pointer_y) / 32767.0f * arena_height};
        const Vec2 toward = place - state.player;
        if (const float away = length(toward); away > 0.01f) state.aim = toward * (1.0f / away);
    }
    if (state.reload > 0) --state.reload;
    if (actions.down(shoot_button) && state.reload == 0) {
        if (const xengine::Entity bullet = state.field.create(); bullet.valid()) {
            state.field.add(bullet, Body{state.player + state.aim * player_radius, state.aim * bullet_speed});
            state.field.add(bullet, Bullet{});
            state.reload = reload_steps;
            ++state.shots;
        }
    }

    arrive(state);

    // Enemies go straight at the player.
    state.field.each<Body, Enemy>([&](xengine::Entity, Body& body, Enemy& enemy) {
        const Vec2 toward = state.player - body.at;
        const float away = length(toward);
        body.velocity = away > 0.05f ? toward * (enemy_speed(enemy.wave) / away) : Vec2{};
        body.at = body.at + body.velocity * per_step;
    });

    // Bullets fly; one that meets an enemy removes it and is spent, one that leaves the arena is gone. A
    // bullet moves a unit a step and the two radii are 2.6: none passes through an enemy between steps.
    std::array<xengine::Entity, Field::capacity> spent;
    std::size_t spent_count = 0;
    state.field.each<Body, Bullet>([&](xengine::Entity bullet, Body& body, Bullet& flown) {
        body.at = body.at + body.velocity * per_step;
        ++flown.age;
        bool hit = false;
        state.field.each<Body, Enemy>([&](xengine::Entity enemy, Body& other, Enemy& what) {
            if (hit || !state.field.alive(enemy)) return;
            const Vec2 apart = other.at - body.at;
            const float reach = bullet_radius + enemy_radius;
            if (dot(apart, apart) >= reach * reach) return;
            hit = true;
            state.score += worth(what.wave);
            ++state.kills, --state.enemies;
            state.field.destroy(enemy);
        });
        const bool outside = body.at.x < 0 || body.at.x > arena_width || body.at.y < 0 || body.at.y > arena_height;
        if (hit || outside) spent[spent_count++] = bullet;
    });
    for (std::size_t index = 0; index < spent_count; ++index) state.field.destroy(spent[index]);

    // An enemy that touches the player costs a life and is gone; for a moment after, nothing does.
    if (state.safe_for > 0) --state.safe_for;
    state.field.each<Body, Enemy>([&](xengine::Entity enemy, Body& body, Enemy&) {
        if (state.safe_for > 0) return;
        const Vec2 apart = body.at - state.player;
        const float reach = player_radius + enemy_radius;
        if (dot(apart, apart) >= reach * reach) return;
        --state.lives, --state.enemies;
        state.safe_for = safe_steps;
        state.field.destroy(enemy);
    });
}

std::uint64_t checksum(const State& state) noexcept {
    // Field by field, no padding hashed: the same play, the same number.
    mlibs::hash::Fnv1a hash;
    hash.u64(state.field.checksum()).i64(state.frame);
    hash.f32(state.player.x).f32(state.player.y).f32(state.aim.x).f32(state.aim.y);
    for (const std::int32_t value : {state.lives, state.score, state.wave, state.to_come, state.spawn_in, state.rest, state.enemies, state.reload, state.safe_for,
                                     state.shots, state.kills})
        hash.i64(value);
    hash.u64(static_cast<std::uint64_t>(state.from)).u64(state.random);
    return hash.value();
}

std::string observe(const State& state, bool full) {
    // A view read by someone who has never seen the game says itself what its numbers are.
    std::string out =
        R"j({"legend":{"arena":"[width, height] in arena units; x to the right, y down, [0, 0] the top-left corner",)j"
        R"j("player":"x, y: its centre; aim: the unit vector the gun points along; safe_for: steps for which a touch costs nothing; reload: steps until the next bullet can leave",)j"
        R"j("enemies":"each x, y (its centre; one still outside the arena is on its way in) and away, its distance from the player; it touches at 4",)j"
        R"j("bullets":"each [x, y]","wave":"the wave arriving or last cleared; to_come: its enemies not yet in the arena; from: the side it comes from",)j"
        R"j("pointer":"world.act's pointer [x / width, y / height] aims at the place (x, y)","time":"120 steps a second"},)j";
    out += std::format(R"("arena":[{},{}],"outcome":"{}","score":{},"lives":{},"wave":{},"to_come":{},"from":"{}",)", arena_width, arena_height,
                       over(state) ? "lost" : "playing", state.score, state.lives, state.wave, state.to_come, name(state.from));
    out += std::format(R"("player":{{"x":{:.2f},"y":{:.2f},"aim":[{:.3f},{:.3f}],"safe_for":{},"reload":{}}},"enemies":[)", state.player.x, state.player.y, state.aim.x,
                       state.aim.y, state.safe_for, state.reload);
    bool first = true;
    state.field.each<Body, Enemy>([&](xengine::Entity, const Body& body, const Enemy&) {
        out += std::format(R"({}{{"x":{:.2f},"y":{:.2f},"away":{:.2f}}})", first ? "" : ",", body.at.x, body.at.y, length(body.at - state.player));
        first = false;
    });
    out += R"(],"bullets":[)";
    first = true;
    state.field.each<Body, Bullet>([&](xengine::Entity, const Body& body, const Bullet&) {
        out += std::format("{}[{:.2f},{:.2f}]", first ? "" : ",", body.at.x, body.at.y);
        first = false;
    });
    out += "]";
    if (full) out += std::format(R"(,"hidden":{{"random":"{:016x}","spawn_in":{},"rest":{},"shots":{},"kills":{}}})", state.random, state.spawn_in, state.rest, state.shots, state.kills);
    return out + "}";
}

}  // namespace swarm
