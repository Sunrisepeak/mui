// Flexbox, after CSS Flexible Box Layout §9: flex base sizes, the automatic
// minimum size of items, line breaking, resolving flexible lengths with the
// freeze loop of §9.7, cross sizes with stretch and baseline alignment,
// justify-content, align-content, reversal, and scroll containers (an axis
// that scrolls does not shrink its content).
module openmui.layout;

import std;
import openmui.geom;
import :internal;

namespace omui::layout {

namespace {

constexpr float infinity = std::numeric_limits<float>::infinity();

bool is_row(Direction d) { return d == Direction::row || d == Direction::row_reverse; }
bool is_reverse(Direction d) { return d == Direction::row_reverse || d == Direction::column_reverse; }

float clamp_opt(float v, std::optional<float> lo, std::optional<float> hi) {
    if (hi && v > *hi) v = *hi;
    if (lo && v < *lo) v = *lo;
    return v < 0 ? 0 : v;
}

struct FlexItem {
    NodeId               id = no_node;
    const Style*         style = nullptr;
    float                base = 0;
    float                hypo = 0;
    float                target = 0;
    std::optional<float> min_main, max_main, min_cross, max_cross;
    bool                 auto_min = false;  // min_main is the automatic minimum
    float                m_main_start = 0, m_main_end = 0, m_cross_start = 0, m_cross_end = 0;
    std::optional<float> cross_known;
    float                cross = 0;
    float                baseline = 0;
    Align                align = Align::stretch;
    bool                 frozen = false;
    float                main_pos = 0, cross_pos = 0;

