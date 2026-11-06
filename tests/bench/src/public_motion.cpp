module omui.bench;
import std;
import hanim.core;
import hanim.object;
import hanim.engine;
import hanim.render.vector;
import mlibs.geom;
import xrender.raster;
import xrender.vector;

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
    std::array<std::array<xrender::GradientStop, 2>, 8> stops;
    std::array<xrender::WeightedPaint, 8> from, to;
    PaintFixture() {
        for (std::size_t i = 0; i < from.size(); ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(from.size());
            from[i] = {xrender::Paint::solid({f, 0.25f, 1 - f, 0.8f}), 0.125f};
            stops[i] = {{{0, {0.2f, f, 1, 0.5f}}, {1, {1, 0.5f, f, 1}}}};
            xrender::Paint gradient; gradient.kind = xrender::Paint::Kind::gradient;
            gradient.gradient.start = {0, 0}; gradient.gradient.end = {256, 256}; gradient.gradient.stops = stops[i];
            to[i] = {gradient, 0.125f};
        }
    }
    auto prepare() const {
        xrender::Paint a, b; a.kind = b.kind = xrender::Paint::Kind::mix; a.mix = from; b.mix = to;
        return xrender::PreparedPaintMix::prepare(a, b);
    }
};
void cpu_note(State& state, std::string work) {
    state.note("timing", "CPU wall-clock only; no GPU submission, device execution or physical-GPU performance measured");
    state.note("work", std::move(work));
}
std::expected<xrender::RecordedPath, xrender::PathRecordError> record_shape(
    xrender::DisplayList& list, const hanim::PreparedShapeMorph& plan, double progress) {
    bool okay = true;
    auto result = list.record_path(plan.segment_count() + 2, plan.segment_count() * 3 + 1, [&](xrender::MutablePathView path) {
        path.verbs[0] = xrender::PathVerb::move; path.verbs.back() = xrender::PathVerb::close;
        auto point = [](hanim::Vec2 p) { return mlibs::geom::Point{static_cast<float>(128 + 96 * p.x), static_cast<float>(128 + 96 * p.y)}; };
        for (std::size_t i = 0; i < plan.segment_count(); ++i) {
            const auto cubic = plan.sample_segment(progress, i);
            if (!cubic) { okay = false; return; }
            if (i == 0) path.points[0] = point(cubic->start);
            path.verbs[i + 1] = xrender::PathVerb::cubic;
            path.points[i * 3 + 1] = point(cubic->control1);
            path.points[i * 3 + 2] = point(cubic->control2);
            path.points[i * 3 + 3] = point(cubic->end);
        }
    });
    if (!okay) return std::unexpected(xrender::PathRecordError::invalid_path);
    return result;
}
}

