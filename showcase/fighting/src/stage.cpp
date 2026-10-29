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

constexpr float picture_w = hud::width, picture_h = hud::height;
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


}  // namespace

void Stage::start_round() {
    const Roster& r = roster();
    if (r.fighters.empty()) return;
    left_ = fighter_index(props.player1);
    right_ = fighter_index(props.player2);
    match_ = std::make_unique<gongfu::Match>(r.system, r.fighters[left_], r.fighters[right_]);
    looks_ = {r.looks[2 * left_], r.looks[2 * right_ + 1]};
    for (p2::Look& look : looks_)
        for (p2::Picture& pic : look.pictures)
            if (const auto it = art_.find(pic.file); it != art_.end()) pic.ref = it->second;
    effects_ = {};
    readout_.clear();
    // The stage's music: the arena's, else player 1's home's (none in training's plain room).
    std::string_view theme = props.arena;
    if (theme.empty() && props.mode != "training") {
        const auto home = r.homes.find(props.player1);
        theme = home == r.homes.end() ? std::string_view() : std::string_view(home->second);
    }
    sounds_.restart(theme == "plain" ? std::string_view() : theme);
    over_reported_ = false;
}

Stage::~Stage() {
    if (!attached()) return;
    if (timer_) services().cancel(timer_);
    for (const auto& [file, ref] : art_) services().images().remove(ref);
}

