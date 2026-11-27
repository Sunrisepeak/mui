// A display list read into cells: each command onto the cells it covers, under the list's transforms, clips, layer
// opacities and node markers.
module mrender.cells;

import std;
import mlibs.geom;
import mlibs.unicode;

namespace mrender::cells {

Frame::Frame(int columns, int rows, Color foreground, Color background)
    : columns_(std::max(columns, 0)),
      rows_(std::max(rows, 0)),
      cells_(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_), Cell{.foreground = foreground, .background = background}) {}

std::string Frame::text(bool trim) const {
    std::vector<std::string> lines;
    for (int row = 0; row < rows_; ++row) {
        std::string line;
        for (int column = 0; column < columns_; ++column) line += at(column, row).text;
        if (trim)
            while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(std::move(line));
    }
    if (trim)
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out;
}

bool Cell::operator==(const Cell& other) const noexcept {
    return text == other.text && foreground == other.foreground && background == other.background && style == other.style && node == other.node;
}

bool Frame::operator==(const Frame& other) const noexcept {
    return columns_ == other.columns_ && rows_ == other.rows_ && cells_ == other.cells_;
}

std::uint32_t Frame::node_at(int column, int row) const noexcept {
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_) return 0;
    return at(column, row).node;
}

namespace {

using mlibs::geom::Affine;

// `top` over `under` at `alpha`: an opaque colour, as a terminal shows one.
Color over(Color under, Color top, float alpha) {
    const float a = std::clamp(top.a * alpha, 0.0f, 1.0f);
    if (a >= 1) return {top.r, top.g, top.b, 1};
    return {under.r + (top.r - under.r) * a, under.g + (top.g - under.g) * a, under.b + (top.b - under.b) * a, 1};
}

// The one colour a paint shows in a cell: a gradient's stops averaged, a mixture's leaves by weight; an image none.
Color colour_of(const Paint& paint) {
    switch (paint.kind) {
        case Paint::Kind::solid:
        case Paint::Kind::image_mask: return paint.color;
        case Paint::Kind::gradient: {
            if (paint.gradient.stops.empty()) return Color::transparent();
            Color sum{0, 0, 0, 0};
            for (const GradientStop& stop : paint.gradient.stops) sum = {sum.r + stop.color.r, sum.g + stop.color.g, sum.b + stop.color.b, sum.a + stop.color.a};
            const float n = static_cast<float>(paint.gradient.stops.size());
            return {sum.r / n, sum.g / n, sum.b / n, sum.a / n};
        }
        case Paint::Kind::mix: {
            Color sum{0, 0, 0, 0};
            for (const WeightedPaint& term : paint.mix) {
                const Color leaf = colour_of(term.paint);
                sum = {sum.r + leaf.r * term.weight, sum.g + leaf.g * term.weight, sum.b + leaf.b * term.weight, sum.a + leaf.a * term.weight};
            }
            sum.a *= paint.mix_opacity;
            return sum;
        }
        case Paint::Kind::image: return Color::transparent();
    }
    return Color::transparent();
}

bool box_drawing(std::string_view text) {
    // U+2500–U+257F (box drawing), U+2580–U+259F (blocks) and the bullet: what a shape drew, which another may replace.
    if (text == "•" || text == "[" || text == "]") return true;
    if (text.size() != 3 || static_cast<unsigned char>(text[0]) != 0xE2) return false;
    const auto second = static_cast<unsigned char>(text[1]);
    return second == 0x94 || second == 0x95 || second == 0x96;
}

class Painter {
public:
    Painter(Frame& frame, const Options& options) : frame_(frame), options_(options) {
        clips_.push_back(Rect{0, 0, static_cast<float>(frame.columns()) * options.grid.column, static_cast<float>(frame.rows()) * options.grid.row});
    }

