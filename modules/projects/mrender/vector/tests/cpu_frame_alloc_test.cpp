// A frame drawn by the CPU renderer allocates nothing once it has been drawn before (AGENTS §4: no allocation a
// frame on a hot path): every kind of command the renderer draws — rectangles, rounded ones and their borders,
// shadows outside and inset, paths filled and stroked (dashed, trimmed), gradients, mixtures, images, glyphs from
// masks and from outlines, clips of every shape, layers and blend modes, effects (a folded fade, a fade, a blur),
// transforms, nodes with their object ids, a host's texture — whole, in bands at the same time, and as the rows a
// damage tracker reports. Its own program: it counts every allocation by replacing `operator new`.
import std;
import mlibs.geom;
import mlibs.memory;
import mrender.raster;
import mrender.vector;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render = mrender;
namespace raster = mrender::raster;
namespace geom = mlibs::geom;

namespace {
std::atomic<std::uint64_t> allocations{0};
}

void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    std::abort();  // core code uses no exceptions; running out of memory ends the run
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

constexpr int width = 640, height = 400;

// Glyphs as a text engine gives them: a coverage mask a glyph (made once, cached behind the source) and an outline
// in ems for the transforms masks cannot express.
class Glyphs final : public render::GlyphSource {
public:
    Glyphs() {
        for (std::size_t glyph = 0; glyph < count; ++glyph) {
            raster::PathBuilder& outline = outlines_[glyph];
            const float wide = 0.3f + 0.05f * static_cast<float>(glyph);
            outline.move_to({0.05f, -0.7f}).line_to({wide, -0.7f}).line_to({wide + 0.1f, 0}).line_to({0, 0}).close();
            raster::fill_coverage(outline.data(), geom::Affine::scale(14, 14), raster::FillRule::nonzero, {-64, -64, 128, 128}, masks_[glyph]);
        }
    }
    render::GlyphMask mask(render::FontRef, std::uint32_t glyph, float, geom::Point) override { return {&masks_[glyph % count], nullptr, {}}; }
    raster::PathData outline(render::FontRef, std::uint32_t glyph) override { return outlines_[glyph % count].data(); }

private:
    static constexpr std::size_t count = 8;
    std::array<raster::Coverage, count> masks_;
    std::array<raster::PathBuilder, count> outlines_;
};

struct Fixture {
    Glyphs glyphs;
    render::ImageStore images;
    render::ImageRef picture;
    raster::Surface texture{24, 24};
    std::optional<render::PreparedPaintMix> mixture;
    std::vector<float> blur, fade;
    std::array<render::GradientStop, 3> stops{{{0, geom::Color::hex(0x6366f1)}, {0.5f, geom::Color::hex(0xf59e0b, 0.6f)},
                                               {1, geom::Color::hex(0x10b981)}}};
    std::array<float, 2> dashes{6, 3};
    std::array<render::Glyph, 12> run{};
    std::array<render::GlyphStyle, 12> styles{};

