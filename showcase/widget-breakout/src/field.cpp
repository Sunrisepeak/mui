module openmui.showcase.widget_breakout;

import std;
import openmui;
import mgame.input;
import mgame.ui;
import openmui.showcase.widget_breakout.sim;
import openmui.ui.canvas;

namespace omui::breakout {

namespace {

constexpr std::int32_t keep = 30 * steps_per_second;   // what the timeline keeps
constexpr std::int32_t scan_steps = 180;               // the X-ray: 1.5 s
constexpr std::int32_t scan_cooldown = 720;            // then 6 s to recharge
constexpr std::int32_t celebrate_steps = 150;          // a cleared room's moment, before the choice
constexpr std::uint32_t rewind_hint = 1u << 8;

mgame::Bindings bindings() {
    mgame::Bindings b;
    for (const Key k : {Key::left, Key::a}) b.key(mgame::ui::code(k), mgame::Axis{0, -1});
    for (const Key k : {Key::right, Key::d}) b.key(mgame::ui::code(k), mgame::Axis{0, 1});
    b.key(mgame::ui::code(Key::space), mgame::Button{launch_button});
    b.key(mgame::ui::code(Key::z), mgame::Button{look_button});  // a tall room: all of it while held
    b.mouse(1, mgame::Button{launch_button});
    // Any gamepad: the left stick and the d-pad move the paddle, south (A, Cross) launches.
    b.pad_axis(mgame::ui::pad(GamepadAxis::left_x), mgame::Axis{0, 1});
    b.pad_button(mgame::ui::pad(GamepadButton::dpad_left), mgame::Axis{0, -1});
    b.pad_button(mgame::ui::pad(GamepadButton::dpad_right), mgame::Axis{0, 1});
    b.pad_button(mgame::ui::pad(GamepadButton::south), mgame::Button{launch_button});
    return b;
}

// What the semantic tree calls a brick's component (the inspector's changes).
const char* role_name(Kind k) {
    switch (k) {
        case Kind::toggle: return "switch";
        case Kind::slider: return "slider";
        case Kind::card: return "region";
        case Kind::gate: return "progress";
        case Kind::wall:
        case Kind::shutter: return "plate";
        case Kind::button:
        case Kind::hidden:
        case Kind::kicker:
        case Kind::core: break;
    }
    return "button";
}

std::int32_t mask_of(std::span<const Gift> kit) {
    std::int32_t m = 0;
    for (const Gift g : kit) m |= gift_bit(g);
    return m;
}

constexpr std::array<std::string_view, 4> orders{"hold", "left", "right", "feed"};

}  // namespace

Field::Field() : sampler_(bindings()) {}

Field::~Field() {
    pacer_.stop();
    device_.reset();  // the device stops pulling before its mixer goes
}

void Field::mounted() {
    services().focus(node());
    if (hearing()) device_ = hearing()(*sounds_);
    pacer_.start(services(), steps_per_second, [this](std::int32_t n) { run(n); });
}

void Field::prop_changed(FieldProp p) {
    if (!attached()) return;
    if (p == FieldProp::room) {
        services().recompose(node());  // its bricks: read into a new simulation on the next step
        restart_room();
        services().focus(node());
    }
    refresh();
}

std::vector<Element> Field::compose() {
    // A brick's control changed by anything but the simulation (an agent's toggle): once it has taken the
    // change (the event comes first), what the simulation holds, again.
    return room_bricks(room(props.room), [this](std::size_t i) {
        (void)services().after(seconds(0), [this, i] {
            if (!timeline_) return;
            const Brick* found = nullptr;
            timeline_->state().world.each<Brick>([&](mgame::Entity, const Brick& b) {
                if (b.id == i) found = &b;
            });
            if (found) show_brick(i, found);
        });
    });
}

void Field::refresh() {
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Field::new_run() {
    run_ = {};
    run_at_start_ = run_;
    hints_ = 0;
    if (props.room != 1) {
        props.room = 1;
        prop_changed(FieldProp::room);
    } else {
        restart_room();
        refresh();
    }
}

void Field::restart_room() {
    timeline_.reset();
    log_.clear();
    guard_x_.reset();
    agent_log_.clear();
    pending_rules_.reset();
    pending_pay_ = 0;
    agent_aim_.reset();
    rewinding_ = false;
    landing_ = 0;
    scan_until_ = scan_ready_ = 0;
    discovered_.fill(false);
    says_.clear();
    intent_.reset();
    choosing_ = false;
    over_steps_ = 0;
    order_ = "feed";
    hint_.clear();
    hint_until_ = 0;
    fx_.clear();
    if (begin()) show_bricks();
    show_style();
}

void Field::show_style() {
    const State* s = state();
    // Cleared, the panel puts on the next room's skin: the celebration.
    const bool next_skin = s && s->over == 1 && over_steps_ > 40 && props.room < room_count;
    std::string want = room(next_skin ? props.room + 1 : props.room).style;
    if (want == style_shown_) return;
    note(std::format("style → {}", want));
    style_shown_ = want;
    emit_style(std::move(want));
}

void Field::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;  // its bricks stand where they are placed (absolutely)
}

std::vector<NodeId> Field::bricks() const {
    if (room(props.room).canvas) return {};  // a canvas's scene holds them, not nodes
    const auto panel = services().children(node());
    return panel.empty() ? std::vector<NodeId>{} : services().children(panel.front());
}

bool Field::begin() {
    const RoomDef& def = room(props.room);
    // Composed for this room yet (a new room's panel comes with the next frame)?
    const auto panel = services().children(node());
    if (panel.empty() || !services().path(panel.front()).ends_with(def.canvas ? std::string("/cloud") : std::format("/room.{}", def.number))) return false;
    if (services().bounds(panel.front()).width <= 0) return false;  // and laid out (a canvas places its live nodes by its size)
    const auto nodes = bricks();
    const Rect me = services().bounds(node());
    if (me.width <= 0 || me.height <= 0 || (!def.canvas && nodes.size() != def.bricks.size())) return false;
    const double sx = field_w / me.width, sy = field_h / me.height;
    Level l;
    l.height = def.height;
    l.chamber = def.chamber;
    l.speed = def.speed;
    l.lives = run_.lives;
    l.seed = def.seed;
    l.gate_from = def.gate_from;
    l.gate_period = def.gate_period;
    l.gate_open = def.gate_open;
    l.second_paddle = def.second_paddle;
    l.score = run_.score;
    l.sparks = run_.sparks;
    l.aim_phase = def.aim_phase;
    l.opening = fresh_;
    fresh_ = Gift::none;
    l.rules = rules_;
    l.rules.gift_mask = mask_of(run_.kit);
    std::size_t slots = 0;
    for (std::size_t i = 0; i < def.bricks.size(); ++i) {
        const BrickDef& b = def.bricks[i];
        mgame::Box box{b.x, b.y, b.width, b.height};  // a canvas room: where the room says, in the field's units
        if (!def.canvas) {
            const Rect r = services().bounds(nodes[i]);
            if (r.width <= 0 || r.height <= 0) return false;  // not laid out yet
            box = {(r.x - me.x) * sx, (r.y - me.y) * sy, r.width * sx, r.height * sy};
        }
        const Gift gift = b.slot && !run_.kit.empty() ? run_.kit[slots++ % run_.kit.size()] : Gift::none;
        l.bricks.push_back({box, b.kind, b.durability, static_cast<std::uint32_t>(i), 0, gift, b.wire, b.on});
    }
    if (def.canvas) fill_scene(def);
    level_ = l;
    timeline_.emplace(start(l), keep, 60);
    sounds_->restart();
    run_at_start_ = run_;
    emit_room_began(props.room, services().path(node()));  // whoever plays along (room 5's copilot) knows where
    return true;
}

Input Field::input() {
    Input in;
    in.player = sampler_.sample();
    if (timeline_ && in.player.pointer_x >= 0 && timeline_->state().zoom != 1) {
        // The pointer across the screen, to across the field: zoomed out, the field is the screen's middle.
        const double z = timeline_->state().zoom, across = 0.5 + (in.player.pointer_x / 32767.0 - 0.5) / z;
        in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(across, 0.0, 1.0) * 32767));
    }
    if (agent_x_) in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(*agent_x_, 0.0, 1.0) * 32767));
    if (guard_x_) in.second.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(*guard_x_, 0.0, 1.0) * 32767));
    if (pending_rules_) {  // at this step's start, all at once
        in.rules = *pending_rules_;
        in.new_rules = 1;
        pending_rules_.reset();
    }
    if (pending_pay_) {  // a rewind's cost: an input, so a replay pays it again
        in.pay = pending_pay_;
        pending_pay_ = 0;
    }
    if (agent_aim_) {
        in.aim_set = 1;
        in.aim = *agent_aim_;
        agent_aim_.reset();
    }
    if (agent_launch_) {
        in.player.held |= 1u << launch_button;
        in.player.pressed |= 1u << launch_button;
        agent_launch_ = false;
    }
    return in;
}

