// A document opened: its shapes made objects of an Engine, its steps made a
// hanim Program, operated through hanim.control — prepared, played, paused
// and sought, each call admitted by mlibs.control and recorded — and drawn
// by hanim.render.vector into xrender's CPU raster.
module hanim.studio;

import std;

namespace hanim::studio {

namespace {

constexpr double circle_handle = 0.5522847498307936;  // a quarter circle's cubic handle, of its radius

// The cubics of an ellipse of radii rx, ry about the origin, from its top, clockwise.
std::vector<ShapeSegment> ellipse_arcs(double rx, double ry) {
    const double kx = rx * circle_handle, ky = ry * circle_handle;
    return {ShapeSegment::cubic({kx, -ry}, {rx, -ky}, {rx, 0}), ShapeSegment::cubic({rx, ky}, {kx, ry}, {0, ry}),
            ShapeSegment::cubic({-kx, ry}, {-rx, ky}, {-rx, 0}), ShapeSegment::cubic({-rx, -ky}, {-kx, -ry}, {0, -ry})};
}

// A shape's one closed contour, about its origin; and its lowest point (where a drop lands).
std::expected<std::pair<std::vector<Contour>, double>, std::string> contour_of(const ShapeSpec& shape) {
    if (shape.kind == ShapeKind::path) {
        const auto data = mlibs::geom::parse_path_data(shape.data);
        if (!data) return std::unexpected(data.error().message);
        auto imported = import_path(data->view());
        if (!imported) return std::unexpected(imported.error().text());
        std::vector<Contour> contours;
        for (auto& contour : imported->contours) contours.push_back(std::move(contour.geometry));
        double bottom = 0;
        for (const auto& point : data->points) bottom = std::max(bottom, static_cast<double>(point.y));
        if (contours.empty()) return std::unexpected(std::string("the path draws nothing"));
        return std::pair{std::move(contours), bottom};
    }
    Vec2 start{};
    std::vector<ShapeSegment> segments;
    double bottom = 0;
    switch (shape.kind) {
    case ShapeKind::rect: {
        const double w = shape.size.x / 2, h = shape.size.y / 2, r = shape.radius, k = r * (1 - circle_handle);
        bottom = h;
        start = {-w + r, -h};
        segments.push_back(ShapeSegment::line({w - r, -h}));
        if (r > 0) segments.push_back(ShapeSegment::cubic({w - k, -h}, {w, -h + k}, {w, -h + r}));
        segments.push_back(ShapeSegment::line({w, h - r}));
        if (r > 0) segments.push_back(ShapeSegment::cubic({w, h - k}, {w - k, h}, {w - r, h}));
        segments.push_back(ShapeSegment::line({-w + r, h}));
        if (r > 0) segments.push_back(ShapeSegment::cubic({-w + k, h}, {-w, h - k}, {-w, h - r}));
        segments.push_back(ShapeSegment::line({-w, -h + r}));
        if (r > 0) segments.push_back(ShapeSegment::cubic({-w, -h + k}, {-w + k, -h}, {-w + r, -h}));
        break;
    }
    case ShapeKind::circle:
        start = {0, -shape.radius};
        segments = ellipse_arcs(shape.radius, shape.radius);
        bottom = shape.radius;
        break;
    case ShapeKind::ellipse:
        start = {0, -shape.size.y};
        segments = ellipse_arcs(shape.size.x, shape.size.y);
        bottom = shape.size.y;
        break;
    case ShapeKind::polygon:
        start = shape.points.front();
        for (std::size_t index = 1; index < shape.points.size(); ++index) segments.push_back(ShapeSegment::line(shape.points[index]));
        for (const auto point : shape.points) bottom = std::max(bottom, point.y);
        break;
    case ShapeKind::star:
        for (int index = 0; index < shape.count * 2; ++index) {
            const double angle = -std::numbers::pi / 2 + std::numbers::pi * index / shape.count;
            const double radius = index % 2 ? shape.inner : shape.radius;
            const Vec2 point{radius * std::cos(angle), radius * std::sin(angle)};
            if (index == 0) start = point;
            else segments.push_back(ShapeSegment::line(point));
            bottom = std::max(bottom, point.y);
        }
        break;
    }
    auto contour = Contour::copy({start, segments, true});
    if (!contour) return std::unexpected(std::string("its outline could not be made"));
    return std::pair{std::vector<Contour>{std::move(*contour)}, bottom};
}

// The objects a step animates (a morph's target is not one).
void animated(const StepSpec& step, std::set<std::string>& out) {
    if (!step.object.empty()) out.insert(step.object);
    if (!step.with.empty()) out.insert(step.with);
    for (const auto& id : step.objects) out.insert(id);
    for (const auto& item : step.items) animated(item, out);
}
void dropped(const StepSpec& step, std::set<std::string>& out) {
    if (step.kind == StepKind::drop || step.kind == StepKind::drop_in) out.insert(step.object);
    for (const auto& item : step.items) dropped(item, out);
}

struct Builder {
    const Document& document;
    const std::map<std::string, ObjectRef>& refs;
    std::map<std::string, Rgba> fills;  // each object's fill as the steps so far leave it (fade_to keeps its colour)
    Problems problems;

