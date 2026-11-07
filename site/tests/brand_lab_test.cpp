// The brand exploration (A1 of the site plan): the mark's O and Mui, drawn by
// the framework from the site's brand data (brand/marks.mark.omui.json,
// brand/mui.sprite.omui.json) and saved headless for the maintainer to choose
// a direction from — each pixel of Mui a vector square, each mark an SVG path.
//
// Sheets in scratch "brand-lab": marks (four O treatments, large and at 32 and
// 16 px), mui16 and mui24 (the states in three palettes), lockups (four
// directions as the site would wear them); each as PNG, marks also as SVG.
import std;
import omui;
import omui.site;
import hanim.engine;
import hanim.render.vector;
import mlibs.image;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

[[noreturn]] void fatal(const std::string& why) {
    std::println(std::cerr, "brand lab: {}", why);
    std::exit(1);
}

const std::filesystem::path brand_dir = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "brand";

Value read_json(const std::string& name) {
    std::ifstream in(brand_dir / name, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    auto parsed = parse_json(text);
    if (!parsed) fatal(std::format("{} does not read: {}", name, parsed.error().text()));
    return std::move(*parsed);
}

Color hex_colour(std::string_view text) {
    std::uint32_t rgb = 0;
    std::from_chars(text.data() + 1, text.data() + text.size(), rgb, 16);
    return Color::hex(rgb);
}

xrender::Path path_of(std::string_view data) {
    xrender::Path out;
    const auto parsed = mlibs::geom::parse_path_data(data);
    if (!parsed) fatal(std::format("path data does not read: {}", parsed.error().message));
    std::size_t at = 0;
    for (const auto verb : parsed->verbs) {
        switch (verb) {
        case mlibs::geom::PathVerb::move: out.move_to(parsed->points[at++]); break;
        case mlibs::geom::PathVerb::line: out.line_to(parsed->points[at++]); break;
        case mlibs::geom::PathVerb::quad: out.quad_to(parsed->points[at], parsed->points[at + 1]); at += 2; break;
        case mlibs::geom::PathVerb::cubic: out.cubic_to(parsed->points[at], parsed->points[at + 1], parsed->points[at + 2]); at += 3; break;
        case mlibs::geom::PathVerb::close: out.close(); break;
        }
    }
    return out;
}

// The mark: an outline and the inner word, in a 100-unit square at `size`.
void draw_mark(xrender::DisplayList& list, const Value& marks, std::string_view outline, Point at, float size, Color ink) {
    list.push_transform(Affine::translate(at.x, at.y) * Affine::scale(size / 100, size / 100));
    const xrender::StrokeStyle line{.width = 6, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round};
    list.stroke_path(path_of(marks.get("outlines")->get(outline)->get("d")->as_string()), line, xrender::Paint::solid(ink));
    const xrender::StrokeStyle letter{.width = 4, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round};
    for (const Value& d : *marks.get("letters")->list()) list.stroke_path(path_of(d.as_string()), letter, xrender::Paint::solid(ink));
    const auto& dot = *marks.get("dot")->list();
    xrender::Path circle;
    circle.add_circle({static_cast<float>(dot[0].as_double()), static_cast<float>(dot[1].as_double())}, static_cast<float>(dot[2].as_double()));
    list.fill_path(circle, xrender::FillRule::nonzero, xrender::Paint::solid(ink));
    list.pop_transform();
}

// A frame of Mui: a vector square a cell, `cell` units wide. The cells of one
// colour are one path, so neighbours share no edge to be anti-aliased twice:
// a square drawn beside another at a fractional position leaves a faint seam
// (each covers half of the shared pixel; over each other they cover 3/4).
void draw_sprite(xrender::DisplayList& list, const Value& sprites, std::string_view sprite, std::string_view frame, std::string_view palette, Point at,
                 float cell) {
    const Value& colours = *sprites.get("palettes")->get(palette);
    const Value& rows = *sprites.get("sprites")->get(sprite)->get("frames")->get(frame);
    std::map<char, xrender::Path> by_colour;
    float y = at.y;
    for (const Value& row : *rows.list()) {
        const std::string_view cells = row.as_string();
        for (std::size_t x = 0; x < cells.size();) {
            const char letter = cells[x];
            std::size_t end = x + 1;
            while (end < cells.size() && cells[end] == letter) ++end;  // a run of one colour: one rectangle
            if (colours.get(std::string_view(&letter, 1)))
                by_colour[letter].add_rect({at.x + static_cast<float>(x) * cell, y, static_cast<float>(end - x) * cell, cell});
            x = end;
        }
        y += cell;
    }
    for (auto& [letter, path] : by_colour)
        list.fill_path(path, xrender::FillRule::nonzero, xrender::Paint::solid(hex_colour(colours.get(std::string_view(&letter, 1))->as_string())));
}

// A frame of Mui as hanim's pixel picture (C1): its palette's inks, its rows.
hanim::PixelGrid pixel_grid(const Value& sprites, std::string_view sprite, std::string_view frame, std::string_view palette, double cell) {
    hanim::PixelGrid grid{.cell = cell};
    for (const auto& [letter, colour] : *sprites.get("palettes")->get(palette)->map())
        grid.inks.push_back({letter[0], *hanim::Rgba::parse_hex(colour.as_string())});
    for (const Value& row : *sprites.get("sprites")->get(sprite)->get("frames")->get(frame)->list()) grid.rows.emplace_back(row.as_string());
    return grid;
}

// A playback's frame at `t` drawn into `list` at `at`: hanim's vector binding, the object's own geometry.
void draw_playback(hanim::Engine& engine, hanim::PlaybackRef playback, double t, xrender::DisplayList& list, Point at) {
    if (!engine.seek(playback, hanim::seconds(t))) fatal("seek");
    const auto frame = engine.frame(playback, 0);
    if (!frame || !frame->animation) fatal("no frame");
    const auto bound = hanim::vector_render::PreparedVector::bind(*frame->animation, frame->effects ? *frame->effects : hanim::PreparedEffects{});
    if (!bound) fatal(bound.error().text());
    const auto needs = bound->requirements();
    xrender::DisplayList staging;
    std::vector<mlibs::geom::Affine> world(needs.world_transforms);
    std::vector<float> parameters(needs.backend_parameters);
    std::vector<mlibs::geom::Rect> layers(needs.effect_layers);
    list.push_transform(Affine::translate(at.x, at.y));
    if (const auto recorded = bound->record(frame->geometry, frame->effect_values, list, {&staging, world, parameters, layers}); !recorded)
        fatal(recorded.error().text());
    list.pop_transform();
}

Element canvas(std::string id, float width, float height, std::function<void(xrender::DisplayList&)> draw) {
    return painter({.id = std::move(id), .width = width, .height = height, .paint = [draw = std::move(draw)](PaintContext& p) { draw(p.list); }});
}
Element caption(std::string id, std::string text, float size = 15) {
    return omui::text({.id = std::move(id), .text = std::move(text), .color = Fill(Color::hex(0x55555f)), .size = size});
}

void save(Headless& h, const std::filesystem::path& dir, std::string_view name, Size size) {
    h.settle(seconds(0.2));
    check(h.save_png(dir / std::format("{}.png", name), Rect{0, 0, size.width, size.height}).has_value(), std::format("{} saved", name));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site.brand_lab"};

    s.test("A1: the mark's O four ways, Mui at 16 and 24 in three palettes, four directions — drawn by the framework and saved", [] {
        const Value marks = read_json("marks.mark.omui.json");
        const Value sprites = read_json("mui.sprite.omui.json");
        const auto dir = scratch_dir("brand-lab");
        const Color indigo = Color::hex(0x4f46e5);
        const std::vector<std::pair<std::string, std::string>> outlines{
            {"now", "today: an ellipse"}, {"ears", "A · ears"}, {"stroke", "B · one stroke, a tail"}, {"ears_tail", "C · ears and a tail"}};

        // The marks, large, then at 32 and 16 px: a mark must hold at a favicon's size.
        {
            const Size size{1080, 420};
            Headless h(HeadlessOptions{.size = size, .scale = 1});
            std::vector<Element> columns;
            for (const auto& [name, words] : outlines)
                columns.push_back(column({.id = name, .gap = 10, .align = "center"},
                                         {canvas(name + "-large", 220, 220, [&marks, name, indigo](xrender::DisplayList& l) { draw_mark(l, marks, name, {10, 10}, 200, indigo); }),
                                          canvas(name + "-small", 220, 40, [&marks, name, indigo](xrender::DisplayList& l) {
                                              draw_mark(l, marks, name, {70, 4}, 32, indigo);
                                              draw_mark(l, marks, name, {130, 12}, 16, indigo);
                                          }),
                                          caption(name + "-words", words)}));
            h.mount("main", stack({.id = "sheet", .width = size.width, .height = size.height, .padding = 30, .background = Fill(Color::hex(0xfbfbfa))},
                                  {row({.id = "marks", .gap = 40}, std::move(columns))}));
            save(h, dir, "marks", size);
            const auto vectors = h.svg();
            check(vectors.has_value() && vectors->islands.empty(), "the marks as SVG, vectors alone");
            if (vectors) std::ofstream(dir / "marks.svg", std::ios::binary) << vectors->svg;
        }

        // Mui: each state in each palette, the head of 16 and the body of 24.
        for (const auto& [sprite, cell] : {std::pair{std::string("head16"), 10.0f}, std::pair{std::string("body24"), 8.0f}}) {
            const Value& frames = *sprites.get("sprites")->get(sprite)->get("frames");
            const float side = static_cast<float>(sprites.get("sprites")->get(sprite)->get("size")->as_double()) * cell;
            std::vector<std::string> states;
            for (const auto& [name, rows] : *frames.map()) states.push_back(name);
            const Size size{40 + (side + 30) * static_cast<float>(states.size()) + 120, 40 + (side + 50) * 4};
            Headless h(HeadlessOptions{.size = size, .scale = 1});
            std::vector<Element> rows;
            // The chosen palettes (A2): indigo and night, and the agent's eyes in each.
            for (const std::string palette : {"indigo", "night", "agent", "agent_night"}) {
                const bool dark = palette.ends_with("night");
                std::vector<Element> cells{omui::text({.id = palette + "-name", .text = palette,
                                                       .color = Fill(dark ? Color::hex(0x9a9aa5) : Color::hex(0x55555f)), .size = 15})};
                for (const auto& state : states)
                    cells.push_back(canvas(palette + "-" + state, side, side, [&sprites, sprite, state, palette, cell](xrender::DisplayList& l) {
                        draw_sprite(l, sprites, sprite, state, palette, {0, 0}, cell);
                    }));
                rows.push_back(row({.id = palette, .gap = 30, .align = "center",
                                    .background = dark ? Fill(Color::hex(0x0c0d10)) : Fill(Color::hex(0xfbfbfa))},
                                   std::move(cells)));
            }
            h.mount("main", column({.id = "sheet", .width = size.width, .height = size.height, .padding = 20, .gap = 20, .background = Fill(Color::hex(0xfbfbfa))},
                                   std::move(rows)));
            save(h, dir, sprite == "head16" ? "mui16" : "mui24", size);
        }

        // Four directions as the site would wear them: the mark in the header,
        // a claim, and Mui beside it.
        {
            struct Direction {
                std::string name, outline, sprite, frame, palette, words;
            };
            const std::vector<Direction> directions{{"d1", "ears", "head16", "idle", "indigo", "1 · ears · the head of 16 · indigo"},
                                                    {"d2", "ears_tail", "body24", "sit", "indigo", "2 · ears and tail · the body of 24 · indigo"},
                                                    {"d3", "ears", "body24", "sit", "cream", "3 · ears · the body of 24 · cream"},
                                                    {"d4", "now", "head16", "happy", "night", "4 · today's O · the head of 16 · night"}};
            const Size size{1200, 760};
            Headless h(HeadlessOptions{.size = size, .scale = 1});
            std::vector<Element> tiles;
            for (const Direction& d : directions) {
                const bool night = d.palette == "night";
                const Color ink = night ? Color::hex(0x8b85ff) : indigo;
                const Color words = night ? Color::hex(0xededef) : Color::hex(0x16161a);
                tiles.push_back(column({.id = d.name, .width = 560, .height = 330, .padding = 24, .gap = 8,
                                        .background = Fill(night ? Color::hex(0x0c0d10) : Color::hex(0xffffff)), .radius = 18},
                                       {row({.id = "header", .gap = 10, .align = "center"},
                                            {canvas("mark", 36, 36, [&marks, d, ink](xrender::DisplayList& l) { draw_mark(l, marks, d.outline, {0, 0}, 36, ink); }),
                                             omui::text({.id = "name", .text = "omui", .color = Fill(ink), .size = 18, .weight = 600.0f})}),
                                        row({.id = "hero", .gap = 24, .align = "center"},
                                            {column({.id = "claim", .gap = 6},
                                                    {omui::text({.id = "one", .text = "One interface.", .color = Fill(words), .size = 34, .weight = 700.0f}),
                                                     omui::text({.id = "two", .text = "Every perspective.", .color = Fill(words), .size = 34, .weight = 700.0f})}),
                                             canvas("mui", 192, 192, [&sprites, d](xrender::DisplayList& l) {
                                                 const float cell = d.sprite == "head16" ? 9.0f : 8.0f;
                                                 draw_sprite(l, sprites, d.sprite, d.frame, d.palette, {0, 0}, cell);
                                             })}),
                                        omui::text({.id = "words", .text = d.words, .color = Fill(night ? Color::hex(0x9a9aa5) : Color::hex(0x55555f)), .size = 14})}));
            }
            h.mount("main", column({.id = "sheet", .width = size.width, .height = size.height, .padding = 30, .gap = 24, .background = Fill(Color::hex(0xeeede9))},
                                   {row({.id = "top", .gap = 24}, {std::move(tiles[0]), std::move(tiles[1])}),
                                    row({.id = "bottom", .gap = 24}, {std::move(tiles[2]), std::move(tiles[3])})}));
            save(h, dir, "lockups", size);
        }
        for (const auto* name : {"marks.png", "mui16.png", "mui24.png", "lockups.png"})
            check(std::filesystem::exists(dir / name) && std::filesystem::file_size(dir / name) < 1'000'000, std::format("{} under 1 MB", name));
    });

    s.test("A2: the mark, chosen — the O with ears round mui, drawn by the framework; site/brand/omui-mark.svg is what it draws", [] {
        const Value marks = read_json("marks.mark.omui.json");
        Headless h(HeadlessOptions{.size = {100, 100}, .scale = 1});
        h.mount("main", stack({.id = "mark", .width = 100.0f, .height = 100.0f},
                              {canvas("o", 100, 100, [&marks](xrender::DisplayList& l) { draw_mark(l, marks, "ears", {0, 0}, 100, Color::hex(0x4f46e5)); })}));
        h.settle();
        const auto vectors = h.svg();
        check(vectors.has_value() && vectors->islands.empty(), "vectors alone");
        if (!vectors) return;
        const auto file = brand_dir / "omui-mark.svg";
        std::ifstream in(file, std::ios::binary);
        const std::string kept{std::istreambuf_iterator<char>(in), {}};
        if (kept != vectors->svg) {
            const auto out = scratch_dir("brand-mark") / "omui-mark.svg";
            std::ofstream(out, std::ios::binary) << vectors->svg;
            check(false, std::format("site/brand/omui-mark.svg is not what the framework draws: copy {} over it", out.string()));
        }
    });

    s.test("C1: Mui as hanim's pixel picture — the staircase into the curve, scattered and back together — sampled and saved", [] {
        const Value sprites = read_json("mui.sprite.omui.json");
        const auto dir = scratch_dir("brand-pixels");
        const auto grid = pixel_grid(sprites, "body24", "sit", "indigo", 6);
        hanim::Engine engine = *hanim::Engine::make();
        const auto sharp = engine.create_object(*hanim::pixel_object("mui", grid));
        const auto round = engine.create_object(*hanim::pixel_object("mui", grid, {.form = hanim::PixelForm::smooth}));
        const auto pixels = engine.create_object(*hanim::pixel_object("mui", grid, {.form = hanim::PixelForm::pixels}));
        check(sharp && round && pixels, "Mui three ways");
        const auto morph = engine.play(hanim::anim::morph(*sharp, *round).duration(hanim::seconds(1)).easing(hanim::ease::linear));
        const auto scatter = engine.play(hanim::sequence({hanim::anim::scatter_out(*pixels, grid, {.seed = 11, .distance = 34}),
                                                          hanim::anim::assemble_in(*pixels, grid, {.seed = 11, .distance = 34})}));
        check(morph && scatter && engine.pause(*morph) && engine.pause(*scatter), "both played, held");
        if (!morph || !scatter) return;
        const double length = engine.inspect(*scatter)->duration.count();
        const std::array<double, 5> morph_at{0, .25, .5, .75, 1};
        const std::array<double, 5> scatter_at{0, .2, .45, .9, 1.35};
        check(std::abs(length - 1.8) < 1e-9, std::format("out and back in 1.8 s: {}", length));
        const Size size{1080, 520};
        Headless h(HeadlessOptions{.size = size, .scale = 1});
        const auto strip = [&](std::string name, hanim::PlaybackRef playback, std::span<const double> times, std::string words) {
            std::vector<Element> cells;
            for (std::size_t i = 0; i < times.size(); ++i)
                cells.push_back(column({.id = std::format("{}{}", name, i), .gap = 4, .align = "center"},
                                       {canvas(std::format("{}-frame{}", name, i), 190, 170,
                                               [&engine, playback, t = times[i]](xrender::DisplayList& l) { draw_playback(engine, playback, t, l, {23, 13}); }),
                                        caption(std::format("{}-t{}", name, i), std::format("{:.2f} s", times[i]), 13)}));
            return column({.id = name, .gap = 6}, {caption(name + "-words", words), row({.id = name + "-frames", .gap = 14}, std::move(cells))});
        };
        h.mount("main", column({.id = "sheet", .width = size.width, .height = size.height, .padding = 24, .gap = 18, .background = Fill(Color::hex(0xfbfbfa))},
                               {strip("morph", *morph, morph_at, "the staircase into the curve: one path an ink, the same segments"),
                                strip("scatter", *scatter, scatter_at, "a part a pixel: scattered from the middle out, then back, the farthest first")}));
        save(h, dir, "mui-pixels", size);
        check(std::filesystem::file_size(dir / "mui-pixels.png") < 1'000'000, "under 1 MB");
    });

    return s.run(argc, argv);
}
