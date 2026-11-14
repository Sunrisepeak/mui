module omui.showcase.widget_breakout.headless;

import std;
import xengine.world;

namespace omui::breakout {

namespace {
constexpr std::string_view kind_name(Kind kind) noexcept {
    switch (kind) {
        case Kind::button: return "button";
        case Kind::toggle: return "toggle";
        case Kind::slider: return "slider";
        case Kind::card: return "card";
        case Kind::wall: return "wall";
        case Kind::hidden: return "hidden";
        case Kind::gate: return "gate";
        case Kind::shutter: return "shutter";
        case Kind::kicker: return "kicker";
        case Kind::core: return "core";
    }
    return "?";
}
}  // namespace

std::string Play::observe(const State& state, bool full) {
    const Paddle& paddle = state.paddles[0];
    std::string out = std::format("{{\"field\":[{},{}],\"paddle\":{{\"x\":{},\"y\":{},\"half\":{}}},\"score\":{},\"lives\":{},\"standing\":{},\"aim\":{},\"over\":{}",
                                  field_w, state.height, paddle.x, paddle.y, paddle.half, state.score, state.lives, state.bricks, state.aim, state.over);
    out += ",\"balls\":[";
    bool first = true;
    state.world.each<Ball>([&](xengine::Entity, const Ball& ball) {
        out += std::format("{}{{\"x\":{},\"y\":{},\"vx\":{},\"vy\":{},\"waiting\":{}}}", first ? "" : ",", ball.at.x, ball.at.y, ball.velocity.x, ball.velocity.y,
                           ball.on != 0);
        first = false;
    });
    out += "],\"bricks\":[";
    first = true;
    state.world.each<Body, Brick>([&](xengine::Entity, const Body& body, const Brick& brick) {
        if (brick.kind == Kind::hidden && !full) return;
        out += std::format("{}{{\"id\":{},\"kind\":\"{}\",\"box\":[{},{},{},{}],\"durability\":{}}}", first ? "" : ",", brick.id, kind_name(brick.kind),
                           body.box.x, body.box.y, body.box.w, body.box.h, brick.durability);
        first = false;
    });
    out += "]";
    if (full) out += std::format(",\"rng\":\"{:016x}\"", state.rng);
    out += "}";
    return out;
}

}  // namespace omui::breakout
