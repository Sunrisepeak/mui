module openmui.site;

import std;
import openmui;

namespace omui::site {

std::span<const BrandShape> brand_geometry() {
    static const auto shapes = [] {
        std::array<BrandShape, 5> g;
        // A slightly tall O, with enough counter space to hold the inner word.
        g[0] = {xrender::Path{}.add_ellipse({8, 5, 84, 90}), 6, "outline"};
        g[1].part = "letter_m";
        g[1].width = 4;
        g[1].path.move_to({24, 64}).line_to({24, 44})
            .move_to({24, 50}).cubic_to({24, 41}, {35, 41}, {35, 50}).line_to({35, 64})
            .move_to({35, 50}).cubic_to({35, 41}, {46, 41}, {46, 50}).line_to({46, 64});
        g[2].part = "letter_u";
        g[2].width = 4;
        g[2].path.move_to({55, 44}).line_to({55, 56}).cubic_to({55, 66}, {69, 66}, {69, 56}).line_to({69, 44});
        g[3] = {xrender::Path{}.move_to({78, 44}).line_to({78, 64}), 4, "letter_i"};
        g[4] = {xrender::Path{}.add_circle({78, 35}, 2.5f), 0, "letter_i"};
        return g;
    }();
    return shapes;
}

namespace {

void paint_shapes(xrender::DisplayList& out, Color ink, Point at, float size, std::string_view part = {}) {
    if (!(size > 0) || !std::isfinite(size)) return;
    out.push_transform(Affine::translate(at.x, at.y) * Affine::scale(size / 100, size / 100));
    const xrender::Paint colour = xrender::Paint::solid(ink);
    for (const BrandShape& shape : brand_geometry()) {
        if (!part.empty() && shape.part != part) continue;
        if (shape.width == 0) out.fill_path(shape.path, xrender::FillRule::nonzero, colour);
        else out.stroke_path(shape.path, {.width = shape.width, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, colour);
    }
    out.pop_transform();
}

std::string path_data(const xrender::Path& path) {
    std::string out;
    std::size_t at = 0;
    auto point = [&] {
        const Point p = path.points()[at++];
        out += std::format(" {:g} {:g}", p.x, p.y);
    };
    for (const xrender::PathVerb verb : path.verbs()) {
        switch (verb) {
            case xrender::PathVerb::move: out += "M"; point(); break;
            case xrender::PathVerb::line: out += "L"; point(); break;
            case xrender::PathVerb::quad: out += "Q"; point(); point(); break;
            case xrender::PathVerb::cubic: out += "C"; point(); point(); point(); break;
            case xrender::PathVerb::close: out += "Z"; break;
        }
    }
    return out;
}

Element study_stage(BrandStudy& study, float room, bool zh) {
    const bool narrow = room < 600;
    const float size = static_cast<float>(std::min(study.size.get(), static_cast<double>(room * (narrow ? 0.55f : 0.3f))));
    const bool unfolded = study.unfolded.get();
    const std::string ink = study.ink.get();
    constexpr std::array<std::string_view, 4> names{"outline", "letter_m", "letter_u", "letter_i"};
    constexpr std::array<std::string_view, 4> labels{"O — enclosing outline", "m — first inner letter", "u — second inner letter", "i — stem and dot"};
    constexpr std::array<std::string_view, 4> chinese_labels{"O — 外圈", "m — 第一个内部字母", "u — 第二个内部字母", "i — 竖线和圆点"};
    constexpr std::array<float, 4> wide_x{-0.65f, 0.04f, 0.40f, 0.65f};
    constexpr std::array<float, 4> phone_x{0, -0.24f, 0, 0.24f};
    std::vector<Element> parts;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string name(names[i]);
        Element drawn = painter({.id = "path", .width = Dim(size), .height = Dim(size), .role = Role::image, .label = std::string(zh ? chinese_labels[i] : labels[i]),
                                 .paint = [name, size, ink](PaintContext& ctx) { paint_shapes(ctx.list, ctx.theme.color(ink), {0, 0}, size, name); }});
        Element part = box({.id = name, .width = Dim(size), .height = Dim(size),
                            .translate_x = unfolded ? size * (narrow ? phone_x[i] : wide_x[i]) : 0,
                            .translate_y = unfolded ? size * (i == 0 ? -0.12f : 0.45f) : 0}, {std::move(drawn)});
        part.prop("motion", Source(Value("morph")));
        parts.push_back(std::move(part));
    }
    return stack({.id = "drawing", .width = Dim(mlibs::layout::pct(100)), .height = Dim(narrow ? 260 : 300), .justify = "center", .align = "center",
                   .background = "surface_variant", .radius = 20}, std::move(parts));
}

}  // namespace

void paint_mark(xrender::DisplayList& out, const Theme& theme, Point at, float size) {
    paint_shapes(out, theme.color("accent"), at, size);
}

Element brand_mark(std::string id, float size) {
    return painter({.id = std::move(id), .width = Dim(size), .height = Dim(size), .role = Role::none,
                     .paint = [size](PaintContext& ctx) { paint_mark(ctx.list, ctx.theme, {0, 0}, size); }});
}

Element animated_brand_mark(std::string id, float size, Signal<bool> unfolded) {
    const float spread = size <= 40 ? 0.36f : 1.0f;
    constexpr std::array<std::string_view, 4> names{"outline", "letter_m", "letter_u", "letter_i"};
    constexpr std::array<std::string_view, 4> labels{"O", "m", "u", "i"};
    constexpr std::array<float, 4> horizontal{-0.03f, -0.16f, 0, 0.12f};
    constexpr std::array<float, 4> vertical{-0.10f, 0.20f, 0.20f, 0.20f};
    std::vector<Element> parts;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string part(names[i]);
        Element drawing = painter({.id = "path", .width = Dim(size), .height = Dim(size), .role = Role::image, .label = std::string(labels[i]),
            .paint = [part, size](PaintContext& ctx) { paint_shapes(ctx.list, ctx.theme.color("accent"), {0, 0}, size, part); }});
        Element group = box({.id = part, .width = Dim(size), .height = Dim(size),
            .translate_x = computed([unfolded, size, spread, x = horizontal[i]] { return unfolded.get() ? size * spread * x : 0.0f; }),
            .translate_y = computed([unfolded, size, spread, y = vertical[i]] { return unfolded.get() ? size * spread * y : 0.0f; })}, {std::move(drawing)});
        group.prop("motion", Source(Value("morph")));
        group.prop("role", Source(Value("group"))).prop("label", Source(Value(std::string(labels[i]))));
        parts.push_back(std::move(group));
    }
    return stack({.id = std::move(id), .width = Dim(size), .height = Dim(size), .justify = "center", .align = "center", .label = "omui"}, std::move(parts));
}

