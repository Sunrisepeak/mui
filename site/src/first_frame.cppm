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
    bool paths = true;  // the report names the PNG and itself; not in a published asset, whose hash they would change
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
// The returned report is also written beside the PNG as *.frame.xdef.json.
// Bounds remain global even when the image is cropped to an actual node.
[[nodiscard]] std::expected<Value, std::string> export_first_frame(const FirstFrameOptions& options);

// The openings the static page shows while the program loads: each feature page's
// opening (its picture and geometry, cropped to the node it is about) in both
// languages, at a phone's and a desktop's size, with full and reduced motion, and
// the home's whole first frame at each as a reference to check the live page
// against. Written into DIR/assets/ by their contents' hash, the catalogue as
// DIR/first-frames.assets.xdef.json (what --prerender's --opening-assets reads);
// what was written is returned (the counts and the catalogue's path).
[[nodiscard]] std::expected<Value, std::string> export_openings(const std::filesystem::path& out);

}  // namespace omui::site
