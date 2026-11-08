module omui.site;

import std;
import omui;
import hanim.engine;
import omui.hanim;

namespace omui::site {
namespace {

hanim::Rgba rgba(Color colour) { return {colour.r, colour.g, colour.b, colour.a}; }

}  // namespace

std::optional<Work> make_work(std::string_view name, Size size, const Theme& theme, WorkTiming timing) {
    if (size.width <= 0 || size.height <= 0) return std::nullopt;
    if (name == "cards-rise") {
        // Three cards rising into place one after another — its timing an agent's to change:
        // later by `delay`, overshooting by `bounce` (a spring less damped).
        const float card_width = std::round(size.width * 0.26f), card_height = std::round(size.height * 0.5f), gap = std::round(size.width * 0.04f);
        const float left = std::round((size.width - 3 * card_width - 2 * gap) / 2), top = std::round((size.height - card_height) / 2);
        std::vector<xrender::Path> paths;
        std::vector<hanim::PathPart> parts;
        const std::array<std::string_view, 3> inks{"accent", "success", "warning"};
        for (int index = 0; index < 3; ++index) {
            const Point at{left + static_cast<float>(index) * (card_width + gap), top};
            xrender::Path card;
            card.add_rounded_rect({0, 0, card_width, card_height}, CornerRadii::all(14));
            paths.push_back(std::move(card));
            hanim::PathPart part{.id = std::format("card_{}", index), .parent = "cards", .local = {.tx = at.x, .ty = at.y}};
            part.fill = rgba(theme.color(inks[static_cast<std::size_t>(index)], theme.color("accent")));
            parts.push_back(std::move(part));
        }
        for (std::size_t index = 0; index < parts.size(); ++index) parts[index].path = paths[index].view();
        auto description = hanim::object_from_paths("cards", parts);
        if (!description) return std::nullopt;
        std::vector<std::pair<std::string, hanim::Vec2>> homes;
        for (std::size_t index = 0; index < parts.size(); ++index)
            homes.emplace_back(parts[index].id, hanim::Vec2{parts[index].local.tx, parts[index].local.ty});
        const double rise = size.height * 0.35;
        return Work{std::move(*description), [homes, rise, timing, theme_inks = std::vector<hanim::Rgba>{parts[0].fill.value(), parts[1].fill.value(), parts[2].fill.value()}](hanim::ObjectRef object) {
                        // Bounce 0: a critically damped spring (no overshoot); 1: much less damped.
                        const double damping = 26.0 * (1.0 - 0.8 * std::clamp(timing.bounce, 0.0, 1.0));
                        const hanim::Ease ease = hanim::spring_ease({170, damping, 1});
                        std::vector<hanim::Program> blank, rises;
                        for (std::size_t index = 0; index < homes.size(); ++index) {
                            const auto& [id, home] = homes[index];
                            const hanim::anim::PartTarget card{object, id};
                            hanim::Rgba clear = theme_inks[index];
                            clear.a = 0;
                            blank.push_back(hanim::anim::to(card, hanim::ch::position = hanim::Vec2{home.x, home.y + rise}, hanim::ch::fill = clear)
                                                .options({.duration = hanim::seconds(0)}));
                            // Its place on the spring (which may overshoot); its colour eased in (a colour cannot overshoot).
                            rises.push_back(hanim::parallel({hanim::anim::to(card, hanim::ch::position = home).options({.duration = hanim::seconds(ease.settle()), .easing = ease}),
                                                             hanim::anim::to(card, hanim::ch::fill = theme_inks[index]).options({.duration = hanim::seconds(0.3), .easing = hanim::ease::out_cubic})}));
                        }
                        std::vector<hanim::Program> whole{hanim::parallel(blank)};
                        if (timing.delay > 0) whole.push_back(hanim::wait(hanim::seconds(timing.delay)));
                        whole.push_back(hanim::stagger(hanim::seconds(0.15), rises));
                        return hanim::sequence(whole);
                    }};
    }
    if (name == "mark-writing") {
        // The mark, its strokes written along their length (hanim's write_in).
        const float side = std::round(std::min(size.width, size.height) * 0.8f);
        const Affine place = Affine::translate(std::round((size.width - side) / 2), std::round((size.height - side) / 2)) * Affine::scale(side / 100, side / 100);
        std::vector<xrender::Path> paths;
        std::vector<hanim::PathPart> parts;
        for (const BrandShape& shape : brand_geometry()) {
            hanim::PathPart part{.id = std::format("stroke_{}", parts.size()), .parent = "mark"};
            if (shape.width > 0) {
                paths.push_back(shape.path.transformed(place));
                part.stroke = hanim::PartStroke{rgba(theme.color("accent")), shape.width * side / 100};
            } else {
                // A filled piece (the i's dot) about its own middle: it scales in place.
                const xrender::Path placed = shape.path.transformed(place);
                const Rect box = placed.bounds();
                const Point middle{box.x + box.width / 2, box.y + box.height / 2};
                paths.push_back(placed.transformed(Affine::translate(-middle.x, -middle.y)));
                part.local = {.tx = middle.x, .ty = middle.y};
                part.fill = rgba(theme.color("accent"));
            }
            parts.push_back(std::move(part));
        }
        for (std::size_t index = 0; index < parts.size(); ++index) parts[index].path = paths[index].view();
        auto description = hanim::object_from_paths("mark", parts);
        if (!description) return std::nullopt;
        std::vector<std::pair<std::string, bool>> pieces;  // each part, and whether it is a stroke
        for (const auto& part : parts) pieces.emplace_back(part.id, part.stroke.has_value());
        // Every piece blank at once (one not begun holds its source otherwise: drawn whole), then one
        // after another — the outline, then each letter written along its length, the i's dot springing in.
        return Work{std::move(*description), [pieces](hanim::ObjectRef object) {
                        std::vector<hanim::Program> blank, writes;
                        for (const auto& [id, stroke] : pieces) {
                            const hanim::anim::PartTarget piece{object, id};
                            if (stroke) {
                                blank.push_back(hanim::anim::to(piece, hanim::ch::trim_end = 0.0).options({.duration = hanim::seconds(0)}));
                                writes.push_back(hanim::anim::write_in(object, {.duration = hanim::seconds(0.9), .part = id}));
                            } else {
                                blank.push_back(hanim::anim::to(piece, hanim::ch::scale = hanim::Vec2{0, 0}).options({.duration = hanim::seconds(0)}));
                                writes.push_back(hanim::anim::to(piece, hanim::ch::scale = hanim::Vec2{1, 1}).spring({}));  // it pops in (a spring overshoots)
                            }
                        }
                        return hanim::sequence({hanim::parallel(blank), hanim::stagger(hanim::seconds(0.35), writes)});
                    }};
    }
    if (name == "mui-gather") {
        // Mui's pixels, each a vector square, gathering from wherever they were (hanim's assemble_in).
        const MascotArt& art = mascot_art();
        const auto rows = art.frames.find("sit");
        const auto inks = art.palettes.find(theme.dark ? "night" : "indigo");
        if (rows == art.frames.end() || inks == art.palettes.end()) return std::nullopt;
        const double cell = std::max(2.0, std::floor(std::min(size.width, size.height) * 0.55 / art.size));
        const double side = cell * art.size;
        const hanim::PixelGrid grid{inks->second, rows->second, cell};
        auto description = hanim::pixel_object("gather", grid, {.form = hanim::PixelForm::pixels});
        if (!description) return std::nullopt;
        description->parts[0].local = {.tx = std::round((size.width - side) / 2), .ty = std::round((size.height - side) / 2)};
        return Work{std::move(*description), [grid, side](hanim::ObjectRef object) {
                        return hanim::anim::assemble_in(object, grid, {.seed = 7, .distance = side * 1.2, .spin = 2.4});
                    }};
    }
    return std::nullopt;
}

ProgramStage::~ProgramStage() {
    if (made_ && attached()) {
        if (tick_) services().cancel(tick_);
        release();
    }
}

void ProgramStage::prop_changed(ProgramStageProp changed) {
    if (!attached()) return;
    if (!writing_ && (changed == ProgramStageProp::at || changed == ProgramStageProp::playing)) {
        if (changed == ProgramStageProp::playing && props.playing) {
            from_at_ = props.at >= length() - 1e-9 ? 0.0 : props.at;
            from_time_ = services().now().count();
        }
        schedule();
    }
    if (changed == ProgramStageProp::work) release();
    if (!writing_ && (changed == ProgramStageProp::delay || changed == ProgramStageProp::bounce)) release();  // made again with its new timing
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size ProgramStage::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float w = width.value_or(aw.is_definite() ? aw.value : 320);
    return {w, height.value_or(std::round(w * 0.62f))};
}

double ProgramStage::length() const { return made_ ? made_->length : 0; }

// Made the first time it is painted with room (again for another room,
// theme or work): its object, its recipe played and paused at its time.
void ProgramStage::ensure(const PaintContext& ctx) const {
    const WorkTiming timing{props.delay, props.bounce};
    if (made_ && made_->room == ctx.size && made_->dark == ctx.theme.dark && made_->work == props.work && made_->timing == timing) return;
    release();
    auto work = make_work(props.work, ctx.size, ctx.theme, timing);
    if (!work) return;
    auto made = std::make_unique<Made>();
    made->engine = &services().engine();
    made->room = ctx.size;
    made->dark = ctx.theme.dark;
    made->work = props.work;
    made->timing = timing;
    hanim::Engine& engine = *made->engine;
    auto object = engine.create_object(work->object);
    if (!object) return;
    made->object = *object;
    auto played = engine.play(work->program(made->object));
    if (!played) {
        (void)engine.destroy_object(made->object);
        return;
    }
    made->playback = *played;
    (void)engine.pause(*made->playback);
    if (const auto info = engine.inspect(*made->playback)) made->length = info->duration.count();
    // Its choreography, as hanim planned it: each piece's start and end.
    if (const auto plan = engine.plan(*made->playback)) {
        for (const auto& node : plan->nodes())
            if (node.kind == hanim::ProgramKind::binding && node.duration.count() > 0)
                made->plan += std::format("{}{:.2f}–{:.2f}", made->plan.empty() ? "" : " · ", node.start.count(), (node.start + node.duration).count());
    }
    (void)engine.seek(*made->playback, hanim::seconds(std::clamp(props.at, 0.0, made->length)));
    made_ = std::move(made);
    // Its length and plan said (a slider bound to it ends there): written at the next frame's start, not in paint.
    if (std::abs(props.length - made_->length) > 1e-9 || props.plan != made_->plan) schedule();
}

void ProgramStage::release() const {
    if (!made_) return;
    Made& made = *made_;
    if (made.playback) (void)made.engine->release(*made.playback);
    if (made.object) (void)made.engine->destroy_object(made.object);
    made_.reset();
}

void ProgramStage::schedule() const {
    if (tick_ || !attached()) return;
    tick_ = services().after(hanim::seconds(0), [this] {
        tick_ = 0;
        step();
    });
    services().request_frame();
}

void ProgramStage::write(double at, bool playing) const {
    writing_ = true;
    if (made_ && std::abs(made_->length - props.length) > 1e-9) services().write_back(node(), "length", Value(made_->length));
    if (made_ && made_->plan != props.plan) services().write_back(node(), "plan", Value(made_->plan));
    if (std::abs(at - props.at) > 1e-9) services().write_back(node(), "at", Value(at));
    if (playing != props.playing) services().write_back(node(), "playing", Value(playing));
    writing_ = false;
}

// Each frame while it plays, at its start: its time from when it started
// (by the runtime's clock), to its end, where it holds.
void ProgramStage::step() const {
    if (!made_ || !made_->playback) return;
    Made& made = *made_;
    double at = std::clamp(props.at, 0.0, made.length);
    bool playing = props.playing;
    if (playing) {
        at = std::min(made.length, from_at_ + (services().now().count() - from_time_));
        if (services().motion_reduced()) at = made.length;  // less motion: simply there
        if (at >= made.length - 1e-9) playing = false;
    }
    (void)made.engine->seek(*made.playback, hanim::seconds(at));
    write(at, playing);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    if (playing) schedule();
}

void ProgramStage::paint(PaintContext& ctx) const {
    ensure(ctx);
    if (!made_ || !made_->playback) return;
    if (props.playing) schedule();
    (void)made_->drawing.record(ctx, *made_->engine, *made_->playback, "figure");
}

void ProgramStage::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label.empty() ? props.work : props.label;
    node.value = Value{}.set("work", props.work).set("at", props.at).set("length", length()).set("playing", props.playing)
                     .set("delay", props.delay).set("bounce", props.bounce).set("plan", made_ ? made_->plan : props.plan);
}

