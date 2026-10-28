// Pixel effects (D-68): the registry, every built-in's pixels pinned for every
// platform, progress 0 being the layer as it is, banded and incremental
// rendering being the whole render byte for byte, an effect reaching outside
// its layer, and an effect a program registers itself.
import std;
import openmui.geom;
import openmui.memory;
import openmui.raster;
import openmui.render;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {

constexpr int W = 240, H = 170;
constexpr geom::Rect layer{50, 35, 140, 100};

// A colourful node: a gradient card, a ring and a dot, inside the layer.
void content(render::DisplayList& dl) {
    static const std::array<render::GradientStop, 3> stops{
        {{0, geom::Color::hex(0x4f46e5)}, {0.5f, geom::Color::hex(0xdb2777)}, {1, geom::Color::hex(0xf59e0b)}}};
    render::Paint g;
    g.kind = render::Paint::Kind::gradient;
    g.gradient.start = {layer.x, layer.y};
    g.gradient.end = {layer.right(), layer.bottom()};
    g.gradient.stops = stops;
    dl.fill_round_rect(layer, geom::CornerRadii::all(14), g);
    dl.stroke_round_rect({layer.x + 30, layer.y + 20, 60, 60}, geom::CornerRadii::all(30), 6, render::Paint::solid(geom::Color::white()));
    dl.fill_round_rect({layer.x + 52, layer.y + 42, 16, 16}, geom::CornerRadii::all(8), render::Paint::solid(geom::Color::hex(0x0f172a)));
}

void scene(render::DisplayList& dl, std::optional<std::pair<std::string_view, std::vector<std::pair<std::string_view, float>>>> fx) {
    dl.clear();
    dl.begin_node(1);
    dl.fill_rect({0, 0, W, H}, geom::Color::hex(0xf8fafc));
    dl.begin_node(2);
    if (fx) {
        const auto ref = render::effects().find(fx->first);
        const auto params = render::effects().params(*ref, fx->second);
        dl.push_layer(1, render::BlendMode::normal, *ref, params, layer);
    }
    content(dl);
    if (fx) dl.pop_layer();
    dl.end_node();
    dl.end_node();
}

raster::Surface draw(const render::DisplayList& dl, const render::CpuOptions& o = {}) {
    raster::Surface s(W, H);
    render::CpuRenderer r;
    render::CpuOptions opts = o;
    opts.background = geom::Color::white();
    (void)r.render(dl, s, {}, opts);
    return s;
}

struct Threads final : render::Parallel {
    void run(std::size_t count, omui::FunctionRef<void(std::size_t)> body) override {
        std::vector<std::jthread> threads;
        for (std::size_t i = 0; i < count; ++i) threads.emplace_back([&body, i] { body(i); });
    }
};

void save(const raster::Surface& s, std::string_view name) {
    omui::imageio::View v{reinterpret_cast<const std::uint8_t*>(s.pixels().data()), s.width(), s.height(), s.width() * 4};
    static const auto dir = scratch_dir("render-effects");  // once: the call empties the directory
    (void)omui::imageio::save_png(dir / std::format("{}.png", name), v);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.effects"};

    s.test("the registry: built-ins in a fixed order, parameters with defaults and limits", [] {
        const std::vector<std::string_view> expected{"dissolve", "wipe", "reveal", "displace", "noise", "chromatic", "blur", "pixelate", "particles"};
        const auto names = render::effects().names();
        check(names.size() >= expected.size() && std::equal(expected.begin(), expected.end(), names.begin()), "built-ins first, in order");
        const auto ref = render::effects().find("dissolve");
        check(ref && ref->id == 1, "refs are the same in every process");
        const std::array<std::pair<std::string_view, float>, 2> v{{{"progress", 2.0f}, {"grain", 4}}};
        const auto p = render::effects().params(*ref, v);
        check(p.size() == 4 && p[0] == 1.0f && p[3] == 4.0f && p[2] == 0.15f, "clamped, given and default");
        check(!render::effects().find("nope"));
    });

    s.test("every built-in, pinned on every platform", [] {
        render::DisplayList dl;
        scene(dl, std::nullopt);
        const auto plain = draw(dl);
        save(plain, "plain");
        using P = std::vector<std::pair<std::string_view, float>>;
        const std::vector<std::pair<std::string_view, P>> cases{
            {"dissolve", {{"progress", 0.45f}, {"seed", 3}, {"grain", 3}}},
            {"wipe", {{"progress", 0.4f}, {"angle", 30}}},
            {"reveal", {{"progress", 0.55f}, {"x", 0.3f}, {"y", 0.4f}}},
            {"displace", {{"amount", 6}, {"wavelength", 30}, {"phase", 0.25f}}},
            {"noise", {{"amount", 0.3f}, {"seed", 9}}},
            {"chromatic", {{"offset", 3}, {"angle", 20}}},
            {"blur", {{"radius", 6}}},
            {"pixelate", {{"size", 8}}},
            {"particles", {{"progress", 0.4f}, {"seed", 5}, {"spread", 60}, {"gravity", 30}, {"size", 4}}},
        };
        Hash all;
        for (const auto& [name, params] : cases) {
            scene(dl, std::pair{name, params});
            const auto out = draw(dl);
            save(out, name);
            check(out.hash() != plain.hash(), std::format("{} changes the layer", name));
            std::println("    {:<10} {:016x}", name, out.hash());
            all.u64(out.hash());
        }
        // Pinned: GCC and LLVM, Linux, macOS and Windows compute these bytes.
        check_eq(all.hex(), std::string("9a97767fe1102700"));
    });

    s.test("liquid glass: the backdrop bent, frosted, tinted and lit under the glass; pinned", [] {
        // A striped backdrop; glass over its middle, a mark on the glass.
        auto build = [](render::DisplayList& dl, std::vector<std::pair<std::string_view, float>> values, std::vector<render::EffectShape> shapes,
                        bool mark = true) {
            dl.clear();
            dl.fill_rect({0, 0, W, H}, geom::Color::hex(0xf8fafc));
            for (int i = 0; i < 12; ++i)
                dl.fill_rect({static_cast<float>(i) * 20.0f, 0, 10, H}, geom::Color{0.1f + 0.07f * static_cast<float>(i), 0.35f, 0.85f - 0.06f * static_cast<float>(i), 1});
            const auto ref = render::effects().find("liquid_glass");
            if (!ref) return;
            const auto params = render::effects().params(*ref, values);
            dl.push_layer(1, render::BlendMode::normal, *ref, params, layer, shapes);
            if (mark) dl.fill_round_rect({layer.x + 60, layer.y + 40, 20, 20}, geom::CornerRadii::all(10), render::Paint::solid(geom::Color::hex(0x0f172a)));
            dl.pop_layer();
        };
        render::DisplayList dl;
        build(dl, {}, {{layer, geom::CornerRadii::all(28)}});
        const auto clear_glass = draw(dl);
        save(clear_glass, "liquid_glass");
        // Under the flat middle the backdrop is as it was (the tint aside);
        // at the bevel it is bent: a stripe's edge moves.
        const raster::Rgba8 middle = clear_glass.at(static_cast<int>(layer.x + 30), static_cast<int>(layer.y + 50));
        check(middle.a == 255, "the glass is over an opaque backdrop");
        build(dl, {{"thickness", 0.0f}, {"tint_a", 0.0f}, {"rim", 0.0f}, {"adaptive", 0.0f}}, {{layer, geom::CornerRadii::all(28)}});
        const auto flat = draw(dl);  // no bevel, no tint, no light: the backdrop itself under the glass
        int moved = 0;
        for (int y = static_cast<int>(layer.y) + 2; y < static_cast<int>(layer.bottom()) - 2; ++y)
            for (int x = static_cast<int>(layer.x) + 2; x < static_cast<int>(layer.x) + 10; ++x)
                moved += clear_glass.at(x, y) == flat.at(x, y) ? 0 : 1;
        check(moved > 50, std::format("the bevel bends the backdrop ({} pixels moved)", moved));
        build(dl, {{"blur", 8}, {"dispersion", 0.8f}, {"tint_a", 0.3f}}, {{layer, geom::CornerRadii::all(28)}});
        const auto frosted = draw(dl);
        save(frosted, "liquid_glass_frosted");
        build(dl, {{"smoothness", 24}},
              {{{layer.x, layer.y + 20, 60, 60}, geom::CornerRadii::all(30)}, {{layer.x + 70, layer.y + 25, 50, 50}, geom::CornerRadii::all(25)}},
              false);
        const auto group = draw(dl);
        save(group, "liquid_glass_group");
        // Between the two drops, merged: glass where neither shape is alone.
        check(!(group.at(static_cast<int>(layer.x + 65), static_cast<int>(layer.y + 50)) == flat.at(static_cast<int>(layer.x + 65), static_cast<int>(layer.y + 50))),
              "the drops merge across the gap");
        Hash all;
        for (const raster::Surface* s : {&clear_glass, &frosted, &group}) all.u64(s->hash());
        std::println("    liquid_glass {}", all.hex());
        check_eq(all.hex(), std::string("5b06f8bd3fe613ad"));
    });

    s.test("the style packs' effects (S2): neon, crt, paper, wobble, metaball — each changes the layer, pinned", [] {
        // Two rounded shapes in the layer (a switch's track and thumb): what
        // neon lights and metaball merges.
        const std::vector<render::EffectShape> shapes{{{layer.x + 10, layer.y + 30, 70, 40}, geom::CornerRadii::all(20)},
                                                      {{layer.x + 90, layer.y + 34, 32, 32}, geom::CornerRadii::all(16)}};
        auto build = [&](render::DisplayList& dl, std::string_view name, std::vector<std::pair<std::string_view, float>> values, bool shaped) {
            dl.clear();
            dl.begin_node(1);
            dl.fill_rect({0, 0, W, H}, geom::Color::hex(0x0f172a));
            dl.begin_node(2);
            const auto ref = render::effects().find(name);
            check(ref.has_value(), std::string(name));
            const auto params = render::effects().params(*ref, values);
            if (shaped) dl.push_layer(1, render::BlendMode::normal, *ref, params, layer, shapes);
            else dl.push_layer(1, render::BlendMode::normal, *ref, params, layer);
            if (shaped)
                for (const auto& sh : shapes) dl.fill_round_rect(sh.rect, sh.radii, render::Paint::solid(geom::Color::hex(0xe0e7ff)));
            else content(dl);
            dl.pop_layer();
            dl.end_node();
            dl.end_node();
        };
        using P = std::vector<std::pair<std::string_view, float>>;
        const std::vector<std::tuple<std::string_view, P, bool>> cases{
            {"neon", {{"radius", 12}, {"intensity", 1.1f}, {"sweep", 0.4f}, {"sweep_width", 20}}, true},
            {"crt", {{"curvature", 0.12f}, {"scanlines", 0.6f}, {"mask", 0.4f}, {"vignette", 0.7f}}, false},
            {"paper", {{"grain", 0.6f}, {"warmth", 0.8f}, {"seed", 4}, {"grain_size", 2}, {"edge", 0.6f}}, false},
            {"wobble", {{"amount", 3}, {"size", 14}, {"seed", 2}, {"ink", 0.8f}}, false},
            {"metaball", {{"smoothness", 24}, {"color_r", 0.4f}, {"color_g", 0.3f}, {"color_b", 0.95f}}, true},
        };
        Hash all;
        render::DisplayList dl;
        for (const auto& [name, params, shaped] : cases) {
            build(dl, name, {}, shaped);
            // The plain layer: the effect with its parameters at "nothing" is not
            // defined for all of them, so compare against no layer at all.
            render::DisplayList plain_dl;
            plain_dl.begin_node(1);
            plain_dl.fill_rect({0, 0, W, H}, geom::Color::hex(0x0f172a));
            plain_dl.begin_node(2);
            if (shaped)
                for (const auto& sh : shapes) plain_dl.fill_round_rect(sh.rect, sh.radii, render::Paint::solid(geom::Color::hex(0xe0e7ff)));
            else content(plain_dl);
            plain_dl.end_node();
            plain_dl.end_node();
            const auto plain = draw(plain_dl);
            build(dl, name, params, shaped);
            const auto out = draw(dl);
            save(out, name);
            check(out.hash() != plain.hash(), std::format("{} changes the layer", name));
            std::println("    {:<10} {:016x}", name, out.hash());
            all.u64(out.hash());
        }
        // metaball merges: between the track and the thumb (a gap of 10 px) it is filled.
        build(dl, "metaball", {{"smoothness", 24}}, true);
        const auto merged = draw(dl);
        const auto bridge = merged.at(static_cast<int>(layer.x + 85), static_cast<int>(layer.y + 50));
        check(bridge.b > 150 && bridge.r < 140, std::format("the bridge is filled: {} {} {}", bridge.r, bridge.g, bridge.b));
        build(dl, "metaball", {{"smoothness", 0}}, true);
        const auto apart = draw(dl).at(static_cast<int>(layer.x + 85), static_cast<int>(layer.y + 50));
        check(apart.r == 15 && apart.g == 23 && apart.b == 42, "smoothness 0: apart");
        // Pinned: GCC and LLVM, Linux, macOS and Windows compute these bytes.
        std::println("    all {}", all.hex());
        check_eq(all.hex(), std::string("40df5182c5755cb0"));
    });

    s.test("progress 0 is the layer as it is (1 for reveal)", [] {
        render::DisplayList dl;
        scene(dl, std::nullopt);
        const auto plain = draw(dl);
        for (std::string_view name : {"dissolve", "wipe", "particles"}) {
            scene(dl, std::pair{name, std::vector<std::pair<std::string_view, float>>{{"progress", 0}}});
            check(draw(dl).hash() == plain.hash(), name);
        }
        scene(dl, std::pair{std::string_view("reveal"), std::vector<std::pair<std::string_view, float>>{{"progress", 1}}});
        check(draw(dl).hash() == plain.hash(), "reveal at 1");
    });

    s.test("bands at the same time render the whole layer's effect exactly", [] {
        render::DisplayList dl;
        scene(dl, std::pair{std::string_view("particles"), std::vector<std::pair<std::string_view, float>>{{"progress", 0.5f}, {"seed", 2}}});
        const auto whole = draw(dl);
        Threads pool;
        const auto banded = draw(dl, {.parallel = &pool, .rows_per_task = 16});
        check(std::ranges::equal(whole.bytes(), banded.bytes()), "parallel bands");
        const std::array<render::RowSpan, 2> rows{{{0, 70}, {70, H}}};
        raster::Surface parts(W, H);
        render::CpuRenderer r;
        (void)r.render(dl, parts, {}, {.background = geom::Color::white(), .rows = rows});
        check(std::ranges::equal(whole.bytes(), parts.bytes()), "row spans");
    });

    s.test("an animated effect, frame after frame incrementally, is the full render", [] {
        raster::Surface incremental(W, H);
        render::CpuRenderer renderer;
        render::DamageTracker tracker;
        render::DisplayList dl;
        for (int f = 0; f <= 10; ++f) {
            const float p = static_cast<float>(f) / 10;
            scene(dl, std::pair{std::string_view("particles"),
                                std::vector<std::pair<std::string_view, float>>{{"progress", p}, {"seed", 4}, {"spread", 90}, {"gravity", 50}}});
            const auto spans = tracker.update(dl, W, H, 1);
            if (!spans.empty()) (void)renderer.render(dl, incremental, {}, {.background = geom::Color::white(), .rows = spans});
            const auto full = draw(dl);
            check(std::ranges::equal(incremental.bytes(), full.bytes()), std::format("frame {}", f));
        }
    });

    s.test("particles reach outside their layer; the damage covers where they go", [] {
        render::DisplayList dl;
        scene(dl, std::pair{std::string_view("particles"),
                            std::vector<std::pair<std::string_view, float>>{{"progress", 0.6f}, {"seed", 7}, {"spread", 40}, {"gravity", 25}}});
        const auto out = draw(dl);
        bool outside = false;
        for (int y = 0; y < H && !outside; ++y)
            for (int x = 0; x < W && !outside; ++x) {
                const geom::Point c{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                const auto px = out.at(x, y);
                if (!layer.contains(c) && !(px.r == 0xf8 && px.g == 0xfa && px.b == 0xfc)) outside = true;
            }
        check(outside, "a particle outside the layer");
    });

    s.test("an effect a program registers is drawn like the built-ins", [] {
        const auto ref = render::effects().add(
            {"invert", "Colours inverted.", {}, nullptr, [](const render::EffectInput& in) {
                 for (int y = 0; y < in.target.height(); ++y)
                     for (int x = 0; x < in.target.width(); ++x) {
                         const auto p = in.source.at(x, y);
                         in.target.at(x, y) = {static_cast<std::uint8_t>(p.a - p.r), static_cast<std::uint8_t>(p.a - p.g),
                                               static_cast<std::uint8_t>(p.a - p.b), p.a};
                     }
             }});
        check(render::effects().add({"invert", "again", {}, nullptr, nullptr}).id == ref.id, "a name is registered once");
        render::DisplayList dl;
        scene(dl, std::pair{std::string_view("invert"), std::vector<std::pair<std::string_view, float>>{}});
        const auto out = draw(dl);
        const auto px = out.at(static_cast<int>(layer.x + 60), static_cast<int>(layer.y + 50));  // the dark dot, inverted
        check(px.r > 200 && px.g > 200 && px.b > 200, std::format("{} {} {}", px.r, px.g, px.b));
    });

    return s.run(argc, argv);
}