    float outer_main(float v) const { return v + m_main_start + m_main_end; }
};

struct FlexLine {
    std::size_t begin = 0, end = 0;
    float       cross = 0;
    float       max_above = 0;  // baseline alignment
    float       offset = 0;     // cross position of the line
};

}  // namespace

struct FlexAlgorithm {
    static Size run(Engine& e, NodeId node, const Engine::Request& r);
};

Size Engine::layout_flex(NodeId node, const Request& r) { return FlexAlgorithm::run(*this, node, r); }

Size FlexAlgorithm::run(Engine& e, NodeId node, const Engine::Request& r) {
    const Style& s = e.at(node).style;
    const bool row = is_row(s.direction);
    const float pb_l = s.padding.left + s.border.left, pb_t = s.padding.top + s.border.top;
    const float pb_w = s.padding.horizontal() + s.border.horizontal();
    const float pb_h = s.padding.vertical() + s.border.vertical();
    auto inner = [](std::optional<float> v, float pb) -> std::optional<float> {
        if (!v) return std::nullopt;
        return std::max(0.0f, *v - pb);
    };
    const std::optional<float> inner_w = inner(r.width, pb_w), inner_h = inner(r.height, pb_h);
    auto avail_inner = [](Available a, float pb) {
        return a.is_definite() ? Available::definite(std::max(0.0f, a.value - pb)) : a;
    };
    const Available avail_w = inner_w ? Available::definite(*inner_w) : avail_inner(r.avail_width, pb_w);
    const Available avail_h = inner_h ? Available::definite(*inner_h) : avail_inner(r.avail_height, pb_h);

    const std::optional<float> inner_main = row ? inner_w : inner_h;
    const std::optional<float> inner_cross = row ? inner_h : inner_w;
    const Available avail_main = row ? avail_w : avail_h;
    const Available avail_cross = row ? avail_h : avail_w;
    const bool scroll_main = (row ? s.overflow_x : s.overflow_y) == Overflow::scroll;
    const bool scroll_cross = (row ? s.overflow_y : s.overflow_x) == Overflow::scroll;
    const float gap_main = row ? s.column_gap : s.row_gap;
    const float gap_cross = row ? s.row_gap : s.column_gap;

    // Lays a child out (measure or perform) with sizes given per axis.
    auto child_layout = [&](NodeId c, std::optional<float> main, std::optional<float> cross, Available am, Available ac,
                            bool perform) {
        Engine::Request cr;
        cr.width = row ? main : cross;
        cr.height = row ? cross : main;
        cr.avail_width = row ? am : ac;
        cr.avail_height = row ? ac : am;
        cr.parent_width = inner_w;
        cr.parent_height = inner_h;
        cr.perform = perform;
        Size sz = e.layout(c, cr);
        return std::pair{row ? sz.width : sz.height, row ? sz.height : sz.width};
    };

    // ── items ──
    std::vector<FlexItem> items;
    for (NodeId c : std::vector<NodeId>(e.at(node).children)) {
        const Style& cs = e.at(c).style;
        if (cs.display == Display::none) {
            e.at(c).frame = {};
            e.hide_subtree(c);
            continue;
        }
        if (cs.position == Position::absolute) continue;
        FlexItem it;
        it.id = c;
        it.style = &cs;
        it.m_main_start = row ? cs.margin.left : cs.margin.top;
        it.m_main_end = row ? cs.margin.right : cs.margin.bottom;
        it.m_cross_start = row ? cs.margin.top : cs.margin.left;
        it.m_cross_end = row ? cs.margin.bottom : cs.margin.right;
        std::optional<float> w = cs.width.resolve(inner_w), h = cs.height.resolve(inner_h);
        if (cs.aspect_ratio > 0) {
            if (w && !h) h = *w / cs.aspect_ratio;
            else if (h && !w) w = *h * cs.aspect_ratio;
        }
        std::optional<float> size_main = row ? w : h;
        it.cross_known = row ? h : w;
        it.min_main = (row ? cs.min_width : cs.min_height).resolve(row ? inner_w : inner_h);
        it.max_main = (row ? cs.max_width : cs.max_height).resolve(row ? inner_w : inner_h);
        it.min_cross = (row ? cs.min_height : cs.min_width).resolve(row ? inner_h : inner_w);
        it.max_cross = (row ? cs.max_height : cs.max_width).resolve(row ? inner_h : inner_w);
        if (it.cross_known) it.cross_known = clamp_opt(*it.cross_known, it.min_cross, it.max_cross);
        it.align = cs.align_self == Align::auto_ ? s.align_items : cs.align_self;

        // Flex base size (§9.2.3).
        std::optional<float> basis = cs.basis.resolve(inner_main);
        if (!basis) basis = size_main;
        if (!basis) {
            // A content basis is the item's max-content main size (§9.2.3 E).
            Available am = Available::max_content();
            Available ac = it.cross_known || !avail_cross.is_definite()
                               ? (it.cross_known ? Available::definite(*it.cross_known) : avail_cross)
                               : Available::definite(std::max(0.0f, avail_cross.value - it.m_cross_start - it.m_cross_end));
            // A stretched item with a definite line cross size is measured at that size (§9.2.3 E).
            std::optional<float> cross = it.cross_known;
            if (!cross && it.align == Align::stretch && inner_cross && !scroll_cross)
                cross = std::max(0.0f, *inner_cross - it.m_cross_start - it.m_cross_end);
            basis = child_layout(c, std::nullopt, cross, am, ac, false).first;
        }
        it.base = *basis;
        // Automatic minimum size (§4.5): the min-content size, for items that
        // do not scroll, capped by a specified size.
        if (!it.min_main && (row ? cs.overflow_x : cs.overflow_y) == Overflow::visible) it.auto_min = true;
        it.hypo = clamp_opt(it.base, it.min_main, it.max_main);
        items.push_back(it);
    }

    auto effective_min = [&](FlexItem& it) -> std::optional<float> {
        if (!it.auto_min) return it.min_main;
        // Computed on first need: only items that shrink pay for it.
        auto content_min = child_layout(it.id, std::nullopt, it.cross_known, Available::min_content(),
                                        it.cross_known ? Available::definite(*it.cross_known) : avail_cross, false)
                               .first;
        const Style& cs = *it.style;
        std::optional<float> specified = (row ? cs.width : cs.height).resolve(row ? inner_w : inner_h);
        float m = specified ? std::min(content_min, *specified) : content_min;
        if (it.max_main) m = std::min(m, *it.max_main);
        it.min_main = m;
        it.auto_min = false;
        return it.min_main;
    };

    // ── lines (§9.3) ──
    float line_limit = infinity;
    if (s.wrap != Wrap::no_wrap && !scroll_main) {
        if (inner_main) line_limit = *inner_main;
        else if (avail_main.is_definite()) line_limit = avail_main.value;
    }
    std::vector<FlexLine> lines;
    {
        FlexLine line;
        float used = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            float outer = items[i].outer_main(items[i].hypo);
            if (line.end > line.begin && used + gap_main + outer > line_limit) {
                lines.push_back(line);
                line = FlexLine{i, i};
                used = 0;
            }
            used += (line.end > line.begin ? gap_main : 0) + outer;
            line.end = i + 1;
        }
        if (line.end > line.begin || lines.empty()) lines.push_back(line);
    }