void Field::play(const Input& in) {
    timeline_->step(in);
    const State& s = timeline_->state();
    const RoomDef& def = room(props.room);
    sounds_->heard(s, def);
    fx_.react(s, def);
    // The bricks the step touched, as the simulation has them now (all of them, if its events overflowed).
    std::array<const Brick*, 128> by_id{};
    s.world.each<Brick>([&](mgame::Entity, const Brick& b) {
        if (b.id < by_id.size()) by_id[b.id] = &b;
    });
    const bool all = s.events >= s.event.size();
    for (std::uint32_t k = 0; k < s.events; ++k) {
        const Event& e = s.event[k];
        if (e.what == Happened::cleared || e.what == Happened::over) {
            emit_room_over(e.what == Happened::cleared, static_cast<std::int32_t>(s.score));
            continue;
        }
        if (e.id >= def.bricks.size() || e.id >= by_id.size()) continue;
        const Brick* b = by_id[e.id];
        switch (e.what) {
            case Happened::broke:
                note(std::format("{} {} · removed", brick_id(e.id), role_name(def.bricks[e.id].kind)));
                emit_brick_broke(brick_id(e.id));
                if (def.bricks[e.id].kind == Kind::hidden) discovered_[e.id] = true;
                break;
            case Happened::flipped: note(std::format("{} switch · checked → {}", brick_id(e.id), e.n != 0)); break;
            case Happened::charged:
                note(std::format("{} slider · value → {:.2f}", brick_id(e.id), static_cast<double>(e.n) / def.bricks[e.id].durability));
                break;
            case Happened::wired:
                if (def.bricks[e.id].kind == Kind::shutter) note(std::format("{} plate · {}", brick_id(e.id), e.n ? "hidden (open)" : "shown (closed)"));
                break;
            case Happened::hit:
                if (def.bricks[e.id].kind == Kind::hidden) discovered_[e.id] = true;
                break;
            case Happened::beam:
            case Happened::core_hit:
            case Happened::armed:
            case Happened::kicked: break;
            default: continue;
        }
        if (!all) show_brick(e.id, b);
    }
    if (all) show_bricks();
    if (scanning())  // what the X-ray shows stays found
        for (std::size_t i = 0; i < def.bricks.size() && i < discovered_.size(); ++i)
            if (def.bricks[i].kind == Kind::hidden) discovered_[i] = true;
    show_gates();
    if (def.canvas) show_camera();
}

