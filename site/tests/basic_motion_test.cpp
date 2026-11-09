// The designs are real drawings, with measurable endpoints and
// sampled motion in between; the site's shared player is tested separately.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {

const std::filesystem::path& media() {
    static const auto dir = scratch_dir("site-basic-motion");
    return dir;
}

struct Probe {
    site::MotionSceneState state;
    Signal<std::string> language;
    std::string design;
    int changes = 0;
    Headless h;

    Probe(std::string_view id, float width = 600, std::string_view lang = "en")
        : language(signal(std::string(lang))), design(id), h(HeadlessOptions{.size = {width + 48, 450}, .parallel_raster = false}) {
        h.runtime().set_theme(site::app().theme);
        const site::MotionCaseInfo* info = nullptr;
        for (const auto& example : site::basic_motions()) if (example.id == id) info = &example;
        Value spec = Value{}.set("use", info ? info->use : "morph").set("order", info ? info->order : "together")
                                .set("stagger", info ? info->stagger : 0.0).set("rise", info ? info->rise : 0.0f)
                                .set("grow", info ? info->grow : 1.0f).set("stretch", info ? info->stretch : 0.0f);
        if (info && !info->curve.empty()) spec.set("curve", info->curve);
        if (info && info->vertex_path) spec.set("vertex_path", Value{}.set("bend", info->bend));
        Element preview = box({.id = "preview", .width = Dim(width), .height = Dim(360)}, {
            dynamic([this, width] {
                site::MotionSceneContext context{.state = &state, .language = &language, .width = width, .height = 360, .changed = [this] { ++changes; }};
                return std::vector<Element>{site::basic_motion_scene(design, context)};
            })});
        preview.prop("motion", Source(spec));
        h.mount("main", column({.width = Dim(width + 48), .height = Dim(450), .padding = Edges(24), .gap = 16, .background = "background"}, {
            text({.id = "title", .text = info ? std::string(lang == "zh" ? info->zh : info->en) : std::string(id), .style = "label", .color = "accent"}),
            std::move(preview)}));
        h.settle();
    }

    std::uint64_t pixels() { return site::frame_hash(h.pixels()); }
};

}  // namespace