    // The main size the lines flex into.
    float container_main = 0;
    if (inner_main) {
        container_main = *inner_main;
    } else {
        float widest = 0;
        for (const FlexLine& l : lines) {
            float sum = 0;
            for (std::size_t i = l.begin; i < l.end; ++i) sum += items[i].outer_main(items[i].hypo);
            if (l.end > l.begin) sum += gap_main * static_cast<float>(l.end - l.begin - 1);
            widest = std::max(widest, sum);
        }
        // fit-content: the max-content size, but no wider than the room given.
        container_main = avail_main.is_definite() && !scroll_main ? std::min(widest, avail_main.value) : widest;
        if (avail_main.kind == Available::Kind::min_content) container_main = widest;
    }

    // ── resolve flexible lengths (§9.7) ──
    for (const FlexLine& l : lines) {
        std::size_t n = l.end - l.begin;
        if (n == 0) continue;
        float gaps = gap_main * static_cast<float>(n - 1);
        float sum_hypo = gaps;
        for (std::size_t i = l.begin; i < l.end; ++i) sum_hypo += items[i].outer_main(items[i].hypo);
        float space = container_main;
        if (scroll_main) space = std::max(space, sum_hypo);  // a scrolling axis never shrinks its content
        bool growing = sum_hypo < space;
        for (std::size_t i = l.begin; i < l.end; ++i) {
            FlexItem& it = items[i];
            float factor = growing ? it.style->grow : it.style->shrink;
            it.frozen = factor == 0 || (growing && it.base > it.hypo) || (!growing && it.base < it.hypo) ||
                        sum_hypo == space;
            it.target = it.frozen ? it.hypo : it.base;
        }
        auto free_space = [&]() {
            float used = gaps;
            for (std::size_t i = l.begin; i < l.end; ++i)
                used += items[i].outer_main(items[i].frozen ? items[i].target : items[i].base);
            return space - used;
        };
        float initial_free = free_space();
        for (int guard = 0; guard < 64; ++guard) {
            bool any = false;
            for (std::size_t i = l.begin; i < l.end; ++i) any = any || !items[i].frozen;
            if (!any) break;
            float remaining = free_space();
            float sum_factors = 0;
            for (std::size_t i = l.begin; i < l.end; ++i)
                if (!items[i].frozen) sum_factors += growing ? items[i].style->grow : items[i].style->shrink;
            if (sum_factors < 1) {
                float v = initial_free * sum_factors;
                if (std::abs(v) < std::abs(remaining)) remaining = v;
            }
            if (remaining != 0) {
                if (growing) {
                    for (std::size_t i = l.begin; i < l.end; ++i)
                        if (!items[i].frozen) items[i].target = items[i].base + remaining * items[i].style->grow / sum_factors;
                } else {
                    float sum_scaled = 0;
                    for (std::size_t i = l.begin; i < l.end; ++i)
                        if (!items[i].frozen) sum_scaled += items[i].style->shrink * items[i].base;
                    for (std::size_t i = l.begin; i < l.end; ++i) {
                        FlexItem& it = items[i];
                        if (it.frozen) continue;
                        float scaled = it.style->shrink * it.base;
                        it.target = sum_scaled > 0 ? it.base - std::abs(remaining) * scaled / sum_scaled : it.base;
                    }
                }
            } else {
                for (std::size_t i = l.begin; i < l.end; ++i)
                    if (!items[i].frozen) items[i].target = items[i].base;
            }
            // Clamp and freeze violators (§9.7.4 d, e).
            float total = 0;
            std::vector<float> violation(n, 0);
            for (std::size_t i = l.begin; i < l.end; ++i) {
                FlexItem& it = items[i];
                if (it.frozen) continue;
                float clamped = clamp_opt(it.target, effective_min(it), it.max_main);
                violation[i - l.begin] = clamped - it.target;
                total += violation[i - l.begin];
                it.target = clamped;
            }
            for (std::size_t i = l.begin; i < l.end; ++i) {
                FlexItem& it = items[i];
                if (it.frozen) continue;
                float v = violation[i - l.begin];
                if (total == 0 || (total > 0 && v > 0) || (total < 0 && v < 0)) it.frozen = true;
            }
        }
    }

