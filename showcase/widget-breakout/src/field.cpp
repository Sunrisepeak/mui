module openmui.showcase.widget_breakout;

import std;
import openmui;
import mgame.input;
import mgame.ui;
import openmui.showcase.widget_breakout.sim;
import openmui.theme.styles;

namespace omui::breakout {

namespace {

constexpr std::int32_t keep = 30 * steps_per_second;  // what a rewind reaches: 30 seconds

mgame::Bindings bindings() {
    mgame::Bindings b;
    for (const Key k : {Key::left, Key::a}) b.key(mgame::ui::code(k), mgame::Axis{0, -1});
    for (const Key k : {Key::right, Key::d}) b.key(mgame::ui::code(k), mgame::Axis{0, 1});
    b.key(mgame::ui::code(Key::space), mgame::Button{launch_button});
    b.mouse(1, mgame::Button{launch_button});
    return b;
}

Color gift_color(Gift g) {
    switch (g) {
        case Gift::wide: return Color::hex(0x4fc3f7);
        case Gift::multi: return Color::hex(0xffd54f);
        case Gift::slow: return Color::hex(0xb39ddb);
        case Gift::pierce: return Color::hex(0xff7043);
        case Gift::none: break;
    }
    return Color::hex(0xffffff);
}

// What the semantic tree calls a brick's component.
const char* role_name(Kind k) {
    switch (k) {
        case Kind::toggle: return "switch";
        case Kind::slider: return "slider";
        case Kind::card: return "text";
        case Kind::button:
        case Kind::hidden:
        case Kind::wall: break;
    }
    return "button";
}

const char* role_of(Kind k) {
    switch (k) {
        case Kind::button: return "button";
        case Kind::toggle: return "switch";
        case Kind::slider: return "slider";
        case Kind::card: return "text";
        case Kind::hidden: return "button (hidden)";
        case Kind::wall: return "wall";
    }
    return "";
}

}  // namespace

Field::Field() : sampler_(bindings()) {}

Field::~Field() { pacer_.stop(); }

void Field::mounted() {
    services().focus(node());
    pacer_.start(services(), steps_per_second, [this](std::int32_t n) { run(n); });
}

void Field::prop_changed(FieldProp p) {
    if (props.lens) discovered_ = true;
    if (!attached()) return;
    if (p == FieldProp::level) {
        services().recompose(node());  // its bricks: read into a new simulation on the next step
        restart_level();
    }
    refresh();
}

std::vector<Element> Field::compose() { return level_rows(level(props.level)); }

void Field::refresh() {
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Field::restart_level() {
    timeline_.reset();
    log_.clear();
    discovered_ = props.lens;
    turns_ = 0;
    for (const NodeId n : bricks()) services().set(n, "visible", Value(true));
    if (begin()) show_bricks();
    show_style();
}

std::string Field::style_wanted() const {
    if (props.level != 2) return {};
    std::int32_t cleared = 0;
    if (timeline_) {
        std::vector<bool> standing(static_cast<std::size_t>(rows_), false);
        timeline_->state().world.each<Brick>([&](mgame::Entity, const Brick& b) {
            if (b.row < standing.size()) standing[b.row] = true;
        });
        cleared = static_cast<std::int32_t>(std::ranges::count(standing, false));
    }
    return std::string(themes::style_names[static_cast<std::size_t>(cleared + turns_) % themes::style_names.size()]);
}

void Field::show_style() {
    std::string want = style_wanted();
    if (want == style_shown_) return;
    note(std::format("style → {}", want.empty() ? "the app's" : want));
    style_shown_ = want;
    emit_style(std::move(want));
}

void Field::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    s.align_items = layout::Align::center;
    s.row_gap = 14;
    s.padding.top = 70;
}

std::vector<NodeId> Field::bricks() const {
    std::vector<NodeId> out;
    for (const NodeId row : services().children(node()))
        for (const NodeId b : services().children(row)) out.push_back(b);
    return out;
}

bool Field::begin() {
    const LevelDef& def = level(props.level);
    const auto nodes = bricks();
    const Rect me = services().bounds(node());
    if (me.width <= 0 || me.height <= 0 || nodes.size() != def.bricks.size()) return false;
    const double sx = field_w / me.width, sy = field_h / me.height;
    Level l;
    l.speed = def.speed;
    l.lives = def.lives;
    l.seed = def.seed;
    l.gate_from = def.gate_from;
    l.gate_to = def.gate_to;
    std::uint32_t row = 0;
    double last_y = -1;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Rect r = services().bounds(nodes[i]);
        if (r.width <= 0 || r.height <= 0) return false;  // not laid out yet
        if (last_y >= 0 && r.y != last_y) ++row;
        last_y = r.y;
        const BrickDef& b = def.bricks[i];
        l.bricks.push_back({mgame::Box{(r.x - me.x) * sx, (r.y - me.y) * sy, r.width * sx, r.height * sy}, b.kind, b.durability, static_cast<std::uint32_t>(i), row});
    }
    rows_ = static_cast<std::int32_t>(row) + 1;
    timeline_.emplace(start(l), keep, 60);
    return true;
}

