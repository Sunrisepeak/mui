// The stage's behaviour: a match ticked at 60 frames a second by a timer of
// the runtime's clock (so a headless run plays it frame by frame, exactly),
// pads from the keyboard or an agent, and the picture: gongfu.present2d's
// frame scaled into the node, the health and the timer over it, and in
// training every box and the frame meter.
module openmui.showcase.fighting;

import std;
import openmui.core;
import openmui.view;
import gongfu.data;
import gongfu.present2d;

namespace omui::fighting {

namespace {

namespace p2 = gongfu::present2d;
using gongfu::Button;
using gongfu::bit;

constexpr float picture_w = 960, picture_h = 540;
constexpr double frame_period = 1.0 / 60.0;

// The keyboard's two players.
struct Layout {
    Key up, down, left, right;
    std::array<Key, 6> buttons;  // LP MP HP LK MK HK
};
constexpr std::array<Layout, 2> layouts{{
    {Key::w, Key::s, Key::a, Key::d, {Key::u, Key::i, Key::o, Key::j, Key::k, Key::l}},
    // The six keys over the arrows: Insert Home PageUp, Delete End PageDown.
    {Key::up, Key::down, Key::left, Key::right, {Key::insert, Key::home, Key::page_up, Key::delete_, Key::end, Key::page_down}},
}};

// A pad written as the notation writes it: a numpad direction, then buttons
// (`6`, `2LK`, `5LP+LK`, `HP`).
std::optional<gongfu::Pad> parse_pad(std::string_view s) {
    int dir = 5;
    if (!s.empty() && s[0] >= '1' && s[0] <= '9') dir = s[0] - '0', s.remove_prefix(1);
    std::uint16_t buttons = 0;
    while (!s.empty()) {
        const std::size_t plus = s.find('+');
        const std::string_view token = s.substr(0, plus);
        constexpr std::array<std::pair<std::string_view, Button>, 6> names{
            {{"LP", Button::lp}, {"MP", Button::mp}, {"HP", Button::hp}, {"LK", Button::lk}, {"MK", Button::mk}, {"HK", Button::hk}}};
        const auto it = std::ranges::find(names, token, &std::pair<std::string_view, Button>::first);
        if (it == names.end()) return std::nullopt;
        buttons |= bit(it->second);
        if (plus == std::string_view::npos) break;
        s.remove_prefix(plus + 1);
    }
    return gongfu::pad(dir, buttons);
}

Color tone(std::uint32_t hex, float alpha = 1) { return Color::hex(hex, alpha); }

// What a fighter's frame is, for the meter: 0 free, 1 startup, 2 active, 3 recovery, 4 stun.
std::uint8_t phase(const gongfu::Match& m, std::int32_t i) {
    const auto& f = m.state().f[static_cast<std::size_t>(i)];
    if (f.mode == gongfu::Mode::hitstun || f.mode == gongfu::Mode::blockstun) return 4;
    if (f.mode != gongfu::Mode::move) return 0;
    const auto& fighter = m.fighter(i);
    const auto& move = fighter.def.moves[static_cast<std::size_t>(f.move)];
    if (std::ranges::any_of(move.hits, [&](const auto& h) { return h.active.contains(f.move_frame); })) return 2;
    return f.move_frame < fighter.startup(f.move) ? 1 : 3;
}

}  // namespace

void Stage::start_round() {
    const Roster& r = roster();
    if (r.fighters.empty()) return;
    match_ = std::make_unique<gongfu::Match>(r.system, r.fighters.front(), r.fighters.size() > 1 ? r.fighters[1] : r.fighters.front());
    effects_ = {};
    meter_.clear();
    over_reported_ = false;
}

Stage::~Stage() {
    if (attached() && timer_) services().cancel(timer_);
}

void Stage::mounted() {
    frame_.reserve(512);
    start_round();
    services().focus(node());  // the keyboard is the players'
    // A game shows every frame it plays: sixty a second while it is on screen.
    pace_ = services().demand_frames({.min_hz = 60, .max_hz = 60, .preferred_hz = 60, .reason = "game"});
    start_ = services().now().count();
    schedule();
}

// The timer wakes at the next frame due; tick() plays every frame due by
// then (a late wake catches up, a long stall is not replayed).
void Stage::schedule() {
    const double next = start_ + static_cast<double>(played_ + 1) * frame_period;
    timer_ = services().after(seconds(std::max(0.0, next - services().now().count())), [this] {
        timer_ = 0;
        tick();
        schedule();
    });
}

void Stage::prop_changed(StageProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

gongfu::Pad Stage::keyboard(std::int32_t player) const {
    const Layout& l = layouts[static_cast<std::size_t>(player)];
    gongfu::Pad p;
    if (held_.contains(l.up)) p.directions |= gongfu::Pad::up;
    if (held_.contains(l.down)) p.directions |= gongfu::Pad::down;
    if (held_.contains(l.left)) p.directions |= gongfu::Pad::left;
    if (held_.contains(l.right)) p.directions |= gongfu::Pad::right;
    for (std::size_t b = 0; b < l.buttons.size(); ++b)
        if (held_.contains(l.buttons[b])) p.buttons |= bit(static_cast<Button>(b));
    return p;
}

gongfu::Pad Stage::pad_of(std::int32_t player) const {
    const auto& agent = agent_[static_cast<std::size_t>(player)];
    return agent ? *agent : keyboard(player);
}

void Stage::advance() {
    match_->tick(pad_of(0), pad_of(1));
    effects_.record(*match_);
    meter_.push_back({phase(*match_, 0), phase(*match_, 1)});
    if (meter_.size() > meter_frames) meter_.erase(meter_.begin());
    if (match_->state().over && !over_reported_) {
        over_reported_ = true;
        emit_round_over(match_->state().winner);
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Stage::tick() {
    const double now = services().now().count();
    if (!match_ || props.paused) {
        start_ = now - static_cast<double>(played_) * frame_period;  // a pause is not owed afterwards
        return;
    }
    constexpr std::int64_t catch_up = 4;
    const auto due = static_cast<std::int64_t>(std::floor((now - start_) / frame_period + 1e-6));
    if (due - played_ > catch_up) start_ = now - static_cast<double>(played_ + catch_up) * frame_period;
    for (std::int64_t n = 0; played_ < due && n < catch_up; ++n, ++played_) advance();
}

Size Stage::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    return {width.value_or(picture_w), height.value_or(picture_h)};
}

void Stage::paint(PaintContext& ctx) const {
    if (!match_) return;
    const Roster& r = roster();
    const p2::Scene scene;
    render::DisplayList& frame = frame_;
    frame.clear();
    const p2::Look& left = r.looks[0];
    const p2::Look& right = r.looks.size() > 1 ? r.looks[1] : r.looks[0];
    painter_.draw(*match_, left, right, effects_, scene, frame);

    const gongfu::State& st = match_->state();
    // The health bars and the timer, over the picture.
    for (std::int32_t i = 0; i < 2; ++i) {
        const float w = 380, h = 16, y = 24, x = i == 0 ? 40 : picture_w - 40 - w;
        const double share = std::clamp(st.f[static_cast<std::size_t>(i)].health / static_cast<double>(match_->fighter(i).def.health), 0.0, 1.0);
        frame.fill_round_rect({x, y, w, h}, CornerRadii::all(4), render::Paint::solid(tone(0x0b0d12, 0.8f)));
        const float filled = static_cast<float>(w * share);
        // Each bar drains from the middle toward its own side, as the genre's do.
        frame.fill_round_rect({i == 0 ? x : x + w - filled, y, filled, h}, CornerRadii::all(4), render::Paint::solid(tone(i == 0 ? 0xf2c14e : 0x4ec9f2)));
    }
    const int seconds_left = (st.timer + 59) / 60;
    for (int d = 0; d < 2; ++d) {
        // Two digits as seven-segment bars: no font needed, the same everywhere.
        const int digit = d == 0 ? seconds_left / 10 % 10 : seconds_left % 10;
        constexpr std::array<std::uint8_t, 10> segments{0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
                                                        0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011};
        const float ox = picture_w / 2 - 22 + d * 24, oy = 16, sw = 16, sh = 30, t = 3;
        const std::array<Rect, 7> bars{Rect{ox, oy, sw, t},          Rect{ox + sw - t, oy, t, sh / 2},     Rect{ox + sw - t, oy + sh / 2, t, sh / 2},
                                       Rect{ox, oy + sh - t, sw, t},  Rect{ox, oy + sh / 2, t, sh / 2},     Rect{ox, oy, t, sh / 2},
                                       Rect{ox, oy + sh / 2 - t / 2, sw, t}};
        for (int s = 0; s < 7; ++s)
            if (segments[static_cast<std::size_t>(digit)] & (1 << (6 - s))) frame.fill_rect(bars[static_cast<std::size_t>(s)], tone(0xf4f1ea));
    }
    // Training: the boxes of this frame (push grey, hurt blue, active hits
    // red) lined up with the picture's camera, and the frame meter.
    if (props.mode == "training") {
        const p2::Camera cam = p2::view_camera(*match_, left, right, effects_, scene);
        auto outline = [&](const gongfu::Rect& b, Color c) {
            const Point a = p2::on_picture(scene, cam, b.left.to_double(), b.top.to_double());
            const Point z = p2::on_picture(scene, cam, b.right.to_double(), b.bottom.to_double());
            const Rect r{a.x, a.y, z.x - a.x, z.y - a.y};
            frame.fill_rect(r, c.with_alpha(0.18f));
            frame.stroke_round_rect(r, {}, 1.5f, render::Paint::solid(c));
        };
        for (std::int32_t i = 0; i < 2; ++i) {
            const auto& f = st.f[static_cast<std::size_t>(i)];
            const auto& def = match_->fighter(i).def;
            const gongfu::MoveDef* move = f.mode == gongfu::Mode::move ? &def.moves[static_cast<std::size_t>(f.move)] : nullptr;
            const bool air = f.pos.y > gongfu::Fixed{}, low = f.mode == gongfu::Mode::crouch || (move && move->stance == gongfu::Stance::crouching);
            const gongfu::Box& hurt = air ? def.hurt_air : low ? def.hurt_crouching : def.hurt_standing;
            outline(gongfu::place(def.push, f.pos, f.facing), tone(0x9aa0a6));
            outline(gongfu::place(hurt, f.pos, f.facing), tone(0x4ea1f2));
            if (move)
                for (const auto& h : move->hits)
                    if (h.active.contains(f.move_frame))
                        for (const auto& b : h.boxes) outline(gongfu::place(b, f.pos, f.facing), tone(0xf24e5a));
        }
        // The meter: each player's last frames, oldest left — startup green,
        // active red, recovery blue, stun yellow, free grey.
        constexpr std::array<std::uint32_t, 5> phase_tone{0x3a3f52, 0x4ef27a, 0xf24e5a, 0x4ea1f2, 0xf2c14e};
        const float cell = 400.0f / meter_frames, left_edge = picture_w / 2 - 200;
        for (std::size_t k = 0; k < meter_.size(); ++k)
            for (std::size_t i = 0; i < 2; ++i)
                frame.fill_rect({left_edge + static_cast<float>(k) * cell, picture_h - 40 + static_cast<float>(i) * 14, cell - 1, 10},
                                tone(phase_tone[meter_[k][i]], 0.9f));
    }

    // Into the node's box: scaled to fit, centred, letterboxed.
    const float s = std::min(ctx.size.width / picture_w, ctx.size.height / picture_h);
    const float ox = (ctx.size.width - picture_w * s) / 2, oy = (ctx.size.height - picture_h * s) / 2;
    ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, tone(0x0b0d12));
    ctx.list.push_clip({0, 0, ctx.size.width, ctx.size.height});
    ctx.list.push_transform(Affine::translate(ox, oy) * Affine::scale(s, s));
    ctx.list.append(frame);
    ctx.list.pop_transform();
    ctx.list.pop_clip();
}

bool Stage::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k) return false;
    bool ours = false;
    for (const Layout& l : layouts) {
        ours = ours || k->key == l.up || k->key == l.down || k->key == l.left || k->key == l.right;
        for (Key b : l.buttons) ours = ours || k->key == b;
    }
    if (!ours) return false;
    if (k->kind == KeyKind::down) held_.insert(k->key);
    else held_.erase(k->key);
    ctx.handled = true;
    return true;
}

