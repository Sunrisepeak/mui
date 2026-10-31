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

// What a gamepad does (controls/pad.controls.omui.json): its buttons to the
// fighting game's, and which of the d-pad and the left stick are directions.
struct Controls {
    std::vector<std::pair<GamepadButton, Button>> buttons;
    bool  dpad = true, stick = true;
    float threshold = 0.5f;  // how far the stick goes before it is a direction
};

const Controls& controls() {
    static const Controls c = [] {
        Controls out;
        const auto v = parse_json(files::pad_controls);
        if (!v) return out;
        constexpr std::array<std::pair<std::string_view, Button>, 6> names{
            {{"LP", Button::lp}, {"MP", Button::mp}, {"HP", Button::hp}, {"LK", Button::lk}, {"MK", Button::mk}, {"HK", Button::hk}}};
        if (const Value* b = v->get("buttons"); b && b->map())
            for (const auto& [pad, what] : *b->map()) {
                const auto button = parse_gamepad_button(pad);
                const auto it = std::ranges::find(names, what.as_string(), &std::pair<std::string_view, Button>::first);
                if (button && it != names.end()) out.buttons.emplace_back(*button, it->second);
            }
        if (const Value* d = v->get("directions")) {
            if (const Value* x = d->get("dpad")) out.dpad = x->as_bool(true);
            if (const Value* x = d->get("leftStick")) out.stick = x->as_bool(true);
            if (const Value* x = d->get("stick")) out.threshold = static_cast<float>(x->as_double(0.5));
        }
        return out;
    }();
    return c;
}

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

std::int32_t Stage::to_win() const noexcept { return std::max(1, props.rounds); }

void Stage::start_round(bool next) {
    const Roster& r = roster();
    if (r.fighters.empty()) return;
    if (next) {
        ++round_;
    } else {
        round_ = 1;
        wins_ = {};
    }
    decided_ = false;
    over_for_ = 0;
    left_ = fighter_index(props.player1);
    right_ = fighter_index(props.player2);
    match_ = std::make_unique<gongfu::Match>(r.system, r.fighters[left_], r.fighters[right_]);
    looks_ = {r.looks[2 * left_], r.looks[2 * right_ + 1]};
    if (art_)
        for (p2::Look& look : looks_) art_->bind(look.pictures);
    // The stage's art, its painted layers' pictures registered.
    stage_.reset();
    if (const p2::Stage* art = stage_art(props.mode == "training" && props.arena.empty() ? std::string_view("plain") : std::string_view(props.arena), props.player1)) {
        stage_ = *art;
        if (art_) art_->bind(stage_->pictures, "stages/");
    }
    effects_ = {};
    readout_.clear();
    make_bots();
    turned_ = {};
    if (!props.demo.empty()) start_demo();
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
}