void add_public_motion_benchmarks() {
    static bool added = false; if (added) return; added = true;
    add("public-motion/image-bindings-resolve-64", "64 ordered public image-effect bindings resolve caller-owned samples; no renderer execution", [](State& s) {
        hanim::Effects effects;effects.reserve(64);
        for(int i=0;i<64;++i)effects.push_back(i%2?hanim::fx::blur(0,2):hanim::EffectSpec{"tone",{{"amount",0,.2f}}});
        const auto plan=hanim::PreparedEffects::prepare(effects);
        if(!plan){s.fail("effect preparation failed");return;}
        const auto bindings=hanim::vector_render::PreparedImageEffects::bind(*plan);
        if(!bindings){s.fail("renderer schema binding failed");return;}
        std::vector<float> sampled(plan->parameter_count()),resolved(bindings->parameter_count());
        s.items(64);cpu_note(s,"64 effect sample blocks and renderer default/range/generation resolution into preallocated output; no geometry, renderer execution or GPU timing.");
        while(s.next()) {
            const double progress=static_cast<double>(s.iteration()%61)/60;
            if(!plan->sample(progress,sampled) || !bindings->resolve(sampled,resolved)){s.fail("sample/binding failed");return;}
            s.counter("backend_parameters",static_cast<double>(resolved.size()));
        }
    });
    auto program_seek=[](State& s,bool scoped) {
        const ShapeFixture input;
        auto triangle = hanim::Contour::copy({{0,-1},input.triangle}), circle = hanim::Contour::copy({{0,-1},input.circle});
        auto registry = hanim::ObjectRegistry::make();
        if (!triangle || !circle || !registry) { s.fail("program setup failed"); return; }
        auto object = [&](double x, const hanim::Contour& contour) {
            return registry->create({{{.id="shape",.local={.tx=x},.contours={contour},.fill=hanim::Rgba{1,0,0,1}}}});
        };
        const auto a=object(0,*triangle),b=object(40,*circle),c=object(80,*triangle),d=object(100,*triangle),e=object(140,*circle);
        if (!a || !b || !c || !d || !e) { s.fail("program object publication failed"); return; }
        const auto source=*registry->capture(*a);
        auto animation=hanim::parallel({
            hanim::sequence({hanim::bind(source,*registry->capture(*b),{hanim::fx::blur(0,4)}),
                             hanim::bind(source,*registry->capture(*c),{hanim::fx::blur(4,0)})}),
            hanim::bind(*registry->capture(*d),*registry->capture(*e),{}, {.duration=hanim::seconds(2)})});
        if(scoped)animation=hanim::composite(hanim::composite(animation).with_effect(hanim::fx::blur(0,4)))
            .with_effect(hanim::fx::tone({}, {.r=.3f,.g=.7f,.b=1,.amount=.5f}));
        auto plan=hanim::PreparedProgram::prepare(animation);
        if (!plan) { s.fail("program compilation failed"); return; }
        std::vector<hanim::Engine> engines; engines.reserve(100);
        std::array<hanim::PlaybackRef,100> refs;
        for (std::size_t i=0;i<refs.size();++i) {
            auto engine=hanim::Engine::make();
            if (!engine) { s.fail("program instance engine failed"); return; }
            auto played=engine->play(*plan);
            if (!played) { s.fail("prepared program play failed"); return; }
            refs[i]=*played; engines.push_back(std::move(*engine));
        }
        cpu_note(s,"One immutable nested schedule reused by 100 separately owned Engine instances. Each seek samples both object tracks, selects the active phase and atomically publishes geometry/effects. Seventeen changing times exercise both stages and endpoints. Source matching, compilation, allocation and rendering excluded. One item is one object-track update including its share of selection/publication; not an entire rendered frame.");
        if(scoped)s.note("scope", "Two nested image scopes add five shared effect parameters per instance; query publishes all track/scope views. No image processing or renderer recording is measured.");
        s.items(200);std::size_t iteration=0;
        while (s.next()) {
            double checksum=0;
            for (std::size_t i=0;i<engines.size();++i) {
                const double progress=static_cast<double>((i+iteration)%17)/16;
                auto sought=engines[i].seek_progress(refs[i],progress);
                auto frame=engines[i].frame(refs[i],0);
                if (!sought || !frame || frame->geometry.cubics.empty()) { s.fail("program seek did not produce geometry"); return; }
                checksum+=frame->geometry.parts[0].local.tx+frame->effect_values[0]+frame->geometry.cubics.front().control1.x;
                if(scoped) {
                    const auto scene=engines[i].program_frame(refs[i]);
                    if(!scene || scene->scene.scope_effect_values.size()!=5) {s.fail("missing scope publication");return;}
                    checksum+=scene->scene.scope_effect_values[0];
                }
            }
            ++iteration;s.counter("tracks",200);s.counter("segments_per_instance",3);s.counter("checksum",checksum);
        }
    };
    add("public-motion/program-seek-100x2", "100 independent instances of a shared nested two-track three-segment program; arbitrary seeks cross phases; setup excluded",
        [program_seek](State& s){program_seek(s,false);});
    add("public-motion/scoped-program-seek-100x2", "100 two-track programs with two nested image scopes; arbitrary seeks and atomic frame queries; setup excluded",
        [program_seek](State& s){program_seek(s,true);});
    add("public-motion/engine-advance-500", "500 public object morph playbacks and blur parameters advanced and committed as one native batch; setup excluded", [](State& s) {
        auto engine = hanim::Engine::make({.objects={.max_objects=instances*2,.max_parts=1},.max_playbacks=instances});
        const ShapeFixture input;
        auto triangle = hanim::Contour::copy({{0,-1},input.triangle}), circle = hanim::Contour::copy({{0,-1},input.circle});
        if (!engine || !triangle || !circle) { s.fail("engine setup failed"); return; }
        std::array<hanim::PlaybackRef,instances> refs;
        for (std::size_t i=0;i<instances;++i) {
            const auto a = engine->create_object({{{.id="shape",.contours={*triangle},.fill=hanim::Rgba{1,0,0,1}}}});
            const auto b = engine->create_object({{{.id="shape",.local={.tx=40},.contours={*circle},.fill=hanim::Rgba{0,0,1,1}}}});
            if (!a || !b) { s.fail("engine object publication failed"); return; }
            const auto played = engine->play(*a,hanim::anim::morph_to(*b),hanim::fx::blur(0,4),{.duration=hanim::seconds(3600)});
            if (!played) { s.fail("public play failed"); return; }
            refs[i]=*played;
        }
        cpu_note(s,"500 independent strict contour/transform/fill morphs plus blur parameters per iteration. All active playbacks prepare into preallocated scratch then commit as a batch. Duration 3600s keeps every handle active even at maximum harness iterations. Preparation, matching, rendering, GPU and business setters excluded. ns/item is per complete playback update, not per cubic or pixel.");
        s.items(instances);
        while (s.next()) {
            const auto advanced=engine->advance(hanim::seconds(1.0/120));
            const auto frame=engine->frame(refs.back());
            if (!advanced || !frame || frame->info.state!=hanim::PlaybackState::playing || frame->geometry.cubics.empty() ||
                frame->geometry.parts[0].local.tx<=0 || frame->effect_values.size()!=1 || frame->effect_values[0]<=0) {
                s.fail("batch did not produce the expected live geometry and effect outputs"); return;
            }
            s.counter("playbacks",static_cast<double>(instances));
            s.counter("cubics_per_playback",static_cast<double>(frame->geometry.cubics.size()));
            s.counter("checksum",frame->geometry.cubics.front().control1.x+frame->geometry.parts[0].local.tx+frame->effect_values[0]);
        }
    });
    add("public-motion/object-query-500", "500 independent owned objects: capture, named/ref lookup, contour view and capability query; preparation excluded", [](State& s) {
        auto objects = hanim::ObjectRegistry::make({.max_objects = instances, .max_parts = 2});
        const std::array edges{hanim::ShapeSegment::line({10, 5})};
        const auto contour = hanim::Contour::copy({{1, 2}, edges, false});
        if (!objects || !contour) { s.fail("owned object setup failed"); return; }
        std::array<hanim::ObjectRef, instances> refs;
        for (std::size_t i = 0; i < instances; ++i) {
            const hanim::ObjectDescription description{{{.id = "group"}, {.id = "outline", .parent = "group",
                .local = {.tx = static_cast<double>(i)}, .contours = {*contour}}}};
            const auto created = objects->create(description);
            if (!created) { s.fail("object publication failed during setup"); return; }
            refs[i] = *created;
        }
        cpu_note(s, "500 real registry captures, named/ref lookups, owned contour views and capability checks per iteration. Object/geometry preparation excluded; tracing disabled by the normal benchmark preset. No animation evaluation, rasterization, GPU or complete frame performance claim. ns/item is per complete object query batch.");
        s.items(instances);
        while (s.next()) {
            double checksum = 0;
            for (const auto ref : refs) {
                const auto snapshot = objects->capture(ref);
                if (!snapshot) { s.fail("capture failed"); return; }
                const auto named = snapshot->find("outline");
                if (!named) { s.fail("named part disappeared"); return; }
                const auto part = snapshot->get(*named);
                if (!part || (*part)->contours.size() != 1 || snapshot->capability_version("hanim.contours") != 1) {
                    s.fail("owned query returned wrong domain"); return;
                }
                const auto shape = (*part)->contours.front().view();
                if (!shape || shape->segments.size() != 1) { s.fail("contour lease query failed"); return; }
                checksum += (*part)->local.tx + shape->segments.front().end.x;
            }
            if (checksum != 129750) { s.fail("query outputs were not preserved"); return; }
            s.counter("objects", static_cast<double>(instances)); s.counter("checksum", checksum);
        }
    });
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
        xrender::DisplayList list;
        std::array<xrender::WeightedPaint, xrender::max_paint_mix_terms> buffer;
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
        xrender::ImageStore images;
        const auto albedo = images.add(4, 4, std::vector<xrender::raster::Rgba8>(16, xrender::raster::Rgba8{180, 130, 70, 255}));
        const auto normal = images.add(4, 4, std::vector<xrender::raster::Rgba8>(16, xrender::raster::Rgba8{128, 128, 255, 255}));
        xrender::Material lighting; lighting.normal = normal; lighting.key = {0.5f, 0.7f, 1, 1};
        xrender::Paint image; image.kind = xrender::Paint::Kind::image; image.image = albedo; image.material = &lighting;
        image.color = mlibs::geom::Color::white(); image.image_transform = mlibs::geom::Affine::scale(64, 64);
        const std::array<xrender::GradientStop, 2> stops{{{0, {1, 0.2f, 0.1f, 0.7f}}, {1, {0.2f, 1, 0.5f, 1}}}};
        xrender::Paint gradient; gradient.kind = xrender::Paint::Kind::gradient;
        gradient.gradient.start = {32, 32}; gradient.gradient.end = {224, 224}; gradient.gradient.stops = stops;
        const auto paint = xrender::PreparedPaintMix::prepare(gradient, image);
        if (!paint) { s.fail("material preparation failed during setup"); return; }
        xrender::DisplayList list; list.reserve(4);
        xrender::raster::Surface pixels(256, 256); xrender::CpuRenderer renderer;
        cpu_note(s, "One CPU frame per iteration: public shape sampling/arena path recording, two-leaf material recording, one FillPath, clear and full 256x256 CPU raster. Image creation and plan preparation excluded. ns/item is per canvas pixel, not per covered pixel. No GPU timing claim; renderer allocation counts are reported, not assumed zero.");
        s.items(256 * 256);
        while (s.next()) {
            const double t = static_cast<double>(s.iteration() % 120) / 119;
            list.clear(); const auto path = record_shape(list, *shape, t); const auto material = paint->record(t, list);
            if (!path || !material || !list.fill_path(*path, xrender::FillRule::nonzero, *material) || list.size() != 1) {
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