void Stage::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = "Fight";
    if (!match_) return;
    const gongfu::State& st = match_->state();
    Value v;
    v.set("frame", st.frame).set("seconds", (st.timer + 59) / 60).set("over", st.over != 0).set("winner", st.winner);
    if (!roster().problems.empty()) v.set("problems", Value(Value::List(roster().problems.begin(), roster().problems.end())));
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto& f = st.f[static_cast<std::size_t>(i)];
        v.set(i == 0 ? "p1" : "p2", Value{}
                                        .set("name", match_->fighter(i).def.name)
                                        .set("health", f.health)
                                        .set("doing", std::string(p2::clip_name(match_->fighter(i), f)))
                                        .set("x", f.pos.x.to_double())
                                        .set("combo", f.combo));
    }
    node.value = std::move(v);
}

ActResult Stage::restart(ActContext&) {
    start_round();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done();
}

ActResult Stage::pause(ActContext&) {
    props.paused = true;
    prop_changed(StageProp::paused);
    return ActResult::done();
}

ActResult Stage::resume(ActContext&) {
    props.paused = false;
    prop_changed(StageProp::paused);
    return ActResult::done();
}

ActResult Stage::step(ActContext&) {
    if (!props.paused) return ActResult::fail("`step` plays one frame of a paused match: pause first");
    if (!match_) return ActResult::fail("no match: the roster did not read");
    advance();
    return ActResult::done(Value(match_->state().frame));
}

ActResult Stage::press(ActContext&, const Value& pad, const Value& player) {
    const auto p = player.as_int(-1);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    const auto parsed = parse_pad(pad.as_string());
    if (!parsed) return ActResult::fail(std::format("`{}` is not a pad: a numpad direction and buttons, `6`, `2LK`, `5LP+LK`", pad.as_string()));
    agent_[static_cast<std::size_t>(p)] = *parsed;
    return ActResult::done();
}

ActResult Stage::release(ActContext&, const Value& player) {
    const auto p = player.as_int(-1);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    agent_[static_cast<std::size_t>(p)].reset();
    return ActResult::done();
}

}  // namespace omui::fighting
