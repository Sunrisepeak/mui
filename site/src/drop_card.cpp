module omui.site;

import std;
import omui;
import hanim.engine;
import omui.hanim;
import mlibs.hash;

namespace omui::site {
namespace {

constexpr float card_radius = 16, readout_height = 22, floor_gap = 6;

hanim::Rgba rgba(Color colour) { return {colour.r, colour.g, colour.b, colour.a}; }

// The card as drawn before it falls (its own space): its width and height in a box of `room`.
Size card_size(Size room) {
    // Falling about twice its height: it lands with some speed.
    const float width = std::clamp(std::min(room.width * 0.42f, room.height * 0.62f), 100.0f, 220.0f);
    return {width, std::round(width * 0.62f)};
}

}  // namespace

DropCard::~DropCard() {
    if (card_ && card_->engine && attached()) {
        if (tick_) services().cancel(tick_);
        release();
    }
}

void DropCard::mounted() {
    readout_.set_style("caption");
    readout_.set_max_lines(1);
}

void DropCard::prop_changed(DropCardProp changed) {
    if (!attached()) return;
    if (changed == DropCardProp::at && !writing_) {
        // Set from outside (a slider dragged): that moment, unless it is where it already is.
        const Now here = now();
        const double step = card_ && card_->body ? card_->engine->body_time(*card_->body).value_or(hanim::BodyTime{}).step_seconds : 0;
        if (std::abs(props.at - here.at) > step / 2) {
            wanted_ = props.at;
            schedule();
        }
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size DropCard::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float w = width.value_or(aw.is_definite() ? aw.value : 360);
    return {w, height.value_or(std::round(w * 0.62f))};
}

// The card made the first time it is painted with room (again for another
// room or theme): its object — the card and what is on it — shown by a
// playback, and the soft body under the card, carrying the rest.
void DropCard::ensure(const PaintContext& ctx) const {
    if (card_ && card_->room == ctx.size && dark_ == ctx.theme.dark) return;
    if (ctx.size.width <= 0 || ctx.size.height <= 0) return;
    release();
    dark_ = ctx.theme.dark;
    auto made = std::make_unique<Card>();
    made->engine = &services().engine();
    made->room = ctx.size;
    const Size card = card_size(ctx.size);
    made->origin = {std::round((ctx.size.width - card.width) / 2), 8};
    // A little turned (0.05 rad): it lands on a corner and falls flat.
    const Affine turn = Affine::translate(card.width / 2, card.height / 2) * Affine::rotation(0.99875026f, 0.04997917f) * Affine::translate(-card.width / 2, -card.height / 2);
    xrender::Path outline;
    outline.add_rounded_rect({0, 0, card.width, card.height}, CornerRadii::all(card_radius));
    std::vector<xrender::Path> paths{outline.transformed(turn)};
    std::vector<hanim::PathPart> parts{{.id = "card", .parent = "drop", .fill = rgba(ctx.theme.color("surface")),
                                        .stroke = hanim::PartStroke{rgba(ctx.theme.color("border")), 1.5}}};
    // On it, a card's content: an avatar, a title, two lines of text, a button.
    std::vector<std::string> carried;
    const float pad = std::round(card.height * 0.16f), avatar = std::round(card.height * 0.26f);
    const auto add = [&](std::string id, const xrender::Path& path, Color colour) {
        paths.push_back(path.transformed(turn));
        parts.push_back({.id = id, .parent = "drop", .fill = rgba(colour)});
        carried.push_back(std::move(id));
    };
    const auto bar = [](Rect r) { return xrender::Path().add_rounded_rect(r, CornerRadii::all(r.height / 2)); };
    add("avatar", xrender::Path().add_circle({pad + avatar / 2, pad + avatar / 2}, avatar / 2), ctx.theme.color("accent"));
    const float text_x = pad + avatar + pad * 0.7f, text_w = card.width - text_x - pad;
    add("title", bar({text_x, pad + avatar * 0.12f, text_w * 0.62f, avatar * 0.3f}), ctx.theme.color("text"));
    add("subtitle", bar({text_x, pad + avatar * 0.6f, text_w * 0.4f, avatar * 0.22f}), ctx.theme.color("text_muted").with_alpha(0.7f));
    const float body_y = pad * 2 + avatar;
    add("line_1", bar({pad, body_y, card.width - 2 * pad, avatar * 0.2f}), ctx.theme.color("text_muted").with_alpha(0.35f));
    add("line_2", bar({pad, body_y + avatar * 0.42f, (card.width - 2 * pad) * 0.7f, avatar * 0.2f}), ctx.theme.color("text_muted").with_alpha(0.35f));
    add("button", bar({card.width - pad - card.width * 0.3f, card.height - pad - avatar * 0.5f, card.width * 0.3f, avatar * 0.5f}), ctx.theme.color("accent"));
    for (std::size_t index = 0; index < parts.size(); ++index) parts[index].path = paths[index].view();
    auto description = hanim::object_from_paths("drop", parts);
    if (!description) return;
    description->parts[0].local = {.tx = made->origin.x, .ty = made->origin.y};
    hanim::Engine& engine = *made->engine;
    auto object = engine.create_object(*description);
    if (!object) return;
    made->object = *object;
    auto shown = engine.play(hanim::anim::morph(made->object, made->object));
    if (!shown) return;
    made->showing = *shown;
    // Jelly (xengine.dynamics' material table): it gives on landing and springs back.
    const auto table = xengine::dynamics::MaterialTable::defaults();
    xengine::dynamics::BodyParams params{.gravity_y = 2200, .area_compliance = 0.0002, .damping = 0.4};
    if (const auto* jelly = table.find("jelly")) params = xengine::dynamics::params_of(*jelly, 1, params);
    const float floor = ctx.size.height - readout_height - floor_gap;
    const std::vector<xengine::dynamics::Plane> walls{{.normal_x = 0, .normal_y = -1, .offset = -(floor - made->origin.y)},
                                                       {.normal_x = 1, .normal_y = 0, .offset = -made->origin.x},
                                                       {.normal_x = -1, .normal_y = 0, .offset = -(ctx.size.width - made->origin.x)}};
    auto body = engine.simulate({made->object, "card"}, {.params = params, .colliders = walls, .points = 64, .carries = carried});
    if (!body) return;
    made->body = *body;
    card_ = std::move(made);
    // Another room: what it played is gone; it waits at the top again.
    state_ = "waiting";
    playing_ = false;
}

void DropCard::release() const {
    if (!card_) return;
    Card& card = *card_;
    if (card.body) (void)card.engine->unsimulate(*card.body);
    if (card.showing) (void)card.engine->release(*card.showing);
    if (card.object) (void)card.engine->destroy_object(card.object);
    card_.reset();
}

// Each frame while it plays, at its start (a timer, before layout and paint):
// the body on by the time since the last, its moment written.
void DropCard::schedule() const {
    if (tick_ || !attached()) return;
    tick_ = services().after(hanim::seconds(0), [this] {
        tick_ = 0;
        step();
    });
    services().request_frame();
}

void DropCard::step() const {
    if (!card_ || !card_->body) return;  // made at its first paint, which schedules again
    hanim::Engine& engine = *card_->engine;
    const hanim::BodyRef body = *card_->body;
    const double t = services().now().count();
    if (dropping_) {
        dropping_ = false;
        const auto time = engine.body_time(body);
        if (time) (void)engine.seek_body(body, time->earliest);
        playing_ = true;
        last_ = t;
        from_step_ = time ? time->earliest : 0;
        state_ = "falling";
        // Less motion asked: simply there, at rest (its whole time played at once).
        if (services().motion_reduced()) {
            if (const auto now_time = engine.body_time(body); now_time && now_time->step_seconds > 0)
                for (auto left = static_cast<std::int64_t>(std::llround(props.span / now_time->step_seconds)); left > 0;) {
                    const auto chunk = std::min<std::int64_t>(left, static_cast<std::int64_t>(1 / now_time->step_seconds));
                    (void)engine.step_body(body, chunk);
                    left -= chunk;
                }
            playing_ = false;
            state_ = "resting";
        }
    }
    if (wanted_) {
        const auto time = engine.body_time(body);
        if (time && time->step_seconds > 0) {
            const std::int64_t step = std::clamp<std::int64_t>(std::llround(*wanted_ / time->step_seconds), time->earliest, time->latest);
            (void)engine.seek_body(body, step);
            playing_ = false;
            // Named by where it is, however it came there: its span's end is resting.
            const double at = static_cast<double>(step) * time->step_seconds;
            state_ = step == 0 && time->latest == 0 ? "waiting" : at >= props.span - 1e-9 ? "resting" : "held";
        }
        wanted_.reset();
    }
    if (playing_) {
        // By count from where it started playing: each frame the steps its
        // time asks for, never past its span's last — every play of it ends
        // on the same step, whatever the frames' timing.
        if (const auto time = engine.body_time(body); time && time->step_seconds > 0) {
            const auto last = static_cast<std::int64_t>(std::llround(props.span / time->step_seconds));
            const auto due = std::min(last, from_step_ + static_cast<std::int64_t>(std::floor((t - last_) / time->step_seconds + 1e-9)));
            for (std::int64_t left = due - time->step; left > 0;) {
                const auto chunk = std::min<std::int64_t>(left, static_cast<std::int64_t>(1 / time->step_seconds));
                (void)engine.step_body(body, chunk);
                left -= chunk;
            }
            if (due >= last) {
                playing_ = false;
                state_ = "resting";
            }
        }
    }
    // Its moment, written (a slider bound to it follows).
    const double at = now().at;
    if (std::abs(at - props.at) > 1e-9) {
        writing_ = true;
        services().write_back(node(), "at", Value(at));
        writing_ = false;
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    if (playing_) schedule();
}

void DropCard::paint(PaintContext& ctx) const {
    ensure(ctx);
    if (!card_ || !card_->showing) return;
    if (playing_ || dropping_ || wanted_) schedule();
    Card& card = *card_;
    hanim::Engine& engine = *card.engine;
    const Theme& theme = ctx.theme;
    // The floor: a hairline it lands on.
    const float floor = ctx.size.height - readout_height - floor_gap;
    ctx.list.fill_rect({0, floor, ctx.size.width, 1}, theme.color("border"));
    (void)card.drawing.record(ctx, engine, *card.showing, "card");
    // The moment, the step and its state's hash: the same each time it comes back.
    const Now here = now();
    readout_.set_text(std::format("{:.2f} s · step {} · {:08x}", here.at, here.step, here.hash >> 32));
    const Color ink = theme.color("text_muted");
    const Size size = readout_.measure(services(), ink, ctx.size.width);
    const Rect line{(ctx.size.width - size.width) / 2, ctx.size.height - size.height, size.width, size.height};
    ctx.part({.role = "readout", .allocation = line, .kind = xrender::PartKind::content}, [&] { readout_.paint(ctx, {line.x, line.y}, ink); });
}

DropCard::Now DropCard::now() const {
    Now out;
    out.state = state_;
    if (!card_ || !card_->body) return out;
    const Card& card = *card_;
    const auto time = card.engine->body_time(*card.body);
    const auto body = card.engine->body(*card.body);
    if (!time || !body || !*body) return out;
    out.step = time->step;
    out.at = static_cast<double>(time->step) * time->step_seconds;
    out.played = static_cast<double>(time->latest) * time->step_seconds;
    mlibs::hash::Fnv1a hash;
    for (std::size_t index = 0; index < (*body)->size(); ++index) {
        hash.f64((*body)->x[index]).f64((*body)->y[index]);
    }
    out.hash = hash.value();
    float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
    for (std::size_t index = 0; index < (*body)->size(); ++index) {
        const float x = card.origin.x + static_cast<float>((*body)->x[index]), y = card.origin.y + static_cast<float>((*body)->y[index]);
        left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
    }
    out.card = {left, top, right - left, bottom - top};
    return out;
}

void DropCard::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = "A soft card";
    const Now here = now();
    Value value;
    value.set("state", here.state).set("at", here.at).set("played", here.played).set("step", here.step);
    value.set("hash", std::format("{:016x}", here.hash));
    value.set("card", Value(Value::List{Value(static_cast<double>(here.card.x)), Value(static_cast<double>(here.card.y)),
                                        Value(static_cast<double>(here.card.width)), Value(static_cast<double>(here.card.height))}));
    node.value = std::move(value);
}

ActResult DropCard::drop(ActContext&) {
    dropping_ = true;
    wanted_.reset();
    schedule();
    return ActResult::done();
}

ActResult DropCard::play(ActContext&) {
    const Now here = now();
    if (here.state == "waiting" || here.at >= props.span - 1e-9) {
        dropping_ = true;
    } else {
        playing_ = true;
        last_ = services().now().count();
        from_step_ = here.step;
        state_ = "falling";
    }
    schedule();
    return ActResult::done();
}

ActResult DropCard::seek(ActContext&, const Value& at) {
    if (!at.is_number()) return ActResult::fail(std::format("seek takes seconds, not {}", at.json()));
    const Now here = now();
    if (at.as_double() < 0 || at.as_double() > here.played + 1e-9)
        return ActResult::fail(std::format("{:.2f} s is not in what it played (0 to {:.2f} s)", at.as_double(), here.played));
    wanted_ = at.as_double();
    schedule();
    return ActResult::done().undo_by("seek", Value{}.set("at", here.at));
}

}  // namespace omui::site