    Fixture() {
        std::vector<raster::Rgba8> pixels(32 * 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                pixels[static_cast<std::size_t>(y * 32 + x)] = {static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(y * 8), 128, 255};
        picture = images.add(32, 32, std::move(pixels));
        texture.clear({20, 120, 220, 255});
        render::Paint gradient;
        gradient.kind = render::Paint::Kind::gradient;
        gradient.gradient.start = {0, 0};
        gradient.gradient.end = {80, 40};
        gradient.gradient.stops = stops;
        if (auto plan = render::PreparedPaintMix::prepare(render::Paint::solid(geom::Color::hex(0xef4444, 0.8f)), gradient)) mixture.emplace(std::move(*plan));
        blur = render::effects().params(*render::effects().find("blur"), std::array{std::pair<std::string_view, float>{"radius", 4}});
        fade = render::effects().params(*render::effects().find("opacity"), std::array{std::pair<std::string_view, float>{"amount", 0.6f}});
        for (std::size_t i = 0; i < run.size(); ++i) {
            run[i] = {static_cast<std::uint32_t>(i), {static_cast<float>(i) * 9.5f, 0}};
            styles[i].opacity = i % 3 == 0 ? 0.5f : 1.0f;
            styles[i].tint = i % 4 == 1 ? geom::Color{1, 0, 0, 0.5f} : geom::Color{0, 0, 0, 0};
            if (i % 5 == 2) styles[i].transform = geom::Affine::translate(0, -2) * render::rotate(0.2f);
        }
    }
};

render::Paint gradient_of(const Fixture& fixture, geom::Point from, geom::Point to) {
    render::Paint paint;
    paint.kind = render::Paint::Kind::gradient;
    paint.gradient.start = from;
    paint.gradient.end = to;
    paint.gradient.stops = fixture.stops;
    return paint;
}

render::Path star(geom::Point centre, float outer, float inner) {
    render::Path path;
    for (int corner = 0; corner < 10; ++corner) {
        const float reach = corner % 2 ? inner : outer;
        const float angle = static_cast<float>(corner) * 0.6283185f;
        const geom::Point point{centre.x + reach * std::sin(angle), centre.y - reach * std::cos(angle)};
        if (corner == 0) path.move_to(point);
        else path.line_to(point);
    }
    path.close();
    return path;
}

// The parts of a frame, each a function of the frame's number (things move, so every frame is drawn again).
using Part = void (*)(render::DisplayList&, const Fixture&, int);

void rectangles(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 7) * 0.37f;
    list.fill_rect({10, 10, 120, 40}, geom::Color::hex(0x0ea5e9));
    list.fill_rect({20.5f + shift, 60.25f, 90.5f, 30.5f}, geom::Color::hex(0x22c55e, 0.7f));
    list.fill_rect({140, 10, 100, 80}, gradient_of(fixture, {140, 10}, {240, 90}));
    list.push_transform(geom::Affine::translate(300, 50) * render::rotate(0.3f + shift * 0.1f));
    list.fill_rect({-30, -20, 60, 40}, geom::Color::hex(0xa855f7));
    list.pop_transform();
    if (fixture.mixture)
        if (const auto mixed = fixture.mixture->record(0.25 + 0.05 * (frame % 10), list)) list.fill_rect({360, 10, 80, 50}, *mixed);
}

void rounded(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 5) * 0.5f;
    list.fill_round_rect({10 + shift, 110, 140, 60}, geom::CornerRadii::all(12), render::Paint::solid(geom::Color::white()));
    list.fill_round_rect({160, 110, 120, 60}, geom::CornerRadii::all(30), gradient_of(fixture, {160, 110}, {280, 170}));
    list.stroke_round_rect({10.5f, 110.5f, 140, 60}, geom::CornerRadii::all(12), 1, render::Paint::solid(geom::Color::hex(0xcbd5e1)));
    list.stroke_round_rect({290, 110, 100 + shift, 50}, geom::CornerRadii::all(8), 3, gradient_of(fixture, {290, 110}, {390, 160}));
}

void shadows(render::DisplayList& list, const Fixture&, int frame) {
    const float blur = 8 + static_cast<float>(frame % 3);
    list.box_shadow({420, 110, 100, 60}, geom::CornerRadii::all(10), {0, 4}, blur, 0, {0, 0, 0, 0.3f});
    list.fill_round_rect({420, 110, 100, 60}, geom::CornerRadii::all(10), render::Paint::solid(geom::Color::white()));
    list.box_shadow({540, 110, 80, 60}, geom::CornerRadii::all(6), {2, 2}, blur / 2, 1, {0, 0, 0.2f, 0.5f}, true);
}

void paths(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float turn = static_cast<float>(frame % 9) * 0.7f;
    list.fill_path(star({60, 240}, 45 + turn, 20), render::FillRule::nonzero, render::Paint::solid(geom::Color::hex(0xf43f5e)));
    list.fill_path(star({170, 240}, 45, 25 + turn), render::FillRule::even_odd, gradient_of(fixture, {120, 200}, {220, 280}));
    render::Path curve;
    curve.move_to({230, 200}).cubic_to({280 + turn, 180}, {300, 300}, {360, 260}).line_to({380, 210});
    list.stroke_path(curve, {.width = 4, .cap = render::LineCap::round, .join = render::LineJoin::round},
                     render::Paint::solid(geom::Color::hex(0x0f172a, 0.8f)));
    list.stroke_path(curve, {.width = 2, .dashes = fixture.dashes, .dash_offset = turn}, render::Paint::solid(geom::Color::hex(0xfacc15)));
    render::Path loop = star({440, 240}, 40, 30);
    list.stroke_path(loop, {.width = 3, .join = render::LineJoin::miter, .trim_start = 0.1f, .trim_end = 0.8f, .trim_offset = turn * 0.05f},
                     render::Paint::solid(geom::Color::hex(0x14b8a6)));
}