Input Field::input() {
    Input in;
    in.player = sampler_.sample();
    if (agent_x_) in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(*agent_x_, 0.0, 1.0) * 32767));
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
    const auto nodes = bricks();
    const LevelDef& def = level(props.level);
    for (std::uint32_t k = 0; k < s.events; ++k) {
        const Event& e = s.event[k];
        if (e.id >= nodes.size() || (e.what != Happened::hit && e.what != Happened::broke)) {
            if (e.what == Happened::cleared || e.what == Happened::over) emit_level_over(e.what == Happened::cleared, static_cast<std::int32_t>(s.score));
            continue;
        }
        const NodeId n = nodes[e.id];
        const BrickDef& b = def.bricks[e.id];
        if (e.what == Happened::broke) {
            services().set(n, "visible", Value(false));  // gone from the screen; hidden in the semantic tree
            note(std::format("{} {} · hidden", brick_id(e.id), role_name(b.kind)));
            emit_brick_broke(brick_id(e.id));
            continue;
        }
        // A hit, on what the brick is.
        std::int32_t left = 0;
        s.world.each<Brick>([&](mgame::Entity, const Brick& br) {
            if (br.id == e.id) left = br.durability;
        });
        if (b.kind == Kind::toggle) {
            const bool checked = !services().get(n, "checked").as_bool(true);
            services().set(n, "checked", Value(checked));
            note(std::format("{} switch · checked → {}", brick_id(e.id), checked));
        }
        if (b.kind == Kind::slider) {
            services().set(n, "value", Value(static_cast<double>(left) / b.durability));
            note(std::format("{} slider · value → {:.2f}", brick_id(e.id), static_cast<double>(left) / b.durability));
        }
    }
    show_style();  // a row cleared: level 2's next pack
    show_gates();
}

void Field::show_gates() {
    if (!timeline_) return;
    const State& s = timeline_->state();
    const auto nodes = bricks();
    const LevelDef& def = level(props.level);
    for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
        if (def.bricks[i].kind != Kind::gate) continue;
        const bool open = gates_open(s), shown = services().get(nodes[i], "visible").as_bool(true);
        if (open == shown) {
            services().set(nodes[i], "visible", Value(!open));
            note(std::format("{} progress · {}", brick_id(i), open ? "hidden (open)" : "shown (closed)"));
        }
        if (open) continue;
        const double v = s.frame < s.gate_from ? static_cast<double>(s.frame) / static_cast<double>(s.gate_from) : 1.0;
        services().set(nodes[i], "value", Value(v));
        services().set(nodes[i], "label", Value(s.frame < s.gate_from ? def.bricks[i].label : std::string("Closed for good — hold R to go back")));
    }
}

void Field::show_bricks() {
    if (!timeline_) return;
    const State& s = timeline_->state();
    const auto nodes = bricks();
    const LevelDef& def = level(props.level);
    std::vector<std::int32_t> left(nodes.size(), 0);
    s.world.each<Brick>([&](mgame::Entity, const Brick& br) {
        if (br.id < left.size()) left[br.id] = br.durability;
    });
    for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
        const BrickDef& b = def.bricks[i];
        services().set(nodes[i], "visible", Value(left[i] > 0));
        const std::int32_t hits = b.durability - left[i];
        if (b.kind == Kind::toggle) services().set(nodes[i], "checked", Value(hits % 2 == 0));
        if (b.kind == Kind::slider) services().set(nodes[i], "value", Value(static_cast<double>(left[i]) / b.durability));
    }
    show_gates();
}

void Field::note(std::string change) {
    log_.push_back(std::move(change));
    while (log_.size() > log_size) log_.pop_front();
}

