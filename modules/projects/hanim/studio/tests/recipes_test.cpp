// Every recipe of hanim's library, its course drawn: one document a recipe (a card on a small stage, the recipe's
// defaults), eight moments from its start to its end drawn by the CPU reference side by side — the strips the
// recipes' page shows (modules/projects/hanim/media/recipes/). The recipes are the library's own list
// (anim::recipes()) and the studio's moves, so a recipe the studio cannot write fails here; each strip must change
// through its course, and the committed ones are held to what is drawn now (within what a compiler's rounding moves).
import std;
import hanim.studio;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace studio = hanim::studio;

namespace {

constexpr int width = 120, height = 90, moments = 8;

// A recipe's document: the card (filled and outlined; only outlined for the recipes that write its line, so the line
// is what is seen), a second card for `swap` and three for `reorder`.
std::string document_of(std::string_view step) {
    const bool writes = step.contains("write");
    return std::format(R"({{
  "version": 1, "size": [{}, {}], "background": "#101820",
  "objects": [
    {{ "id": "card", "shape": {{ "rect": {{ "size": [36, 36], "radius": 8 }} }}, {}"stroke": {{ "color": "#ffd9c7", "width": {} }}, "at": [60, 45] }}{}
  ],
  "program": {}
}})",
                       width, height, writes ? "" : R"("fill": "#ff6a3d", )", writes ? 4 : 2,
                       step.contains("\"swap\"") ? R"(,
    { "id": "other", "shape": { "rect": { "size": [36, 36], "radius": 8 } }, "fill": "#3db8ff", "at": [96, 45] })"
                       : step.contains("\"reorder\"")
                           ? R"(,
    { "id": "two", "shape": { "rect": { "size": [24, 24], "radius": 6 } }, "fill": "#3db8ff", "at": [24, 45] },
    { "id": "three", "shape": { "rect": { "size": [24, 24], "radius": 6 } }, "fill": "#ffd23d", "at": [96, 45] })"
                           : "",
                       step);
}

struct Recipe {
    std::string name, step;  // the step as a document writes it
};

std::vector<Recipe> all_recipes() {
    std::vector<Recipe> out;
    for (const auto& recipe : hanim::anim::recipes())
        out.push_back({std::string(recipe.name), std::format(R"({{ "do": "{}", "object": "card" }})", recipe.name)});
    out.push_back({"drop", R"({ "do": "drop", "object": "card", "by": [0, 24], "bounces": 2 })"});
    out.push_back({"arc_to", R"({ "do": "arc_to", "object": "card", "to": [96, 45] })"});
    out.push_back({"swap", R"({ "do": "swap", "object": "card", "with": "other", "by": [36, 0] })"});
    out.push_back({"reorder", R"({ "do": "reorder", "objects": ["two", "card", "three"], "by": [[36, 0], [36, 0], [-72, 0]] })"});
    return out;
}

// The committed strips, beside this package.
std::filesystem::path committed() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path() / "media" / "recipes";
}

}  // namespace

UTest cases{"hanim.studio.recipes", [] {
    "every recipe's course in eight moments, the committed strips what is drawn now"_test = [] {
        const auto dir = mlibs::testing::scratch_dir("hanim-recipes");
        const auto recipes = all_recipes();
        std::size_t drawn = 0;
        for (const Recipe& recipe : recipes) {
            const auto document = studio::read(document_of(recipe.step));
            expect(document.has_value()) << recipe.name << ": " << (document ? "" : document.error().front().text);
            if (!document) continue;
            auto opened = studio::Studio::open(*document);
            expect(opened.has_value()) << recipe.name;
            if (!opened) continue;
            const double length = opened->duration().count();
            expect(std::isfinite(length) && length > 0) << recipe.name << ": a course of " << length << " s";
            if (!(std::isfinite(length) && length > 0)) continue;
            xrender::raster::Surface strip(width * moments, height);
            std::set<std::vector<std::uint8_t>> distinct;
            for (int moment = 0; moment < moments; ++moment) {
                xrender::raster::Surface frame(width, height);
                const auto rendered = opened->render(hanim::seconds(length * moment / (moments - 1)), frame);
                expect(rendered.has_value()) << recipe.name;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(frame.pixels().data());
                distinct.emplace(bytes, bytes + frame.pixels().size() * 4);
                for (int y = 0; y < height; ++y) std::copy_n(frame.row(y), width, strip.row(y) + moment * width);
            }
            expect(ge(distinct.size(), std::size_t{4})) << recipe.name << ": its course changes what is seen";
            const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(strip.pixels().data()), strip.width(), height, strip.width() * 4,
                                          mlibs::image::Alpha::premultiplied};
            const auto png = mlibs::image::encode_png(view);
            expect(png.has_value());
            if (!png) continue;
            const auto name = recipe.name + ".png";
            std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
            ++drawn;
            // The committed strip: the same pixels, but for a compiler's rounding at a few edges.
            std::ifstream in(committed() / name, std::ios::binary);
            expect(in.good()) << name << " is not committed: copy it from " << dir.generic_string();
            if (!in) continue;
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
            const auto kept = mlibs::image::decode_png(bytes);
            expect(kept && kept->width == strip.width() && kept->height == height) << name;
            if (!kept || kept->width != strip.width() || kept->height != height) continue;
            mlibs::image::Image now = *mlibs::image::decode_png(*png);
            std::size_t off = 0;
            for (std::size_t at = 0; at < now.rgba.size(); ++at) off += std::abs(int(now.rgba[at]) - int(kept->rgba[at])) > 8;
            expect(le(off, now.rgba.size() / 1000)) << name << ": " << off << " channels differ from the committed strip — copy it from " << dir.generic_string() << " if the change is meant";
        }
        std::println("    {} recipes drawn in {}", drawn, dir.generic_string());
        expect(eq(drawn, recipes.size()));
    };

    "the recipes' page shows every strip"_test = [] {
        std::ifstream in(committed().parent_path().parent_path() / "RECIPES.md", std::ios::binary);
        const std::string page{std::istreambuf_iterator<char>(in), {}};
        for (const Recipe& recipe : all_recipes())
            expect(page.contains(std::format("| `{}` |", recipe.name)) && page.contains(std::format("(media/recipes/{}.png)", recipe.name))) << recipe.name;
    };
}};
