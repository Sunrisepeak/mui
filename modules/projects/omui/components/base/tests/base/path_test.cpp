// A vector shape from SVG path data (G5): scaled from its view box into its own with the ratio kept and centred,
// stroked and filled in the theme's colours, its stroke drawn in part by `trim`; data that is not a path draws
// nothing; its words are its label, an image's.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {

// How many pixels are mostly red (the stroke's colour in these cases).
int red_in(const mrender::raster::Surface& pixels, Rect within = {0, 0, 1e6f, 1e6f}) {
    int count = 0;
    for (int row = 0; row < pixels.height(); ++row)
        for (int column = 0; column < pixels.width(); ++column) {
            if (column < within.x || row < within.y || column >= within.x + within.width || row >= within.y + within.height) continue;
            const auto pixel = pixels.at(column, row);
            if (pixel.r > 180 && pixel.g < 90 && pixel.b < 90) ++count;
        }
    return count;
}

constexpr std::string_view square = "M10 10 H90 V90 H10 Z";

}  // namespace

static UTest cases{"ui.base.path", [] {
    register_base_components();

    "stroked: an image named by its label, its stroke inked; trimmed to half, about half of it"_test = [] {
        Headless whole(HeadlessOptions{.size = {140, 140}});
        whole.mount("main", column({.padding = 10}, {path({.id = "mark", .d = std::string(square), .label = "The mark", .stroke = "#ff0000", .stroke_width = 4,
                                                           .width = 120, .height = 120})}));
        whole.settle();
        const auto node = whole.tree().node("main/mark");
        expect(fatal(node.has_value()));
        expect(node->role == Role::image && node->name == "The mark");
        const int all = red_in(whole.pixels());
        // A square of side 96 (80 data units × 1.2) stroked 4.8 wide: about 4 × 96 × 4.8 pixels.
        expect(all > 1500 && all < 2300) << all;

        Headless half(HeadlessOptions{.size = {140, 140}});
        half.mount("main", column({.padding = 10}, {path({.id = "mark", .d = std::string(square), .stroke = "#ff0000", .stroke_width = 4, .trim = 0.5,
                                                          .width = 120, .height = 120})}));
        half.settle();
        const int drawn = red_in(half.pixels());
        expect(drawn > all * 0.4 && drawn < all * 0.6) << drawn << " of " << all;
        // From its start (the top left, along the top, down the right): the left side is not drawn.
        expect(red_in(half.pixels(), {18, 40, 12, 60}) == 0 && red_in(whole.pixels(), {18, 40, 12, 60}) > 0);
    };

    "filled, the ratio kept and centred in a wider box; data that is not a path draws nothing"_test = [] {
        Headless h(HeadlessOptions{.size = {220, 120}});
        h.mount("main", column({.padding = 10}, {path({.id = "dot", .d = "M50 0 A50 50 0 1 1 50 100 A50 50 0 1 1 50 0 Z", .fill = "#ff0000",
                                                       .width = 200, .height = 100})}));
        h.settle();
        const auto& pixels = h.pixels();
        // The circle's 100 units are 100 pixels, centred: from x 60 to 160 in the window, nothing beside it.
        expect(red_in(pixels, {105, 55, 10, 10}) == 100) << "its middle filled";
        expect(red_in(pixels, {10, 10, 45, 100}) == 0 && red_in(pixels, {165, 10, 45, 100}) == 0) << "nothing past its ratio";

        Headless bad(HeadlessOptions{.size = {140, 140}});
        bad.mount("main", column({.padding = 10}, {path({.id = "mark", .d = "M10 10 Q", .fill = "#ff0000", .stroke = "#ff0000", .width = 120, .height = 120})}));
        bad.settle();
        expect(red_in(bad.pixels()) == 0) << "not a path: nothing drawn, nothing broken";
    };

    "unsized, its view box's ratio at the default height; one side given, the other by the ratio"_test = [] {
        Headless h(HeadlessOptions{.size = {320, 200}});
        h.mount("main", column({.align = "start"}, {path({.id = "plain", .d = std::string(square), .stroke = "accent"}),
                                                     path({.id = "wide", .d = std::string(square), .stroke = "accent", .view_box = "0 0 200 100"}),
                                                     path({.id = "given", .d = std::string(square), .stroke = "accent", .view_box = "0 0 200 100", .width = 100})}));
        h.settle();
        const auto size_of = [&](std::string_view at) { return h.runtime().screen_bounds(h.runtime().find(at)).size(); };
        expect(eq(size_of("main/plain").width, 24.0f) && eq(size_of("main/plain").height, 24.0f));
        expect(eq(size_of("main/wide").width, 48.0f) && eq(size_of("main/wide").height, 24.0f));
        expect(eq(size_of("main/given").width, 100.0f) && eq(size_of("main/given").height, 50.0f));
    };
}};