void Field::run(std::int32_t steps) {
    if (!timeline_) {
        if (!begin()) return;  // waiting for layout to place the bricks
        show_style();
        refresh();
    }
    if (props.paused) return;
    if (rewinding_) {
        // Back through time at twice its pace, to the oldest frame kept.
        const std::int64_t to = std::max(timeline_->earliest(), timeline_->frame() - 2 * static_cast<std::int64_t>(steps));
        if (to != timeline_->frame() && timeline_->seek(to)) show_bricks();
    } else {
        for (std::int32_t k = 0; k < steps; ++k) play(input());
    }
    pacer_.clock().scale = timeline_->state().slow > 0 ? 0.5 : 1.0;  // the slow capsule: the game's clock at half speed
    refresh();
}

void Field::paint(PaintContext& ctx) const {
    const Theme& theme = services().theme();
    ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, theme.color("background", Color::hex(0x0d111c)));
    // Each brick's plate under its component: where the ball meets it (a hidden one has none).
    const Rect me = services().bounds(node());
    const auto nodes = bricks();
    const LevelDef& def = level(props.level);
    for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
        if (def.bricks[i].kind == Kind::hidden || def.bricks[i].kind == Kind::button || !services().get(nodes[i], "visible").as_bool(true)) continue;
        const Rect r = services().bounds(nodes[i]);
        ctx.list.fill_round_rect({r.x - me.x, r.y - me.y, r.width, r.height}, CornerRadii::all(10), render::Paint::solid(theme.color("surface_variant", Color::hex(0x1b2235))));
    }
}

