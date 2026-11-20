// Text as the renderer draws it: mlibs.text's paragraphs painted and its
// glyphs rasterised by xrender.text, pixels pinned for every platform (the
// hashes these tests had in mlibs.text, before the renderer's side left it).
import std;
import mlibs.geom;
import mlibs.text;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;
import xrender.raster;
import xrender.vector;
import xrender.text;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace text = mlibs::text;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;

namespace {

text::FontCollection& fonts() {
    static text::FontCollection f = [] {
        text::FontCollection c;
        auto r = c.add_defaults();
        if (!r) std::println("fonts: {}", r.error());
        return c;
    }();
    return f;
}

text::ParagraphStyle style(float size = 16) {
    text::ParagraphStyle p;
    p.style.size = size;
    return p;
}

}  // namespace

UTest cases{"xrender.text", [] {

    "pixels: mixed scripts and emoji, pinned"_test = [] {
        auto st = style(18);
        st.style.color = geom::Color::hex(0x0f172a);
        std::vector<text::StyledRange> ranges{{6, 13, {}}};
        ranges[0].style.size = 18;
        ranges[0].style.weight = 700;
        ranges[0].style.color = geom::Color::hex(0x4f46e5);
        text::Paragraph p(fonts(), "Hello openmui! 统一的语义层 مرحبا \U0001F680\U0001F3A8", st, ranges);
        p.layout(320);
        render::DisplayList dl;
        xrender::text::paint(p, dl, {10, 10});
        xrender::text::GlyphCache glyphs(fonts());
        raster::Surface surf(340, 30 + static_cast<int>(p.size().height));
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, surf, {nullptr, &glyphs}, {.background = geom::Color::white()});
        expect(eq(report.missing_glyphs, std::size_t{0}));
        auto dir = scratch_dir("text");
        mlibs::image::View v{reinterpret_cast<const std::uint8_t*>(surf.pixels().data()), surf.width(), surf.height(),
                            surf.width() * 4};
        expect(mlibs::image::save_png(dir / "mixed.png", v).has_value());
        std::println("    {:016x} ({})", surf.hash(), (dir / "mixed.png").generic_string());
        expect(eq(std::format("{:016x}", surf.hash()), std::string("09abccfc87e4202b")));
    };

    "per-glyph styles: translation, opacity, tint, rotation (D-68)"_test = [] {
        auto st = style(20);
        st.style.color = geom::Color::hex(0x0f172a);
        text::Paragraph p(fonts(), "Animate me \U0001F680", st);
        p.layout(400);
        xrender::text::GlyphCache glyphs(fonts());
        auto draw = [&](auto&& record) {
            render::DisplayList dl;
            record(dl);
            raster::Surface surf(220, 50);
            render::CpuRenderer cpu;
            (void)cpu.render(dl, surf, {nullptr, &glyphs}, {.background = geom::Color::white()});
            return surf;
        };
        // Styles on every glyph of every run of the painted paragraph.
        auto styled = [&](render::DisplayList& dl, auto&& style_of) {
            xrender::text::paint(p, dl, {10, 10});
            for (std::size_t i = 0; i < dl.commands().size(); ++i)
                if (const auto* g = std::get_if<render::DrawGlyphs>(&dl.commands()[i])) {
                    std::vector<render::GlyphStyle> v;
                    for (std::size_t k = 0; k < g->glyphs.size(); ++k) v.push_back(style_of(g->glyphs[k]));
                    dl.style_glyphs(i, v);
                }
        };
        const auto moved = draw([&](auto& dl) { styled(dl, [](const render::Glyph&) { return render::GlyphStyle{.transform = geom::Affine::translate(3, 2)}; }); });
        const auto shifted = draw([&](auto& dl) { xrender::text::paint(p, dl, {13, 12}); });
        expect(moved.hash() == shifted.hash()) << "a translated glyph is the glyph drawn there, byte for byte";
        const auto hidden = draw([&](auto& dl) { styled(dl, [](const render::Glyph&) { return render::GlyphStyle{.opacity = 0}; }); });
        const auto blank = draw([](auto&) {});
        expect(hidden.hash() == blank.hash()) << "opacity 0 draws nothing";
        const auto tinted = draw([&](auto& dl) {
            styled(dl, [](const render::Glyph& g) {
                return g.grapheme < 7 ? render::GlyphStyle{.tint = geom::Color::hex(0xdc2626)} : render::GlyphStyle{};
            });
        });
        auto red = st;
        text::Paragraph p2(fonts(), "Animate me \U0001F680", red, {{0, 7, [&] { auto x = red.style; x.color = geom::Color::hex(0xdc2626); return x; }()}});
        p2.layout(400);
        const auto colored = draw([&](auto& dl) { xrender::text::paint(p2, dl, {10, 10}); });
        expect(tinted.hash() == colored.hash()) << "a full tint is the colour";
        // Rotation about each glyph's origin: outlines (and the emoji's image under the transform).
        const auto turned = draw([&](auto& dl) {
            styled(dl, [](const render::Glyph& g) {
                const float a = 0.2f * static_cast<float>(g.grapheme % 3) - 0.2f;
                return render::GlyphStyle{.transform = geom::Affine::rotation(1 - a * a / 2, a), .opacity = 0.85f};
            });
        });
        expect(turned.hash() != moved.hash() && turned.hash() != blank.hash());
        std::println("    rotated {:016x}", turned.hash());
        {
            mlibs::image::View v{reinterpret_cast<const std::uint8_t*>(turned.pixels().data()), turned.width(), turned.height(), turned.width() * 4};
            expect(mlibs::image::save_png(scratch_dir("text") / "glyph-styles.png", v).has_value());
        }
        // Pinned for every platform: rotated glyphs go through the fixed-point rasterizer.
        expect(eq(std::format("{:016x}", turned.hash()), std::string("dd0a611f25d4ddce")));
        // The styled command's bounds hold every pixel it drew (damage stays exact).
        render::DisplayList dl;
        styled(dl, [](const render::Glyph& g) {
            return render::GlyphStyle{.transform = geom::Affine::translate(0, static_cast<float>(g.grapheme % 4) * 3) * geom::Affine::rotation(0.8f, 0.6f)};
        });
        std::optional<geom::Rect> box;
        for (const auto& c : dl.commands())
            if (auto b = render::device_bounds(c, geom::Affine{}, &glyphs)) box = box ? box->united(*b) : *b;
        const auto surf = draw([&](auto& d) { d.append(dl); });
        bool inside = true;
        for (int y = 0; y < surf.height(); ++y)
            for (int x = 0; x < surf.width(); ++x)
                if (!(surf.at(x, y) == blank.at(0, 0)) && box &&
                    !box->contains(geom::Point{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}))
                    inside = false;
        expect(box.has_value() && inside) << "every drawn pixel is inside the command's bounds";
        expect(render::hash_command(dl.commands().back(), 0) != 0);
    };

    "a collection without fonts draws nothing instead of failing"_test = [] {
        text::FontCollection none;
        expect(none.add_defaults("/no/fonts/here").has_value() == false);
        text::Paragraph p(none, "Hello 统一 مرحبا \U0001F680", style());
        p.layout(200);
        render::DisplayList dl;
        xrender::text::paint(p, dl, {0, 0});
        xrender::text::GlyphCache glyphs(none);
        raster::Surface surf(64, 32);
        render::CpuRenderer cpu;
        (void)cpu.render(dl, surf, {nullptr, &glyphs}, {.background = geom::Color::white()});
        expect(std::ranges::all_of(surf.pixels(), [&](auto px) { return px == surf.pixels().front(); })) << "nothing drawn";
    };
}};