    ObjectRef ref(const std::string& id) const { return refs.at(id); }

    anim::BoundAnimation timed(anim::BoundAnimation animation, const StepSpec& step) const {
        if (step.spring) return animation.spring(*step.spring);
        return animation.duration(step.duration.value_or(seconds(0.5))).easing(step.ease.value_or(ease::in_out_cubic));
    }
    // A recipe's options: the step's timing where it says, the recipe's own otherwise; the object's one part.
    anim::RecipeOptions recipe(const StepSpec& step) const { return {.duration = step.duration, .easing = step.ease, .part = "shape"}; }
    anim::RecipeOptions feedback(const StepSpec& step) const {
        anim::RecipeOptions options;
        if (step.duration) options.duration = *step.duration;
        if (step.ease) options.easing = *step.ease;
        return options;
    }
    std::vector<Program> all(const std::vector<StepSpec>& items) {
        std::vector<Program> out;
        for (const auto& item : items) out.push_back(program(item));
        return out;
    }
    Program program(const StepSpec& step) {
        switch (step.kind) {
        case StepKind::sequence: return sequence(std::span<const Program>(all(step.items)));
        case StepKind::parallel: return parallel(std::span<const Program>(all(step.items)));
        case StepKind::stagger: return stagger(step.step, std::span<const Program>(all(step.items)));
        case StepKind::wait: return wait(step.duration.value_or(seconds(0.5)));
        case StepKind::loop: return loop(program(step.items.front()), step.count, step.alternate);
        case StepKind::marker: return marker(step.name);
        case StepKind::move_to: return timed(anim::move_to(ref(step.object), step.vector), step);
        case StepKind::move_by: return timed(anim::move_by(ref(step.object), step.vector), step);
        case StepKind::rotate_to: return timed(anim::rotate_to(ref(step.object), step.number), step);
        case StepKind::rotate_by: return timed(anim::rotate_by(ref(step.object), step.number), step);
        case StepKind::scale_to: return timed(anim::scale_to(ref(step.object), step.vector), step);
        case StepKind::scale_by: return timed(anim::scale_by(ref(step.object), step.vector), step);
        case StepKind::fill_to:
            fills[step.object] = *step.color;
            return timed(anim::fill_to(ref(step.object), *step.color), step);
        case StepKind::fade_to: {
            Rgba color = fills.contains(step.object) ? fills[step.object] : Rgba{1, 1, 1, 1};
            color.a = static_cast<float>(step.number);
            fills[step.object] = color;
            return timed(anim::fill_to(ref(step.object), color), step);
        }
        case StepKind::morph: return timed(anim::morph(ref(step.object), ref(step.into)), step);
        case StepKind::drop: return anim::drop(part(ref(step.object), "shape"), step.vector, step.drop);
        case StepKind::pulse: return anim::pulse(ref(step.object), step.vector, feedback(step));
        case StepKind::shake: return anim::shake(ref(step.object), step.vector, feedback(step));
        case StepKind::wobble: return anim::wobble(ref(step.object), step.number, feedback(step));
        case StepKind::fade_in: return anim::fade_in(ref(step.object), recipe(step));
        case StepKind::rise_in: return anim::rise_in(ref(step.object), step.number, recipe(step));
        case StepKind::sink_in: return anim::sink_in(ref(step.object), step.number, recipe(step));
        case StepKind::slide_in: return anim::slide_in(ref(step.object), step.vector, recipe(step));
        case StepKind::pop_in: return anim::pop_in(ref(step.object), recipe(step));
        case StepKind::zoom_in: return anim::zoom_in(ref(step.object), recipe(step));
        case StepKind::spin_in: return anim::spin_in(ref(step.object), step.number, recipe(step));
        case StepKind::unfold_in: return anim::unfold_in(ref(step.object), recipe(step));
        case StepKind::write_in: return anim::write_in(ref(step.object), recipe(step));
        case StepKind::drop_in: return anim::drop_in(ref(step.object), step.number, recipe(step));
        case StepKind::fade_out: return anim::fade_out(ref(step.object), recipe(step));
        case StepKind::rise_out: return anim::rise_out(ref(step.object), step.number, recipe(step));
        case StepKind::sink_out: return anim::sink_out(ref(step.object), step.number, recipe(step));
        case StepKind::slide_out: return anim::slide_out(ref(step.object), step.vector, recipe(step));
        case StepKind::pop_out: return anim::pop_out(ref(step.object), recipe(step));
        case StepKind::zoom_out: return anim::zoom_out(ref(step.object), recipe(step));
        case StepKind::spin_out: return anim::spin_out(ref(step.object), step.number, recipe(step));
        case StepKind::fold_out: return anim::fold_out(ref(step.object), recipe(step));
        case StepKind::unwrite: return anim::unwrite(ref(step.object), recipe(step));
        case StepKind::bounce: return anim::bounce(ref(step.object), step.number, recipe(step));
        case StepKind::flash: return anim::flash(ref(step.object), static_cast<int>(step.number), recipe(step));
        case StepKind::heartbeat: return anim::heartbeat(ref(step.object), step.number, recipe(step));
        case StepKind::rubber_band: return anim::rubber_band(ref(step.object), recipe(step));
        case StepKind::swing: return anim::swing(ref(step.object), step.number, recipe(step));
        case StepKind::tada: return anim::tada(ref(step.object), recipe(step));
        case StepKind::nudge: return anim::nudge(ref(step.object), step.vector, recipe(step));
        case StepKind::bob: return anim::bob(ref(step.object), step.number, step.period, step.count, recipe(step));
        case StepKind::breathe: return anim::breathe(ref(step.object), step.number, step.period, step.count, recipe(step));
        case StepKind::spin: return anim::spin(ref(step.object), step.period, step.count, recipe(step));
        case StepKind::sway: return anim::sway(ref(step.object), step.number, step.period, step.count, recipe(step));
        case StepKind::orbit: return anim::orbit(ref(step.object), step.number, step.period, step.count, recipe(step));
        case StepKind::blink: return anim::blink(ref(step.object), step.period, step.count, recipe(step));
        case StepKind::arc_to: return anim::arc_to(ref(step.object), step.vector, step.number, recipe(step));
        case StepKind::swap: return anim::swap(ref(step.object), ref(step.with), step.vector, step.number, recipe(step));
        case StepKind::reorder: {
            std::vector<ObjectInput> moving;
            for (const auto& id : step.objects) moving.emplace_back(ref(id));
            return anim::reorder(moving, step.offsets, step.step, recipe(step));
        }
        }
        return wait(seconds(0));
    }
};

// The box a part's geometry covers once placed by its transform.
std::pair<Vec2, Vec2> bounds_of(const FrameView& frame, std::size_t part) {
    const auto m = frame.parts[part].local.matrix();
    Vec2 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 high{-low.x, -low.y};
    for (const auto& cubic : frame.cubics)
        for (const double parameter : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const Vec2 local = cubic.point(parameter);
            const Vec2 placed{m[0] * local.x + m[2] * local.y + m[4], m[1] * local.x + m[3] * local.y + m[5]};
            low = {std::min(low.x, placed.x), std::min(low.y, placed.y)};
            high = {std::max(high.x, placed.x), std::max(high.y, placed.y)};
        }
    if (low.x > high.x) return {{}, {}};
    return {low, high};
}

// The first step each object takes, in the document's order.
void first_steps(const StepSpec& step, std::map<std::string, StepKind>& out) {
    if (!step.object.empty()) out.try_emplace(step.object, step.kind);
    if (!step.with.empty()) out.try_emplace(step.with, step.kind);
    for (const auto& id : step.objects) out.try_emplace(id, step.kind);
    for (const auto& item : step.items) first_steps(item, out);
}
bool entrance(StepKind kind) {
    switch (kind) {
    case StepKind::fade_in: case StepKind::rise_in: case StepKind::sink_in: case StepKind::slide_in: case StepKind::pop_in:
    case StepKind::zoom_in: case StepKind::spin_in: case StepKind::unfold_in: case StepKind::write_in: case StepKind::drop_in: return true;
    default: return false;
    }
}

}  // namespace

struct Studio::Data {
    Document document;
    Engine engine;
    std::vector<ObjectRef> refs;          // the document's objects, in its order
    std::shared_ptr<mlibs::control::Access> access;
    mlibs::control::Invocation call;
    mlibs::control::InvocationContext context;
    std::optional<control::Controller> controller;
    PreparedProgram plan;
    PlaybackRef playback;
    std::vector<Operation> operations;
    // Each object's track (none: it stands still unanimated), and from when it is on the stage:
    // an object whose first step is an entrance is not there before that entrance begins.
    std::vector<std::optional<std::size_t>> track_of;
    std::vector<double> enters_at;
    // Drawn object by object in the document's order (its z order), each segment bound once.
    std::map<std::size_t, vector_render::PreparedVector> bindings;
    xrender::CpuRenderer renderer;
    xrender::DisplayList list, recording;
    std::vector<mlibs::geom::Affine> world;
    std::vector<float> parameters;
    std::vector<mlibs::geom::Rect> layers;