void Field::paint_over(PaintContext& ctx) const {
    const float sx = ctx.size.width / static_cast<float>(field_w), sy = ctx.size.height / static_cast<float>(field_h);
    auto& out = ctx.list;
    auto& fonts = services().fonts();
    const Color ink = services().theme().color("text", Color::hex(0xe8eaf6));  // the paddles, balls and score: the style's text
    if (words_.size() > 256) words_.clear();  // a clock's and a hash's text change every frame: shaped again, not kept
    // Text shaped once (`width`: wrapped to it).
    auto label = [&](std::string_view text, float size, Color color, float width = 0) -> const typeset::Paragraph& {
        const std::string key = std::format("{}|{}|{},{},{},{}|{}", text, size, color.r, color.g, color.b, color.a, width);
        auto it = words_.find(key);
        if (it == words_.end()) {
            typeset::ParagraphStyle ps;
            ps.style.size = size;
            ps.style.weight = 600;
            ps.style.color = color;
            ps.wrap = width > 0;
            typeset::Paragraph p(fonts, std::string(text), std::move(ps));
            p.layout(width > 0 ? width : std::numeric_limits<float>::infinity());
            it = words_.emplace(key, std::move(p)).first;
        }
        return it->second;
    };
    // The semantic lens: every brick's role and id, the hidden ones too (once
    // seen, they stay faintly outlined), and the tree's latest changes.
    if (discovered_ && !props.lens) {
        const Rect me = services().bounds(node());
        const auto nodes = bricks();
        const LevelDef& def = level(props.level);
        for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
            if (def.bricks[i].kind != Kind::hidden || !services().get(nodes[i], "visible").as_bool(true)) continue;
            const Rect r = services().bounds(nodes[i]);
            out.stroke_round_rect({r.x - me.x, r.y - me.y, r.width, r.height}, CornerRadii::all(6), 1.5f, render::Paint::solid(Color::hex(0xff7043, 0.35f)));
        }
    }
    if (props.lens) {
        const float x = ctx.size.width - 330, line = 20, top = ctx.size.height - 90 - line * static_cast<float>(log_size + 1);
        out.fill_round_rect({x - 12, top - 10, 318, line * static_cast<float>(log_size + 1) + 16}, CornerRadii::all(8), render::Paint::solid(Color::hex(0x0b0e17, 0.85f)));
        label("SEMANTIC TREE · CHANGES", 11, Color::hex(0x64ffda, 0.7f)).paint(out, {x, top});
        for (std::size_t k = 0; k < log_.size(); ++k) {
            const float alpha = 0.45f + 0.55f * static_cast<float>(k + 1) / static_cast<float>(log_.size());  // the latest brightest
            label(log_[k], 12, Color::hex(0xe8eaf6, alpha)).paint(out, {x, top + line * static_cast<float>(k + 1)});
        }
        const Rect me = services().bounds(node());
        const auto nodes = bricks();
        const LevelDef& def = level(props.level);
        for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
            if (!services().get(nodes[i], "visible").as_bool(true)) continue;
            const Rect r = services().bounds(nodes[i]);
            const Rect at{r.x - me.x, r.y - me.y, r.width, r.height};
            const bool hidden = def.bricks[i].kind == Kind::hidden;
            const Color c = hidden ? Color::hex(0xff7043) : Color::hex(0x64ffda);
            out.stroke_round_rect(at, CornerRadii::all(6), hidden ? 2.5f : 1.5f, render::Paint::solid(c));
            const auto& p = label(std::format("{} · {}", role_of(def.bricks[i].kind), brick_id(i)), 11, c);
            p.paint(out, {at.x + 4, at.y - p.size().height - 1});
        }
    }
    if (!timeline_) return;
    const State& s = timeline_->state();
    // Capsules, paddles, balls.
    s.world.each<Capsule>([&](mgame::Entity, const Capsule& k) {
        const Rect r{static_cast<float>(k.at.x) * sx - 16 * sx, static_cast<float>(k.at.y) * sy - 7 * sy, 32 * sx, 14 * sy};
        out.fill_round_rect(r, CornerRadii::all(7 * sy), render::Paint::solid(gift_color(k.gift)));
    });
    for (const Paddle& p : s.paddles) {
        if (!p.active) continue;
        const Rect r{static_cast<float>(p.x - p.half) * sx, static_cast<float>(p.y - 8) * sy, static_cast<float>(2 * p.half) * sx, 16 * sy};
        out.fill_round_rect(r, CornerRadii::all(8 * sy), render::Paint::solid(p.wide > 0 ? Color::hex(0x4fc3f7) : ink));
    }
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) {
        const float r = static_cast<float>(b.radius);
        const Rect box{static_cast<float>(b.at.x - b.radius) * sx, static_cast<float>(b.at.y - b.radius) * sy, 2 * r * sx, 2 * r * sy};
        out.fill_round_rect(box, CornerRadii::all(r * sx), render::Paint::solid(b.pierce > 0 ? Color::hex(0xff7043) : ink));
    });
    // The score and the lives.
    const auto& score = label(std::format("{}", s.score), 22, ink);
    score.paint(out, {24, 18});
    for (std::int32_t i = 0; i < s.lives; ++i)
        out.fill_round_rect({ctx.size.width - 30 - 20.0f * static_cast<float>(i), 26, 12, 12}, CornerRadii::all(6), render::Paint::solid(ink));
    // Level 2: the style, and the play's checksum — the same under every style.
    if (props.level == 2) {
        const auto& line = label(std::format("{}  ·  play {:016x}  ·  S: next style", style_shown_, checksum(s)), 12, ink.with_alpha(0.7f));
        line.paint(out, {24, ctx.size.height - 26});
    }
    // Level 4: the clock, the gate's moment, and a rewind in progress; an open gate's place outlined.
    if (s.gate_to > 0) {
        auto clock = [](std::int64_t frame) { return std::format("{}:{:05.2f}", frame / (60 * steps_per_second), static_cast<double>(frame % (60 * steps_per_second)) / steps_per_second); };
        const auto& line = label(std::format("{}  ·  the gate: {} – {}  ·  hold R: rewind", clock(s.frame), clock(s.gate_from), clock(s.gate_to)), 12, ink.with_alpha(0.7f));
        line.paint(out, {24, ctx.size.height - 26});
        if (rewinding_) {
            const auto& back = label("◀◀  REWINDING", 20, Color::hex(0x64ffda));
            back.paint(out, {ctx.size.width / 2 - back.size().width / 2, 22});
        }
        // The gate: when it opens (over its bar), open (its place outlined), or closed for good.
        const Rect me = services().bounds(node());
        const auto nodes = bricks();
        const LevelDef& def = level(props.level);
        for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
            if (def.bricks[i].kind != Kind::gate) continue;
            const Rect r = services().bounds(nodes[i]);
            const Rect at{r.x - me.x, r.y - me.y, r.width, r.height};
            const bool open = gates_open(s), before = s.frame < s.gate_from;
            if (open) out.stroke_round_rect(at, CornerRadii::all(6), 1.5f, render::Paint::solid(Color::hex(0x64ffda, 0.8f)));
            const std::string what = open     ? std::format("open · {:.1f} s", static_cast<double>(s.gate_to - s.frame) / steps_per_second)
                                     : before ? std::format("opens in {:.1f} s", static_cast<double>(s.gate_from - s.frame) / steps_per_second)
                                              : std::string("closed for good — hold R");
            const auto& p = label(what, 12, open ? Color::hex(0x64ffda) : ink.with_alpha(0.75f));
            p.paint(out, {at.x + at.width / 2 - p.size().width / 2, open ? at.y + at.height / 2 - p.size().height / 2 : at.y + 3});
        }
    }
    // Over: what the level showed (cleared), and what next.
    if (s.over) {
        const float w = 640;
        const auto& lesson = label(s.over == 1 ? level(props.level).lesson : std::string("Out of balls."), 18, Color::hex(0xf5f5f5), w);
        const auto& hint = label(s.over == 1 && next_level(props.level) ? "Enter — the next level" : "Enter — this level again", 13, Color::hex(0xffd54f));
        const float h = lesson.size().height + hint.size().height + 52, x = ctx.size.width / 2 - w / 2 - 24, y = ctx.size.height / 2 - h / 2 + 60;
        out.fill_round_rect({x, y, w + 48, h}, CornerRadii::all(14), render::Paint::solid(Color::hex(0x0b0e17, 0.9f)));
        out.stroke_round_rect({x, y, w + 48, h}, CornerRadii::all(14), 1.5f, render::Paint::solid(Color::hex(0xffd54f, 0.6f)));
        lesson.paint(out, {x + 24, y + 20});
        hint.paint(out, {x + 24, y + 32 + lesson.size().height});
    }
}