void images(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 4);
    list.draw_image(fixture.picture, {0, 0, 32, 32}, {500 + shift, 200, 64, 64});
    list.draw_image(fixture.picture, {4, 4, 24, 24}, {580, 200, 48, 48}, render::Sampling::nearest, 0.8f);
    list.push_transform(geom::Affine::translate(560, 300) * render::rotate(0.4f));
    list.draw_image(fixture.picture, {0, 0, 32, 32}, {-20, -20, 40, 40});
    list.pop_transform();
    list.external_texture(7, {500, 320, 48, 48}, 0.9f);
}

void text(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 6) * 0.25f;
    list.draw_glyphs({1}, 14, {20 + shift, 320}, fixture.run, render::Paint::solid(geom::Color::hex(0x111827)));
    list.draw_glyphs({1}, 14, {20, 345}, fixture.run, render::Paint::solid(geom::Color::hex(0x2563eb)), fixture.styles);
    list.draw_glyphs({1}, 14, {150, 320}, fixture.run, gradient_of(fixture, {150, 300}, {260, 330}), fixture.styles);
    list.push_transform(geom::Affine::translate(160, 370) * render::rotate(-0.15f));
    list.draw_glyphs({1}, 14, {0, 0}, fixture.run, render::Paint::solid(geom::Color::hex(0x7c3aed)));
    list.pop_transform();
}

void clips(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 3) * 0.3f;
    list.push_clip({280, 300, 100, 80});
    list.fill_rect({270, 290, 120, 100}, geom::Color::hex(0xfde68a));
    list.push_clip({290.5f + shift, 310.5f, 60, 50});
    list.fill_round_rect({280, 300, 100, 80}, geom::CornerRadii::all(20), render::Paint::solid(geom::Color::hex(0xf97316, 0.8f)));
    list.pop_clip();
    list.push_clip({300, 320, 70, 50}, geom::CornerRadii::all(14));
    list.fill_rect({280, 300, 100, 80}, gradient_of(fixture, {280, 300}, {380, 380}));
    list.pop_clip();
    list.push_clip(star({330, 340}, 30, 15));
    list.fill_rect({280, 300, 100, 80}, geom::Color::hex(0x0284c7, 0.6f));
    list.pop_clip();
    list.pop_clip();
}

void layers(render::DisplayList& list, const Fixture&, int frame) {
    const float shift = static_cast<float>(frame % 4) * 0.5f;
    list.push_layer(0.7f);
    list.fill_round_rect({390, 300, 60, 40}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0xef4444)));
    list.fill_rect({410 + shift, 320, 60, 40}, geom::Color::hex(0x22c55e));
    list.pop_layer();
    list.push_layer(1, render::BlendMode::multiply);
    list.fill_rect({400, 350, 60, 30}, geom::Color::hex(0x6366f1));
    list.pop_layer();
}

void effects(render::DisplayList& list, const Fixture& fixture, int frame) {
    const float shift = static_cast<float>(frame % 4);
    const auto blur = *render::effects().find("blur");
    const auto fade = *render::effects().find("opacity");
    // One fill under a fade: folded into its alpha.
    list.push_layer(1, render::BlendMode::normal, fade, fixture.fade, {560, 10, 60, 40});
    list.fill_rect({560, 10, 60, 40}, geom::Color::hex(0x0891b2));
    list.pop_layer();
    // Two: a layer.
    list.push_layer(1, render::BlendMode::normal, fade, fixture.fade, {450, 20, 100, 50});
    list.fill_rect({450, 20, 60, 40}, geom::Color::hex(0xdb2777));
    list.fill_round_rect({480 + shift, 30, 60, 40}, geom::CornerRadii::all(8), render::Paint::solid(geom::Color::hex(0x65a30d)));
    list.pop_layer();
    list.push_layer(1, render::BlendMode::normal, blur, fixture.blur, {560, 60, 70, 40});
    list.fill_round_rect({565 + shift, 65, 60, 30}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0x1d4ed8)));
    list.pop_layer();
}