void Field::show_brick(std::size_t i, const Brick* b) {
    const RoomDef& def = room(props.room);
    if (i >= def.bricks.size()) return;
    const Kind kind = def.bricks[i].kind;
    if (def.canvas) {  // the scene's node, and its live component if near enough to be one
        auto& scene = world::scene(cloud_scene);
        const auto n = scene.find(brick_id(i));
        if (!b) {
            if (n) scene.remove(*n);
            return;
        }
        if (!n) {
            const BrickDef& d = def.bricks[i];
            const world::NodeRef added = scene.add({.kind = kind == Kind::toggle ? "switch" : "note", .x = d.x, .y = d.y, .width = d.width, .height = d.height,
                                                    .key = brick_id(i), .fill = Color::hex(d.fill), .radius = 6});
            scene.set(added, "title", d.label);
            scene.set(added, "on", static_cast<double>(b->on));
        } else if (kind == Kind::toggle && scene.number(*n, "on") != b->on) {
            scene.set(*n, "on", static_cast<double>(b->on));
            scene.touch(*n);  // its live toggle composed again, reading it
        }
        return;
    }
    const auto nodes = bricks();
    if (i >= nodes.size()) return;
    const NodeId n = nodes[i];
    if (!b) {
        services().set(n, "visible", Value(false));  // gone from the screen; hidden in the semantic tree
        return;
    }
    switch (kind) {
        case Kind::toggle: services().set(n, "checked", Value(b->on != 0)); break;
        case Kind::slider: services().set(n, "value", Value(static_cast<double>(b->charge) / b->durability)); break;
        case Kind::shutter: services().set(n, "visible", Value(b->on == 0)); break;  // closed: there; open: gone
        case Kind::gate: break;                                                      // its moments: show_gates
        default: services().set(n, "visible", Value(true)); break;
    }
}