void Stage::mounted() {
    frame_.reserve(512);
    for (const auto& [file, a] : roster().art)
        if (!art_.contains(file)) art_.emplace(file, services().images().add(a.width, a.height, a.pixels));
    start_round();
    if (hearing() && !heard_) heard_ = hearing()(sounds_);  // the program hears the fight (a device on its mixer)
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

void Stage::prop_changed(StageProp p) {
    if (!attached()) return;
    if (p == StageProp::player1 || p == StageProp::player2) start_round();  // another fighter: a new round
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

void Stage::changed() {
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Stage::advance() {
    const gongfu::Pad a = dummies_[0].step(pad_of(0)), b = dummies_[1].step(pad_of(1));
    match_->tick(a, b);
    effects_.record(*match_);
    readout_.record(*match_, a, b);
    sounds_.heard(*match_, looks_[0], looks_[1]);
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
    p2::Scene scene;
    scene.stage = stage_art(props.mode == "training" && props.arena.empty() ? std::string_view("plain") : std::string_view(props.arena), props.player1);
    render::DisplayList& frame = frame_;
    frame.clear();
    // Each side in its own palette: the first for the left, the second for the right.
    const p2::Look& left = looks_[0];
    const p2::Look& right = looks_[1];
    painter_.draw(*match_, left, right, effects_, scene, frame);

    hud::bars(frame, *match_);
    hud::timer(frame, (match_->state().timer + 59) / 60);
    // Training's overlays, but not over a super's flash or a cinematic: those are the picture.
    if (props.mode == "training" && match_->state().freeze == 0 && match_->state().lock.owner < 0) {
        hud::boxes(frame, *match_, scene, p2::view_camera(*match_, left, right, effects_, scene));
        hud::meter(frame, readout_);
        hud::inputs(frame, readout_, 0);
        hud::inputs(frame, readout_, 1);
        auto dummy = [&](std::size_t i) {
            return dummies_[i].recording() ? hud::Dummy::recording : dummies_[i].playing() ? hud::Dummy::playing : hud::Dummy::none;
        };
        hud::readings(frame, readout_, words_, ctx.services.fonts(), {dummy(0), dummy(1)}, match_->state().frame);
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
                                        .set("combo", f.combo)
                                        .set("posture", f.posture)
                                        .set("meter", f.meter));
    }
    // Training's readings, as a person sees them.
    Value training;
    if (auto a = readout_.startup(0)) training.set("startup", *a);
    if (auto a = readout_.advantage()) training.set("advantage", readout_.advantage_attacker() == 0 ? *a : -*a);  // player 1's side
    const auto& c = readout_.combo();
    if (c.attacker >= 0) training.set("combo", Value{}.set("by", c.attacker).set("hits", c.hits).set("damage", c.damage).set("going", c.going));
    for (std::size_t i = 0; i < 2; ++i)
        if (dummies_[i].recording() || dummies_[i].playing())
            training.set(i == 0 ? "dummy1" : "dummy2", dummies_[i].recording() ? "recording" : "playing");
    training.set("saved", saved_.has_value());
    // System 0.3's exchanges: the latest, and each player's counts.
    if (const auto& x = readout_.exchange(); x.frame >= 0) {
        const char* kind = x.kind == gongfu::Event::Kind::clash ? (x.by < 0 ? "perfect clash" : "clash")
                         : x.kind == gongfu::Event::Kind::deflect ? "deflect"
                                                                 : "guard break";
        training.set("exchange", Value{}.set("kind", kind).set("by", x.by).set("frame", x.frame));
    }
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto& t = readout_.tally(i);
        training.set(i == 0 ? "tally1" : "tally2", Value{}.set("clashes", t.clashes).set("won", t.clashes_won).set("perfect", t.perfect).set("deflects", t.deflects).set("breaks", t.breaks));
    }
    v.set("training", std::move(training));
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

ActResult Stage::press(ActContext&, const PressArgs& args) {
    const Value &pad = args.pad, &player = args.player;
    const auto p = player.as_int(-1);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    const auto parsed = parse_pad(pad.as_string());
    if (!parsed) return ActResult::fail(std::format("`{}` is not a pad: a numpad direction and buttons, `6`, `2LK`, `5LP+LK`", pad.as_string()));
    agent_[static_cast<std::size_t>(p)] = *parsed;
    return ActResult::done();
}

ActResult Stage::save(ActContext&) {
    if (!match_) return ActResult::fail("no match: the roster did not read");
    saved_ = Saved{match_->state(), effects_};
    changed();
    return ActResult::done(Value(match_->state().frame));
}

ActResult Stage::load(ActContext&) {
    if (!match_ || !saved_) return ActResult::fail("nothing saved: `save` first");
    match_->restore(saved_->state);
    effects_ = saved_->effects;
    readout_.clear();  // its history is of frames no longer played
    over_reported_ = match_->state().over != 0;
    changed();
    return ActResult::done(Value(match_->state().frame));
}

namespace {

std::expected<std::size_t, std::string> player_of(const Value& v) {
    const auto p = v.as_int(-1);
    if (p != 0 && p != 1) return std::unexpected(std::string("`player` is 0 or 1"));
    return static_cast<std::size_t>(p);
}

}  // namespace

ActResult Stage::record(ActContext&, const Value& player) {
    auto p = player_of(player);
    if (!p) return ActResult::fail(p.error());
    dummies_[*p].start_recording();
    changed();
    return ActResult::done();
}

ActResult Stage::replay(ActContext&, const Value& player) {
    auto p = player_of(player);
    if (!p) return ActResult::fail(p.error());
    if (dummies_[*p].recording()) dummies_[*p].stop();
    dummies_[*p].start_playback();
    if (!dummies_[*p].playing()) return ActResult::fail("nothing recorded for this player: `record` first");
    changed();
    return ActResult::done(Value(static_cast<std::int64_t>(dummies_[*p].length())));
}

ActResult Stage::stop(ActContext&, const Value& player) {
    auto p = player_of(player);
    if (!p) return ActResult::fail(p.error());
    dummies_[*p].stop();
    changed();
    return ActResult::done(Value(static_cast<std::int64_t>(dummies_[*p].length())));
}

ActResult Stage::fill(ActContext&) {
    if (props.mode != "training") return ActResult::fail("`fill` is training's: the stage's mode is not training");
    if (!match_) return ActResult::fail("no match: the roster did not read");
    gongfu::State st = match_->state();
    for (auto& f : st.f) {
        f.meter = match_->system().meter_max;
        f.posture = 0;
    }
    match_->restore(st);
    changed();
    return ActResult::done();
}

ActResult Stage::release(ActContext&, const Value& player) {
    const auto p = player.as_int(-1);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    agent_[static_cast<std::size_t>(p)].reset();
    return ActResult::done();
}

}  // namespace omui::fighting