std::string brand_svg(Color ink) {
    auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); };
    const std::string colour = std::format("#{:02x}{:02x}{:02x}", channel(ink.r), channel(ink.g), channel(ink.b));
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" role=\"img\" aria-label=\"omui\">"
                      "<title>omui</title>";
    for (const BrandShape& shape : brand_geometry())
        svg += shape.width == 0 ? std::format("<path d=\"{}\" fill=\"{}\"/>", path_data(shape.path), colour)
                               : std::format("<path d=\"{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"{:g}\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",
                                             path_data(shape.path), colour, shape.width);
    return svg + "</svg>";
}

Element brand_study(BrandStudy& study, Runtime* runtime, Signal<std::string>& lang, float room) {
    auto say = [&lang](std::string_view en, std::string_view zh) {
        return computed([language = &lang, en = std::string(en), zh = std::string(zh)] { return language->get() == "zh" ? zh : en; });
    };
    auto* state = &study;
    auto* language = &lang;
    Element title = text({.id = "title", .text = say("A mark. Made here.", "标志，也是程序。"), .style = "title"});
    title.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    return column({.id = "identity", .gap = 20}, {
        text({.id = "eyebrow", .text = say("OMUI / IDENTITY", "OMUI / 品牌"), .style = "label", .color = "accent"}),
        std::move(title),
        text({.id = "lead", .text = say("An O holds three letters. Five vector paths, drawn by omui itself. Pull the mark apart, put it back together, and take the original with you.",
                                         "一个 O，容纳三个字母。五条矢量路径，由 omui 自己绘制。展开它、重新组装，再把原件带走。"),
              .style = "body", .max_width = Dim(700.0f)}),
        dynamic([state, room, language] { return std::vector<Element>{study_stage(*state, room, language->get() == "zh")}; }),
        row({.id = "controls", .gap = 16, .align = "center", .wrap = "wrap"}, {
            button({.id = "assemble", .text = computed([state, language] { return std::string(state->unfolded.get() ? (language->get() == "zh" ? "重新组装" : "Assemble") : (language->get() == "zh" ? "展开标志" : "Take apart")); }),
                     .variant = ButtonVariant::primary, .on_activated = [state] { state->unfolded.set(!state->unfolded.peek()); state->copied.set(std::string()); }}),
            column({.id = "sizing", .gap = 4}, {
                text({.id = "label", .text = say("Size", "尺寸"), .style = "caption", .color = "text_muted"}),
                slider({.id = "size", .label = say("Size", "尺寸"), .max = 240.0, .min = 64.0, .step = 8.0, .value = study.size, .width = Dim(std::min(room, 240.0f))})}),
            segmented({.id = "ink", .labels = computed([language] { return language->get() == "zh" ? std::vector<std::string>{"品牌色", "单色"} : std::vector<std::string>{"Brand", "Mono"}; }),
                        .options = std::vector<std::string>{"accent", "text"}, .value = study.ink}),
            button({.id = "reset", .text = say("Reset", "重置"), .variant = ButtonVariant::ghost, .on_activated = [state] {
                state->unfolded.set(false); state->size.set(192.0); state->ink.set(std::string("accent")); state->copied.set(std::string());
            }})}),
        row({.id = "original", .gap = 16, .align = "center", .wrap = "wrap"}, {
            button({.id = "copy_svg", .text = say("Copy SVG", "复制 SVG"), .on_activated = [state, runtime] {
                if (!runtime) return;
                runtime->set_clipboard_text(brand_svg(runtime->theme().color(state->ink.peek())));
                state->copied.set(std::string("svg"));
            }}),
            text({.id = "status", .text = computed([state, language] { return state->copied.get().empty() ? std::string() : (language->get() == "zh" ? std::string("原始矢量已复制。") : std::string("Original vector copied.")); }), .style = "caption", .color = "text_muted"})}),
        text({.id = "standard", .text = say("omui is the implementation. openmui is the open specification. This mark belongs to omui.",
                                             "omui 是具体实现，openmui 是开放规范。这个标志属于 omui。"), .style = "body", .max_width = Dim(700.0f)})});
}

std::string brand_html(bool zh, Color ink) {
    return std::format("<h1>{}</h1><p>{}</p>{}<p>{}</p>\n", zh ? "标志，也是程序。" : "A mark. Made here.",
                       zh ? "大写 O 内含小写 mui，由 omui 的矢量代码绘制。" : "An O encloses the inner letters, drawn by omui's own vector code.",
                       brand_svg(ink), zh ? "omui 是具体实现，openmui 是开放规范。" : "omui is the implementation. openmui is the open specification.");
}

}  // namespace omui::site
