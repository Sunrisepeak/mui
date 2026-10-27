// Line layout of a shaped paragraph: line breaking (UAX #14 opportunities,
// hanging spaces, overflow breaks inside a word wider than the line), bidi
// reordering per line (UBA L2 over runs), alignment and justification,
// ellipsis; and the queries carets and selections need.
module openmui.text;

import std;
import openmui.unicode;
import :shaped;

namespace omui::typeset {

Paragraph::Paragraph(FontCollection& fonts, std::string text, ParagraphStyle style, std::vector<StyledRange> ranges)
    : shaped_(std::make_unique<Shaped>()) {
    Shaped& s = *shaped_;
    s.fonts = &fonts;
    s.text = std::move(text);
    s.paragraph = std::move(style);
    s.styles.push_back(s.paragraph.style);
    std::ranges::sort(ranges, {}, &StyledRange::begin);
    for (StyledRange& r : ranges) {
        r.end = std::min(r.end, s.text.size());
        if (r.begin >= r.end) continue;
        s.styles.push_back(r.style);
        s.ranges.push_back(r);
    }
    shape(s);
    layout(std::numeric_limits<float>::infinity());
}

Paragraph::~Paragraph() = default;
Paragraph::Paragraph(Paragraph&&) noexcept = default;
Paragraph& Paragraph::operator=(Paragraph&&) noexcept = default;

const std::string& Paragraph::text() const noexcept {
    static const std::string empty;
    return shaped_ ? shaped_->text : empty;
}
std::span<const Line> Paragraph::lines() const noexcept { return lines_; }
Size Paragraph::size() const noexcept { return size_; }
bool Paragraph::truncated() const noexcept { return truncated_; }
float Paragraph::first_baseline() const noexcept { return lines_.empty() ? 0 : lines_.front().baseline; }

float Paragraph::min_intrinsic_width() const {
    if (!shaped_) return 0;
    const Shaped& s = *shaped_;
    float widest = 0, current = 0;
    for (std::size_t i = 0; i < s.graphemes.size(); ++i) {
        if (s.break_before[i] != 0) current = 0;
        if (!s.graphemes[i].space) current += s.graphemes[i].advance;
        widest = std::max(widest, current);
    }
    return widest;
}

float Paragraph::max_intrinsic_width() const {
    if (!shaped_) return 0;
    const Shaped& s = *shaped_;
    float widest = 0, current = 0, pending_space = 0;
    for (std::size_t i = 0; i < s.graphemes.size(); ++i) {
        if (s.break_before[i] == 2) {
            current = 0;
            pending_space = 0;
        }
        const Grapheme& g = s.graphemes[i];
        if (g.space) pending_space += g.advance;
        else {
            current += pending_space + g.advance;
            pending_space = 0;
        }
        widest = std::max(widest, current);
    }
    return widest;
}

namespace {

float line_height_of(const TextStyle& st, const Metrics& m) {
    if (st.line_height <= 0) return m.line_height();
    if (st.line_height < 4) return st.line_height * st.size;
    return st.line_height;
}

}  // namespace

void Paragraph::layout(float width) {
    lines_.clear();
    truncated_ = false;
    size_ = {};
    if (!shaped_) return;
    const Shaped& s = *shaped_;
    const std::size_t n = s.graphemes.size();
    const bool wrap = s.paragraph.wrap && width < std::numeric_limits<float>::infinity();
    const bool rtl = s.base_level == 1;

    // ── choose line ranges (grapheme indices) ──
    struct Range {
        std::size_t begin, end;
        bool        hard;  // ended by a mandatory break (or the end of text)
    };
    std::vector<Range> ranges;
    std::size_t start = 0;
    while (start < n || (start == 0 && n == 0)) {
        if (n == 0) {
            ranges.push_back({0, 0, true});
            break;
        }
        float w = 0, pending = 0;
        std::size_t last_break = start;  // index of a grapheme with an opportunity before it
        std::size_t i = start;
        bool hard = false;
        for (; i < n; ++i) {
            if (i > start && s.break_before[i] == 2) {
                hard = true;
                break;
            }
            if (i > start && s.break_before[i] == 1) last_break = i;
            const Grapheme& g = s.graphemes[i];
            if (g.space || g.newline) {
                pending += g.advance;
                continue;
            }
            float next = w + pending + g.advance;
            if (wrap && next > width + 0.001f && i > start) {
                if (last_break > start) i = last_break;  // the last opportunity in this line
                break;                                   // else: an overflow break inside the word
            }
            w = next;
            pending = 0;
        }
        if (i >= n) hard = true;
        ranges.push_back({start, i, hard});
        start = i;
    }
    // Text that ends with a newline has an empty last line (for the caret).
    if (n > 0 && s.graphemes.back().newline) ranges.push_back({n, n, true});

    std::size_t keep = ranges.size();
    if (s.paragraph.max_lines > 0 && ranges.size() > static_cast<std::size_t>(s.paragraph.max_lines)) {
        keep = static_cast<std::size_t>(s.paragraph.max_lines);
        truncated_ = true;
    }

    // ── each line ──
    float top = 0, widest = 0;
    for (std::size_t li = 0; li < keep; ++li) {
        Range r = ranges[li];
        Line line;
        line.rtl = rtl;
        line.begin = r.begin < n ? s.graphemes[r.begin].begin : s.text.size();
        line.end = r.end > r.begin ? s.graphemes[r.end - 1].end : line.begin;
        bool ellipsize = truncated_ && li + 1 == keep && s.paragraph.ellipsis;
        // Visible graphemes: drop the trailing newline; trailing spaces hang.
        std::size_t vis_end = r.end;
        while (vis_end > r.begin && s.graphemes[vis_end - 1].newline) --vis_end;
        std::size_t content_end = vis_end;
        while (content_end > r.begin && s.graphemes[content_end - 1].space) --content_end;
        if (ellipsize) {
            float budget = (wrap ? width : std::numeric_limits<float>::infinity()) - s.ellipsis_width;
            float acc = 0;
            std::size_t fit = r.begin;
            for (; fit < content_end && acc + s.graphemes[fit].advance <= budget; ++fit) acc += s.graphemes[fit].advance;
            while (fit > r.begin && s.graphemes[fit - 1].space) --fit;
            content_end = vis_end = fit;
            line.ellipsized = true;
        }

        // Sub-runs in logical order, split at the line's bounds.
        struct Sub {
            std::size_t run;
            std::size_t g0, g1;  // grapheme range
            std::uint8_t level;
        };
        std::vector<Sub> subs;
        for (std::size_t g = r.begin; g < vis_end;) {
            std::size_t run = s.graphemes[g].run;
            std::size_t h = g + 1;
            while (h < vis_end && s.graphemes[h].run == run) ++h;
            subs.push_back({run, g, h, s.runs[run].level});
            g = h;
        }
        // UBA L2: reverse sequences at each level from the highest to the lowest odd one.
        std::vector<std::size_t> order(subs.size());
        std::iota(order.begin(), order.end(), 0);
        if (!subs.empty()) {
            std::uint8_t hi = 0, lo_odd = 255;
            for (const Sub& sb : subs) {
                hi = std::max(hi, sb.level);
                if (sb.level & 1) lo_odd = std::min(lo_odd, sb.level);
            }
            // Trailing hanging spaces take the paragraph level (L1).
            for (std::uint8_t lv = hi; lv >= 1 && lv >= std::min<std::uint8_t>(lo_odd, hi); --lv) {
                for (std::size_t i = 0; i < order.size();) {
                    if (subs[order[i]].level < lv) {
                        ++i;
                        continue;
                    }
                    std::size_t j = i;
                    while (j < order.size() && subs[order[j]].level >= lv) ++j;
                    std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j));
                    i = j;
                }
                if (lv == 0) break;
            }
        }