void Stage::mounted() {
    frame_.reserve(512);
    art_ = registered(services().images());
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
    // Another fighter, another mode or another stage: a new round.
    if (p == StageProp::player1 || p == StageProp::player2 || p == StageProp::mode || p == StageProp::arena || p == StageProp::rounds) start_round();
    if (p == StageProp::cpu || p == StageProp::cpu_level) make_bots();
    if (p == StageProp::demo || p == StageProp::demo_guard) start_demo();
    if (p == StageProp::paused && !props.paused) {  // playing again: the keyboard is the players'; a finished match, a new one
        if (match_ && match_->state().over && (decided_ || props.mode == "training")) start_round();
        services().focus(node());
    }
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

// The player's gamepad: opposite directions held together go to gongfu as
// they are, and its SOCD rule decides, as for the keyboard.
gongfu::Pad Stage::gamepad(std::int32_t player) const {
    const Gamepad& g = gamepads_[static_cast<std::size_t>(player)];
    gongfu::Pad p;
    if (!g.device) return p;
    const Controls& c = controls();
    auto held = [&](GamepadButton b) { return g.held[static_cast<std::size_t>(b)]; };
    if (c.dpad) {
        if (held(GamepadButton::dpad_up)) p.directions |= gongfu::Pad::up;
        if (held(GamepadButton::dpad_down)) p.directions |= gongfu::Pad::down;
        if (held(GamepadButton::dpad_left)) p.directions |= gongfu::Pad::left;
        if (held(GamepadButton::dpad_right)) p.directions |= gongfu::Pad::right;
    }
    if (c.stick) {
        if (g.y <= -c.threshold) p.directions |= gongfu::Pad::up;  // y is down, as the screen's
        if (g.y >= c.threshold) p.directions |= gongfu::Pad::down;
        if (g.x <= -c.threshold) p.directions |= gongfu::Pad::left;
        if (g.x >= c.threshold) p.directions |= gongfu::Pad::right;
    }
    for (const auto& [pad, button] : c.buttons)
        if (held(pad)) p.buttons |= bit(button);
    return p;
}

// The computer's player, as cpu and cpu_level say: a bot of the fighter on
// that side, seeded by the round (each round plays the same every time).
void Stage::make_bots() {
    bots_ = {};
    if (!match_ || props.mode == "training" || (props.cpu != 0 && props.cpu != 1)) return;
    gongfu::ai::Style style;
    switch (std::clamp(props.cpu_level, 1, 3)) {
        case 1: style = {.reaction = 24, .aggression = 40, .guard = 45, .anti_air = 30, .punish = 25, .offense = 70, .jump_over = 4}; break;
        case 3: style = {.reaction = 10, .aggression = 60, .guard = 85, .anti_air = 80, .punish = 90, .offense = 100, .jump_over = 10}; break;
        default: break;  // gongfu.ai's: a person's
    }
    const auto side = static_cast<std::size_t>(props.cpu);
    const Roster& r = roster();
    bots_[side].emplace(r.fighters[side == 0 ? left_ : right_], props.cpu, 0x5eed + static_cast<std::uint64_t>(round_), style);
}

gongfu::Pad Stage::pad_of(std::int32_t player) const {
    const auto& agent = agent_[static_cast<std::size_t>(player)];
    if (agent) return *agent;
    // The keyboard and the gamepad together: what either holds.
    const gongfu::Pad k = keyboard(player), g = gamepad(player);
    return {static_cast<std::uint16_t>(k.directions | g.directions), static_cast<std::uint16_t>(k.buttons | g.buttons)};
}

bool Stage::on_gamepad(const GamepadEvent& e) {
    auto seat = std::ranges::find_if(gamepads_, [&](const Gamepad& g) { return g.device == e.device; });
    if (e.kind == GamepadKind::connected) {
        if (seat != gamepads_.end()) return true;
        // The first free seat: the first two connected play; one plugged in again takes the seat it left.
        const auto free = std::ranges::find_if(gamepads_, [](const Gamepad& g) { return !g.device; });
        if (free == gamepads_.end()) return false;
        *free = Gamepad{.device = e.device};
        return true;
    }
    if (seat == gamepads_.end() && e.kind != GamepadKind::disconnected) {
        // A pad connected while another screen had the keyboard (the select): it takes a free seat now.
        seat = std::ranges::find_if(gamepads_, [](const Gamepad& g) { return !g.device; });
        if (seat != gamepads_.end()) *seat = Gamepad{.device = e.device};
    }
    if (seat == gamepads_.end()) return false;
    switch (e.kind) {
        case GamepadKind::disconnected: *seat = Gamepad{}; break;
        case GamepadKind::down:
        case GamepadKind::up: seat->held[static_cast<std::size_t>(e.button)] = e.kind == GamepadKind::down; break;
        case GamepadKind::axis:
            if (e.axis == GamepadAxis::left_x) seat->x = e.value;
            if (e.axis == GamepadAxis::left_y) seat->y = e.value;
            break;
        case GamepadKind::connected: break;
    }
    return true;
}

void Stage::changed() {
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Stage::advance() {
    // A round's end in slow motion: its first 90 frames played at half speed (the fall seen), then on as it was.
    if (over_reported_ && ++over_for_ <= 90 && over_for_ % 2 == 1) {
        services().invalidate_paint(node());
        return;
    }
    // A demonstration plays its own pads, and again from its start after a rest.
    if (demo_ && demo_at_ >= demo_->frames + demo_rest) demo_from_start();
    const auto demo_pad = [&](std::int32_t player) {
        if (player == 1) return demo_->dummy;
        return demo_at_ < static_cast<std::int32_t>(demo_->attacker.size()) ? demo_->attacker[static_cast<std::size_t>(demo_at_)] : gongfu::pad(5);
    };
    // A player's pad: the demonstration's, the computer's, else the person's (or agent's) through the training dummy.
    const auto input = [&](std::int32_t p) {
        if (demo_) return demo_pad(p);
        if (auto& bot = bots_[static_cast<std::size_t>(p)]) return bot->next(*match_);
        return dummies_[static_cast<std::size_t>(p)].step(pad_of(p));
    };
    const gongfu::Pad a = input(0), b = input(1);
    if (demo_) ++demo_at_;
    match_->tick(a, b);
    effects_.record(*match_);
    readout_.record(*match_, a, b);
    if (!quiet_) sounds_.heard(*match_, looks_[0], looks_[1]);
    const gongfu::State& st = match_->state();
    const bool versus = props.mode != "training";
    // The announcer as a round begins: which round (the final one when either could take the match), then "fight".
    if (versus && st.frame == 1) {
        const bool last = wins_[0] == to_win() - 1 && wins_[1] == to_win() - 1 && to_win() > 1;
        const std::string_view call = last ? "call_final_round" : round_ == 1 ? "call_round_1" : round_ == 2 ? "call_round_2" : "call_round_3";
        if (to_win() > 1) sounds_.cue(call, 1);
    }
    if (versus && st.frame == (to_win() > 1 ? 80 : 20)) sounds_.cue("call_fight", st.frame);
    if (st.over && !over_reported_) {
        over_reported_ = true;
        if (versus) {
            // The round to its winner (a draw to both); the match to whoever has won enough, or after five rounds to the one ahead.
            if (st.winner == 0 || st.winner == 1) ++wins_[static_cast<std::size_t>(st.winner)];
            else ++wins_[0], ++wins_[1];
            decided_ = std::max(wins_[0], wins_[1]) >= to_win() || round_ >= 2 * to_win() + 1;
        }
        if (!versus || decided_) sounds_.finish();  // the K.O.: the theme's outro over the finish, then quiet for the menus' own
        if (versus) announce();
        emit_round_over(st.winner);
        if (versus && decided_) emit_match_over(wins_[0] > wins_[1] ? 0 : wins_[1] > wins_[0] ? 1 : 2);
    }
    // Three seconds after a round's K.O., the next (counted here: an ended match's frame stands still).
    if (versus && over_reported_ && !decided_ && over_for_ >= 180) start_round(true);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

// The round over, the announcer: "time" when the clock ran out; then, the
// K.O.'s moment past, whose round it was — "player 1 … winner", "flawless
// victory" when the winner was never touched — or a tie.
void Stage::announce() {
    const gongfu::State& st = match_->state();
    const bool time = std::ranges::any_of(match_->events(), [](const gongfu::Event& e) { return e.kind == gongfu::Event::Kind::time_over; });
    if (time) sounds_.cue("call_time", st.frame);
    if (!decided_) return;  // a round: the next one calls itself
    constexpr std::int32_t after = 75, name = 72;  // frames: the K.O.'s moment, then "player 1" said
    if (st.winner != 0 && st.winner != 1) {
        sounds_.cue("call_tie", st.frame, after);
        return;
    }
    const auto w = static_cast<std::size_t>(st.winner);
    const bool flawless = st.f[w].health == match_->fighter(st.winner).def.health;
    sounds_.cue(st.winner == 0 ? "call_player_1" : "call_player_2", st.frame, after);
    sounds_.cue(flawless ? "call_flawless" : "call_winner", st.frame, after + name);
}

void Stage::tick() {
    const double now = services().now().count();
    if (!match_ || props.paused || props.turn_frames > 0) {  // turns: the match moves when the players have said
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
    scene.stage = stage_ ? &*stage_ : nullptr;
    render::DisplayList& frame = frame_;
    frame.clear();
    // Each side in its own palette: the first for the left, the second for the right.
    const p2::Look& left = looks_[0];
    const p2::Look& right = looks_[1];
    painter_.draw(*match_, left, right, effects_, scene, frame);

    const auto art = [&](std::string_view file) { return art_ ? art_->picture(file) : render::ImageRef{}; };
    hud::versus(frame, *match_, effects_, {art("hud/frames.webp"), art("hud/type.webp"), art("hud/names.webp"), art("hud/fill.webp")}, {&left, &right},
                {r.names[left_], r.names[right_]}, props.mode == "training" ? std::array<std::int32_t, 2>{} : wins_, to_win());
    // Training's overlays, but not over a super's flash or a cinematic: those are the picture.
    if (props.mode == "training" && props.overlays && match_->state().freeze == 0 && match_->state().lock.owner < 0) {
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
    if (const auto* g = std::get_if<GamepadEvent>(&e)) {
        if (!on_gamepad(*g)) return false;
        ctx.handled = true;
        return true;
    }
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
    if (bots_[0] || bots_[1]) v.set("cpu", bots_[0] ? 0 : 1).set("cpu_level", std::clamp(props.cpu_level, 1, 3));
    if (props.turn_frames > 0) {
        Value::List waiting;
        for (std::int64_t i = 0; i < 2; ++i)
            if (!turned_[static_cast<std::size_t>(i)] && !bots_[static_cast<std::size_t>(i)]) waiting.emplace_back(i);
        v.set("turns", Value{}.set("played", turns_).set("frames", props.turn_frames).set("waiting", Value(std::move(waiting))));
    }
    if (props.mode != "training")
        v.set("round", round_).set("wins", Value(Value::List{Value(wins_[0]), Value(wins_[1])})).set("to_win", to_win()).set("decided", decided_);
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
    if (demo_) training.set("demo", Value{}.set("move", props.demo).set("frame", demo_at_).set("frames", demo_->frames).set("guard", props.demo_guard));
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

void Stage::start_demo() {
    demo_.reset();
    demo_at_ = 0;
    if (!match_ || props.mode != "training" || props.demo.empty()) return;
    const Roster& r = roster();
    const std::int32_t move = r.fighters[left_].find(props.demo);
    if (move < 0) return;
    demo_ = gongfu::training::demo(match_->system(), r.fighters[left_], r.fighters[right_], move, props.demo_guard);
    demo_from_start();
}

void Stage::demo_from_start() {
    match_->restore(demo_->start.state());
    demo_at_ = 0;
    effects_ = {};
    readout_.clear();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

ActResult Stage::demo_frame(ActContext&, const Value& frame) {
    if (!demo_) return ActResult::fail("no demonstration: set `demo` to a move of player 1's fighter, in training");
    const std::int32_t to = std::clamp(static_cast<std::int32_t>(frame.as_int(0)), 0, demo_->frames);
    const std::int32_t was = demo_at_;
    props.paused = true;
    demo_from_start();
    quiet_ = true;
    for (std::int32_t i = 0; i < to; ++i) advance();
    quiet_ = false;
    return ActResult::done(Value(demo_at_)).undo_by("demo_frame", Value{}.set("frame", was));
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

ActResult Stage::turn(ActContext&, const TurnArgs& args) {
    if (props.turn_frames <= 0) return ActResult::fail("not a match by turns: set `turn_frames` (frames a turn) first");
    if (!match_) return ActResult::fail("no match: the roster did not read");
    const auto p = args.player.as_int(-1);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    if (bots_[static_cast<std::size_t>(p)]) return ActResult::fail(std::format("player {} is the computer's", p));
    // A pad, or pads a frame each, the last held for the rest of the turn ("2,3,6LP": a quarter circle and a punch).
    std::vector<gongfu::Pad> pads;
    for (auto part : std::views::split(args.pad.as_string(), ',')) {
        const std::string_view one(part);
        const auto parsed = parse_pad(one);
        if (!parsed) return ActResult::fail(std::format("`{}` is not a pad: a numpad direction and buttons, `6`, `2LK`, `5LP+LK`; several a frame each: `2,3,6LP`", one));
        pads.push_back(*parsed);
    }
    if (pads.empty() || static_cast<std::int32_t>(pads.size()) > props.turn_frames)
        return ActResult::fail(std::format("a turn is {} frames: give 1 to {} pads", props.turn_frames, props.turn_frames));
    turn_pads_[static_cast<std::size_t>(p)] = std::move(pads);
    agent_[static_cast<std::size_t>(p)] = turn_pads_[static_cast<std::size_t>(p)].front();
    turned_[static_cast<std::size_t>(p)] = true;
    Value said;
    said.set("turn", turns_).set("player", p);
    for (std::size_t i = 0; i < 2; ++i)
        if (!turned_[i] && !bots_[i]) {
            changed();
            return ActResult::done(said.set("waiting", static_cast<std::int64_t>(i)));  // the other player has not said yet
        }
    // Every player has said: the turn plays, and what happened in it is said.
    const std::array<std::int32_t, 2> before{match_->state().f[0].health, match_->state().f[1].health};
    for (std::int32_t i = 0; i < props.turn_frames; ++i, ++played_) {
        for (std::size_t s = 0; s < 2; ++s)
            if (const auto& pads = turn_pads_[s]; !pads.empty()) agent_[s] = pads[std::min<std::size_t>(static_cast<std::size_t>(i), pads.size() - 1)];
        advance();
    }
    turned_ = {};
    turn_pads_ = {};
    ++turns_;
    const gongfu::State& st = match_->state();
    said.set("played", true).set("frame", st.frame);
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto& f = st.f[static_cast<std::size_t>(i)];
        said.set(i == 0 ? "p1" : "p2", Value{}.set("health", f.health).set("lost", before[static_cast<std::size_t>(i)] - f.health)
                                           .set("doing", std::string(p2::clip_name(match_->fighter(i), f))).set("x", f.pos.x.to_double()));
    }
    if (st.over) said.set("over", true).set("winner", st.winner);
    changed();
    return ActResult::done(std::move(said));
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