void Field::show_bricks() {
    if (!timeline_) return;
    const State& s = timeline_->state();
    const RoomDef& def = room(props.room);
    std::vector<const Brick*> by_id(def.bricks.size(), nullptr);
    s.world.each<Brick>([&](mgame::Entity, const Brick& b) {
        if (b.id < by_id.size()) by_id[b.id] = &b;
    });
    for (std::size_t i = 0; i < def.bricks.size(); ++i) show_brick(i, by_id[i]);
    show_gates();
    show_camera();
}

void Field::show_gates() {
    if (!timeline_) return;
    const State& s = timeline_->state();
    const RoomDef& def = room(props.room);
    if (def.gate_period <= 0) return;
    const auto nodes = bricks();
    for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
        if (def.bricks[i].kind != Kind::gate) continue;
        const bool open = gates_open(s), shown = services().get(nodes[i], "visible").as_bool(true);
        if (open == shown) {
            services().set(nodes[i], "visible", Value(!open));
            note(std::format("{} progress · {}", brick_id(i), open ? "hidden (open)" : "shown (closed)"));
            if (open) sounds_->cue("gate");
        }
        if (open) continue;
        // How near its next opening: full when it opens.
        const std::int64_t closed = s.gate_period - s.gate_open;
        const double v = s.frame < s.gate_from ? static_cast<double>(s.frame) / static_cast<double>(s.gate_from)
                                               : static_cast<double>((s.frame - s.gate_from) % s.gate_period - s.gate_open) / static_cast<double>(closed);
        services().set(nodes[i], "value", Value(std::clamp(v, 0.0, 1.0)));
    }
}

void Field::show_camera() {
    if (!timeline_ || !room(props.room).canvas) return;
    const auto kids = services().children(node());
    if (kids.empty()) return;
    // The camera: the field's bottom on the screen's, at the simulation's zoom (in screen pixels a unit).
    const State& s = timeline_->state();
    const Rect me = services().bounds(node());
    services().set(kids[0], "zoom", Value(s.zoom * me.width / field_w));
    services().set(kids[0], "x", Value(field_w / 2));
    services().set(kids[0], "y", Value(s.height - field_h / s.zoom / 2));
}

void Field::note(std::string change) {
    log_.push_back(std::move(change));
    while (log_.size() > log_size) log_.pop_front();
}