        // Vertical metrics.
        float ascent = 0, descent = 0, height = 0;
        auto add_metrics = [&](const ShapedRun& run) {
            const TextStyle& st = s.styles[run.style];
            float lh = line_height_of(st, run.metrics);
            float extra = (lh - (run.metrics.ascent + run.metrics.descent)) / 2;
            ascent = std::max(ascent, run.metrics.ascent + extra);
            descent = std::max(descent, run.metrics.descent + extra);
        };
        for (const Sub& sb : subs) add_metrics(s.runs[sb.run]);
        if (subs.empty()) {
            const ShapedRun* near = nullptr;
            if (r.begin < n) near = &s.runs[s.graphemes[r.begin].run];
            else if (n > 0) near = &s.runs[s.graphemes.back().run];
            if (near) add_metrics(*near);
            else {
                const TextStyle& st = s.styles[0];
                FaceId face = s.fonts->select(st.families, U"x");
                Metrics m = s.fonts->metrics(s.fonts->font(face, st.weight, st.slant), st.size);
                float lh = line_height_of(st, m), extra = (lh - (m.ascent + m.descent)) / 2;
                ascent = m.ascent + extra;
                descent = m.descent + extra;
            }
        }
        height = ascent + descent;
        line.top = top;
        line.ascent = ascent;
        line.descent = descent;
        line.height = height;
        line.baseline = top + ascent;

