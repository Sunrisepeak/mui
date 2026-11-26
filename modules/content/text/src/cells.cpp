// The cell measure (:shaper's `cells`; terminal design §3 ①): text as a terminal's grid shows it. Every face is the
// terminal's own font, which no file describes: it has every character, and a grapheme takes its cells
// (mlibs::unicode::columns) times a column's width, whatever the size; a line is a row. A glyph's id is its code
// point, a grapheme's code points its glyphs in order, one cluster, standing where the grapheme starts (its cells
// are the last one's advance): a cell renderer reads the characters back from a display list's glyphs.
module mlibs.text.api;

import std;
import mlibs.compose;
import mlibs.unicode;
import :shaped;
import :shaper;

namespace mlibs::text::cells {

namespace {

// The grid faces made now measure by (set by `install`, before text is measured).
Grid& current() noexcept {
    static Grid grid;
    return grid;
}

class CellFace final : public ShaperFace {
public:
    explicit CellFace(Grid grid) : grid_(grid) {}
    [[nodiscard]] bool covers(char32_t) const override { return true; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const override { return {}; }
    [[nodiscard]] std::size_t held() const override { return 0; }
    [[nodiscard]] Grid grid() const noexcept { return grid_; }

private:
    Grid grid_;
};

class CellFont final : public ShaperFont {
public:
    explicit CellFont(Grid grid) : grid_(grid) {}
    // A row, whatever the size: the baseline where the grid has it, an underline in the cell below it.
    [[nodiscard]] Metrics metrics(float) const override {
        const float below = grid_.row - grid_.baseline;
        return {.ascent = grid_.baseline, .descent = below, .line_gap = 0, .x_height = grid_.baseline / 2, .cap_height = grid_.baseline * 3 / 4,
                .underline_offset = below / 2, .underline_thickness = 1};
    }
    [[nodiscard]] GlyphOutline outline(std::uint32_t) const override { return {}; }
    [[nodiscard]] std::vector<ColorLayer> color_layers(std::uint32_t) const override { return {}; }
    [[nodiscard]] Grid grid() const noexcept { return grid_; }

private:
    Grid grid_;
};

ShaperFaceMade cell_face() {
    FaceInfo info;
    info.family = "cell";
    info.style = "Regular";
    info.variable = true;  // any weight: a font of each weight asked for, which a cell renderer shows as bold or not
    info.min_weight = 1;
    info.max_weight = 1000;
    return {std::move(info), std::make_unique<CellFace>(current())};
}

bool is_space(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x00A0 || cp == 0x3000 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x205F;
}
bool is_newline(char32_t cp) { return cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029 || cp == 0x85; }

// The grid a run's font measures by.
Grid grid_of(const FontCollection& fonts, FontRef font) {
    const auto* made = static_cast<const CellFont*>(fonts.shaper_font(font));
    return made ? made->grid() : current();
}

void shape(Paragraph::Shaped& s, std::span<const std::uint8_t> level_at) {
    // A line is a row and a grapheme its cells: no spacing or line height of their own.
    for (TextStyle& style : s.styles) {
        style.letter_spacing = 0;
        style.line_height = 0;
    }
    struct Cluster {
        std::size_t    begin, end, style;
        FaceId         face;
        std::uint8_t   level;
        std::u32string text;
    };
    const std::vector<std::size_t> bounds = unicode::grapheme_boundaries(s.text);
    std::vector<Cluster> clusters;
    for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
        const std::size_t begin = bounds[i], end = bounds[i + 1];
        std::u32string text = unicode::to_utf32(std::string_view(s.text).substr(begin, end - begin));
        const std::size_t style = style_at(s, begin);
        const FaceId face = s.fonts->select(s.styles[style].families, text);
        clusters.push_back({begin, end, style, face, level_at[begin], std::move(text)});
    }