bool Field::dark() const {
    const State* s = state();
    const RoomDef& def = room(props.room);
    if (!s || !def.dark) return false;
    bool lights_off = false;  // its switch on: dark mode
    s->world.each<Brick>([&](mgame::Entity, const Brick& b) { lights_off = lights_off || (b.kind == Kind::toggle && b.on != 0); });
    return lights_off;
}

bool Field::scanning() const { return timeline_ && timeline_->frame() < scan_until_; }

std::optional<std::int64_t> Field::reach() const {
    if (!timeline_) return std::nullopt;
    const State& s = timeline_->state();
    if (s.over || s.sparks - pending_pay_ < rewind_cost) return std::nullopt;
    const std::int64_t back = std::max({timeline_->earliest(), landing_, timeline_->frame() - rewind_reach});
    if (back >= timeline_->frame()) return std::nullopt;
    return back;
}

bool Field::seek_back(std::int64_t to) {
    if (!timeline_->seek(to)) return false;
    show_bricks();
    fx_.clear();
    scan_until_ = std::min(scan_until_, to);
    scan_ready_ = std::min(scan_ready_, to + scan_cooldown);
    return true;
}

void Field::end_rewind() {
    rewinding_ = false;
    if (!timeline_ || timeline_->frame() >= rewind_from_) return;
    pending_pay_ += rewind_cost;
    landing_ = timeline_->frame();
    fx_.ghost_from(landing_, rewind_from_);
    timeline_->branch();
    note(std::format("rewound to frame {} · −{} sparks", landing_, rewind_cost));
}

void Field::over_step() {
    const State& s = timeline_->state();
    fx_.age();
    ++over_steps_;
    show_style();
    if (over_steps_ != celebrate_steps) return;
    // The moment passed: what the room brings to the run, and (cleared, rooms to go) the choice.
    run_.score = s.score;
    run_.lives = s.lives;
    run_.sparks = std::max(0, s.sparks);
    run_.best = std::max(run_.best, s.best);
    if (s.over == 1 && props.room < room_count) {
        choosing_ = true;
        emit_choosing(props.room, true);
    }
}

void Field::run(std::int32_t steps) {
    if (!timeline_) {
        if (!begin()) return;  // waiting for layout to place the bricks
        show_bricks();
        show_style();
        refresh();
    }
    if (props.paused || props.inspector) return;  // looking inside: the game waits
    const State& s = timeline_->state();
    if (s.over) {
        for (std::int32_t k = 0; k < steps; ++k) over_step();
        refresh();
        return;
    }
    if (rewinding_) {
        // Back through time at twice its pace, as far as it reaches.
        const std::int64_t floor = std::max({timeline_->earliest(), landing_, rewind_from_ - rewind_reach});
        const std::int64_t to = std::max(floor, timeline_->frame() - 2 * static_cast<std::int64_t>(steps));
        if (to != timeline_->frame()) (void)seek_back(to);
    } else {
        for (std::int32_t k = 0; k < steps && !timeline_->state().over; ++k) play(input());
    }
    // A hint, once, when it is needed.
    const RoomDef& def = room(props.room);
    const std::uint32_t mine = 1u << props.room;
    if (!(hints_ & mine) && !def.hint.empty()) {
        hints_ |= mine;
        hint_ = def.hint;
        hint_until_ = s.frame + 6 * steps_per_second;
    }
    if (props.room == 1 && !hint_.empty()) {  // the first: until the first launch
        bool waiting = false;
        s.world.each<Ball>([&](mgame::Entity, const Ball& b) { waiting = waiting || b.on == 1; });
        if (waiting && s.frame < 30 * steps_per_second) hint_until_ = std::max(hint_until_, s.frame + 1);
    }
    if (!(hints_ & rewind_hint) && s.sparks >= rewind_cost) {
        hints_ |= rewind_hint;
        hint_ = "Hold R  take back 2 seconds · 100 ◆";
        hint_until_ = s.frame + 5 * steps_per_second;
    }
    pacer_.clock().scale = s.slow > 0 ? s.rules.slow_percent / 100.0 : 1.0;  // slow motion: the game's clock slower
    refresh();
}

