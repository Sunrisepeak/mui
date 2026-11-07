// Fixed-time production pixels, not a separately composed loading mockup.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.os;

using namespace omui;
using namespace mlibs::testing;

namespace {
std::expected<site::FirstFrameOptions, std::string> options(std::initializer_list<std::string_view> args) {
    return site::parse_first_frame_options({args.begin(), args.size()});
}
const std::filesystem::path& output() { static const auto path = scratch_dir("site-first-frame"); return path; }
std::string file(std::string_view name) { return mcpplibs::os::fs::generic_utf8(output() / mcpplibs::os::fs::from_utf8(name)); }

std::unique_ptr<Headless> first(int width, int height, std::string_view route, std::string_view language, bool reduced = false) {
    auto host = std::make_unique<Headless>(HeadlessOptions{.size = {static_cast<float>(width), static_cast<float>(height)}, .reduce_motion = reduced, .parallel_raster = false});
    auto app = site::app(route, language, route);
    host->runtime().set_theme(app.theme); app.setup(host->runtime());
    for (auto& verb : app.verbs) (void)host->runtime().verbs().add(verb.desc, verb.fn);
    host->mount("main", app.view());
    check(host->step().failure.empty());
    return host;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.first_frame");
    suite.test("the export command parses exact options and rejects ambiguous or malformed requests", [] {
        const auto parsed = options({"--first-frame", "a.png", "--route", "#/zh/components/text_box", "--width", "390", "--height", "844", "--time", "0.375", "--root", "main/body"});
        check(parsed.has_value());
        if (parsed) {
            check(parsed->lang == "zh" && parsed->width == 390 && parsed->height == 844 && parsed->time == 0.375 && parsed->root == "main/body");
        }
        const auto reduced = options({"--reduced-motion", "--first-frame", "a.png"});
        check(reduced && reduced->reduce_motion);
        for (const auto& args : std::vector<std::vector<std::string_view>>{
            {}, {"--first-frame"}, {"--first-frame", "a.jpg"}, {"--first-frame", "a.png", "--lang", "fr"},
            {"--first-frame", "a.png", "--route", "zh/components", "--lang", "en"},
            {"--first-frame", "a.png", "--route", "components/unknown"},
            {"--first-frame", "a.png", "--width", "0"}, {"--first-frame", "a.png", "--width", "-1"},
            {"--first-frame", "a.png", "--width", "3.5"}, {"--first-frame", "a.png", "--height", "32px"},
            {"--first-frame", "a.png", "--width", "8193"}, {"--first-frame", "a.png", "--width", "8192", "--height", "8192"},
            {"--first-frame", "a.png", "--time", "nan"}, {"--first-frame", "a.png", "--time", "inf"},
            {"--first-frame", "a.png", "--time", "-1"}, {"--first-frame", "a.png", "--time", "1e999"},
            {"--first-frame", "a.png", "--time", "3601"}, {"--first-frame", "a.png", "--time", "1second"},
            {"--first-frame", "a.png", "--root", ""}, {"--first-frame", "a.png", "--root"},
            {"--first-frame", "a.png", "--width", "300", "--width", "400"},
            {"--first-frame", "a.png", "--first-frame", "b.png"}, {"--first-frame", "a.png", "--unknown", "1"},
            {"--first-frame", "a.png", "--reduced-motion", "--reduced-motion"}, {"--first-frame", "a.png", "--reduced-motion", "false"},
            {"--first-frame", "a.png", "--prerender", "index.html"}, {"--first-frame=a.png"}})
            check(!site::parse_first_frame_options(args), "invalid arguments are rejected before rendering or file IO");
    });

    suite.test("time zero matches an independent production first frame and exports its actual global rectangles", [] {
        for (const auto& [width, height, language] : {std::tuple{1280, 800, "en"}, std::tuple{390, 844, "zh"}}) {
            site::FirstFrameOptions request;
            request.png = file(std::string("home-") + language + ".png"); request.lang = language; request.width = width; request.height = height;
            const auto report = site::export_first_frame(request);
            check(report.has_value(), report ? "" : report.error()); if (!report) continue;
            auto expected = first(width, height, std::string_view(language) == "zh" ? "zh" : "", language);
            check(report->get("time")->as_double() == 0 && report->get("sampled_frames")->as_int() == 1, "exactly one frame at zero; no settle");
            check(report->get("renderer")->as_string() == "cpu" && report->get("tier")->as_string() == "T3" && report->get("raster_bands")->as_int() == 1);
            check(report->get("viewport")->list()->size() == 2 && report->get("viewport")->list()->at(0).as_int() == width && report->get("viewport")->list()->at(1).as_int() == height);
            check(report->get("pixel_hash")->as_string() == std::format("{:016x}", site::frame_hash(expected->pixels())));
            const auto png = mlibs::image::load_png(mcpplibs::os::fs::from_utf8(request.png));
            check(png && png->width == width && png->height == height);
            if (png) for (int y = 0; y < height; ++y) {
                const auto actual = std::as_bytes(png->row(y));
                const auto wanted = std::as_bytes(std::span(expected->pixels().row(y), static_cast<std::size_t>(width)));
                check(std::ranges::equal(actual, wanted), "exported PNG pixels are the production frame");
            }
            const auto semantic = expected->tree().snapshot();
            const auto* records = report->get("nodes")->list();
            const auto placed = std::ranges::count_if(semantic.nodes, [](const auto& node) { return node.bounds.has_value(); });
            check(records && records->size() == static_cast<std::size_t>(placed));
            check(records && records->size() + report->get("unplaced_nodes")->list()->size() == semantic.nodes.size());
            if (records) for (const auto& entry : *records) {
                const auto found = std::ranges::find(semantic.nodes, entry.get("id")->as_string(), &spec::SemanticNode::id);
                check(found != semantic.nodes.end() && found->bounds.has_value()); if (found == semantic.nodes.end() || !found->bounds) continue;
                check(entry.get("role")->as_string() == spec::name(found->role));
                const auto& bounds = *entry.get("bounds")->list();
                check(bounds.size() == 4 && bounds[0].as_double() == found->bounds->x && bounds[1].as_double() == found->bounds->y &&
                    bounds[2].as_double() == found->bounds->width && bounds[3].as_double() == found->bounds->height, "geometry comes from the actual production semantic tree");
            }
            const auto text = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(report->get("sidecar")->as_string()));
            check(text.has_value());
            if (text) {
                const auto json = parse_json(*text);
                check(json && json->get("pixel_hash")->as_string() == report->get("pixel_hash")->as_string());
                check(json && json->get("nodes")->list()->size() == records->size());
            }
        }
    });