    void command(const Command& command) {
        std::visit([this](const auto& one) { draw(one); }, command);
    }

private:
    // ── state ──
    [[nodiscard]] const Affine& transform() const { return transforms_.back(); }
    [[nodiscard]] float opacity() const { return opacities_.back(); }
    [[nodiscard]] std::uint32_t node() const { return nodes_.back(); }
    [[nodiscard]] Rect device(Rect rect) const {
        const Affine& m = transform();
        const Point corners[4]{m.apply({rect.x, rect.y}), m.apply({rect.right(), rect.y}), m.apply({rect.x, rect.bottom()}), m.apply({rect.right(), rect.bottom()})};
        float left = corners[0].x, right = left, top = corners[0].y, bottom = top;
        for (const Point& p : corners) {
            left = std::min(left, p.x), right = std::max(right, p.x), top = std::min(top, p.y), bottom = std::max(bottom, p.y);
        }
        return Rect::ltrb(left, top, right, bottom);
    }
    // The cells whose centres a device rectangle covers, inside the clip: [c0, c1) × [r0, r1).
    struct Span {
        int c0 = 0, c1 = 0, r0 = 0, r1 = 0;
        [[nodiscard]] bool empty() const { return c0 >= c1 || r0 >= r1; }
    };
    [[nodiscard]] Span covered(Rect rect) const {
        const Rect in = rect.intersection(clips_.back());
        if (in.empty()) return {};
        const Grid& g = options_.grid;
        Span span{static_cast<int>(std::ceil(in.x / g.column - 0.5f)), static_cast<int>(std::ceil(in.right() / g.column - 0.5f)),
                  static_cast<int>(std::ceil(in.y / g.row - 0.5f)), static_cast<int>(std::ceil(in.bottom() / g.row - 0.5f))};
        span.c0 = std::max(span.c0, 0), span.r0 = std::max(span.r0, 0);
        span.c1 = std::min(span.c1, frame_.columns()), span.r1 = std::min(span.r1, frame_.rows());
        return span;
    }
    [[nodiscard]] bool inside(int column, int row) const { return column >= 0 && row >= 0 && column < frame_.columns() && row < frame_.rows(); }
    [[nodiscard]] bool clipped(int column, int row) const {
        const Grid& g = options_.grid;
        return !clips_.back().contains(Point{(static_cast<float>(column) + 0.5f) * g.column, (static_cast<float>(row) + 0.5f) * g.row});
    }