    // ── cross sizes (§9.4) ──
    for (FlexItem& it : items) {
        if (it.cross_known) {
            it.cross = *it.cross_known;
        } else {
            Available ac = avail_cross.is_definite() && !scroll_cross
                               ? Available::definite(std::max(0.0f, avail_cross.value - it.m_cross_start - it.m_cross_end))
                               : (scroll_cross ? Available::max_content() : avail_cross);
            it.cross = clamp_opt(child_layout(it.id, it.target, std::nullopt, Available::definite(it.target), ac, false).second,
                                 it.min_cross, it.max_cross);
        }
        it.baseline = row ? e.at(it.id).baseline : it.cross;
    }
    const bool single = s.wrap == Wrap::no_wrap;
    float lines_cross = 0;
    for (FlexLine& l : lines) {
        if (single && inner_cross) {
            l.cross = *inner_cross;
        } else {
            float below = 0;
            for (std::size_t i = l.begin; i < l.end; ++i) {
                const FlexItem& it = items[i];
                if (it.align == Align::baseline && row) {
                    l.max_above = std::max(l.max_above, it.m_cross_start + it.baseline);
                    below = std::max(below, it.cross + it.m_cross_end - it.baseline);
                } else {
                    l.cross = std::max(l.cross, it.cross + it.m_cross_start + it.m_cross_end);
                }
            }
            l.cross = std::max(l.cross, l.max_above + below);
        }
        lines_cross += l.cross;
    }
    if (!lines.empty()) lines_cross += gap_cross * static_cast<float>(lines.size() - 1);
    float container_cross = inner_cross ? *inner_cross : lines_cross;
    if (!inner_cross) {
        // An auto cross size is still within the container's own min and max
        // (border-box sizes), and a single line takes the clamped size: items
        // stretch to it (§9.4 15), so a popup's options span a popup widened
        // to its anchor.
        const float pb_cross = row ? pb_h : pb_w;
        const std::optional<float> parent_cross = row ? r.parent_height : r.parent_width;
        if (auto lo = (row ? s.min_height : s.min_width).resolve(parent_cross)) container_cross = std::max(container_cross, *lo - pb_cross);
        if (auto hi = (row ? s.max_height : s.max_width).resolve(parent_cross)) container_cross = std::min(container_cross, *hi - pb_cross);
        container_cross = std::max(0.0f, container_cross);
    }
    if (single && !lines.empty() && !inner_cross) lines.front().cross = container_cross;

    // align-content (§9.4 15)
    {
        float free = container_cross - lines_cross;
        float offset = 0, between = gap_cross;
        float extra_per_line = 0;
        std::size_t n = lines.size();
        if (!single && n > 0) {
            switch (s.align_content) {
                case Align::end: offset = free; break;
                case Align::center: offset = free / 2; break;
                case Align::stretch:
                    if (free > 0) extra_per_line = free / static_cast<float>(n);
                    break;
                default: break;
            }
            // space-between / around / evenly arrive through Justify-like values in align_content's
            // Align vocabulary only as start; distribution beyond stretch is not offered (CSS's
            // `normal` is stretch).
        }
        float pos = offset;
        for (FlexLine& l : lines) {
            l.cross += extra_per_line;
            l.offset = pos;
            pos += l.cross + between;
        }
        if (s.wrap == Wrap::wrap_reverse)
            for (FlexLine& l : lines) l.offset = container_cross - l.offset - l.cross;
    }

