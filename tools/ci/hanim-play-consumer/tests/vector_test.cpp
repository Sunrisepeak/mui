import std;
import hanim.render.vector;
import openmui.raster;
import openmui.testing;

using namespace hanim;
using namespace hanim::vector_render;
using namespace omui::testing;
namespace render = omui::render;
namespace geom = mlibs::geom;
namespace raster = omui::raster;

namespace {
struct GeometryBuffers {
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    explicit GeometryBuffers(const PreparedObjectMorph& plan) : parts(plan.requirements().parts), cubics(plan.requirements().cubics) {}
    FrameView sample(const PreparedObjectMorph& plan, double t) {
        const auto frame = plan.sample(t, {parts, cubics}); check(frame.has_value());
        return frame ? *frame : FrameView{};
    }
};
struct RecordBuffers {
    render::DisplayList staging;
    std::vector<geom::Affine> world;
    std::vector<float> parameters;
    std::vector<geom::Rect> layers;
    explicit RecordBuffers(const PreparedVector& plan) : world(plan.requirements().world_transforms),
        parameters(plan.requirements().backend_parameters), layers(plan.requirements().effect_layers) {}
    VectorScratch get() { return {&staging, world, parameters, layers}; }
};
struct Fixture { ObjectRegistry objects; Snapshot from, to; PreparedObjectMorph plan; };
Fixture fixture(bool box = false) {
    auto objects = ObjectRegistry::make(); check(objects.has_value());
    const std::array triangle{ShapeSegment::line({18, 0}), ShapeSegment::line({9, 18})};
    const std::array square{ShapeSegment::line({18, 0}), ShapeSegment::line({18, 18}), ShapeSegment::line({0, 18})};
    const auto a = Contour::copy({{}, box ? std::span<const ShapeSegment>(square) : std::span<const ShapeSegment>(triangle), true});
    check(a.has_value());
    ObjectDescription description{{{.id = "root", .local = {.tx = 16, .ty = 16}},
        {.id = "shape", .parent = "root", .contours = {*a}, .fill = Rgba{1, 1, 1, 1}}}};
    const auto object = objects->create(description); check(object.has_value());
    const auto from = objects->capture(*object); check(from.has_value());
    if (!box) {
        constexpr double k = 9 * 0.5522847498307936;
        const std::array circle{ShapeSegment::cubic({9 + k, 0}, {18, 9 - k}, {18, 9}),
            ShapeSegment::cubic({18, 9 + k}, {9 + k, 18}, {9, 18}),
            ShapeSegment::cubic({9 - k, 18}, {0, 9 + k}, {0, 9}),
            ShapeSegment::cubic({0, 9 - k}, {9 - k, 0}, {9, 0})};
        const auto b = Contour::copy({{9, 0}, circle, true}); check(b.has_value());
        description.parts[0].local.tx = 32; description.parts[0].local.ty = 24;
        description.parts[1].contours = {*b}; description.parts[1].fill = Rgba{0.1f, 0.5f, 1, 1};
    }
    check(objects->update(*object, description).has_value());
    const auto to = objects->capture(*object); check(to.has_value());
    const auto plan = PreparedObjectMorph::prepare(*from, *to); check(plan.has_value());
    return {std::move(*objects), *from, *to, *plan};
}
raster::Surface draw(const render::DisplayList& list) {
    raster::Surface pixels(80, 80); render::CpuRenderer cpu;
    const auto report = cpu.render(list, pixels, {});
    check(report.effects_skipped == 0 && report.invalid_paints == 0 && report.missing_images == 0 && report.missing_glyphs == 0);
    return pixels;
}
}

