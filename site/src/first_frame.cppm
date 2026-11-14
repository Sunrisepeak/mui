// Build-time pixels and geometry from the site's actual production view.
export module omui.site:first_frame;

import std;
import mcpplibs.cmdline;
import omui;

export namespace omui::site {

struct FirstFrameOptions {
    std::string png;
    std::string route;
    std::string lang = "en";
    int width = 1280, height = 800;
    double time = 0;
    std::string root;  // empty: complete viewport; otherwise a real explicit node id
    bool reduce_motion = false;
};

// The export's options on a command line (mcpplibs.cmdline): --first-frame
// PNG, --route, --width, --height, --time, --root, --reduced-motion, and
// --lang unless the line declares it already (omui-site's is RunOptions's).
void declare_first_frame(mcpplibs::cmdline::App& line, bool language);
// What they say. Export mode accepts only its own options (and --lang), once
// each, and no other argument. A route's explicit language must agree with
// --lang; without --lang the route's prefix determines it.
[[nodiscard]] std::expected<FirstFrameOptions, std::string> first_frame_options(const mcpplibs::cmdline::ParsedArgs& args);
// Both, over arguments that exclude argv[0] and include --first-frame <PNG>.
[[nodiscard]] std::expected<FirstFrameOptions, std::string> parse_first_frame_options(std::span<const std::string_view> args);

// The output directory must exist. CPU, one raster band, scale 1, fixed T3.
// The default is exactly the first frame at t=0, never settle(). --time is
// sampled on a controlled 60 Hz clock, with an exact final fractional frame.
// The returned report is also written beside the PNG as *.frame.omui.json.
// Bounds remain global even when the image is cropped to an actual node.
[[nodiscard]] std::expected<Value, std::string> export_first_frame(const FirstFrameOptions& options);

}  // namespace omui::site