    s.runs.clear();
    s.graphemes.assign(clusters.size(), {});
    for (std::size_t i = 0; i < clusters.size();) {
        std::size_t j = i + 1;
        while (j < clusters.size() && clusters[j].style == clusters[i].style && clusters[j].face == clusters[i].face &&
               clusters[j].level == clusters[i].level)
            ++j;
        const TextStyle& style = s.styles[clusters[i].style];
        ShapedRun run{.begin = clusters[i].begin, .end = clusters[j - 1].end, .level = clusters[i].level, .style = clusters[i].style};
        run.font = s.fonts->font(clusters[i].face, style.weight, style.slant);
        const Grid grid = grid_of(*s.fonts, run.font);
        run.metrics = s.fonts->metrics(run.font, style.size);
        // Visual order: a right-to-left run's graphemes reversed, each grapheme's code points kept in order.
        std::vector<std::size_t> order(j - i);
        std::iota(order.begin(), order.end(), i);
        if (run.level & 1) std::ranges::reverse(order);
        for (const std::size_t k : order) {
            const Cluster& cluster = clusters[k];
            const char32_t first = cluster.text.empty() ? U' ' : cluster.text.front();
            const bool newline = is_newline(first);
            const float advance = newline ? 0 : static_cast<float>(std::max(unicode::columns(cluster.text), first == '\t' ? 1 : 0)) * grid.column;
            // One cluster a grapheme; its cells on its last code point, so its marks stand where its base does.
            for (std::size_t point = 0; point < cluster.text.size(); ++point)
                run.glyphs.push_back({.id = static_cast<std::uint32_t>(cluster.text[point]), .cluster = static_cast<std::uint32_t>(cluster.begin),
                                      .advance = point + 1 == cluster.text.size() ? advance : 0});
            s.graphemes[k] = {.begin = cluster.begin, .end = cluster.end, .advance = advance, .run = s.runs.size(), .space = is_space(first),
                              .newline = newline};
        }
        s.runs.push_back(std::move(run));
        i = j;
    }

    // The ellipsis: one cell, in the paragraph's font.
    const TextStyle& style = s.styles[0];
    s.ellipsis = {.begin = 0, .end = std::string_view("…").size(), .level = s.base_level, .style = 0};
    s.ellipsis.font = s.fonts->font(s.fonts->select(style.families, U"…"), style.weight, style.slant);
    s.ellipsis.metrics = s.fonts->metrics(s.ellipsis.font, style.size);
    s.ellipsis_width = grid_of(*s.fonts, s.ellipsis.font).column;
    s.ellipsis.glyphs = {{.id = U'…', .cluster = 0, .advance = s.ellipsis_width}};
}

}  // namespace

const Shaper& shaper() {
    // Made when first asked for (at install), never at program start: a program that installs another has none of it.
    static const Shaper table{
        .name = "cells",
        .file = [](const std::filesystem::path&) -> std::expected<ShaperFaceMade, std::string> { return cell_face(); },
        .data = [](std::vector<std::uint8_t>) -> std::expected<ShaperFaceMade, std::string> { return cell_face(); },
        .font = [](const ShaperFace& face, const FaceInfo&, float, Slant) -> std::unique_ptr<ShaperFont> {
            return std::make_unique<CellFont>(static_cast<const CellFace&>(face).grid());
        },
        .shape = &shape,
        .defaults = [](FontCollection& fonts, const std::filesystem::path&) -> std::expected<void, std::string> {
            auto face = fonts.add_data({});
            if (!face) return std::unexpected(face.error());
            fonts.set_fallback({*face});
            return {};
        },
    };
    return table;
}

std::expected<void, compose::Error> install(Grid grid) {
    current() = grid;
    compose::Composition& composition = compose::process();
    if (composition.provider(shaper_slot.info) == shaper().name) return {};
    return composition.install(shaper_slot, shaper(), shaper().name);
}

Grid grid() noexcept { return current(); }

}  // namespace mlibs::text::cells