    // Stretch (§9.4 11).
    for (const FlexLine& l : lines)
        for (std::size_t i = l.begin; i < l.end; ++i) {
            FlexItem& it = items[i];
            if (it.align == Align::stretch && !it.cross_known)
                it.cross = clamp_opt(l.cross - it.m_cross_start - it.m_cross_end, it.min_cross, it.max_cross);
        }

    // ── main-axis positions (§9.5) and cross-axis positions (§9.6) ──
    float content_main = 0, content_cross = 0;
    for (const FlexLine& l : lines) {
        std::size_t n = l.end - l.begin;
        if (n == 0) continue;
        float used = gap_main * static_cast<float>(n - 1);
        for (std::size_t i = l.begin; i < l.end; ++i) used += items[i].outer_main(items[i].target);
        float free = container_main - used;
        float offset = 0, between = gap_main;
        if (free > 0) {
            switch (s.justify) {
                case Justify::start: break;
                case Justify::end: offset = free; break;
                case Justify::center: offset = free / 2; break;
                case Justify::space_between:
                    if (n > 1) between += free / static_cast<float>(n - 1);
                    break;
                case Justify::space_around:
                    offset = free / static_cast<float>(n) / 2;
                    between += free / static_cast<float>(n);
                    break;
                case Justify::space_evenly:
                    offset = free / static_cast<float>(n + 1);
                    between += free / static_cast<float>(n + 1);
                    break;
            }
        } else if (free < 0 && s.justify == Justify::end && !scroll_main) {
            offset = free;
        } else if (free < 0 && s.justify == Justify::center && !scroll_main) {
            offset = free / 2;
        }
        float pos = offset;
        for (std::size_t i = l.begin; i < l.end; ++i) {
            FlexItem& it = items[i];
            pos += it.m_main_start;
            it.main_pos = is_reverse(s.direction) ? container_main - pos - it.target : pos;
            pos += it.target + it.m_main_end + between;
            switch (it.align) {
                case Align::end: it.cross_pos = l.cross - it.cross - it.m_cross_end; break;
                case Align::center: it.cross_pos = it.m_cross_start + (l.cross - it.m_cross_start - it.m_cross_end - it.cross) / 2; break;
                case Align::baseline:
                    it.cross_pos = row ? l.max_above - it.baseline : it.m_cross_start;
                    break;
                default: it.cross_pos = it.m_cross_start; break;
            }
            it.cross_pos += l.offset;
            content_main = std::max(content_main, it.main_pos + it.target + it.m_main_end);
            content_cross = std::max(content_cross, it.cross_pos + it.cross + it.m_cross_end);
        }
    }

    float width = r.width ? *r.width : (row ? container_main : container_cross) + pb_w;
    float height = r.height ? *r.height : (row ? container_cross : container_main) + pb_h;

    if (r.perform) {
        float final_inner_w = std::max(0.0f, width - pb_w), final_inner_h = std::max(0.0f, height - pb_h);
        for (FlexItem& it : items) {
            Engine::Request cr;
            cr.width = row ? it.target : it.cross;
            cr.height = row ? it.cross : it.target;
            cr.avail_width = Available::definite(*cr.width);
            cr.avail_height = Available::definite(*cr.height);
            cr.parent_width = final_inner_w;
            cr.parent_height = final_inner_h;
            cr.perform = true;
            Size sz = e.layout(it.id, cr);
            float x = row ? it.main_pos : it.cross_pos, y = row ? it.cross_pos : it.main_pos;
            e.at(it.id).frame = {pb_l + x, pb_t + y, sz.width, sz.height};
        }
        Engine::Node& m = e.at(node);
        m.content = row ? Size{content_main, content_cross} : Size{content_cross, content_main};
        m.baseline = items.empty() ? height
                                   : e.at(items.front().id).frame.y + e.at(items.front().id).baseline;
    }
    return {width, height};
}

}  // namespace omui::layout