bool Field::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    auto act = [&](auto&& f) {
        const Value nothing;
        ActContext here{services(), node(), nothing};
        (void)f(here);
    };
    auto handled = [&] {
        ctx.handled = true;
        return true;
    };
    if (k && k->key == Key::tab) {
        if (k->kind == KeyKind::down && !k->repeat) {
            props.inspector = !props.inspector;
            prop_changed(FieldProp::inspector);
        }
        return handled();
    }
    // R held: rewinding, while it reaches (it costs sparks).
    if (k && k->key == Key::r) {
        if (!k->repeat && k->kind == KeyKind::down) {
            if (const auto back = reach(); back && !rewinding_) {
                rewinding_ = true;
                rewind_from_ = timeline_->frame();
                sounds_->cue("rewind");
            } else if (!rewinding_) {
                sounds_->cue("denied");
            }
        }
        if (!k->repeat && k->kind == KeyKind::up && rewinding_) end_rewind();
        return handled();
    }
    if (k && k->key == Key::x) {
        if (k->kind == KeyKind::down && !k->repeat) act([&](ActContext& c) { return scan(c); });
        return handled();
    }
    // 1–4: room 5's orders; between rooms, 1 and 2 take what is offered.
    if (k && k->key >= Key::digit1 && k->key <= Key::digit4) {
        if (k->kind == KeyKind::down && !k->repeat) {
            const auto i = static_cast<std::size_t>(static_cast<int>(k->key) - static_cast<int>(Key::digit1));
            if (const auto o = offered(); o && i < 2 && (*o)[i] != Gift::none) {
                const Value g(std::string(contraption((*o)[i])->script));
                ActContext here{services(), node(), g};
                (void)choose(here, g);
            } else if (room(props.room).second_paddle) {
                const Value to{std::string(orders[i])};
                ActContext here{services(), node(), to};
                (void)order(here, to);
            }
        }
        return handled();
    }
    // Enter: on from a room over.
    if (k && k->key == Key::enter) {
        if (k->kind == KeyKind::down && !k->repeat) act([&](ActContext& c) { return next(c); });
        return handled();
    }
    if (mgame::ui::feed(sampler_, e, services().bounds(node()))) return handled();
    return false;
}

void Field::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    const RoomDef& def = room(props.room);
    node.name = std::format("{} {}", def.name, def.title);
    if (!timeline_) return;
    const State& s = timeline_->state();
    Value v;
    v.set("room", props.room).set("hash", std::format("{:016x}", checksum(s)));
    v.set("frame", s.frame).set("score", s.score).set("lives", s.lives).set("sparks", s.sparks).set("chain", s.combo).set("best", s.best);
    v.set("bricks", s.bricks).set("cores", s.cores).set("over", static_cast<std::int32_t>(s.over));
    v.set("paddle", s.paddles[0].x / field_w).set("zoom", s.zoom).set("aim", s.aim);
    Value::List balls;
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) {
        balls.push_back(Value{}.set("x", b.at.x / field_w).set("y", b.at.y / field_h).set("vx", b.velocity.x).set("vy", b.velocity.y).set("on_paddle", b.on != 0));
    });
    v.set("balls", Value(std::move(balls)));
    v.set("inspector", props.inspector).set("copilot", props.copilot).set("scanning", scanning()).set("dark", dark());
    if (const auto r = reach()) v.set("rewind_to", *r);
    Value::List kit;
    for (const Gift g : run_.kit) kit.emplace_back(std::string(contraption(g)->script));
    v.set("kit", Value(std::move(kit)));
    v.set("choosing", choosing_);
    if (const auto o = offered()) {
        Value::List offers;
        for (const Gift g : *o)
            if (g != Gift::none) offers.emplace_back(std::string(contraption(g)->script));
        v.set("offers", Value(std::move(offers)));
    }
    v.set("rules", Value{}
                       .set("wide_steps", s.rules.wide_steps)
                       .set("wide_half", s.rules.wide_half)
                       .set("multi_balls", s.rules.multi_balls)
                       .set("slow_steps", s.rules.slow_steps)
                       .set("slow_percent", s.rules.slow_percent)
                       .set("pierce_steps", s.rules.pierce_steps)
                       .set("drop_one_in", s.rules.drop_one_in)
                       .set("magnet_catches", s.rules.magnet_catches));
    if (s.paddles[1].active) {
        v.set("top", s.paddles[1].x / field_w).set("order", order_).set("duet", s.duet).set("finisher", s.finisher != 0);
        v.set("agent", Value(Value::List(agent_log_.begin(), agent_log_.end())));
    }
    v.set("changes", Value(Value::List(log_.begin(), log_.end())));
    node.value = std::move(v);
}