    // ── cells ──
    // Text replaced in a cell: a wide grapheme it was half of loses its other half.
    void clear_text(int column, int row) {
        Cell& cell = frame_.at(column, row);
        if (cell.text.empty() && column > 0) frame_.at(column - 1, row).text = " ";
        if (column + 1 < frame_.columns() && frame_.at(column + 1, row).text.empty()) frame_.at(column + 1, row).text = " ";
        cell.text = " ";
        cell.style = 0;
    }
    void put(int column, int row, std::string text, Color colour, std::uint8_t style_bits, int width) {
        if (!inside(column, row) || clipped(column, row)) return;
        clear_text(column, row);
        Cell& cell = frame_.at(column, row);
        cell.text = std::move(text);
        cell.foreground = over(cell.background, colour, opacity());
        cell.style = style_bits;
        cell.node = node();
        if (width == 2 && inside(column + 1, row)) {
            clear_text(column + 1, row);
            Cell& next = frame_.at(column + 1, row);
            next.text.clear();
            next.foreground = cell.foreground;
            next.style = style_bits;
            next.node = node();
        }
    }
    // A mark a shape leaves (a rule, a box's edge, a dot): only where no text is, or another shape's mark.
    void mark(int column, int row, std::string_view text, Color colour) {
        if (!inside(column, row) || clipped(column, row)) return;
        const Cell& cell = frame_.at(column, row);
        if (cell.text != " " && !box_drawing(cell.text)) return;
        put(column, row, std::string(text), colour, 0, 1);
    }
    void fill(const Span& span, Color colour) {
        const float alpha = colour.a * opacity();
        if (alpha <= 0) return;
        for (int row = span.r0; row < span.r1; ++row)
            for (int column = span.c0; column < span.c1; ++column) {
                Cell& cell = frame_.at(column, row);
                cell.background = over(cell.background, colour, opacity());
                if (alpha > 0.5f) clear_text(column, row);  // a fill more than half opaque covers what was drawn under it
                cell.node = node();
            }
    }
    // A fill too thin or small for a cell: a rule along a row or a column, or a dot.
    bool thin(Rect rect, Color colour) {
        const Grid& g = options_.grid;
        const bool low = rect.height < g.row / 2, narrow = rect.width < g.column / 2;
        if (!low && !narrow) return false;
        if (colour.a * opacity() <= 0) return true;
        if (low && !narrow) {  // a rule, an underline or a strikethrough, a thin bar
            const int row = static_cast<int>(std::floor((rect.y + rect.height / 2) / g.row));
            const float baseline = static_cast<float>(row) * g.row + g.baseline;
            const int c0 = static_cast<int>(std::ceil(rect.x / g.column - 0.5f)), c1 = static_cast<int>(std::ceil(rect.right() / g.column - 0.5f));
            for (int column = std::max(c0, 0); column < std::min(c1, frame_.columns()); ++column) {
                if (!inside(column, row) || clipped(column, row)) continue;
                Cell& cell = frame_.at(column, row);
                if (cell.text != " " && !box_drawing(cell.text)) cell.style |= rect.y >= baseline - 1 ? style::underline : style::strikethrough;
                else mark(column, row, "─", colour);
            }
        } else if (narrow && !low) {  // a divider, a caret
            const int column = static_cast<int>(std::floor((rect.x + rect.width / 2) / g.column));
            const int r0 = static_cast<int>(std::ceil(rect.y / g.row - 0.5f)), r1 = static_cast<int>(std::ceil(rect.bottom() / g.row - 0.5f));
            for (int row = std::max(r0, 0); row < std::min(r1, frame_.rows()); ++row) mark(column, row, "│", colour);
        } else if (rect.width > 0 && rect.height > 0) {  // a dot
            mark(static_cast<int>(std::floor((rect.x + rect.width / 2) / g.column)), static_cast<int>(std::floor((rect.y + rect.height / 2) / g.row)), "•",
                 colour);
        }
        return true;
    }