    [[nodiscard]] bool on_stage(std::size_t index, seconds time) const noexcept { return time.count() >= enters_at[index]; }

    explicit Data(Engine made) : engine(std::move(made)) {}

    template <class T>
    bool admitted(std::string_view name, const control::Reply<T>& reply) {
        operations.push_back({std::string(name), reply ? openas::Decision::run : reply.error().decision});
        return reply && reply->executed;
    }
    // The playback at `time`, sought through hanim.control.
    std::expected<void, Problems> seek(seconds time) {
        const seconds end = plan.duration();
        const seconds at = std::isfinite(end.count()) ? std::clamp(time, seconds(0), end) : std::max(time, seconds(0));
        if (!admitted("animation.seek", controller->seek(call, playback, at)))
            return std::unexpected(Problems{{"", std::format("seeking to {} s was refused or failed", at.count())}});
        return {};
    }
};

Studio::Studio(std::unique_ptr<Data> data) noexcept : data_(std::move(data)) {}
Studio::Studio(Studio&&) noexcept = default;
Studio& Studio::operator=(Studio&&) noexcept = default;
Studio::~Studio() = default;

std::expected<Studio, Problems> Studio::open(const Document& document) {
    auto engine = Engine::make();
    if (!engine) return std::unexpected(Problems{{"", "no engine: " + engine.error().text()}});
    auto data = std::make_unique<Data>(std::move(*engine));
    data->document = document;
    std::set<std::string> moving, falling;
    animated(document.program, moving);
    dropped(document.program, falling);
    Problems problems;
    std::map<std::string, ObjectRef> refs;
    std::map<std::string, Rgba> fills;
    for (std::size_t index = 0; index < document.objects.size(); ++index) {
        const auto& spec = document.objects[index];
        const std::string where = std::format("/objects/{}", index);
        auto contour = contour_of(spec.shape);
        if (!contour) {
            problems.push_back({where + "/shape", contour.error()});
            continue;
        }
        // One part an object, the same id for all: a morph pairs parts by id.
        PartDescription part{.id = "shape",
                             .local = {.tx = spec.at.x, .ty = spec.at.y, .sx = spec.scale.x, .sy = spec.scale.y, .rotate = spec.rotation},
                             .contours = contour->first,
                             .fill = spec.fill,
                             .stroke = spec.stroke};
        if (falling.contains(spec.id)) part.deformers.push_back(deform::squash({0, contour->second}));
        auto made = data->engine.create_object({{std::move(part)}});
        if (!made) {
            problems.push_back({where, std::string(describe(made.error()).name)});
            continue;
        }
        refs.emplace(spec.id, *made);
        data->refs.push_back(*made);
        if (spec.fill) fills.emplace(spec.id, *spec.fill);
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    // What stands still is held where it is, behind what moves; a hidden one is drawn only while it moves.
    Builder builder{document, refs, fills, {}};
    std::vector<Program> scene;
    for (const auto& spec : document.objects)
        if (!moving.contains(spec.id) && !spec.hidden)
            scene.push_back(anim::move_to(refs.at(spec.id), spec.at).duration(seconds(0)));
    scene.push_back(builder.program(document.program));
    const Program program = scene.size() == 1 ? scene.front() : parallel(std::span<const Program>(scene));

    namespace control_ns = mlibs::control;
    data->access = std::make_shared<control_ns::Access>(control_ns::policy_for_profile("dev"));
    data->call = {.actor = {openas::Kind::agent, "hanim-studio", {}}, .access = data->access, .session = data->access->open_session()};
    data->controller.emplace(data->engine, data->access, data->context);
    const auto prepared = data->controller->prepare(data->call, program);
    if (!data->admitted("animation.prepare", prepared) || !prepared->value)
        return std::unexpected(Problems{{"/program", prepared ? "it could not be prepared" : prepared.error().engine ? prepared.error().engine->text() : "refused"}});
    data->plan = *prepared->value;
    const auto played = data->controller->play(data->call, data->plan);
    if (!data->admitted("animation.play", played) || !played->value)
        return std::unexpected(Problems{{"/program", played && !played.error().engine ? "refused" : "it could not be played"}});
    data->playback = *played->value;
    if (!data->admitted("animation.pause", data->controller->pause(data->call, data->playback)))
        return std::unexpected(Problems{{"", "pausing was refused"}});
    const auto tracks = data->plan.tracks();
    data->track_of.assign(document.objects.size(), std::nullopt);
    data->enters_at.assign(document.objects.size(), -std::numeric_limits<double>::infinity());
    std::map<std::string, StepKind> first;
    first_steps(document.program, first);
    for (std::size_t index = 0; index < document.objects.size(); ++index)
        for (std::size_t track = 0; track < tracks.size(); ++track)
            if (tracks[track].object == data->refs[index]) {
                data->track_of[index] = track;
                if (const auto found = first.find(document.objects[index].id); found != first.end() && entrance(found->second))
                    data->enters_at[index] = data->plan.segments()[tracks[track].first_segment].start.count();
            }
    return Studio(std::move(data));
}

const Document& Studio::document() const noexcept { return data_->document; }
seconds Studio::duration() const noexcept { return data_->plan.duration(); }
const PreparedProgram& Studio::plan() const noexcept { return data_->plan; }
std::span<const ProgramMarker> Studio::markers() const noexcept { return data_->plan.markers(); }
std::span<const Operation> Studio::operations() const noexcept { return data_->operations; }

std::expected<std::vector<ObjectState>, Problems> Studio::state(seconds time) {
    if (auto sought = data_->seek(time); !sought) return std::unexpected(sought.error());
    const auto frame = data_->engine.program_frame(data_->playback);
    if (!frame) return std::unexpected(Problems{{"", frame.error().text()}});
    std::vector<ObjectState> states;
    for (std::size_t index = 0; index < data_->document.objects.size(); ++index) {
        ObjectState state{.id = data_->document.objects[index].id};
        const auto tracks = data_->plan.tracks();
        for (std::size_t track = 0; track < tracks.size(); ++track) {
            if (tracks[track].object != data_->refs[index] || !data_->on_stage(index, time)) continue;
            const FrameView& view = frame->scene.tracks[track].geometry;
            if (view.parts.empty()) break;
            const auto& local = view.parts[0].local;
            state.drawn = true;
            state.position = {local.tx, local.ty};
            state.scale = {local.sx, local.sy};
            state.rotation = local.rotate;
            state.fill = view.parts[0].fill;
            std::tie(state.low, state.high) = bounds_of(view, 0);
            // Its group opacity: the opacity effect's amount, when the segment it is in has one.
            const auto& track_view = frame->scene.tracks[track];
            if (track_view.segment_index < data_->plan.segments().size())
                for (const auto& effect : data_->plan.segments()[track_view.segment_index].effects.effects())
                    if (effect.id == "opacity" && effect.offset < track_view.effect_values.size()) state.opacity = track_view.effect_values[effect.offset];
        }
        states.push_back(std::move(state));
    }
    return states;
}

std::expected<const xrender::DisplayList*, Problems> Studio::display(seconds time) {
    if (auto sought = data_->seek(time); !sought) return std::unexpected(sought.error());
    const auto frame = data_->engine.program_frame(data_->playback);
    if (!frame) return std::unexpected(Problems{{"", frame.error().text()}});
    auto& d = *data_;
    d.list.clear();
    for (std::size_t index = 0; index < d.document.objects.size(); ++index) {
        if (!d.track_of[index] || !d.on_stage(index, time)) continue;
        const auto frame = d.engine.frame(d.playback, *d.track_of[index]);
        if (!frame || !frame->animation) return std::unexpected(Problems{{"", frame ? "no frame" : frame.error().text()}});
        auto bound = d.bindings.find(frame->segment_index);
        if (bound == d.bindings.end()) {
            auto made = frame->effects ? vector_render::PreparedVector::bind(*frame->animation, *frame->effects) : vector_render::PreparedVector::bind(*frame->animation);
            if (!made) return std::unexpected(Problems{{"", "it cannot be drawn: " + made.error().text()}});
            bound = d.bindings.emplace(frame->segment_index, std::move(*made)).first;
        }
        const auto needs = bound->second.requirements();
        d.world.resize(std::max(d.world.size(), needs.world_transforms));
        d.parameters.resize(std::max(d.parameters.size(), needs.backend_parameters));
        d.layers.resize(std::max(d.layers.size(), needs.effect_layers));
        const auto recorded = bound->second.record(frame->geometry, frame->effect_values, d.list, {&d.recording, d.world, d.parameters, d.layers});
        if (!recorded) return std::unexpected(Problems{{"", "drawing failed: " + recorded.error().text()}});
    }
    return &d.list;
}

std::expected<void, Problems> Studio::render(seconds time, xrender::raster::Surface& surface) {
    const auto list = display(time);
    if (!list) return std::unexpected(list.error());
    auto& d = *data_;
    const auto& background = d.document.background;
    const auto report = d.renderer.render(**list, surface, {}, {.background = mlibs::geom::Color{background.r, background.g, background.b, background.a}});
    if (report.invalid_paints || report.effects_skipped)
        return std::unexpected(Problems{{"", "the renderer skipped part of the frame"}});
    return {};
}

}  // namespace hanim::studio
