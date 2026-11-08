module omui.site;

import std;
import omui;
import hanim.engine;
import omui.hanim;

namespace omui::site {
namespace {

hanim::Rgba rgba(Color colour) { return {colour.r, colour.g, colour.b, colour.a}; }

}  // namespace

std::optional<Work> make_work(std::string_view name, Size size, const Theme& theme) {
    if (size.width <= 0 || size.height <= 0) return std::nullopt;
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
    if (made_ && made_->room == ctx.size && made_->dark == ctx.theme.dark && made_->work == props.work) return;
    release();
    auto work = make_work(props.work, ctx.size, ctx.theme);
    if (!work) return;
    auto made = std::make_unique<Made>();
    made->engine = &services().engine();
    made->room = ctx.size;
    made->dark = ctx.theme.dark;
    made->work = props.work;
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
    (void)engine.seek(*made->playback, hanim::seconds(std::clamp(props.at, 0.0, made->length)));
    made_ = std::move(made);
    // Its length said (a slider bound to it ends there): written at the next frame's start, not in paint.
    if (std::abs(props.length - made_->length) > 1e-9) schedule();
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
    node.value = Value{}.set("work", props.work).set("at", props.at).set("length", length()).set("playing", props.playing);
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

}  // namespace omui::site
