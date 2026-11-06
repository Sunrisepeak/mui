// Measure real caption strokes over their actual same-frame carrier, using
// the production display list and a glyph coverage pass (not AA edge colors).
import std;
import omui;
import mlibs.math;
import omui.site;
import mlibs.testing;
import xrender.text;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view root = "main/body/view/agent/page/agent_feature";
std::string path(std::string_view leaf) { return std::string(root) + "/" + std::string(leaf); }

struct Contrast { double minimum = 21; std::size_t samples = 0; };
Contrast contrast(Runtime& rt, NodeId target) {
    xrender::DisplayList without, coverage;
    std::vector<NodeId> nodes;
    const auto& source = rt.display_list();
    for (const auto& command : source.commands()) {
        if (const auto* b = std::get_if<xrender::BeginNode>(&command)) nodes.push_back(rt.node_at_index(b->node));
        const bool selected = !nodes.empty() && nodes.back() == target;
        const auto* glyphs = std::get_if<xrender::DrawGlyphs>(&command);
        if (!glyphs || !selected) without.append_command(command);
        if (const auto* layer = std::get_if<xrender::PushLayer>(&command)) {
            // Coverage is geometric. Keep compositing opacity, but remove
            // material RGB treatment from this diagnostic mask only.
            coverage.push_layer(layer->opacity, xrender::BlendMode::normal, {}, {}, layer->bounds, layer->shapes);
        } else {
            const auto index = coverage.size(); coverage.append_command(command);
            coverage.recolor(index, index + 1, [selected, glyphs](Color color) {
                return glyphs && selected ? Color::white().with_alpha(color.a) : Color::black().with_alpha(color.a);
            });
        }
        if (std::holds_alternative<xrender::EndNode>(command) && !nodes.empty()) nodes.pop_back();
    }
    check(without.balanced() && coverage.balanced());
    const int width = static_cast<int>(rt.size().width), height = static_cast<int>(rt.size().height);
    xrender::raster::Surface full(width, height), paper(width, height), mask(width, height);
    xrender::text::GlyphCache glyphs(rt.fonts()); xrender::CpuRenderer renderer;
    const xrender::Resources resources{&rt.images(), &glyphs};
    const auto a = renderer.render(source, full, resources, {.background = Color::white()});
    const auto b = renderer.render(without, paper, resources, {.background = Color::white()});
    const auto c = renderer.render(coverage, mask, resources, {.background = Color::black()});
    check(a.missing_glyphs == 0 && b.missing_glyphs == 0 && c.missing_glyphs == 0);
    check(a.missing_images == 0 && b.missing_images == 0 && c.missing_images == 0);
    Contrast result;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto m = mask.at(x, y); if (m.r < 230) continue;
        const double alpha = static_cast<double>(m.r) / 255;
        const auto pixel = full.at(x, y), under = paper.at(x, y);
        const auto ink = [alpha](std::uint8_t p, std::uint8_t b) {
            return std::clamp((static_cast<double>(p) / 255 - (1 - alpha) * static_cast<double>(b) / 255) / alpha, 0.0, 1.0);
        };
        const double a = mlibs::math::color::relative_luminance(ink(pixel.r, under.r), ink(pixel.g, under.g), ink(pixel.b, under.b));
        const double b = mlibs::math::color::relative_luminance(static_cast<double>(under.r) / 255, static_cast<double>(under.g) / 255, static_cast<double>(under.b) / 255);
        result.minimum = std::min(result.minimum, mlibs::math::color::contrast_ratio(a, b)); ++result.samples;
    }
    return result;
}
struct Page {
    Headless h;
    explicit Page(std::string_view lang, float width)
        : h(HeadlessOptions{.size = {width, 844}, .tier = mlibs::pipeline::Tier::t4}) {
        auto app = site::app("agent/develop", lang); h.runtime().set_theme(app.theme); app.setup(h.runtime());
        h.mount("main", app.view()); (void)h.step();
        h.move(h.runtime().screen_bounds(h.runtime().find(path("specimen/title"))).center());
        h.move({1, 1}); h.settle(seconds(0.8));
    }
    void design() {
        Invocation call; call.args = Value{}.set("value", "design");
        check(h.tree().act(path("modes"), "set_value", call).exit_code() == std::optional<std::int64_t>(0));
        (void)h.step();
    }
};
}

int main(int argc, char** argv) {
    Suite suite("site.agent-contrast");
    const auto media = scratch_dir("agent-contrast");
    suite.test("persistent caption strokes remain readable while the actual task changes light-dark polarity", [media] {
        for (const auto lang : {"en", "zh"}) for (const float width : {390.0f, 1280.0f}) {
            Page page(lang, width); const auto value = page.h.tree().node(path("specimen/title"))->value;
            page.design(); Value::List frames;
            for (int frame = 0; frame < 24; ++frame) {
                Value row; row.set("frame", frame);
                for (const auto name : {"eyebrow", "receipt"}) {
                    const auto target = page.h.runtime().find(path(std::string("specimen/") + name));
                    const auto sampled = contrast(page.h.runtime(), target);
                    check(sampled.samples >= 3, std::format("{} {} has actual stroke interiors", lang, name));
                    // 8-bit raster inference has quantization error; the
                    // analytic public-Morph tests use the exact 4.5 target.
                    check(sampled.minimum >= 4.4, std::format("{} {} {}px frame{} contrast={}", lang, name, width, frame, sampled.minimum));
                    row.set(name, Value{}.set("minimum", sampled.minimum).set("samples", static_cast<std::int64_t>(sampled.samples)));
                }
                frames.push_back(std::move(row));
                if (frame == 6 || frame == 12) check(page.h.save_png(media / std::format("{}-{}-frame{}.png", lang, static_cast<int>(width), frame)).has_value());
                page.h.record(); (void)page.h.step();
            }
            check(page.h.tree().node(path("specimen/title"))->value == value, "readability protection preserves task data");
            check(page.h.save_recording(media / std::format("{}-{}.webp", lang, static_cast<int>(width)), {.delay_num = 1, .delay_den = 60}).has_value());
            std::println("contrast evidence {} {}: {}", lang, static_cast<int>(width), Value(std::move(frames)).json());
        }
    });
    return suite.run(argc, argv);
}