int main(int argc, char** argv) {
    Suite s{"hanim.render.vector"};
    s.test("public image bindings resolve ordered defaults atomically without owning geometry or a clock", [] {
        const std::array descriptions{
            EffectSpec{"tone",{{"amount",0,1}}},fx::blur(0,4),fx::blur(4,0)};
        const auto effects=PreparedEffects::prepare(descriptions);check(effects.has_value());if(!effects)return;
        auto binding=PreparedImageEffects::bind(*effects);check(binding.has_value());if(!binding)return;
        check(binding->bindings().size()==3 && binding->bindings()[1].ref.id==binding->bindings()[2].ref.id,
              "repeated effects retain separate ordered parameter blocks");
        std::vector<float> sampled(effects->parameter_count()),resolved(binding->parameter_count()+2,-99);
        check(effects->sample(.25,sampled).has_value());check(binding->resolve(sampled,resolved).has_value());
        const auto bound=binding->bindings();
        for(std::size_t i=0;i<bound.size();++i) {
            const auto* info=render::effects().info(bound[i].ref);check(info!=nullptr);if(!info)return;
            for(std::size_t p=0;p<info->params.size();++p) {
                const auto name=info->params[p].name;
                const float expected=i==0 && name=="amount"?.25f:i>0 && name=="radius"?(i==1?1.f:3.f):info->params[p].value;
                check(resolved[bound[i].offset+p]==expected,"renderer parameter order and omitted defaults are resolved");
            }
        }
        check(resolved.back()==-99 && resolved[resolved.size()-2]==-99,"extra capacity is untouched");
        const auto before=resolved;sampled.back()=std::numeric_limits<float>::quiet_NaN();
        check(!binding->resolve(sampled,resolved) && resolved==before,"a late invalid parameter never partly publishes the earlier fields");
        check(effects->sample(.5,sampled).has_value());
        check(!binding->resolve(sampled,std::span(resolved).first(1)) && resolved==before,"short output is atomic");
        std::vector<float> alias(std::max(sampled.size(),resolved.size()),2);
        const auto original_alias=alias;
        check(!binding->resolve(std::span<const float>(alias).first(sampled.size()),alias) && alias==original_alias,"aliased source/output is refused");
        const auto ref=bound.front().ref;const auto original=*render::effects().info(ref);
        check(render::effects().replace(ref,original));const auto stale=binding->resolve(sampled,resolved);
        check(render::effects().replace(ref,original));
        check(!stale && stale.error().code==VectorError::stale_effect && resolved==before,"reload requires a rebind before any output changes");
        const auto empty=PreparedImageEffects::bind({});check(empty.has_value());if(empty)check(empty->resolve({},resolved).has_value() && resolved==before);
    });
    s.test("public pixelate parameters bind independently of UI and render exact off and distinct intermediate frames", [] {
        auto source = fixture();
        const auto descriptions = fx::chain({fx::pixelate({7, 0}, {7, 1}, {fx::PixelUnit::device, -2, 3})});
        const auto effects = PreparedEffects::prepare(descriptions); check(effects.has_value()); if (!effects) return;
        const auto bound = PreparedVector::bind(source.plan, *effects);
        const auto plain = PreparedVector::bind(source.plan); check(bound && plain); if (!bound || !plain) return;
        GeometryBuffers geometry(source.plan); const auto frame = geometry.sample(source.plan, .4);
        RecordBuffers scratch(*bound), plain_scratch(*plain);
        render::DisplayList reference; check(plain->record(frame, {}, reference, plain_scratch.get()).has_value());
        std::vector<float> values(effects->parameter_count());
        std::array<std::uint64_t, 3> hashes{};
        for (int step = 0; step < 3; ++step) {
            check(effects->sample(static_cast<double>(step) / 2, values).has_value());
            render::DisplayList output; check(bound->record(frame, values, output, scratch.get()).has_value());
            hashes[static_cast<std::size_t>(step)] = draw(output).hash();
        }
        check(hashes[0] == draw(reference).hash());
        check(hashes[0] != hashes[1] && hashes[1] != hashes[2]);
        check(effects->sample(.5, values).has_value());
        render::DisplayList again; check(bound->record(frame, values, again, scratch.get()).has_value());
        check(draw(again).hash() == hashes[1]);
    });
    s.test("current contour frame and nested transforms produce real seekable CPU pixels", [] {
        auto source = fixture();
        auto bound = PreparedVector::bind(source.plan); check(bound.has_value()); if (!bound) return;
        GeometryBuffers geometry(source.plan); RecordBuffers buffers(*bound); render::DisplayList output;
        std::array<std::uint64_t, 3> hashes{};
        for (int step = 0; step < 3; ++step) {
            const auto frame = geometry.sample(source.plan, static_cast<double>(step) / 2);
            output.clear(); check(bound->record(frame, {}, output, buffers.get()).has_value());
            check(output.balanced());
            std::size_t fills = 0;
            for (const auto& command : output.commands()) if (const auto* fill = std::get_if<render::FillPath>(&command)) {
                ++fills;
                check(fill->path.verbs.size() == frame.contours[0].cubic_count + 2, "one current geometry, not two endpoint masks");
                check(fill->path.points[0] == geom::Point{static_cast<float>(frame.cubics[0].start.x), static_cast<float>(frame.cubics[0].start.y)});
            }
            check(fills == 1);
            const auto pixels = draw(output); hashes[step] = pixels.hash();
            check(pixels.at(25 + step * 8, 25 + step * 4).a > 0);
            check(pixels.at(2, 2).a == 0);
        }
        check(hashes[0] != hashes[1] && hashes[1] != hashes[2] && hashes[0] != hashes[2]);
        output.clear(); check(bound->record(geometry.sample(source.plan, 0.5), {}, output, buffers.get()).has_value());
        check(draw(output).hash() == hashes[1], "random seek replays the same current pixels");
    });
    s.test("solid to owned gradient shades the same current shape after inputs and plans release", [] {
        render::DisplayList output;
        {
            auto source = fixture(true);
            std::array<render::GradientStop, 2> stops{{{0, {1, 0, 0, 1}}, {1, {0, 0, 1, 1}}}};
            render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
            gradient.gradient.start = {0, 0}; gradient.gradient.end = {18, 0}; gradient.gradient.stops = stops;
            const std::array materials{MaterialBinding{"shape", render::Paint::solid({0, 1, 0, 1}), gradient}};
            const auto bound = PreparedVector::bind(source.plan, {}, materials); check(bound.has_value()); if (!bound) return;
            stops[0].color = geom::Color::black(); stops[1].color = geom::Color::black();
            GeometryBuffers geometry(source.plan); RecordBuffers buffers(*bound);
            check(bound->record(geometry.sample(source.plan, 0.5), {}, output, buffers.get()).has_value());
        }
        check(output.balanced());
        const auto pixels = draw(output);
        check(pixels.at(18, 25).r > pixels.at(18, 25).b && pixels.at(31, 25).b > pixels.at(31, 25).r);
        check(pixels.at(23, 25).g > 80 && pixels.at(23, 25).a == 255, "current material contains both green solid and red-blue gradient");
        check(pixels.at(10, 25).a == 0, "material does not invent a second geometry mask");
    });
    s.test("nested local placement composes through a rotated parent without flattening the hierarchy", [] {
        auto source = fixture(true);
        const auto shape = source.from.get(*source.from.find("shape")); check(shape.has_value());
        const ObjectDescription description{{{.id = "root", .local = {.tx = 40, .ty = 20, .rotate = dm::pi / 2}},
            {.id = "shape", .parent = "root", .local = {.tx = 10}, .contours = (*shape)->contours, .fill = Rgba{1, 0, 0, 1}}}};
        check(source.objects.update(source.from.object(), description).has_value());
        const auto snapshot = source.objects.capture(source.from.object()); check(snapshot.has_value());
        const auto plan = PreparedObjectMorph::prepare(*snapshot, *snapshot); check(plan.has_value());
        const auto bound = PreparedVector::bind(*plan); check(bound.has_value()); if (!bound) return;
        GeometryBuffers geometry(*plan); RecordBuffers buffers(*bound); render::DisplayList output;
        check(bound->record(geometry.sample(*plan, 0.5), {}, output, buffers.get()).has_value());
        const auto pixels = draw(output);
        check(pixels.at(31, 39).r == 255 && pixels.at(31, 39).a == 255);
        check(pixels.at(45, 29).a == 0 && pixels.at(30, 25).a == 0,
            "the child's x=10 translation rotates with its parent rather than after it");
    });
    s.test("opposite winding contours share one current nonzero mask and retain its hole", [] {
        auto source = fixture(true);
        const auto shape = source.from.get(*source.from.find("shape")); check(shape.has_value());
        auto contours = (*shape)->contours;
        const std::array edges{ShapeSegment::line({6, 12}), ShapeSegment::line({12, 12}), ShapeSegment::line({12, 6})};
        const auto hole = Contour::copy({{6, 6}, edges, true}); check(hole.has_value()); contours.push_back(*hole);
        const ObjectDescription description{{{.id = "root", .local = {.tx = 16, .ty = 16}},
            {.id = "shape", .parent = "root", .contours = contours, .fill = Rgba{1, 1, 1, 1}}}};
        check(source.objects.update(source.from.object(), description).has_value());
        const auto snapshot = source.objects.capture(source.from.object()); check(snapshot.has_value());
        const auto plan = PreparedObjectMorph::prepare(*snapshot, *snapshot); check(plan.has_value());
        const auto bound = PreparedVector::bind(*plan); check(bound.has_value()); if (!bound) return;
        GeometryBuffers geometry(*plan); RecordBuffers buffers(*bound); render::DisplayList output;
        check(bound->record(geometry.sample(*plan, 0.5), {}, output, buffers.get()).has_value());
        std::size_t fills = 0; for (const auto& command : output.commands()) fills += std::holds_alternative<render::FillPath>(command);
        check(fills == 1, "subcontours are not separately filled into two masks");
        const auto pixels = draw(output);
        check(pixels.at(20, 25).a == 255 && pixels.at(25, 25).a == 0 && pixels.at(10, 25).a == 0);
    });
    s.test("ordered CPU blur then tone retains the inner blur halo", [] {
        auto source = fixture(true);
        const auto descriptions = fx::chain({fx::blur(4, 4), fx::tone({0, 0, 1, 1}, {0, 0, 1, 1})});
        const auto effects = PreparedEffects::prepare(descriptions); check(effects.has_value());
        const auto bound = PreparedVector::bind(source.plan, *effects); check(bound.has_value()); if (!bound) return;
        std::vector<float> values(effects->parameter_count()); check(effects->sample(0.5, values).has_value());
        GeometryBuffers geometry(source.plan); RecordBuffers buffers(*bound); render::DisplayList output;
        check(bound->record(geometry.sample(source.plan, 0.5), values, output, buffers.get()).has_value());
        const auto tone = render::effects().find("tone"), blur = render::effects().find("blur"); check(tone && blur);
        const auto* first = std::get_if<render::PushLayer>(&output.commands()[0]);
        const auto* second = std::get_if<render::PushLayer>(&output.commands()[1]);
        check(first && second && first->effect.id == tone->id && second->effect.id == blur->id);
        check(first->bounds.width > second->bounds.width && first->bounds.x < second->bounds.x);
        const auto pixels = draw(output);
        check(pixels.at(14, 25).a > 0, "outer tone retains blur output outside original x=16 boundary");
        check(pixels.at(25, 25).b > pixels.at(25, 25).r && pixels.at(25, 25).b > pixels.at(25, 25).g);
    });
    s.test("strict bind rejects unknown unsupported effects parameters and material resources", [] {
        auto source = fixture(true);
        auto effect_error = [&](EffectSpec description, VectorError error) {
            const std::array descriptions{std::move(description)};
            const auto effects = PreparedEffects::prepare(descriptions); check(effects.has_value());
            const auto bound = PreparedVector::bind(source.plan, *effects); check(!bound && bound.error().code == error);
        };
        effect_error({"not-a-real-effect", {}}, VectorError::unknown_effect);
        effect_error({"blur", {{"typo", 0, 1}}}, VectorError::unknown_parameter);
        effect_error(fx::blur(0, 201), VectorError::invalid_parameter);
        effect_error({"liquid_glass", {}}, VectorError::unsupported_effect);
        const auto solid = render::Paint::solid(geom::Color::white());
        std::array materials{MaterialBinding{"missing", solid, solid}};
        check(PreparedVector::bind(source.plan, {}, materials).error().code == VectorError::unknown_part);
        materials[0].part = "shape"; materials[0].to.kind = render::Paint::Kind::image;
        check(PreparedVector::bind(source.plan, {}, materials).error().code == VectorError::invalid_material);
        const std::array duplicates{MaterialBinding{"shape", solid, solid}, MaterialBinding{"shape", solid, solid}};
        check(PreparedVector::bind(source.plan, {}, duplicates).error().code == VectorError::duplicate_material);
        render::Paint bad_gradient; bad_gradient.kind = render::Paint::Kind::gradient;
        std::array<render::GradientStop, 2> bad_stops{{{0.8f, geom::Color::white()}, {0.2f, geom::Color::black()}}};
        bad_gradient.gradient.stops = bad_stops; materials[0].to = bad_gradient;
        check(PreparedVector::bind(source.plan, {}, materials).error().code == VectorError::invalid_material);
    });
    s.test("record failures preserve an existing destination and reject old preparation identity", [] {
        auto source = fixture(true);
        const auto bound = PreparedVector::bind(source.plan); check(bound.has_value()); if (!bound) return;
        GeometryBuffers geometry(source.plan); RecordBuffers buffers(*bound);
        auto frame = geometry.sample(source.plan, 0.5);
        render::DisplayList output; output.fill_rect({0, 0, 2, 2}, render::Paint::solid({0, 1, 0, 1}));
        const auto before = output.hash();
        auto scratch = buffers.get(); scratch.world = scratch.world.first(1);
        check(bound->record(frame, {}, output, scratch).error().code == VectorError::buffer_too_small && output.hash() == before);
        scratch = buffers.get(); scratch.recording = &output;
        check(bound->record(frame, {}, output, scratch).error().code == VectorError::recording_alias && output.hash() == before);
        frame.progress = std::numeric_limits<double>::quiet_NaN();
        check(bound->record(frame, {}, output, buffers.get()).error().code == VectorError::invalid_progress && output.hash() == before);
        frame = geometry.sample(source.plan, 0.5);
        geometry.parts[0].local.tx = std::numeric_limits<double>::max();
        check(bound->record(frame, {}, output, buffers.get()).error().code == VectorError::invalid_geometry && output.hash() == before);
        frame = geometry.sample(source.plan, 0.5); geometry.cubics[0].end.x += 1;
        check(bound->record(frame, {}, output, buffers.get()).error().code == VectorError::invalid_geometry && output.hash() == before);
        const auto other = PreparedObjectMorph::prepare(source.from, source.to); check(other.has_value());
        GeometryBuffers other_geometry(*other); const auto other_frame = other_geometry.sample(*other, 0.5);
        check(bound->record(other_frame, {}, output, buffers.get()).error().code == VectorError::foreign_frame && output.hash() == before,
            "same ids, revisions and counts do not authorize an old renderer binding for a new prepared plan");
    });
    s.test("sampled ranges and effect reload generations are checked before recording", [] {
        auto source = fixture(true);
        const auto descriptions = fx::chain({fx::blur(0, 4)}); const auto effects = PreparedEffects::prepare(descriptions);
        const auto bound = PreparedVector::bind(source.plan, *effects); check(bound.has_value()); if (!bound) return;
        GeometryBuffers geometry(source.plan); RecordBuffers buffers(*bound); const auto frame = geometry.sample(source.plan, 0.5);
        render::DisplayList output; output.fill_rect({0, 0, 2, 2}, render::Paint::solid({0, 1, 0, 1})); const auto before = output.hash();
        std::array<float, 1> values{std::numeric_limits<float>::quiet_NaN()};
        check(bound->record(frame, values, output, buffers.get()).error().code == VectorError::invalid_parameter && output.hash() == before);
        values[0] = 201;
        check(bound->record(frame, values, output, buffers.get()).error().code == VectorError::invalid_parameter && output.hash() == before);
        values[0] = 2;
        const auto ref = render::effects().find("blur"); check(ref.has_value());
        const auto original = *render::effects().info(*ref);
        check(render::effects().replace(*ref, original));
        const auto failed = bound->record(frame, values, output, buffers.get());
        check(render::effects().replace(*ref, original));
        check(!failed && failed.error().code == VectorError::stale_effect && output.hash() == before);
    });
    return s.run(argc, argv);
}