std::optional<std::array<Gift, 2>> Field::offered() const {
    if (!choosing_) return std::nullopt;
    return offers(props.room, run_.kit);
}

ActResult Field::launch(ActContext&) {
    agent_launch_ = true;
    return ActResult::done();
}

ActResult Field::aim(ActContext&, const Value& across) {
    if (!across.is_number()) return ActResult::fail("`across` is a number: across per up, −1.2 … 1.2");
    agent_aim_ = std::clamp(across.as_double(), -aim_most, aim_most);
    return ActResult::done();
}

ActResult Field::move_to(ActContext&, const Value& x) {
    agent_x_ = std::clamp(x.as_double(0.5), 0.0, 1.0);
    return ActResult::done();
}

ActResult Field::release(ActContext&) {
    agent_x_.reset();
    return ActResult::done();
}

ActResult Field::pause(ActContext&) {
    props.paused = true;
    prop_changed(FieldProp::paused);
    return ActResult::done();
}

ActResult Field::resume(ActContext&) {
    props.paused = false;
    prop_changed(FieldProp::paused);
    return ActResult::done();
}

ActResult Field::step(ActContext&) {
    if (!props.paused) return ActResult::fail("`step` plays one frame of a paused field: pause first");
    if (!timeline_ && !begin()) return ActResult::fail("the bricks are not laid out yet");
    if (timeline_->state().over) over_step();
    else play(input());
    refresh();
    return ActResult::done(Value(timeline_->frame()));
}

ActResult Field::restart(ActContext&) {
    run_ = run_at_start_;  // what the run brought into it
    restart_room();
    refresh();
    return ActResult::done();
}

ActResult Field::again(ActContext&) {
    new_run();
    return ActResult::done();
}

ActResult Field::rewind(ActContext&, const Value& frames) {
    if (!timeline_) return ActResult::fail("nothing played yet");
    const std::int64_t n = std::max<std::int64_t>(0, frames.as_int(0));
    if (n > rewind_reach) return ActResult::fail(std::format("at most {} frames (2 s) a rewind", rewind_reach));
    const auto back = reach();
    if (!back) return ActResult::fail(std::format("a rewind needs {} sparks ({} now), and somewhere to go back to", rewind_cost, timeline_->state().sparks - pending_pay_));
    const std::int64_t to = timeline_->frame() - n;
    if (to < *back) return ActResult::fail(std::format("not that far back: frame {} at the earliest", *back));
    rewind_from_ = timeline_->frame();
    if (n > 0 && !seek_back(to)) return ActResult::fail("not kept");
    end_rewind();
    refresh();
    return ActResult::done(Value(timeline_->frame()));
}

ActResult Field::scan(ActContext&) {
    if (!timeline_) return ActResult::fail("nothing playing yet");
    const std::int64_t now = timeline_->frame();
    if (now < scan_ready_) return ActResult::fail(std::format("recharging: {:.1f} s", static_cast<double>(scan_ready_ - now) / steps_per_second));
    scan_until_ = now + scan_steps;
    scan_ready_ = now + scan_steps + scan_cooldown;
    sounds_->cue("scan");
    note("X-ray · the hidden ones shown");
    refresh();
    return ActResult::done();
}

