// A display list as bytes and back (W16, D-154): every kind of command read
// back exactly — the same hash, the same bytes again, the same pixels — and a
// damaged list appends nothing.
import std;
import xrender.vector;
import xrender.raster;
import mlibs.geom;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render = xrender;
namespace geom = mlibs::geom;

namespace {

// Something of everything a frame can hold.
void everything(render::DisplayList& list) {
    list.begin_node(7);
    list.node_frame(7, {10, 10, 200, 120}, 0.9f, {8, 8, 204, 124}, {200, 120}, geom::Affine::translate(1, 2), geom::Point{3, 4});
    list.fill_rect({0, 0, 320, 240}, geom::Color::hex(0xf8fafc));
    list.fill_round_rect({10, 10, 200, 120}, geom::CornerRadii{12, 12, 12, 12, 0.6f}, render::Paint::solid(geom::Color::hex(0x4f46e5, 0.8f)));
    list.stroke_round_rect({20, 20, 80, 40}, geom::CornerRadii::all(6), 2, render::Paint::solid(geom::Color::hex(0x0f172a)));
    list.box_shadow({30, 30, 60, 30}, geom::CornerRadii::all(8), {0, 4}, 10, -2, geom::Color::hex(0x000000, 0.2f));
    render::Path path;
    path.move_to({40, 150}).cubic_to({60, 110}, {120, 190}, {160, 150}).line_to({160, 200}).close();
    std::array<render::GradientStop, 3> stops{{{0, geom::Color::hex(0xff0000)}, {0.5f, geom::Color::hex(0x00ff00)}, {1, geom::Color::hex(0x0000ff)}}};
    render::Paint gradient;
    gradient.kind = render::Paint::Kind::gradient;
    gradient.gradient.kind = render::GradientKind::radial;
    gradient.gradient.start = {100, 160};
    gradient.gradient.end = {110, 170};
    gradient.gradient.radius = 50;
    gradient.gradient.spread = render::Spread::reflect;
    gradient.gradient.stops = stops;
    list.fill_path(path, geom::FillRule::even_odd, gradient);
    const std::array<float, 2> dashes{6, 3};
    list.stroke_path(path, {.width = 3, .cap = render::LineCap::round, .join = render::LineJoin::bevel, .dashes = dashes, .dash_offset = 1.5f, .trim_end = 0.8f},
                     render::Paint::solid(geom::Color::hex(0x16a34a)));
    list.push_clip({0, 0, 300, 220}, geom::CornerRadii::all(4));
    list.push_clip(path, geom::FillRule::nonzero);
    list.push_transform(geom::Affine::scale(1.5f, 1.5f));
    const std::array<float, 1> params{0.25f};
    const std::array<render::EffectShape, 1> shapes{{{{5, 5, 50, 50}, geom::CornerRadii::all(3)}}};
    list.push_layer(0.7f, render::BlendMode::multiply, {3}, params, {0, 0, 100, 100}, shapes, render::LayerBoundsMode::content);
    const std::array<render::Glyph, 2> glyphs{{{36, {0, 0}, 0, 0, 0, 0}, {37, {9.5f, 0}, 1, 1, 0, 0}}};
    list.draw_glyphs({1}, 16, {50, 60}, glyphs, render::Paint::solid(geom::Color::hex(0x0f172a)));
    list.style_glyphs(list.size() - 1, std::array<render::GlyphStyle, 2>{{{geom::Affine::translate(0, -2), 0.5f, {1, 0, 0, 0.3f}}, {}}});
    list.draw_image({4}, {0, 0, 16, 16}, {60, 60, 32, 32}, render::Sampling::nearest, 0.5f);
    // A lit mesh: image paints with a material.
    const std::array<geom::Point, 3> positions{{{0, 0}, {40, 0}, {0, 40}}}, uvs{{{0, 0}, {1, 0}, {0, 1}}};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    const render::Material material{.normal = {5}, .bands = 3, .rim_strength = 0.4f};
    list.draw_mesh({4}, positions, uvs, indices, render::Sampling::linear, 0.9f, 0.5f, &material);
    list.pop_layer();
    list.pop_transform();
    list.pop_clip();
    list.pop_clip();
    list.external_texture(99, {0, 0, 10, 10}, 0.3f);
    list.reference_box(7, "thumb", {5, 6, 7, 8});
    const std::array<render::ReferenceWeight, 1> lineage{{{"card", 0.5}}};
    list.begin_part({.owner = 7, .role = "thumb", .id = "t1", .allocation = {1, 2, 3, 4}, .anchor = {0.25f, 0.75f}, .kind = render::PartKind::box, .lineage = lineage});
    list.end_part();
    list.end_node();
}

}  // namespace

static UTest cases{"render.wire", [] {
    "every kind of command, read back exactly: the same hash, the same bytes written again, a list as balanced"_test = [] {
        render::DisplayList list;
        everything(list);
        const std::string bytes = render::encode(list);
        render::DisplayList again;
        const auto read = render::decode(bytes, again);
        expect(read.has_value()) << (read ? std::string() : read.error());
        expect(eq(again.size(), list.size()) && eq(again.hash(), list.hash()) && again.balanced());
        expect(render::encode(again) == bytes) << "written again, the same bytes (the markers the hash leaves out too)";
        std::set<std::size_t> kinds;
        for (const auto& command : again.commands()) kinds.insert(command.index());
        expect(eq(kinds.size(), std::variant_size_v<render::Command>)) << std::format("{} of {} kinds exercised", kinds.size(), std::variant_size_v<render::Command>);
    };

    "drawn from its bytes: the same pixels"_test = [] {
        render::DisplayList list;
        list.fill_rect({0, 0, 64, 64}, geom::Color::hex(0xffffff));
        render::Path path;
        path.move_to({4, 50}).cubic_to({20, 0}, {44, 70}, {60, 10});
        list.stroke_path(path, {.width = 5, .cap = render::LineCap::round}, render::Paint::solid(geom::Color::hex(0x4f46e5, 0.7f)));
        list.fill_round_rect({10, 10, 30, 20}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0xdc2626, 0.5f)));
        render::DisplayList again;
        expect(render::decode(render::encode(list), again).has_value());
        xrender::raster::Surface first(64, 64), second(64, 64);
        render::CpuRenderer renderer;
        (void)renderer.render(list, first, {});
        (void)renderer.render(again, second, {});
        expect(std::ranges::equal(first.bytes(), second.bytes()));
    };

    "a damaged list: refused with where, nothing appended"_test = [] {
        render::DisplayList list;
        everything(list);
        const std::string bytes = render::encode(list);
        render::DisplayList into;
        expect(!render::decode(bytes.substr(0, bytes.size() / 2), into).has_value() && into.empty()) << "cut short";
        expect(!render::decode("nope", into).has_value() && into.empty());
        std::string wrong = bytes;
        wrong[4] = 9;  // another version
        const auto refused = render::decode(wrong, into);
        expect(!refused && refused.error().contains("version") && into.empty());
    };
}};
