// A frame as a terminal's output: cursor positioning, SGR attributes and colours (24-bit, the 256-colour palette, or
// the 16 standard colours), the graphemes; only the cells that changed when the frame before is given; the whole
// inside synchronized output (DEC private mode 2026), so the terminal shows the frame at once and never half drawn.
module mrender.cells;

import std;

namespace mrender::cells {

namespace {

struct Rgb {
    int r = 0, g = 0, b = 0;
};

Rgb rgb(Color c) {
    const auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return {channel(c.r), channel(c.g), channel(c.b)};
}

// xterm's 16 colours, as most terminals show them by default.
constexpr std::array<Rgb, 16> standard{{{0, 0, 0},       {205, 0, 0},   {0, 205, 0},   {205, 205, 0},   {0, 0, 238},     {205, 0, 205},
                                        {0, 205, 205},   {229, 229, 229}, {127, 127, 127}, {255, 0, 0},  {0, 255, 0},     {255, 255, 0},
                                        {92, 92, 255},   {255, 0, 255}, {0, 255, 255}, {255, 255, 255}}};

int distance(Rgb a, Rgb b) { return (a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b); }

// The nearest of the 256-colour palette: its 6×6×6 cube or its 24 greys.
int palette256(Rgb c) {
    const auto level = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
    const int r = level(c.r), g = level(c.g), b = level(c.b);
    const auto value = [](int l) { return l == 0 ? 0 : 55 + 40 * l; };
    const Rgb cube{value(r), value(g), value(b)};
    const int grey_level = std::clamp((c.r + c.g + c.b) / 3 > 238 ? 23 : ((c.r + c.g + c.b) / 3 - 3) / 10, 0, 23);
    const Rgb grey{8 + 10 * grey_level, 8 + 10 * grey_level, 8 + 10 * grey_level};
    return distance(c, grey) < distance(c, cube) ? 232 + grey_level : 16 + 36 * r + 6 * g + b;
}

int palette16(Rgb c) {
    int best = 0;
    for (int i = 1; i < 16; ++i)
        if (distance(c, standard[static_cast<std::size_t>(i)]) < distance(c, standard[static_cast<std::size_t>(best)])) best = i;
    return best;
}

std::string colour(Color c, bool background, Colors colors) {
    const Rgb v = rgb(c);
    switch (colors) {
        case Colors::truecolor: return std::format("{};2;{};{};{}", background ? 48 : 38, v.r, v.g, v.b);
        case Colors::palette256: return std::format("{};5;{}", background ? 48 : 38, palette256(v));
        case Colors::palette16: {
            const int index = palette16(v);
            return std::format("{}", (background ? 40 : 30) + (index & 7) + (index >= 8 ? 60 : 0));
        }
    }
    return {};
}

// The attributes a cell is written with.
std::string attributes(const Cell& cell, Colors colors) {
    std::string sgr = "\x1b[0";
    if (cell.style & style::bold) sgr += ";1";
    if (cell.style & style::italic) sgr += ";3";
    if (cell.style & style::underline) sgr += ";4";
    if (cell.style & style::strikethrough) sgr += ";9";
    sgr += ";" + colour(cell.foreground, false, colors) + ";" + colour(cell.background, true, colors) + "m";
    return sgr;
}

class Writer {
public:
    explicit Writer(Colors colors) : colors_(colors) { out_ = "\x1b[?2026h"; }

    void move(int column, int row) {
        if (column == column_ && row == row_) return;
        out_ += std::format("\x1b[{};{}H", row + 1, column + 1);
        column_ = column, row_ = row;
    }
    void cell(const Frame& frame, int column, int row) {
        const Cell& one = frame.at(column, row);
        if (one.text.empty()) return;  // the second half of a wide grapheme: written with its first
        move(column, row);
        if (std::string sgr = attributes(one, colors_); sgr != sgr_) {
            out_ += sgr;
            sgr_ = std::move(sgr);
        }
        out_ += one.text;
        column_ += column + 1 < frame.columns() && frame.at(column + 1, row).text.empty() ? 2 : 1;
        if (column_ >= frame.columns()) column_ = -1;  // past the right edge: where the cursor is, each terminal says otherwise
    }
    std::string finish() {
        out_ += "\x1b[0m\x1b[?2026l";
        return std::move(out_);
    }

private:
    Colors      colors_;
    std::string out_, sgr_;
    int         column_ = -1, row_ = -1;
};

}  // namespace

std::string ansi(const Frame& frame, Colors colors) {
    Writer writer(colors);
    for (int row = 0; row < frame.rows(); ++row)
        for (int column = 0; column < frame.columns(); ++column) writer.cell(frame, column, row);
    return writer.finish();
}

std::string ansi(const Frame& before, const Frame& after, Colors colors) {
    if (before.columns() != after.columns() || before.rows() != after.rows()) return ansi(after, colors);
    Writer writer(colors);
    for (int row = 0; row < after.rows(); ++row)
        for (int column = 0; column < after.columns(); ++column) {
            if (before.at(column, row) == after.at(column, row)) continue;
            // A changed second half is written with its first; a first whose second half changed, again.
            const int from = after.at(column, row).text.empty() && column > 0 ? column - 1 : column;
            writer.cell(after, from, row);
        }
    return writer.finish();
}

}  // namespace mrender::cells