        // Widths: content (hanging spaces excluded).
        float content = 0;
        for (std::size_t g = r.begin; g < content_end; ++g) content += s.graphemes[g].advance;
        float full = 0;
        for (std::size_t g = r.begin; g < vis_end; ++g) full += s.graphemes[g].advance;
        if (line.ellipsized) content += s.ellipsis_width;

        // Alignment.
        float avail = wrap ? width : content;
        TextAlign align = s.paragraph.align;
        if (align == TextAlign::start) align = rtl ? TextAlign::right : TextAlign::left;
        if (align == TextAlign::end) align = rtl ? TextAlign::left : TextAlign::right;
        bool justify = align == TextAlign::justify && !r.hard && !line.ellipsized && wrap;
        if (align == TextAlign::justify) align = rtl ? TextAlign::right : TextAlign::left;
        float offset = 0;  // the left edge of the visible content
        if (align == TextAlign::center) offset = (avail - content) / 2;
        else if (align == TextAlign::right) offset = avail - content;
        if (!wrap) offset = 0;
        line.left = offset;
        // Hanging spaces of an RTL line sit at its visual left, before the content.
        float hanging = full - (content - (line.ellipsized ? s.ellipsis_width : 0));
        float pen_start = rtl ? offset - hanging : offset;
        float stretch = 0;
        if (justify) {
            std::size_t spaces = 0;
            for (std::size_t g = r.begin; g < content_end; ++g)
                if (s.graphemes[g].space) ++spaces;
            if (spaces > 0) stretch = (avail - content) / static_cast<float>(spaces);
        }

