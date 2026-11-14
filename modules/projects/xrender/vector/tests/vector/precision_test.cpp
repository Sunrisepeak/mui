// Where content lands under deep magnification (D-188, plan §5.9 gap 4): the camera's translation and
// scale over content in its own coordinates, as a canvas draws a node's list.
import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;

namespace {

// A black rectangle whose left edge the camera puts at device x = `edge` exactly (in doubles): content
// `extent` units wide seen at `zoom`, the rectangle at the content's middle. Returns where the edge
// landed, read from the coverage of the pixel it crosses.
double landed(double extent, double zoom, double edge) {
    const double at = extent / 2;                 // the rectangle's left, in content units
    const double origin = edge - at * zoom;       // the content's origin on the device
    render::DisplayList list;
    list.push_transform(geom::Affine::translate(static_cast<float>(origin), 0) * geom::Affine::scale(static_cast<float>(zoom), static_cast<float>(zoom)));
    list.fill_rect({static_cast<float>(at), 0, static_cast<float>(64 / zoom), static_cast<float>(64 / zoom)}, geom::Color::black());
    list.pop_transform();
    raster::Surface    out(48, 8);
    render::CpuRenderer cpu;
    render::ImageStore  images;
    cpu.render(list, out, {&images, nullptr}, {.scale = 1, .background = geom::Color::white()});
    // The first column that is not white holds the edge: its darkness is what of it the rectangle covers.
    for (std::uint32_t x = 0; x < 48; ++x) {
        const double dark = 1.0 - out.at(x, 2).r / 255.0;
        if (dark > 0) return x + 1 - dark;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

static UTest cases{"render.precision", [] {
    // The reference's bound (the device's paths are held to the same in the render CTS, deep-zoom): the
    // camera's translation and scale are single precision, so what they add grows with how far the
    // content's origin is from the frame — nothing readable up to 2^20 pixels across, a float's last place beyond.
    "an edge lands where it is while the magnified content is within 2^20 pixels across"_test = [] {
        const double extent = 1000;
        for (const double pixels : {1e3, 1e4, 1e5, 262144.0, 1048576.0, 4e6, 1.6e7, 1e8}) {
            const double zoom = pixels / extent;
            double worst = 0;
            for (int step = 0; step < 16; ++step) {
                const double edge = 10.25 + step * 0.4375;
                worst = std::max(worst, std::abs(landed(extent, zoom, edge) - edge));
            }
            std::println("    content {:>10.0f} px across (zoom {:>9.1f}): the edge off by at most {:.4f} px", pixels, zoom, worst);
            // A level of coverage is 1/255 of a pixel: what the reading itself resolves.
            if (pixels <= 1048576.0) expect(le(worst, 1.0 / 64)) << std::format("{} px across: off by {} px", pixels, worst);
            // Beyond: no more than the last place of a float that large.
            else expect(le(worst, pixels / 2 / 8388608.0 * 2)) << std::format("{} px across: off by {} px", pixels, worst);
        }
    };
}};