ActResult ProgramStage::play(ActContext&) {
    const bool was = props.playing;
    from_at_ = props.at >= length() - 1e-9 ? 0.0 : props.at;
    from_time_ = services().now().count();
    write_playing(true);
    schedule();
    return was ? ActResult::done() : ActResult::done().undo_by("pause");
}

ActResult ProgramStage::pause(ActContext&) {
    const bool was = props.playing;
    write_playing(false);
    schedule();
    return was ? ActResult::done().undo_by("play") : ActResult::done();
}

ActResult ProgramStage::seek(ActContext&, const Value& at) {
    if (!at.is_number()) return ActResult::fail(std::format("seek takes seconds, not {}", at.json()));
    const double to = at.as_double();
    if (to < 0 || (made_ && to > made_->length + 1e-9)) return ActResult::fail(std::format("{:.2f} s is not in it (0 to {:.2f} s)", to, length()));
    const double before = props.at;
    write_playing(false);
    write_at(to);
    schedule();
    return ActResult::done().undo_by("seek", Value{}.set("at", before));
}

ActResult ProgramStage::retime(ActContext&, const RetimeArgs& args) {
    if (!args.delay.is_number() || !args.bounce.is_number()) return ActResult::fail("retime takes a delay (seconds) and a bounce (0 to 1)");
    const double delay = args.delay.as_double(), bounce = args.bounce.as_double();
    if (delay < 0 || delay > 5 || bounce < 0 || bounce > 1) return ActResult::fail(std::format("a delay of 0 to 5 s and a bounce of 0 to 1, not {} and {}", delay, bounce));
    const double before_delay = props.delay, before_bounce = props.bounce;
    write_delay(delay);
    write_bounce(bounce);
    release();
    // Played again from its start: the new timing seen at once.
    write_at(0);
    from_at_ = 0;
    from_time_ = services().now().count();
    write_playing(true);
    schedule();
    services().invalidate_paint(node());
    return ActResult::done().undo_by("retime", Value{}.set("delay", before_delay).set("bounce", before_bounce));
}

}  // namespace omui::site