    // ── commands ──
    void draw(const FillRect& c) {
        const Rect rect = device(c.rect);
        const Color colour = colour_of(c.paint);
        if (!thin(rect, colour)) fill(covered(rect), colour);
    }
    void draw(const FillRoundRect& c) {
        const Rect rect = device(c.rect);
        const Color colour = colour_of(c.paint);
        if (!thin(rect, colour)) fill(covered(rect), colour);
    }
    void draw(const StrokeRoundRect& c) {
        const Rect rect = device(c.rect);
        const Color colour = colour_of(c.paint);
        if (colour.a * opacity() <= 0) return;
        const Grid& g = options_.grid;
        const int c0 = static_cast<int>(std::lround(rect.x / g.column)), c1 = static_cast<int>(std::lround(rect.right() / g.column)) - 1;
        const int r0 = static_cast<int>(std::lround(rect.y / g.row)), r1 = static_cast<int>(std::lround(rect.bottom() / g.row)) - 1;
        if (c1 <= c0 || r1 < r0) return;
        if (r1 - r0 < 2) {  // too low for a box: brackets about what it holds, on its middle row
            const int row = static_cast<int>(std::floor((rect.y + rect.height / 2) / g.row));
            mark(c0, row, "[", colour);
            mark(c1, row, "]", colour);
            return;
        }
        const float corner = std::max({c.radii.top_left, c.radii.top_right, c.radii.bottom_right, c.radii.bottom_left});
        const bool round = corner >= std::min(g.column, g.row) / 2;
        mark(c0, r0, round ? "╭" : "┌", colour);
        mark(c1, r0, round ? "╮" : "┐", colour);
        mark(c0, r1, round ? "╰" : "└", colour);
        mark(c1, r1, round ? "╯" : "┘", colour);
        for (int column = c0 + 1; column < c1; ++column) {
            mark(column, r0, "─", colour);
            mark(column, r1, "─", colour);
        }
        for (int row = r0 + 1; row < r1; ++row) {
            mark(c0, row, "│", colour);
            mark(c1, row, "│", colour);
        }
    }
    void draw(const BoxShadow&) {}  // shadows are not drawn in cells
    void draw(const FillPath& c) {
        mlibs::geom::PathPolylines lines;
        mlibs::geom::flatten_path(c.path, transform(), lines, 0.5f);
        if (lines.points.empty()) return;
        Rect box = Rect::ltrb(lines.points[0].x, lines.points[0].y, lines.points[0].x, lines.points[0].y);
        for (const Point& p : lines.points) box = Rect::ltrb(std::min(box.x, p.x), std::min(box.y, p.y), std::max(box.right(), p.x), std::max(box.bottom(), p.y));
        const Color colour = colour_of(c.paint);
        if (box.width < options_.grid.column / 2 || box.height < options_.grid.row / 2) {
            (void)thin(box, colour);
            return;
        }
        const Span span = covered(box);
        const Grid& g = options_.grid;
        const float alpha = colour.a * opacity();
        if (alpha <= 0) return;
        for (int row = span.r0; row < span.r1; ++row)
            for (int column = span.c0; column < span.c1; ++column) {
                const Point centre{(static_cast<float>(column) + 0.5f) * g.column, (static_cast<float>(row) + 0.5f) * g.row};
                int winding = 0;
                for (std::size_t polygon = 0; polygon < lines.count(); ++polygon) {
                    const auto points = lines.polygon(polygon);
                    for (std::size_t i = 0; i < points.size(); ++i) {
                        const Point a = points[i], b = points[(i + 1) % points.size()];
                        if (a.y <= centre.y && b.y > centre.y && (b.x - a.x) * (centre.y - a.y) - (centre.x - a.x) * (b.y - a.y) > 0) ++winding;
                        else if (a.y > centre.y && b.y <= centre.y && (b.x - a.x) * (centre.y - a.y) - (centre.x - a.x) * (b.y - a.y) < 0) --winding;
                    }
                }
                const bool in = c.rule == FillRule::even_odd ? (winding & 1) != 0 : winding != 0;
                if (!in) continue;
                Cell& cell = frame_.at(column, row);
                cell.background = over(cell.background, colour, opacity());
                if (alpha > 0.5f) clear_text(column, row);
                cell.node = node();
            }
    }
    void draw(const StrokePath& c) {
        const Color colour = colour_of(c.paint);
        if (colour.a * opacity() <= 0) return;
        mlibs::geom::PathPolylines lines;
        mlibs::geom::flatten_path(c.path, transform(), lines, 0.5f);
        const Grid& g = options_.grid;
        const float step = std::min(g.column, g.row) / 2;
        for (std::size_t polygon = 0; polygon < lines.count(); ++polygon) {
            const auto points = lines.polygon(polygon);
            const std::size_t segments = lines.closed[polygon] ? points.size() : points.size() - 1;
            for (std::size_t i = 0; i < segments && points.size() > 1; ++i) {
                const Point a = points[i], b = points[(i + 1) % points.size()];
                const float dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
                const std::string_view line = std::abs(dx) * g.row > 2 * std::abs(dy) * g.column   ? "─"
                                              : std::abs(dy) * g.column > 2 * std::abs(dx) * g.row ? "│"
                                              : (dx > 0) == (dy > 0)                                ? "╲"
                                                                                                    : "╱";
                const int count = std::max(1, static_cast<int>(std::ceil(length / step)));
                for (int k = 0; k <= count; ++k) {
                    const float t = static_cast<float>(k) / static_cast<float>(count);
                    mark(static_cast<int>(std::floor((a.x + dx * t) / g.column)), static_cast<int>(std::floor((a.y + dy * t) / g.row)), line, colour);
                }
            }
        }
    }
    void draw(const DrawImage&) {}         // pictures are not drawn in cells (a terminal's image protocols: §3 ②, later)
    void draw(const ExternalTexture&) {}
    void draw(const DrawGlyphs& c) {
        const Color colour = colour_of(c.paint);
        const std::uint8_t bits = options_.font_style ? options_.font_style(c.font) : 0;
        const Grid& g = options_.grid;
        for (std::size_t i = 0; i < c.glyphs.size();) {
            // A cluster's glyphs are one grapheme: their ids its code points.
            std::size_t j = i + 1;
            while (j < c.glyphs.size() && c.glyphs[j].cluster == c.glyphs[i].cluster) ++j;
            std::u32string points;
            for (std::size_t k = i; k < j; ++k)
                if (c.glyphs[k].id >= 0x20 && c.glyphs[k].id != 0x7F && c.glyphs[k].id <= 0x10FFFF) points += static_cast<char32_t>(c.glyphs[k].id);
            const float alpha = c.styles.empty() ? 1.0f : c.styles[i].opacity;
            const Point at = transform().apply({c.origin.x + c.glyphs[i].position.x, c.origin.y + c.glyphs[i].position.y});
            i = j;
            const int width = mlibs::unicode::columns(points);
            if (points.empty() || width == 0 || alpha * colour.a * opacity() < 0.5f) continue;
            std::string text;
            for (const char32_t point : points) mlibs::unicode::append_utf8(text, point);
            const int column = static_cast<int>(std::lround(at.x / g.column));
            const int row = static_cast<int>(std::lround((at.y - g.baseline) / g.row));
            put(column, row, std::move(text), colour, bits, width);
        }
    }
    void draw(const PushClipRect& c) { clips_.push_back(device(c.rect).intersection(clips_.back())); }
    void draw(const PushClipPath& c) {
        mlibs::geom::PathPolylines lines;
        mlibs::geom::flatten_path(c.path, transform(), lines, 0.5f);
        Rect box{};
        for (std::size_t i = 0; i < lines.points.size(); ++i) {
            const Point p = lines.points[i];
            box = i == 0 ? Rect{p.x, p.y, 0, 0} : Rect::ltrb(std::min(box.x, p.x), std::min(box.y, p.y), std::max(box.right(), p.x), std::max(box.bottom(), p.y));
        }
        clips_.push_back(box.intersection(clips_.back()));
    }
    void draw(const PopClip&) {
        if (clips_.size() > 1) clips_.pop_back();
    }
    void draw(const PushTransform& c) { transforms_.push_back(transform() * c.transform); }
    void draw(const PopTransform&) {
        if (transforms_.size() > 1) transforms_.pop_back();
    }
    void draw(const PushLayer& c) { opacities_.push_back(opacity() * c.opacity); }
    void draw(const PopLayer&) {
        if (opacities_.size() > 1) opacities_.pop_back();
    }
    void draw(const BeginNode& c) { nodes_.push_back(c.node); }
    void draw(const EndNode&) {
        if (nodes_.size() > 1) nodes_.pop_back();
    }
    void draw(const NodeFrame&) {}
    void draw(const ReferenceBox&) {}
    void draw(const BeginPart&) {}
    void draw(const EndPart&) {}

    Frame&                     frame_;
    const Options&             options_;
    std::vector<Affine>        transforms_{Affine{}};
    std::vector<Rect>          clips_;
    std::vector<float>         opacities_{1.0f};
    std::vector<std::uint32_t> nodes_{0};
};

}  // namespace

Frame render(const DisplayList& list, int columns, int rows, const Options& options) {
    Frame frame(columns, rows, options.foreground, options.background);
    Painter painter(frame, options);
    for (const Command& command : list.commands()) painter.command(command);
    return frame;
}

}  // namespace mrender::cells