    suite.test("a crop is a pixel-for-pixel region of the same complete first frame", [] {
        site::FirstFrameOptions whole; whole.png = file("source.png");
        const auto complete = site::export_first_frame(whole); check(complete.has_value()); if (!complete) return;
        auto region = whole; region.png = file("visual.png"); region.root = "main/body/view/home/page/landing/scene/visual";
        const auto report = site::export_first_frame(region); check(report.has_value(), report ? "" : report.error()); if (!report) return;
        const auto source = mlibs::image::load_png(mcpplibs::os::fs::from_utf8(whole.png));
        const auto cropped = mlibs::image::load_png(mcpplibs::os::fs::from_utf8(region.png));
        check(source && cropped); if (!source || !cropped) return;
        const auto& bounds = *report->get("crop")->list();
        const int x = static_cast<int>(bounds[0].as_int()), y = static_cast<int>(bounds[1].as_int());
        check(bounds[2].as_int() == cropped->width && bounds[3].as_int() == cropped->height);
        check(report->get("viewport_pixel_hash")->as_string() == complete->get("pixel_hash")->as_string());
        check(report->get("image")->get("width")->as_int() == cropped->width && report->get("image")->get("height")->as_int() == cropped->height);
        for (int row = 0; row < cropped->height; ++row)
            check(std::ranges::equal(cropped->row(row), source->row(y + row).subspan(static_cast<std::size_t>(x) * 4, static_cast<std::size_t>(cropped->width) * 4)), "native crop contains the source pixels without recomposition");
        check(report->get("nodes")->list()->front().get("bounds")->list()->at(0).as_double() == 0, "tree coordinates stay global rather than shifting with the crop");
    });

    suite.test("unknown, hidden and empty crop roots fail without manufacturing geometry", [] {
        site::FirstFrameOptions request; request.png = file("rejected.png"); request.root = "main/unknown";
        check(!site::export_first_frame(request));
        request.root = "main/body/settings"; check(!site::export_first_frame(request));
        request.height = 100; request.root = "main/body/view/home/page/landing/scene/controls/pause";
        check(!site::export_first_frame(request), "a real node outside the viewport is not replaced with a guessed rectangle");
        check(!std::filesystem::exists(mcpplibs::os::fs::from_utf8(request.png)));
    });

    suite.test("comparison times are exact and repeatable, without waiting for animation to settle", [] {
        site::FirstFrameOptions request; request.png = file("comparison-a.png"); request.time = 0.375;
        const auto first = site::export_first_frame(request); check(first.has_value()); if (!first) return;
        request.png = file("comparison-b.png"); const auto again = site::export_first_frame(request);
        check(again && first->get("pixel_hash")->as_string() == again->get("pixel_hash")->as_string());
        check(first->get("time")->as_double() == 0.375 && first->get("sampled_frames")->as_int() == 24);
        request.png = file("comparison-zero.png"); request.time = 0;
        const auto zero = site::export_first_frame(request);
        check(zero && zero->get("pixel_hash")->as_string() != first->get("pixel_hash")->as_string(), "the real first-visit animation produces a distinct comparison frame");
    });

    suite.test("the reduced-motion first frame uses the actual already assembled production view", [] {
        site::FirstFrameOptions request; request.png = file("home-reduced.png"); request.reduce_motion = true;
        const auto report = site::export_first_frame(request); check(report.has_value()); if (!report) return;
        auto expected = first(request.width, request.height, "", "en", true);
        check(report->get("reduce_motion")->as_bool() && report->get("time")->as_double() == 0 && report->get("sampled_frames")->as_int() == 1);
        check(expected->runtime().get(expected->runtime().find("main/body/view/home/page/landing/scene/visual/mark/letter_m"), "translate_x").as_double() == 0);
        check(report->get("pixel_hash")->as_string() == std::format("{:016x}", site::frame_hash(expected->pixels())));
        request.png = file("home-normal.png"); request.reduce_motion = false;
        const auto normal = site::export_first_frame(request);
        check(normal && normal->get("pixel_hash")->as_string() != report->get("pixel_hash")->as_string());
    });

    suite.test("file errors are returned instead of claiming a successful export", [] {
        site::FirstFrameOptions request; request.png = file("missing-parent/frame.png");
        const auto result = site::export_first_frame(request);
        check(!result && result.error().contains("PNG export failed"));
    });
    return suite.run(argc, argv);
}
