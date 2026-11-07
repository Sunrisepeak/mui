// The quality pass's review sheet (B4–B6 of the site plan): the action and
// input controls, each at rest, under the pointer, pressed and
// keyboard-focused, in the light theme and the dark — saved headless for the
// maintainer and the benchmark review.
//
// Sheets in scratch "quality-lab": controls.png (a row per control, the four
// states in light, then in dark). Checked here: every state of a button is
// its own picture.
import std;
import omui;
import omui.theme;
import omui.theme.styles;
import mlibs.image;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

constexpr Size cell{220, 84};
constexpr float scale = 2;

struct Control {
    std::string name, target;  // the node the pointer and the focus go to
    std::function<Element()> make;
};

enum class Shown { rest, hover, pressed, focused };
constexpr std::array<std::string_view, 4> shown_names{"rest", "hover", "pressed", "focus"};

std::vector<Control> controls() {
    return {
        {"primary", "main/it", [] { return button({.id = "it", .text = "Save changes", .variant = ButtonVariant::primary}); }},
        {"secondary", "main/it", [] { return button({.id = "it", .text = "Cancel"}); }},
        {"danger", "main/it", [] { return button({.id = "it", .text = "Delete", .variant = ButtonVariant::danger}); }},
        {"toggle", "main/it", [] { return toggle({.id = "it", .checked = true, .label = "Sync"}); }},
        {"checkbox", "main/it", [] { return checkbox({.id = "it", .checked = true, .label = "Back up"}); }},
        {"slider", "main/it", [] { return slider({.id = "it", .max = 100.0, .value = 60.0, .width = 170}); }},
        {"segmented", "main/it", [] { return segmented({.id = "it",
                                                                    .labels = std::vector<std::string>{"Day", "Week", "Month"},
                                                                    .options = std::vector<std::string>{"day", "week", "month"},
                                                                    .value = std::string("week")}); }},
        {"text box", "main/it", [] { return text_box({.id = "it", .placeholder = "Search", .width = 170}); }},
    };
}

// One control in one state under one theme: its pixels.
xrender::raster::Surface shoot(const Control& control, Shown shown, bool dark) {
    Headless h(HeadlessOptions{.size = cell, .scale = scale});
    const std::vector<std::string> layers{dark ? "dark" : "light"};
    check(theme::apply(h.runtime(), layers).has_value(), "the theme composes");
    h.mount("main", row({.id = "cell", .width = cell.width, .height = cell.height, .justify = "center", .align = "center",
                         .background = Fill(dark ? Color::hex(0x0b1120) : Color::hex(0xf6f7f9))},
                        {control.make()}));
    h.settle();
    Runtime& rt = h.runtime();
    const NodeId node = rt.find(control.target);
    if (!rt.valid(node)) {
        std::string ids;
        for (const auto& n : h.tree().snapshot({}).nodes) ids += n.id + " ";
        check(false, std::format("{} is not there: {}", control.target, ids));
    }
    const Rect box = rt.screen_bounds(node);
    // A slider is pressed at its thumb, the rest at their middle.
    const Point at = control.name == "slider" ? Point{box.x + 9 + (box.width - 18) * 0.6f, box.y + box.height / 2} : box.center();
    switch (shown) {
        case Shown::rest: break;
        case Shown::hover: (void)h.step({PointerEvent{.kind = PointerKind::move, .position = at}}); break;
        case Shown::pressed:
            (void)h.step({PointerEvent{.kind = PointerKind::move, .position = at}});
            (void)h.step({PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}});
            break;
        case Shown::focused: rt.focus_visibly(node); break;
    }
    h.settle(seconds(0.8));
    return h.pixels();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site.quality_lab"};

    s.test("B4: eight controls in four states, light beside dark — saved; each state of a button its own picture", [] {
        check(themes::add_styles().has_value(), "the default themes and the packs");
        const auto list = controls();
        const int cell_w = static_cast<int>(cell.width * scale), cell_h = static_cast<int>(cell.height * scale);
        constexpr int gutter = 24;
        mlibs::image::Image sheet;
        sheet.width = cell_w * 8 + gutter;
        sheet.height = cell_h * static_cast<int>(list.size());
        sheet.alpha = mlibs::image::Alpha::premultiplied;
        sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 4, 128);
        std::set<std::uint64_t> buttons;
        for (std::size_t r = 0; r < list.size(); ++r)
            for (int column = 0; column < 8; ++column) {
                const bool dark = column >= 4;
                const auto shown = static_cast<Shown>(column % 4);
                const auto pixels = shoot(list[r], shown, dark);
                if (r == 0) buttons.insert(std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(pixels.bytes().data()), pixels.bytes().size())));
                const int x0 = column * cell_w + (dark ? gutter : 0), y0 = static_cast<int>(r) * cell_h;
                for (int y = 0; y < std::min(cell_h, pixels.height()); ++y)
                    std::memcpy(sheet.rgba.data() + (static_cast<std::size_t>(y0 + y) * sheet.width + x0) * 4, pixels.row(y),
                                static_cast<std::size_t>(std::min(cell_w, pixels.width())) * 4);
            }
        check_eq(buttons.size(), std::size_t{8});  // rest, hover, pressed, focus — in light and in dark
        const auto dir = scratch_dir("quality-lab");
        check(mlibs::image::save_png(dir / "controls.png", mlibs::image::view(sheet)).has_value(), "controls.png saved");
        std::println("quality lab: {} (columns: {} in light, then in dark)", (dir / "controls.png").string(),
                     std::views::join_with(shown_names, std::string_view(", ")) | std::ranges::to<std::string>());
    });

    return s.run(argc, argv);
}
