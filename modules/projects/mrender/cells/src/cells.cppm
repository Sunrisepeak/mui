// mrender.cells — a display list as a terminal's character cells (terminal design §3 ①, D-249).
//
// The terminal is another reader of the same list the CPU and GPU renderers read: a frame is a value of
// `columns × rows` cells, each a grapheme, a foreground and a background colour and style bits. Rectangles fill
// their cells' background, a rounded rectangle's stroke is drawn with box-drawing characters, paths fill the cells
// whose centres they cover and strokes mark the cells they pass, glyphs land in the cells their pen position names,
// clips and translations are taken to whole cells, a layer's opacity mixes its colours into what is under it; shadows,
// images and effects are not drawn. A glyph's id is read as its code point and a cluster's glyphs as one grapheme —
// the cell measure's (mlibs::text::cells) glyphs — so text measured in cells is drawn back as the characters it was.
//
// Frames compare and print as text (`text`), so a terminal's picture is a text baseline a test compares; `ansi`
// writes one to a terminal, or only the cells that changed since the last, inside synchronized output.
//
//     mrender::cells::Frame frame = mrender::cells::render(list, 80, 24, {.grid = {8, 16, 12}});
//     std::print("{}", mrender::cells::ansi(last, frame));   // what changed, at once
export module mrender.cells;

import std;
export import mrender.display;

export namespace mrender::cells {

// The grid in the list's units: a column's width, a row's height and the baseline's depth in a row — the numbers
// the cell measure lays text out by (the host gives both the same).
struct Grid {
    float column = 8;
    float row = 16;
    float baseline = 12;
};

// A cell's style bits.
namespace style {
inline constexpr std::uint8_t bold = 1, italic = 2, underline = 4, strikethrough = 8;
}

struct Cell {
    std::string   text = " ";  // a grapheme (UTF-8); empty: the second cell of a wide one before it
    Color         foreground;
    Color         background;
    std::uint8_t  style = 0;
    std::uint32_t node = 0;    // the node that drew it last (the list's node markers), 0 none
    // Written out: GCC 16 does not import a defaulted comparison of this struct (an internal error).
    [[nodiscard]] bool operator==(const Cell& other) const noexcept;
};

// A terminal's picture: a value, compared cell by cell.
class Frame {
public:
    Frame() = default;
    Frame(int columns, int rows, Color foreground, Color background);

    [[nodiscard]] int columns() const noexcept { return columns_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] const Cell& at(int column, int row) const { return cells_[static_cast<std::size_t>(row * columns_ + column)]; }
    [[nodiscard]] Cell& at(int column, int row) { return cells_[static_cast<std::size_t>(row * columns_ + column)]; }
    // The characters, a row a line (a wide grapheme once): the baseline a test compares. `trim`: without the
    // spaces that end a row, and without the empty rows that end the frame.
    [[nodiscard]] std::string text(bool trim = true) const;
    // The node that drew a cell last (its text, or its background), 0 none or outside: a picture's provenance.
    [[nodiscard]] std::uint32_t node_at(int column, int row) const noexcept;

    [[nodiscard]] bool operator==(const Frame& other) const noexcept;

private:
    int               columns_ = 0, rows_ = 0;
    std::vector<Cell> cells_;
};

struct Options {
    Grid  grid;
    Color foreground = Color::black();  // the terminal's own: what a cell no text was drawn in shows
    Color background = Color::white();
    // A font's style bits (bold for a heavy weight, italic): the text side knows; none given, every glyph plain.
    std::function<std::uint8_t(FontRef)> font_style;
};

// The frame `list` draws on `columns × rows` cells.
[[nodiscard]] Frame render(const DisplayList& list, int columns, int rows, const Options& options = {});

// How many colours a terminal shows.
enum class Colors : std::uint8_t { truecolor, palette256, palette16 };

// Terminal output (ECMA-48 and xterm's extensions) inside synchronized output (DEC mode 2026, which a terminal
// without it ignores): the whole frame from the top left, or what changed since `before` (another size: the whole).
[[nodiscard]] std::string ansi(const Frame& frame, Colors colors = Colors::truecolor);
[[nodiscard]] std::string ansi(const Frame& before, const Frame& after, Colors colors = Colors::truecolor);

}  // namespace mrender::cells
