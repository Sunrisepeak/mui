// Liquid glass as a person, an agent and a renderer meet it (D-64): the
// glass changes how what lies under it looks and nothing else — the
// semantic tree is an ordinary group's, the same with and without
// transparency; a group's members are one piece of glass, merging where they
// come close; reduced transparency draws an opaque panel.
import std;

import omui.ui.liquid_glass;
import omui.host.headless;
import xrender.vector;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

// Stripes under everything, the glass over them.
Element backdrop() {
    std::vector<Element> stripes;
    for (int i = 0; i < 32; ++i)
        stripes.push_back(box({.width = 12, .height = 200,
                               .background = Fill(i % 2 ? Color::hex(0xf8fafc) : Color{0.2f + 0.02f * static_cast<float>(i), 0.3f, 0.85f - 0.02f * static_cast<float>(i), 1})}));
    return row({.width = 384, .height = 200}, std::move(stripes));
}

Element scene(Element glass) {
    return stack({.width = 384, .height = 200}, {backdrop(), column({.id = "over", .padding = 40}, {std::move(glass)})});
}

Element toolbar() {
    return liquid_glass({.id = "bar", .label = "Toolbar", .radius = 28, .width = 260, .height = 80},
                        {box({.id = "play", .width = 24, .height = 24, .background = Fill(Color::hex(0x0f172a)), .label = "Play"})});
}

std::vector<std::pair<std::string, Role>> roles(Headless& h) {
    std::vector<std::pair<std::string, Role>> out;
    for (const auto& n : h.tree().snapshot({}).nodes) out.emplace_back(n.id, n.role);
    return out;
}

std::size_t glass_layers(const xrender::DisplayList& list, std::size_t* shapes = nullptr) {
    const auto ref = xrender::effects().find("liquid_glass");
    std::size_t n = 0;
    for (const auto& c : list.commands())
        if (const auto* l = std::get_if<xrender::PushLayer>(&c); l && ref && l->effect.id == ref->id) {
            ++n;
            if (shapes) *shapes = l->shapes.size();
        }
    return n;
}

}  // namespace

UTest cases{"ui.liquid_glass", [] {
    register_liquid_glass_components();

    "the glass bends and tints what lies under it; its semantics are an ordinary group's"_test = [] {
        Headless plain({.size = {384, 200}});
        plain.mount("main", scene(column({.id = "bar", .width = 260, .height = 80}, {})));
        plain.settle();
        Headless h({.size = {384, 200}});
        h.mount("main", scene(toolbar()));
        h.settle();
        const auto bar = h.tree().node("main/over/bar");
        expect(bar && bar->role == Role::group && bar->name == "Toolbar") << "a group named by its label";
        expect(h.tree().node("main/over/bar/play").has_value()) << "its content is in the tree as it is";
        expect(eq(glass_layers(h.runtime().display_list()), std::size_t{1}));
        (void)h.save_png(scratch_dir("ui-liquid-glass") / "toolbar.png");
        // Under the glass the stripes change (bent, tinted); outside it they do not.
        const auto& a = plain.pixels();
        const auto& b = h.pixels();
        int changed = 0, outside = 0;
        for (int y = 44; y < 116; ++y)
            for (int x = 44; x < 296; ++x) changed += a.at(x, y) == b.at(x, y) ? 0 : 1;
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 384; ++x) outside += a.at(x, y) == b.at(x, y) ? 0 : 1;
        expect(changed > 5000) << std::format("{} pixels under the glass changed", changed);
        expect(eq(outside, 0));
    };

    "reduced transparency: an opaque panel, the same semantic tree"_test = [] {
        Headless h({.size = {384, 200}});
        h.mount("main", scene(toolbar()));
        h.settle();
        const auto glassy = roles(h);
        const auto see_through = h.pixels().at(60, 100);
        h.runtime().set_reduce_transparency(true);
        h.settle();
        expect(roles(h) == glassy) << "reduced transparency changes no node";
        expect(eq(glass_layers(h.runtime().display_list()), std::size_t{0}));
        // The panel is the theme's surface: white, whatever stripe is behind.
        const auto panel = h.pixels().at(60, 100);
        expect(panel.r == 255 && panel.g == 255 && panel.b == 255) << std::format("an opaque surface ({} {} {})", panel.r, panel.g, panel.b);
        expect(!(panel == see_through)) << "not the glass any more";
        h.runtime().set_reduce_transparency(false);
        h.settle();
        expect(eq(glass_layers(h.runtime().display_list()), std::size_t{1}));
    };

    "a group is one piece of glass: its members' shapes merge like drops"_test = [] {
        Headless h({.size = {384, 200}});
        h.mount("main", scene(glass_group({.id = "controls", .gap = 10, .smoothness = 24},
                                          {liquid_glass({.id = "play", .radius = 30, .width = 60, .height = 60}, {}),
                                           liquid_glass({.id = "seek", .radius = 25, .width = 120, .height = 50}, {})})));
        h.settle();
        std::size_t shapes = 0;
        expect(eq(glass_layers(h.runtime().display_list(), &shapes), std::size_t{1}));
        expect(eq(shapes, std::size_t{2}));
        expect(h.tree().node("main/over/controls/play") && h.tree().node("main/over/controls/seek")) << "the members are nodes";
        // In the gap between the members, where they come within the
        // smoothness of each other, there is glass: the stripes differ from
        // the same group with smoothness 0.
        const auto merged = h.pixels();
        (void)h.save_png(scratch_dir("ui-liquid-glass-group") / "group.png");
        h.runtime().set(h.runtime().find("main/over/controls"), "smoothness", Value(0.0));
        h.settle();
        const auto apart = h.pixels();
        int bridged = 0;
        for (int y = 60; y < 80; ++y)
            for (int x = 98; x < 112; ++x) bridged += merged.at(x, y) == apart.at(x, y) ? 0 : 1;
        expect(bridged > 20) << std::format("{} pixels of the gap became glass", bridged);
    };
}};
