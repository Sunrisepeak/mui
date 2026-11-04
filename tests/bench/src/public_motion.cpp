module openmui.bench;
import std;
import hanim.core;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;

namespace omui::bench {
namespace {
constexpr double circle_control = 0.5522847498307936;
constexpr std::size_t instances = 500;
struct ShapeFixture {
    std::array<hanim::ShapeSegment, 32> star;
    std::array<hanim::ShapeSegment, 2> triangle{{hanim::ShapeSegment::line({-1, 1}), hanim::ShapeSegment::line({1, 1})}};
    std::array<hanim::ShapeSegment, 4> circle{{
        hanim::ShapeSegment::cubic({circle_control, -1}, {1, -circle_control}, {1, 0}),
        hanim::ShapeSegment::cubic({1, circle_control}, {circle_control, 1}, {0, 1}),
        hanim::ShapeSegment::cubic({-circle_control, 1}, {-1, circle_control}, {-1, 0}),
        hanim::ShapeSegment::cubic({-1, -circle_control}, {-circle_control, -1}, {0, -1})}};
    ShapeFixture() {
        for (std::size_t i = 0; i < star.size(); ++i) {
            const auto vertex = (i + 1) % star.size();
            const double angle = static_cast<double>(vertex) * hanim::dm::two_pi / static_cast<double>(star.size());
            const double radius = vertex % 2 ? 0.45 : 1;
            star[i] = hanim::ShapeSegment::line({radius * hanim::dm::sin(angle), -radius * hanim::dm::cos(angle)});
        }
    }
    auto prepare(bool concave = true) const {
        return hanim::PreparedShapeMorph::prepare({{0, -1}, concave ? std::span<const hanim::ShapeSegment>(star) : std::span<const hanim::ShapeSegment>(triangle)}, {{0, -1}, circle});
    }
};
struct PaintFixture {
    std::array<std::array<render::GradientStop, 2>, 8> stops;
    std::array<render::WeightedPaint, 8> from, to;
    PaintFixture() {
        for (std::size_t i = 0; i < from.size(); ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(from.size());
            from[i] = {render::Paint::solid({f, 0.25f, 1 - f, 0.8f}), 0.125f};
            stops[i] = {{{0, {0.2f, f, 1, 0.5f}}, {1, {1, 0.5f, f, 1}}}};
            render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
            gradient.gradient.start = {0, 0}; gradient.gradient.end = {256, 256}; gradient.gradient.stops = stops[i];
            to[i] = {gradient, 0.125f};
        }
    }
    auto prepare() const {
        render::Paint a, b; a.kind = b.kind = render::Paint::Kind::mix; a.mix = from; b.mix = to;
        return render::PreparedPaintMix::prepare(a, b);
    }
};
void cpu_note(State& state, std::string work) {
    state.note("timing", "CPU wall-clock only; no GPU submission, device execution or physical-GPU performance measured");
    state.note("work", std::move(work));
}
std::expected<render::RecordedPath, render::PathRecordError> record_shape(
    render::DisplayList& list, const hanim::PreparedShapeMorph& plan, double progress) {
    bool okay = true;
    auto result = list.record_path(plan.segment_count() + 2, plan.segment_count() * 3 + 1, [&](render::MutablePathView path) {
        path.verbs[0] = render::PathVerb::move; path.verbs.back() = render::PathVerb::close;
        auto point = [](hanim::Vec2 p) { return geom::Point{static_cast<float>(128 + 96 * p.x), static_cast<float>(128 + 96 * p.y)}; };
        for (std::size_t i = 0; i < plan.segment_count(); ++i) {
            const auto cubic = plan.sample_segment(progress, i);
            if (!cubic) { okay = false; return; }
            if (i == 0) path.points[0] = point(cubic->start);
            path.verbs[i + 1] = render::PathVerb::cubic;
            path.points[i * 3 + 1] = point(cubic->control1);
            path.points[i * 3 + 2] = point(cubic->control2);
            path.points[i * 3 + 3] = point(cubic->end);
        }
    });
    if (!okay) return std::unexpected(render::PathRecordError::invalid_path);
    return result;
}
}

void add_public_motion_benchmarks() {
    static bool added = false; if (added) return; added = true;
    add("public-motion/shape-prepare-star32", "one preparation and destruction: 32-edge concave star to four-cubic circle, correspondence and owned subdivisions", [](State& s) {
        const ShapeFixture input;
        cpu_note(s, "One prepare+destroy per iteration. Source 32 line segments; target 4 original cubics. Default arc_samples=64/alignment_samples=32. No rasterization.");
        s.items(1);
        while (s.next()) {
            const auto plan = input.prepare();
            if (!plan) { s.fail("shape preparation failed"); return; }
            s.counter("source_segments", 32); s.counter("target_segments", 4);
            s.counter("prepared_cubics", static_cast<double>(plan->segment_count()));
        }
    });
    add("public-motion/shape-sample-500", "500 prepared star/circle objects sampled by segment into preallocated cubic buffers; preparation excluded", [](State& s) {
        const ShapeFixture input; const auto plan = input.prepare();
        if (!plan) { s.fail("shape preparation failed during setup"); return; }
        const auto count = plan->segment_count();
        std::vector<hanim::ShapeCubic> output(instances * count);
        cpu_note(s, "Preparation and output allocation excluded. Each iteration samples 500 differently phased objects, all cubic segments, and consumes every output coordinate in a checksum pass. ns/item is per cubic sample, not per object/frame.");
        s.items(instances * count);
        while (s.next()) {
            double checksum = 0;
            for (std::size_t instance = 0; instance < instances; ++instance) {
                const double t = static_cast<double>((s.iteration() * 37 + instance * 17) % 1000) / 999;
                for (std::size_t i = 0; i < count; ++i) {
                    const auto cubic = plan->sample_segment(t, i);
                    if (!cubic) { s.fail("prepared segment sampling failed"); return; }
                    output[instance * count + i] = *cubic;
                }
            }
            for (const auto& cubic : output)
                checksum += cubic.start.x + cubic.start.y + cubic.control1.x + cubic.control1.y +
                            cubic.control2.x + cubic.control2.y + cubic.end.x + cubic.end.y;
            s.counter("objects", static_cast<double>(instances)); s.counter("cubics_per_object", static_cast<double>(count));
            s.counter("cubic_samples", static_cast<double>(instances * count)); s.counter("checksum", checksum);
        }
    });
    add("public-motion/paint-prepare-16", "one 16-leaf material plan preparation/destruction: 8 solids to 8 distinct two-stop gradients", [](State& s) {
        const PaintFixture input;
        cpu_note(s, "One prepare+destroy per iteration. Flatten/deduplicate two 8-leaf endpoints and deep-own 8 two-stop gradients. Resource decoding, shaders and rasterization excluded.");
        s.items(16);
        while (s.next()) {
            const auto plan = input.prepare();
            if (!plan || plan->term_count() != 16) { s.fail("expected sixteen distinct prepared material leaves"); return; }
            s.counter("material_leaves", static_cast<double>(plan->term_count())); s.counter("gradient_stops_owned", 16);
        }
    });
    add("public-motion/paint-sample-record-500", "500 16-leaf plans sampled into caller storage and independently recorded in a reusable display-list arena", [](State& s) {
        const PaintFixture input; const auto plan = input.prepare();
        if (!plan) { s.fail("material preparation failed during setup"); return; }
        render::DisplayList list;
        std::array<render::WeightedPaint, render::max_paint_mix_terms> buffer;
        cpu_note(s, "Preparation excluded. Per iteration: 500 sample() calls plus 500 record() calls (record samples independently), 16000 term writes total, deep-owning stops into a reusable arena. No draw commands, shader execution or rasterization. ns/item is per object doing both APIs.");
        s.items(instances);
        while (s.next()) {
            list.clear(); double checksum = 0;
            for (std::size_t i = 0; i < instances; ++i) {
                const double t = static_cast<double>((s.iteration() * 19 + i * 13) % 1000) / 999;
                const auto sampled = plan->sample(t, buffer);
                const auto recorded = plan->record(t, list);
                if (!sampled || !recorded) { s.fail("material sample/record failed"); return; }
                for (std::size_t term = 0; term < plan->term_count(); ++term)
                    checksum += buffer[term].weight * static_cast<double>(term + 1) +
                                recorded->mix[term].weight * static_cast<double>(term + 2);
            }
            s.counter("objects", static_cast<double>(instances)); s.counter("leaves_per_object", 16);
            s.counter("sample_calls", static_cast<double>(instances)); s.counter("record_calls", static_cast<double>(instances));
            s.counter("term_writes", static_cast<double>(instances * 16 * 2)); s.counter("checksum", checksum);
        }
    });
    add("public-motion/draw-cpu-256", "one 256x256 CPU frame: prepared triangle/circle geometry with gradient and normal-lit image mixed before one coverage mask", [](State& s) {
        const ShapeFixture input; const auto shape = input.prepare(false);
        if (!shape) { s.fail("shape preparation failed during setup"); return; }
        render::ImageStore images;
        const auto albedo = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{180, 130, 70, 255}));
        const auto normal = images.add(4, 4, std::vector<raster::Rgba8>(16, raster::Rgba8{128, 128, 255, 255}));
        render::Material lighting; lighting.normal = normal; lighting.key = {0.5f, 0.7f, 1, 1};
        render::Paint image; image.kind = render::Paint::Kind::image; image.image = albedo; image.material = &lighting;
        image.color = geom::Color::white(); image.image_transform = geom::Affine::scale(64, 64);
        const std::array<render::GradientStop, 2> stops{{{0, {1, 0.2f, 0.1f, 0.7f}}, {1, {0.2f, 1, 0.5f, 1}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
        gradient.gradient.start = {32, 32}; gradient.gradient.end = {224, 224}; gradient.gradient.stops = stops;
        const auto paint = render::PreparedPaintMix::prepare(gradient, image);
        if (!paint) { s.fail("material preparation failed during setup"); return; }
        render::DisplayList list; list.reserve(4);
        raster::Surface pixels(256, 256); render::CpuRenderer renderer;
        cpu_note(s, "One CPU frame per iteration: public shape sampling/arena path recording, two-leaf material recording, one FillPath, clear and full 256x256 CPU raster. Image creation and plan preparation excluded. ns/item is per canvas pixel, not per covered pixel. No GPU timing claim; renderer allocation counts are reported, not assumed zero.");
        s.items(256 * 256);
        while (s.next()) {
            const double t = static_cast<double>(s.iteration() % 120) / 119;
            list.clear(); const auto path = record_shape(list, *shape, t); const auto material = paint->record(t, list);
            if (!path || !material || !list.fill_path(*path, render::FillRule::nonzero, *material) || list.size() != 1) {
                s.fail("expected one recorded current geometry and one material-mix FillPath"); return;
            }
            const auto report = renderer.render(list, pixels, {&images, nullptr});
            if (report.invalid_paints || report.missing_images) { s.fail("CPU frame had invalid material/resources"); return; }
            const auto center = pixels.at(128, 128);
            if (center.a == 0) { s.fail("the real CPU current shape did not render"); return; }
            s.counter("draw_paths", 1); s.counter("material_leaves", static_cast<double>(paint->term_count()));
            s.counter("canvas_pixels", 256 * 256); s.counter("prepared_cubics", static_cast<double>(shape->segment_count()));
            s.counter("center_rgba_sum", center.r + center.g + center.b + center.a);
        }
    });
}
} // namespace omui::bench