        // Place runs and graphemes left to right.
        float pen = pen_start;
        auto emit_ellipsis = [&](float x) {
            GlyphRun er;
            er.font = s.ellipsis.font;
            er.size = s.styles[0].size;
            er.color = s.styles[0].color;
            er.rtl = rtl;
            er.x = x;
            er.width = s.ellipsis_width;
            er.baseline = line.baseline;
            float px = x;
            for (const ShapedGlyph& g : s.ellipsis.glyphs) {
                const auto last = static_cast<std::uint32_t>(s.graphemes.empty() ? 0 : s.graphemes.size() - 1);
                er.glyphs.push_back({g.id, {px + g.x_offset, line.baseline + g.y_offset}, static_cast<std::uint32_t>(s.text.size()), last,
                                     s.graphemes.empty() ? 0 : s.graphemes.back().word + 1, static_cast<std::uint32_t>(lines_.size())});
                px += g.advance;
            }
            return er;
        };
        if (line.ellipsized && rtl) {
            line.runs.push_back(emit_ellipsis(pen));
            pen += s.ellipsis_width;
        }
        for (std::size_t oi : order) {
            const Sub& sb = subs[oi];
            const ShapedRun& run = s.runs[sb.run];
            const TextStyle& st = s.styles[run.style];
            std::size_t b0 = s.graphemes[sb.g0].begin, b1 = s.graphemes[sb.g1 - 1].end;
            GlyphRun gr;
            gr.font = run.font;
            gr.size = st.size;
            gr.color = st.color;
            gr.begin = b0;
            gr.end = b1;
            gr.rtl = (run.level & 1) != 0;
            gr.x = pen;
            gr.baseline = line.baseline;
            gr.underline = st.underline;
            gr.strikethrough = st.strikethrough;
            gr.decoration_offset = run.metrics.underline_offset;
            gr.decoration_thickness = std::max(1.0f, run.metrics.underline_thickness);
            // Glyphs of this sub-run, in visual order; letter spacing and
            // justification after each cluster.
            float px = pen;
            std::uint32_t prev_cluster = std::numeric_limits<std::uint32_t>::max();
            for (std::size_t gi = 0; gi < run.glyphs.size(); ++gi) {
                const ShapedGlyph& g = run.glyphs[gi];
                if (g.cluster < b0 || g.cluster >= b1) continue;
                bool newline = false;
                std::size_t gidx = static_cast<std::size_t>(
                    std::ranges::upper_bound(s.graphemes, g.cluster, {}, &Grapheme::begin) - s.graphemes.begin()) - 1;
                newline = s.graphemes[gidx].newline;
                if (newline) continue;
                gr.glyphs.push_back({g.id, {px + g.x_offset, line.baseline + g.y_offset}, g.cluster, static_cast<std::uint32_t>(gidx),
                                     s.graphemes[gidx].word, static_cast<std::uint32_t>(lines_.size())});
                px += g.advance;
                bool last_of_cluster = gi + 1 >= run.glyphs.size() || run.glyphs[gi + 1].cluster != g.cluster;
                if (last_of_cluster && g.cluster != prev_cluster) {
                    px += st.letter_spacing;
                    if (stretch > 0 && s.graphemes[gidx].space && gidx < content_end) px += stretch;
                }
                if (last_of_cluster) prev_cluster = g.cluster;
            }
            // Grapheme boxes, from the logical advances in visual order.
            float bx = pen;
            auto place = [&](std::size_t gidx) {
                float a = s.graphemes[gidx].advance;
                if (stretch > 0 && s.graphemes[gidx].space && gidx < content_end) a += stretch;
                line.graphemes.push_back({s.graphemes[gidx].begin, s.graphemes[gidx].end, bx, bx + a, gr.rtl});
                bx += a;
            };
            if (gr.rtl)
                for (std::size_t gidx = sb.g1; gidx-- > sb.g0;) place(gidx);
            else
                for (std::size_t gidx = sb.g0; gidx < sb.g1; ++gidx) place(gidx);
            gr.width = bx - pen;
            pen = bx;
            line.runs.push_back(std::move(gr));
        }
        if (line.ellipsized && !rtl) line.runs.push_back(emit_ellipsis(pen));
        // The newline grapheme gets a zero-width box at the line end (carets after it go to the next line).
        for (std::size_t g = vis_end; g < r.end; ++g)
            line.graphemes.push_back({s.graphemes[g].begin, s.graphemes[g].end, pen, pen, rtl});
        std::ranges::sort(line.graphemes, {}, &GraphemeBox::begin);
        line.width = content;
        widest = std::max(widest, content);
        top += height;
        lines_.push_back(std::move(line));
    }
    // Shrink to fit: the widest line. A caller that aligns in a wider box lays
    // out again at that width.
    size_ = {widest, top};
}

// ── queries ───────────────────────────────────────────────────────────────

std::size_t Paragraph::line_of(std::size_t offset) const {
    if (lines_.empty()) return 0;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        bool last = i + 1 == lines_.size();
        if (offset < l.end || (last && offset <= l.end)) {
            // An offset at the very start of the next line belongs there.
            if (offset == l.end && !last) continue;
            return i;
        }
    }
    return lines_.size() - 1;
}

