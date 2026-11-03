// The implementation's mark, its shared vector original, and its real study.
import std;
import openmui;
import openmui.site;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {

std::unique_ptr<Headless> page(Size size, std::string_view language = "en") {
    auto h = std::make_unique<Headless>(HeadlessOptions{.size = size});
    AppSpec spec = site::app("brand", language);
    h->runtime().set_theme(spec.theme);
    spec.setup(h->runtime());
    h->mount("main", spec.view());
    h->settle();
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site.brand"};
    const auto dir = scratch_dir("brand");

    s.test("the O and inner letters: font-free paths at icon and display sizes, in both themes", [dir] {
        for (const bool dark : {false, true}) {
            Headless h{HeadlessOptions{.size = {920, 460}}};
            Theme theme = dark ? Theme::dark_theme() : Theme::light();
            h.runtime().set_theme(theme);
            std::vector<Element> samples;
            for (const int size : {16, 24, 32, 64, 256})
                samples.push_back(column({.id = std::format("s{}", size), .gap = 16, .align = "center"}, {
                    site::brand_mark("mark", static_cast<float>(size)), text({.id = "size", .text = std::format("{} px", size), .style = "caption"})}));
            h.mount("sheet", column({.padding = Edges(32), .gap = 32, .background = "background"}, {
                text({.id = "title", .text = std::string("omui / the vector original"), .style = "title"}),
                row({.id = "sizes", .gap = 44, .align = "center"}, std::move(samples))}));
            h.settle();
            check(h.save_png(dir / (dark ? "mark-dark.png" : "mark-light.png")).has_value());
            render::DisplayList list;
            site::paint_mark(list, theme, {20, 30}, 256);
            check(list.balanced() && list.size() > 5, "the mark records balanced vector commands");
            const auto svg = site::brand_svg(theme.color("accent"));
            check(svg.find("<title>omui</title>") != std::string::npos && svg.find("<text") == std::string::npos && svg.find("<image") == std::string::npos,
                  "the export is the same font-free vector mark");
            std::ofstream(dir / (dark ? "mark-dark.svg" : "mark-light.svg")) << svg;
        }
    });

    s.test("a person assembles it, an agent changes it, and the original can be taken away", [dir] {
        auto h = page({1280, 800});
        const std::string root = "main/body/view/brand/page/identity";
        check(h->tree().node(root + "/title").has_value(), "the official construction study has a deep link");
        const auto before = site::frame_hash(h->pixels());
        check(h->save_png(dir / "study-assembled.png").has_value());
        check(h->click(root + "/controls/assemble"), "a real click takes it apart");
        h->settle();
        const auto unfolded = site::frame_hash(h->pixels());
        check(before != unfolded, "the same parts now have different positions");
        check(h->save_png(dir / "study-unfolded.png").has_value());
        Invocation agent;
        agent.actor = {"agent", "brand-designer", {}};
        agent.args = Value{}.set("value", Value(128.0));
        check(h->tree().act(root + "/controls/sizing/size", "set_value", agent).exit_code() == std::optional<int>(0), "an agent changes the size");
        h->settle();
        check(site::frame_hash(h->pixels()) != unfolded, "the size changes the actual vectors");
        check(h->click(root + "/original/copy_svg"));
        check(h->runtime().clipboard_text() == site::brand_svg(h->runtime().theme().color("accent")), "the clipboard holds the current original SVG");
        check(h->click(root + "/controls/reset"));
        h->settle();
        check(h->tree().node(root + "/controls/assemble")->name == "Take apart", "reset assembled the mark");
        check(h->runtime().frame_demand().idle(), "the study comes to rest");
    });

    s.test("the Chinese phone study keeps its controls usable and returns through the brand link", [dir] {
        auto h = page({390, 844}, "zh");
        const std::string root = "main/body/view/brand/page/identity";
        check(h->tree().node(root + "/title")->name == "标志，也是程序。", "Chinese identity, not an English placeholder");
        check(h->save_png(dir / "study-phone.png").has_value());
        Invocation call;
        check(h->tree().act(root + "/controls/assemble", "activate", call).exit_code() == std::optional<int>(0));
        h->settle();
        check(h->tree().node(root + "/controls/assemble")->name == "重新组装");
        check(h->click("main/bar/mark"), "the omui brand link returns home");
        h->settle();
        check(h->tree().node("main/body/view/home/page/hero").has_value());
    });

    return s.run(argc, argv);
}