ActResult Field::order(ActContext&, const Value& to) {
    if (!room(props.room).second_paddle) return ActResult::fail("the copilot plays in room 5");
    const std::string_view o = to.as_string();
    if (std::ranges::find(orders, o) == orders.end()) return ActResult::fail("an order is `hold`, `left`, `right` or `feed`");
    order_ = std::string(o);
    note(std::format("order → {}", order_));
    sounds_->cue("order");
    refresh();
    return ActResult::done();
}

ActResult Field::guard_to(ActContext&, const GuardToArgs& args) {
    if (!room(props.room).second_paddle) return ActResult::fail("the top paddle is room 5's");
    guard_x_ = std::clamp(args.x.as_double(0.5), 0.0, 1.0);
    const std::string_view says = args.says.as_string("");
    if (!says.empty() && says != says_) {
        says_ = std::string(says);
        says_until_ = (timeline_ ? timeline_->frame() : 0) + 150;
    }
    if (args.aim.is_number()) intent_ = std::clamp(args.aim.as_double(), 0.0, 1.0) * field_w;
    agent_log_.push_back(std::format("guard_to({:.3f}) · {}", *guard_x_, args.seen.as_string("—")));
    while (agent_log_.size() > log_size) agent_log_.pop_front();
    refresh();
    return ActResult::done();
}

ActResult Field::set_rules(ActContext&, const SetRulesArgs& args) {
    Rules r = rules_;
    const std::array<std::pair<const Value*, std::int32_t*>, 8> given{{{&args.drop_one_in, &r.drop_one_in},
                                                                       {&args.magnet_catches, &r.magnet_catches},
                                                                       {&args.multi_balls, &r.multi_balls},
                                                                       {&args.pierce_steps, &r.pierce_steps},
                                                                       {&args.slow_percent, &r.slow_percent},
                                                                       {&args.slow_steps, &r.slow_steps},
                                                                       {&args.wide_half, &r.wide_half},
                                                                       {&args.wide_steps, &r.wide_steps}}};
    for (const auto& [v, to] : given)
        if (!v->is_null()) *to = static_cast<std::int32_t>(v->as_int(*to));
    r.gift_mask = mask_of(run_.kit);
    if (!(bounded(r) == r)) return ActResult::fail("a rule is outside its bounds: none changed");
    rules_ = r;
    if (timeline_) pending_rules_ = r;
    note("rules → from the next step");
    return ActResult::done();
}

ActResult Field::choose(ActContext&, const Value& gift) {
    const auto o = offered();
    if (!o) return ActResult::fail("nothing is offered: between rooms only");
    const Contraption* c = contraption(gift.as_string());
    if (!c || std::ranges::find(*o, c->gift) == o->end()) return ActResult::fail(std::format("`{}` is not one of the two offered", gift.as_string()));
    run_.kit.push_back(c->gift);
    fresh_ = c->gift;
    choosing_ = false;
    emit_choosing(props.room, false);
    props.room = std::min(props.room + 1, room_count);
    prop_changed(FieldProp::room);
    services().focus(node());  // the cards gone: the keys are the game's again (Space, Enter), not the card's button
    return ActResult::done(Value(static_cast<std::int64_t>(props.room)));
}

ActResult Field::next(ActContext& ctx) {
    if (!timeline_ || !timeline_->state().over) return ActResult::fail("the room is not over yet");
    const State& s = timeline_->state();
    if (s.over == 1 && props.room < room_count) {  // cleared: straight to the choice
        while (over_steps_ < celebrate_steps) over_step();
        return ActResult::done(Value(static_cast<std::int64_t>(props.room)));
    }
    return again(ctx);  // lost, or the run complete: a new one
}

}  // namespace omui::breakout