Caret Paragraph::caret(std::size_t offset) const {
    if (lines_.empty()) return {};
    std::size_t li = line_of(offset);
    const Line& l = lines_[li];
    Caret c{0, l.top, l.height, li};
    if (l.graphemes.empty()) {
        c.x = l.rtl ? l.left + l.width : l.left;
        return c;
    }
    for (const GraphemeBox& g : l.graphemes) {
        if (g.begin == offset) {
            c.x = g.rtl ? g.x1 : g.x0;
            return c;
        }
    }
    // After the last grapheme of the line.
    const GraphemeBox* last = nullptr;
    for (const GraphemeBox& g : l.graphemes)
        if (g.end == offset) last = &g;
    if (!last) last = &l.graphemes.back();
    c.x = last->rtl ? last->x0 : last->x1;
    return c;
}

std::size_t Paragraph::offset_at(Point p) const {
    if (lines_.empty()) return 0;
    std::size_t li = 0;
    while (li + 1 < lines_.size() && p.y >= lines_[li].top + lines_[li].height) ++li;
    const Line& l = lines_[li];
    const GraphemeBox* best = nullptr;
    float best_d = std::numeric_limits<float>::infinity();
    for (const GraphemeBox& g : l.graphemes) {
        if (g.x1 <= g.x0 && li + 1 < lines_.size()) continue;  // the newline box
        float d = p.x < g.x0 ? g.x0 - p.x : (p.x > g.x1 ? p.x - g.x1 : 0);
        if (d < best_d) {
            best_d = d;
            best = &g;
        }
    }
    if (!best) return l.begin;
    bool leading = p.x < (best->x0 + best->x1) / 2;
    if (best->rtl) leading = !leading;
    std::size_t off = leading ? best->begin : best->end;
    // Never place the caret after a line's own newline.
    if (off == l.end && li + 1 < lines_.size() && l.end > l.begin) {
        std::string_view t = text();
        if (off > 0 && (t[off - 1] == '\n')) off = unicode::previous_grapheme(t, off);
    }
    return off;
}

std::vector<Rect> Paragraph::selection(std::size_t begin, std::size_t end) const {
    std::vector<Rect> out;
    if (begin > end) std::swap(begin, end);
    for (const Line& l : lines_) {
        float x0 = std::numeric_limits<float>::infinity(), x1 = -x0;
        std::vector<std::pair<float, float>> spans;
        for (const GraphemeBox& g : l.graphemes)
            if (g.begin >= begin && g.end <= end && g.end > g.begin) spans.emplace_back(g.x0, g.x1);
        std::ranges::sort(spans);
        for (auto [a, b] : spans) {
            if (!out.empty() && x1 >= a - 0.01f && x1 != -std::numeric_limits<float>::infinity()) {
                x1 = std::max(x1, b);
                continue;
            }
            if (x1 > x0) out.push_back({x0, l.top, x1 - x0, l.height});
            x0 = a;
            x1 = b;
        }
        if (x1 > x0) out.push_back({x0, l.top, x1 - x0, l.height});
    }
    return out;
}

std::size_t Paragraph::offset_above(std::size_t offset, float x) const {
    std::size_t li = line_of(offset);
    if (li == 0) return 0;
    const Line& l = lines_[li - 1];
    return offset_at({x, l.top + l.height / 2});
}

std::size_t Paragraph::offset_below(std::size_t offset, float x) const {
    std::size_t li = line_of(offset);
    if (li + 1 >= lines_.size()) return text().size();
    const Line& l = lines_[li + 1];
    return offset_at({x, l.top + l.height / 2});
}

void Paragraph::paint(render::DisplayList& list, Point origin) const {
    for (const Line& l : lines_) {
        for (const GlyphRun& r : l.runs) {
            if (r.glyphs.empty()) continue;
            list.draw_glyphs(r.font, r.size, origin, r.glyphs, render::Paint::solid(r.color));
            if (r.underline)
                list.fill_rect({origin.x + r.x, origin.y + r.baseline + r.decoration_offset, r.width, r.decoration_thickness},
                               render::Paint::solid(r.color));
            if (r.strikethrough)
                list.fill_rect({origin.x + r.x, origin.y + r.baseline - r.size * 0.3f, r.width, r.decoration_thickness},
                               render::Paint::solid(r.color));
        }
    }
}

}  // namespace omui::typeset