bool Field::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->key == Key::tab) {
        if (k->kind == KeyKind::down && !k->repeat) {
            props.lens = !props.lens;
            prop_changed(FieldProp::lens);
        }
        ctx.handled = true;
        return true;
    }
    // R held: rewinding.
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->key == Key::r) {
        if (!k->repeat) rewinding_ = k->kind == KeyKind::down;
        if (!rewinding_ && timeline_) note(std::format("rewound to frame {}", timeline_->frame()));
        ctx.handled = true;
        return true;
    }
    // Enter: on from a level over; S: level 2's next style.
    if (const auto* k = std::get_if<KeyEvent>(&e); k && (k->key == Key::enter || (k->key == Key::s && props.level == 2))) {
        if (k->kind == KeyKind::down && !k->repeat) {
            const Value nothing;
            ActContext here{services(), node(), nothing};
            (void)(k->key == Key::enter ? next(here) : next_style(here));
        }
        ctx.handled = true;
        return true;
    }
    if (mgame::ui::feed(sampler_, e, services().bounds(node()))) {
        ctx.handled = true;
        return true;
    }
    return false;
}

void Field::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = level(props.level).title;
    if (!timeline_) return;
    const State& s = timeline_->state();
    Value v;
    // The play's checksum: under every style the same (level 2 shows it).
    v.set("level", props.level).set("style", style_shown_).set("hash", std::format("{:016x}", checksum(s)));
    v.set("frame", s.frame).set("score", s.score).set("lives", s.lives).set("bricks", s.bricks).set("over", static_cast<std::int32_t>(s.over));
    v.set("paddle", s.paddles[0].x / field_w);
    Value::List balls;
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) {
        balls.push_back(Value{}.set("x", b.at.x / field_w).set("y", b.at.y / field_h).set("vx", b.velocity.x).set("vy", b.velocity.y).set("on_paddle", b.on != 0));
    });
    v.set("balls", Value(std::move(balls)));
    v.set("lens", props.lens);
    v.set("changes", Value(Value::List(log_.begin(), log_.end())));
    node.value = std::move(v);
}

ActResult Field::launch(ActContext&) {
    agent_launch_ = true;
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
    play(input());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value(timeline_->frame()));
}

ActResult Field::restart(ActContext&) {
    restart_level();
    refresh();
    return ActResult::done();
}

ActResult Field::next(ActContext&) {
    if (!timeline_ || !timeline_->state().over) return ActResult::fail("the level is not over yet");
    if (timeline_->state().over == 1 && next_level(props.level)) {
        props.level = next_level(props.level);
        prop_changed(FieldProp::level);
    } else {
        restart_level();
        refresh();
    }
    return ActResult::done(Value(static_cast<std::int64_t>(props.level)));
}

ActResult Field::next_style(ActContext&) {
    if (props.level != 2) return ActResult::fail("styles are level 2's");
    ++turns_;
    show_style();
    refresh();
    return ActResult::done(Value(style_shown_));
}

ActResult Field::rewind(ActContext&, const Value& frames) {
    if (!timeline_) return ActResult::fail("nothing played yet");
    const std::int64_t to = timeline_->frame() - std::max<std::int64_t>(0, frames.as_int(0));
    if (!timeline_->seek(to))
        return ActResult::fail(std::format("not that far back: frames {} to {} are kept", timeline_->earliest(), timeline_->latest()));
    show_bricks();
    note(std::format("rewound to frame {}", timeline_->frame()));
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value(timeline_->frame()));
}

}  // namespace omui::breakout