constexpr std::array<std::pair<std::string_view, Part>, 9> parts{{{"rectangles", rectangles}, {"rounded", rounded}, {"shadows", shadows},
                                                                  {"paths", paths}, {"images", images}, {"text", text}, {"clips", clips},
                                                                  {"layers", layers}, {"effects", effects}}};

// The frame: the parts asked for, each inside a node of its own (the object ids are drawn too).
void record(render::DisplayList& list, const Fixture& fixture, int frame, std::span<const Part> chosen) {
    list.clear();
    list.fill_rect({0, 0, width, height}, geom::Color::hex(0xf8fafc));
    std::uint32_t node = 1;
    for (const Part part : chosen) {
        list.begin_node(node++);
        part(list, fixture, frame);
        list.end_node();
    }
}

// Bands drawn one after another on the calling thread: what a host's pool runs, without its threads.
struct InOrder final : render::Parallel {
    void run(std::size_t count, mlibs::memory::FunctionRef<void(std::size_t)> body) override {
        for (std::size_t band = 0; band < count; ++band) body(band);
    }
};

// The same frame drawn whole again; frames that move, drawn whole, in bands (on the calling thread), and as the rows
// a damage tracker reports.
enum class Way { same, whole, bands, damage };
std::string_view name_of(Way way) { return way == Way::same ? "the same, whole" : way == Way::whole ? "whole" : way == Way::bands ? "bands" : "damage"; }

// Allocations in drawing `frames` frames of the chosen parts (the damage tracked and the pixels drawn; the list's
// recording is the producer's, not counted), after 64 drawn to warm up.
std::uint64_t allocations_drawing(Fixture& fixture, std::span<const Part> chosen, Way way, int frames = 100) {
    render::DisplayList list;
    render::CpuRenderer renderer;
    render::DamageTracker damage;
    raster::Surface surface(width, height);
    InOrder in_order;
    const render::Resources resources{&fixture.images, &fixture.glyphs};
    render::CpuOptions options;
    options.object_ids = true;
    options.background = geom::Color::white();
    options.external = [&fixture](std::uint64_t id) -> std::optional<raster::ImageView> {
        if (id != 7) return std::nullopt;
        return fixture.texture.view();
    };
    if (way == Way::bands) {
        options.parallel = &in_order;
        options.band_work = 1u << 14;  // many bands: every band its own canvas's turn
    }
    const auto draw = [&](int frame) {
        record(list, fixture, frame, chosen);
        const std::uint64_t before = allocations.load(std::memory_order_relaxed);
        if (way == Way::damage) options.rows = damage.update(list, width, height, 1, resources);
        if (way != Way::damage || !options.rows.empty()) (void)renderer.render(list, surface, resources, options);
        return allocations.load(std::memory_order_relaxed) - before;
    };
    // Warm: the parts move through a few positions each (at most nine), and a buffer only grows to the largest.
    for (int frame = 0; frame < 64; ++frame) (void)draw(frame);
    std::uint64_t made = 0;
    for (int frame = 0; frame < frames; ++frame) made += draw(way == Way::same ? 0 : 64 + frame);
    return made;
}

}  // namespace

static UTest cases{"render.cpu-frame-alloc", [] {
    "every kind of command, drawn again, allocates nothing"_test = [] {
        Fixture fixture;
        for (const auto& [name, part] : parts) {
            const std::array<Part, 1> alone{part};
            for (const Way way : {Way::whole, Way::bands, Way::damage}) {
                const std::uint64_t made = allocations_drawing(fixture, alone, way);
                expect(eq(made, std::uint64_t{0})) << std::format("{} ({}): {} allocations in 100 frames", name, name_of(way), made);
            }
        }
    };
    "a full frame of them all, 100 times after warming up: no allocation, the same frame or moving, whole, in bands, by damage"_test = [] {
        Fixture fixture;
        std::vector<Part> all;
        for (const auto& [name, part] : parts) all.push_back(part);
        for (const Way way : {Way::same, Way::whole, Way::bands, Way::damage}) {
            const std::uint64_t made = allocations_drawing(fixture, all, way);
            expect(eq(made, std::uint64_t{0})) << std::format("the full frame ({}): {} allocations in 100 frames", name_of(way), made);
        }
    };
}};
