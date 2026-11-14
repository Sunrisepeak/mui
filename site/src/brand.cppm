// omui's identity: one O enclosing its inner letters, drawn by the framework.
export module omui.site:brand;

import std;
import omui;

export namespace omui::site {

struct BrandShape {
    xrender::Path path;
    float width = 0;  // zero: fill; otherwise the stroke width in a 100-unit square
    std::string_view part;
};

// The original geometry, shared by the runtime, SVG and the construction study.
[[nodiscard]] std::span<const BrandShape> brand_geometry();
void paint_mark(xrender::DisplayList& out, const Theme& theme, Point at, float size);
[[nodiscard]] Element brand_mark(std::string id, float size);
// The same geometry in named runtime motion groups. The owner supplies the
// state; hover, focus and a short first-visit assembly need no custom paint loop.
[[nodiscard]] Element animated_brand_mark(std::string id, float size, Signal<bool> unfolded);
[[nodiscard]] std::string brand_svg(Color ink);

struct BrandStudy {
    Signal<bool> unfolded = signal(false);
    Signal<double> size = signal(192.0);
    Signal<std::string> ink = signal(std::string("accent"));
    Signal<std::string> copied = signal(std::string());
};

// A real application of the mark's parts: state belongs here, movement to motion.
[[nodiscard]] Element brand_study(BrandStudy& study, Runtime* runtime, Signal<std::string>& lang, float room);
[[nodiscard]] std::string brand_html(bool zh, Color ink);

}  // namespace omui::site