UTest cases{"site.basic_motion", [] {

    "at least forty state drawings and separately authored public programs have distinct names"_test = [] {
        std::set<std::string> ids, names, groups;
        expect(site::basic_motions().size() >= 40);
        for (const auto& info : site::basic_motions()) {
            expect(ids.insert(std::string(info.id)).second) << std::string(info.id);
            expect(names.insert(std::string(info.en)).second) << std::string(info.en);
            expect(!info.zh.empty() && !info.purpose_en.empty() && !info.purpose_zh.empty() && !info.components.empty()) << std::string(info.id);
            groups.insert(std::string(info.group));
            // Authored programs are driven through the public Engine/host
            // adapter, covered by component_motion_test with real frames.
            if(info.authored_program)continue;
            Probe p(info.id);
            expect(!p.h.tree().node("main/preview/composition/choose")) << "the catalog has a real scene builder: " + std::string(info.id);
            const auto before = p.pixels();
            p.state.end.set(true); p.h.settle(seconds(4));
            expect(before != p.pixels()) << "the endpoint drawing changes: " + std::string(info.id);
        }
        expect(groups.size() >= 8);
    };

    "the designs fit phone and desktop viewports in English and Chinese"_test = [] {
        for (const float width : {260.0f, 700.0f}) for (std::string_view language : {"en", "zh"}) for (const auto& info : site::basic_motions()) {
            Probe p(info.id, width, language);
            for (const bool end : {false, true}) {
                p.state.end.set(end); p.h.settle(seconds(4));
                const auto stage = p.h.runtime().screen_bounds(p.h.runtime().find("main/preview"));
                for (const auto& node : p.h.tree().snapshot({.root = "main/preview"}).nodes) if (node.bounds) {
                    expect(node.bounds->x >= stage.x - 0.5f && node.bounds->right() <= stage.right() + 0.5f) << std::format("{} {} {} {}: x {} + {} outside {}..{}", info.id, language, width, end, node.bounds->x, node.bounds->width, stage.x, stage.right());
                    expect(node.bounds->y >= stage.y - 0.5f && node.bounds->bottom() <= stage.bottom() + 0.5f) << std::format("{} {} {} {}: y {} + {} outside {}..{} ({})", info.id, language, width, end, node.bounds->y, node.bounds->height, stage.y, stage.bottom(), node.id);
                }
            }
            if ((width == 700 && language == "en" && (info.id == "list-grid" || info.id == "stagger-grid" || info.id == "bars-rebalance")) ||
                (width == 260 && language == "zh" && (info.id == "list-grid" || info.id == "radio-focus")))
                expect(p.h.save_png(media() / std::format("{}-{}-{}.png", info.id, language, static_cast<int>(width))).has_value());
        }
    };

    "colour, depth, transforms, text, layout and rhythm have real intermediate frames"_test = [] {
        for (const std::string_view id : {"shape-morph", "surface-color", "outline-weight", "corner-shape", "depth", "slide-card", "lift-card", "scale-card", "turn-tile", "squash-chip", "list-grid", "reorder-list", "title-replace", "stagger-grid"}) {
            Probe p(id);
            const auto before = p.pixels();
            expect(p.h.save_png(media() / (std::string(id) + "-a.png")).has_value());
            p.state.end.set(true); p.h.advance(seconds(0.1));
            const auto middle = p.pixels();
            expect(p.h.save_png(media() / (std::string(id) + "-middle.png")).has_value());
            p.h.settle(seconds(4));
            const auto after = p.pixels();
            expect(p.h.save_png(media() / (std::string(id) + "-b.png")).has_value());
            expect(before != after && middle != before && middle != after) << std::string(id) + " changes on the way, not only at the endpoints";
        }
    };

    "the controls edit the shared scene model and notify the player"_test = [] {
        auto act = [](Probe& p, std::string_view node, std::string_view action, Value args = {}) {
            Invocation invocation; invocation.args = std::move(args);
            expect(p.h.tree().act("main/preview/composition/" + std::string(node), action, invocation).exit_code() == std::optional<int>(0)) << std::string(node);
            p.h.settle(seconds(4));
        };
        Probe toggle("toggle-thumb"); act(toggle, "setting/quiet", "toggle");
        expect(toggle.state.checked.peek() && toggle.changes > 0);
        Probe radio("radio-focus"); act(radio, "frequency/monthly", "select");
        expect(radio.state.checked.peek() && radio.changes > 0);
        Probe tabs("tab-indicator"); act(tabs, "subject/tabs/activity", "activate");
        expect(tabs.state.checked.peek() && tabs.changes > 0);
        Probe menu("menu-highlight"); act(menu, "menu/archive", "activate");
        expect(eq(menu.state.choice.peek(), std::string("archive"))); expect(menu.changes > 0);
        const auto selected = menu.h.tree().node("main/preview/composition/menu/archive");
        expect(selected && selected->states.has(State::selected));
        Probe progress("progress-fill"); act(progress, "upload/amount", "set_value", Value{}.set("value", 65.0));
        expect(eq(progress.state.value.peek(), 65.0)); expect(progress.changes > 0);
        Probe badge("badge-count"); act(badge, "inbox/add", "activate");
        expect(eq(badge.state.count.peek(), std::int64_t{4})); expect(badge.changes > 0);
        Probe step("step-advance"); act(step, "setup/steps", "increment");
        expect(eq(step.state.count.peek(), std::int64_t{4})); expect(step.changes > 0);
        Probe range("range-handles"); act(range, "interval/range", "set_range", Value{}.set("low", 25.0).set("high", 85.0));
        expect(eq(range.state.value.peek(), 25.0)); expect(eq(range.state.secondary.peek(), 85.0)); expect(range.changes > 0);
        range.state.end.set(true); range.h.settle();
        act(range, "interval/range", "set_range", Value{}.set("low", 40.0).set("high", 90.0));
        expect(eq(range.state.value.peek(), 10.0)); expect(eq(range.state.secondary.peek(), 60.0));
        Probe action("emphasis"); act(action, "actions/continue", "activate");
        expect(action.state.end.peek() && action.changes > 0);
    };

    "the signature motions can be watched as framework-rendered recordings"_test = [] {
        for (const std::string_view id : {"list-grid", "turn-tile", "stagger-grid"}) {
            Probe p(id, 460);
            for (int direction = 0; direction < 2; ++direction) {
                for (int i = 0; i < 5; ++i) p.h.record();
                p.state.end.set(direction == 0);
                for (int i = 0; i < 24; ++i) { p.h.advance(seconds(1.0 / 15)); p.h.record(); }
            }
            const auto file = media() / (std::string(id) + ".webp");
            expect(p.h.save_recording(file, {.delay_num = 1, .delay_den = 15, .quality = 82}).has_value());
            const auto animation = mlibs::image::load_animated_webp(file);
            expect(animation && animation->frames.size() > 2) << std::string(id) + " contains actual changing animation frames";
            expect(std::filesystem::file_size(file) < 1024 * 1024);
        }
    };
}};
