// Grid, after CSS Grid Layout §8 (placement) and §11 (track sizing), in the
// subset openmui's spec names:
//
// - placement: explicit lines (negative lines count from the end), spans, and
//   auto-placement in row or column flow (sparse, cursor-based), creating
//   implicit tracks of the auto track size;
// - track sizing: fixed and percentage tracks; auto, min-content and
//   max-content tracks sized by the items in them (base size from min-content
//   contributions, growth limit from max-content); minmax(); spanning items
//   distribute what they need beyond their tracks evenly over the intrinsic
//   tracks they span; free space grows tracks to their limits, then fr tracks
//   share what remains (§11.7, finding the size of an fr), then auto tracks
//   stretch;
// - alignment of items in their cells by justify_self / align_self
//   (stretch by default).
module openmui.layout;

import std;
import openmui.geom;
import :internal;

namespace omui::layout {

namespace {

constexpr float infinity = std::numeric_limits<float>::infinity();

struct Placed {
    NodeId id;
    int    col = 0, col_span = 1;
    int    row = 0, row_span = 1;
    Size   size;  // laid-out border box
};

struct TrackSize {
    Track::Kind min_kind = Track::Kind::auto_;
    float       min_value = 0;
    Track::Kind max_kind = Track::Kind::auto_;
    float       max_value = 0;
    float       base = 0;
    float       limit = infinity;
    float       fr = 0;
};

TrackSize to_sizing(const Track& t, std::optional<float> container) {
    TrackSize s;
    auto fixed = [&](Track::Kind k, float v) -> std::optional<float> {
        if (k == Track::Kind::points) return v;
        if (k == Track::Kind::percent && container) return *container * v / 100.0f;
        return std::nullopt;
    };
    Track::Kind lo = t.kind, hi = t.kind;
    float lov = t.value, hiv = t.value;
    if (t.kind == Track::Kind::minmax) {
        lo = t.min_kind;
        lov = t.min_value;
        hi = t.max_kind;
        hiv = t.max_value;
    }
    if (t.kind == Track::Kind::fr) {
        lo = Track::Kind::auto_;
        hi = Track::Kind::fr;
    }
    if (lo == Track::Kind::percent && !container) lo = Track::Kind::auto_;
    if (hi == Track::Kind::percent && !container) hi = Track::Kind::auto_;
    s.min_kind = lo;
    s.min_value = lov;
    s.max_kind = hi;
    s.max_value = hiv;
    if (auto f = fixed(lo, lov)) s.base = *f;
    if (auto f = fixed(hi, hiv)) s.limit = *f;
    if (hi == Track::Kind::fr) {
        s.fr = hiv;
        s.limit = infinity;
    }
    if (s.limit < s.base) s.limit = s.base;
    return s;
}

bool intrinsic_min(const TrackSize& t) {
    return t.min_kind == Track::Kind::auto_ || t.min_kind == Track::Kind::min_content || t.min_kind == Track::Kind::max_content;
}
bool intrinsic_max(const TrackSize& t) {
    return t.max_kind == Track::Kind::auto_ || t.max_kind == Track::Kind::min_content || t.max_kind == Track::Kind::max_content;
}

// Resolves a 1-based (or negative) line to a 0-based track index.
int line_to_index(int line, int explicit_tracks) {
    if (line > 0) return line - 1;
    if (line < 0) return std::max(0, explicit_tracks + 1 + line);
    return 0;
}

}  // namespace

struct GridAlgorithm {
    static Size run(Engine& e, NodeId node, const Engine::Request& r);
};

Size Engine::layout_grid(NodeId node, const Request& r) { return GridAlgorithm::run(*this, node, r); }

Size GridAlgorithm::run(Engine& e, NodeId node, const Engine::Request& r) {
    const Style& s = e.at(node).style;
    const float pb_l = s.padding.left + s.border.left, pb_t = s.padding.top + s.border.top;
    const float pb_w = s.padding.horizontal() + s.border.horizontal();
    const float pb_h = s.padding.vertical() + s.border.vertical();
    std::optional<float> inner_w = r.width ? std::optional<float>(std::max(0.0f, *r.width - pb_w)) : std::nullopt;
    std::optional<float> inner_h = r.height ? std::optional<float>(std::max(0.0f, *r.height - pb_h)) : std::nullopt;
    std::optional<float> room_w = inner_w;
    if (!room_w && r.avail_width.is_definite()) room_w = std::max(0.0f, r.avail_width.value - pb_w);

    const int explicit_cols = static_cast<int>(s.columns.size());
    const int explicit_rows = static_cast<int>(s.rows.size());

    // ── placement (§8.5) ──
    std::vector<Placed> items;
    for (NodeId c : std::vector<NodeId>(e.at(node).children)) {
        const Style& cs = e.at(c).style;
        if (cs.display == Display::none) {
            e.at(c).frame = {};
            e.hide_subtree(c);
            continue;
        }
        if (cs.position == Position::absolute) continue;
        Placed p{c};
        p.col_span = std::max<int>(1, cs.column_start.span);
        p.row_span = std::max<int>(1, cs.row_start.span);
        p.col = cs.column_start.line ? line_to_index(cs.column_start.line, explicit_cols) : -1;
        p.row = cs.row_start.line ? line_to_index(cs.row_start.line, explicit_rows) : -1;
        items.push_back(p);
    }
    int cols = std::max(1, explicit_cols);
    for (const Placed& p : items)
        if (p.col >= 0) cols = std::max(cols, p.col + p.col_span);
        else cols = std::max(cols, p.col_span);
    int rows = explicit_rows;
    std::vector<std::vector<bool>> taken;  // [row][col]
    auto ensure_rows = [&](int n) {
        while (static_cast<int>(taken.size()) < n) taken.emplace_back(static_cast<std::size_t>(cols), false);
        rows = std::max(rows, n);
    };
    auto fits = [&](int row, int col, int rs, int cspan) {
        if (col + cspan > cols) return false;
        ensure_rows(row + rs);
        for (int y = row; y < row + rs; ++y)
            for (int x = col; x < col + cspan; ++x)
                if (taken[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) return false;
        return true;
    };
    auto occupy = [&](const Placed& p) {
        ensure_rows(p.row + p.row_span);
        for (int y = p.row; y < p.row + p.row_span; ++y)
            for (int x = p.col; x < p.col + p.col_span; ++x) taken[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = true;
    };
    // Items with both lines first, then items locked to a row or column, then the rest.
    for (Placed& p : items)
        if (p.col >= 0 && p.row >= 0) occupy(p);
    for (Placed& p : items) {
        if ((p.col >= 0) == (p.row >= 0)) continue;
        if (p.row >= 0) {
            int c = 0;
            while (!fits(p.row, c, p.row_span, p.col_span)) ++c;
            if (c + p.col_span > cols) c = 0;
            p.col = c;
        } else {
            int rr = 0;
            while (!fits(rr, p.col, p.row_span, p.col_span)) ++rr;
            p.row = rr;
        }
        occupy(p);
    }
    int cursor_row = 0, cursor_col = 0;
    for (Placed& p : items) {
        if (p.col >= 0 && p.row >= 0) continue;
        if (s.grid_flow == GridFlow::row) {
            while (!fits(cursor_row, cursor_col, p.row_span, p.col_span)) {
                if (++cursor_col + p.col_span > cols) {
                    cursor_col = 0;
                    ++cursor_row;
                }
            }
        } else {
            while (!fits(cursor_row, cursor_col, p.row_span, p.col_span)) {
                ensure_rows(cursor_row + p.row_span);
                if (++cursor_row + p.row_span > std::max(rows, explicit_rows) && cursor_row > 0) {
                    cursor_row = 0;
                    ++cursor_col;
                    if (cursor_col + p.col_span > cols) {
                        cols = cursor_col + p.col_span;
                        for (auto& rowv : taken) rowv.resize(static_cast<std::size_t>(cols), false);
                    }
                }
            }
        }
        p.row = cursor_row;
        p.col = cursor_col;
        occupy(p);
    }
    rows = std::max(rows, 1);

    // ── track sizing (§11) ──
    auto track_at = [](const std::vector<Track>& list, const Track& implicit, int i) {
        return i < static_cast<int>(list.size()) ? list[static_cast<std::size_t>(i)] : implicit;
    };
    std::vector<TrackSize> col_tracks, row_tracks;
    for (int i = 0; i < cols; ++i) col_tracks.push_back(to_sizing(track_at(s.columns, s.auto_columns, i), inner_w));
    for (int i = 0; i < rows; ++i) row_tracks.push_back(to_sizing(track_at(s.rows, s.auto_rows, i), inner_h));

    auto measure = [&](NodeId c, std::optional<float> w, std::optional<float> h, Available aw, Available ah) {
        Engine::Request cr;
        cr.width = w;
        cr.height = h;
        cr.avail_width = aw;
        cr.avail_height = ah;
        cr.parent_width = inner_w;
        cr.parent_height = inner_h;
        return e.layout(c, cr);
    };

    auto size_tracks = [&](std::vector<TrackSize>& tracks, bool columns, std::optional<float> space, float gap,
                           const std::vector<float>* other_axis) {
        // Contributions of the items in intrinsic tracks.
        auto contribution = [&](const Placed& p, bool min_content) {
            const Style& cs = e.at(p.id).style;
            float margin = columns ? cs.margin.horizontal() : cs.margin.vertical();
            if (columns) {
                Size sz = measure(p.id, std::nullopt, std::nullopt,
                                  min_content ? Available::min_content() : Available::max_content(), Available::max_content());
                return sz.width + margin;
            }
            // Rows: the item's height at the width of the columns it spans.
            float w = 0;
            for (int i = p.col; i < p.col + p.col_span; ++i) w += (*other_axis)[static_cast<std::size_t>(i)];
            w += s.column_gap * static_cast<float>(p.col_span - 1);
            w = std::max(0.0f, w - cs.margin.horizontal());
            Size sz = measure(p.id, w, std::nullopt, Available::definite(w), Available::max_content());
            return sz.height + margin;
        };
        for (int pass = 0; pass < 2; ++pass) {  // pass 0: span 1, pass 1: spanning items
            for (const Placed& p : items) {
                int start = columns ? p.col : p.row;
                int span = columns ? p.col_span : p.row_span;
                if ((pass == 0) != (span == 1)) continue;
                bool any_intrinsic = false;
                for (int i = start; i < start + span; ++i)
                    any_intrinsic = any_intrinsic || intrinsic_min(tracks[static_cast<std::size_t>(i)]) ||
                                    intrinsic_max(tracks[static_cast<std::size_t>(i)]);
                if (!any_intrinsic) continue;
                float min_c = contribution(p, true);
                float max_c = columns ? contribution(p, false) : min_c;
                if (span == 1) {
                    TrackSize& t = tracks[static_cast<std::size_t>(start)];
                    if (t.min_kind == Track::Kind::max_content) t.base = std::max(t.base, max_c);
                    else if (intrinsic_min(t)) t.base = std::max(t.base, min_c);
                    if (t.max_kind == Track::Kind::min_content) t.limit = t.limit == infinity ? min_c : std::max(t.limit, min_c);
                    else if (intrinsic_max(t)) t.limit = t.limit == infinity ? max_c : std::max(t.limit, max_c);
                } else {
                    float have = gap * static_cast<float>(span - 1);
                    std::vector<std::size_t> flexible;
                    for (int i = start; i < start + span; ++i) {
                        have += tracks[static_cast<std::size_t>(i)].base;
                        if (intrinsic_min(tracks[static_cast<std::size_t>(i)])) flexible.push_back(static_cast<std::size_t>(i));
                    }
                    if (min_c > have && !flexible.empty()) {
                        float extra = (min_c - have) / static_cast<float>(flexible.size());
                        for (std::size_t i : flexible) tracks[i].base += extra;
                    }
                }
            }
        }
        for (TrackSize& t : tracks)
            if (t.limit < t.base) t.limit = t.base;
        float gaps = gap * static_cast<float>(std::max<std::size_t>(tracks.size(), 1) - 1);
        auto used = [&]() {
            float u = gaps;
            for (const TrackSize& t : tracks) u += t.base;
            return u;
        };
        // Maximise tracks (§11.6): grow non-fr tracks toward their limits.
        if (space) {
            float free = *space - used();
            for (int guard = 0; guard < 16 && free > 0; ++guard) {
                std::vector<std::size_t> growable;
                for (std::size_t i = 0; i < tracks.size(); ++i)
                    if (tracks[i].fr == 0 && tracks[i].base < tracks[i].limit && tracks[i].limit != infinity) growable.push_back(i);
                if (growable.empty()) break;
                float share = free / static_cast<float>(growable.size());
                for (std::size_t i : growable) {
                    float g = std::min(share, tracks[i].limit - tracks[i].base);
                    tracks[i].base += g;
                    free -= g;
                }
            }
        }
        // Flexible tracks (§11.7).
        float sum_fr = 0;
        for (const TrackSize& t : tracks) sum_fr += t.fr;
        if (sum_fr > 0) {
            float fr_size = 0;
            if (space) {
                float leftover = *space - gaps;
                for (const TrackSize& t : tracks)
                    if (t.fr == 0) leftover -= t.base;
                std::vector<bool> inflexible(tracks.size(), false);
                for (int guard = 0; guard < 16; ++guard) {
                    float flex = 0, left = leftover;
                    for (std::size_t i = 0; i < tracks.size(); ++i) {
                        if (tracks[i].fr == 0) continue;
                        if (inflexible[i]) left -= tracks[i].base;
                        else flex += tracks[i].fr;
                    }
                    // The hypothetical fr size: leftover / max(Σ flex, 1) (§11.7.1).
                    fr_size = flex > 0 ? std::max(0.0f, left) / std::max(flex, 1.0f) : 0;
                    bool changed = false;
                    for (std::size_t i = 0; i < tracks.size(); ++i)
                        if (tracks[i].fr > 0 && !inflexible[i] && tracks[i].base > fr_size * tracks[i].fr) {
                            inflexible[i] = true;
                            changed = true;
                        }
                    if (!changed) break;
                }
            } else {
                // Indefinite space: the fr size that fits every flexible track's content (§11.7.1).
                for (const TrackSize& t : tracks)
                    if (t.fr > 0) fr_size = std::max(fr_size, t.base / (t.fr < 1 ? 1 : t.fr));
            }
            for (TrackSize& t : tracks)
                if (t.fr > 0) t.base = std::max(t.base, fr_size * t.fr);
        } else if (space) {
            // Stretch auto tracks (§11.8) over what is left.
            float free = *space - used();
            std::vector<std::size_t> autos;
            for (std::size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].max_kind == Track::Kind::auto_) autos.push_back(i);
            if (free > 0 && !autos.empty())
                for (std::size_t i : autos) tracks[i].base += free / static_cast<float>(autos.size());
        }
    };

    size_tracks(col_tracks, true, inner_w ? inner_w : room_w, s.column_gap, nullptr);
    std::vector<float> col_sizes;
    for (const TrackSize& t : col_tracks) col_sizes.push_back(t.base);
    size_tracks(row_tracks, false, inner_h, s.row_gap, &col_sizes);
    std::vector<float> row_sizes;
    for (const TrackSize& t : row_tracks) row_sizes.push_back(t.base);

    auto offsets = [](const std::vector<float>& sizes, float gap) {
        std::vector<float> out(sizes.size() + 1, 0);
        for (std::size_t i = 0; i < sizes.size(); ++i) out[i + 1] = out[i] + sizes[i] + (i + 1 < sizes.size() ? gap : 0);
        return out;
    };
    auto col_off = offsets(col_sizes, s.column_gap);
    auto row_off = offsets(row_sizes, s.row_gap);
    float grid_w = col_off.back(), grid_h = row_off.back();
    float width = r.width ? *r.width : grid_w + pb_w;
    float height = r.height ? *r.height : grid_h + pb_h;

    if (r.perform) {
        float iw = std::max(0.0f, width - pb_w), ih = std::max(0.0f, height - pb_h);
        float content_w = 0, content_h = 0;
        for (Placed& p : items) {
            const Style& cs = e.at(p.id).style;
            auto cu = static_cast<std::size_t>(p.col), ru = static_cast<std::size_t>(p.row);
            float cell_x = col_off[cu], cell_y = row_off[ru];
            float cell_w = col_off[cu + static_cast<std::size_t>(p.col_span)] - cell_x -
                           (cu + static_cast<std::size_t>(p.col_span) < col_sizes.size() ? s.column_gap : 0);
            float cell_h = row_off[ru + static_cast<std::size_t>(p.row_span)] - cell_y -
                           (ru + static_cast<std::size_t>(p.row_span) < row_sizes.size() ? s.row_gap : 0);
            Align h = cs.justify_self == Align::auto_ ? Align::stretch : cs.justify_self;
            Align v = cs.align_self == Align::auto_ ? (s.align_items == Align::auto_ ? Align::stretch : s.align_items) : cs.align_self;
            float avail_w = std::max(0.0f, cell_w - cs.margin.horizontal());
            float avail_h = std::max(0.0f, cell_h - cs.margin.vertical());
            Engine::Request cr;
            cr.parent_width = iw;
            cr.parent_height = ih;
            cr.avail_width = Available::definite(avail_w);
            cr.avail_height = Available::definite(avail_h);
            if (h == Align::stretch && cs.width.is_auto()) cr.width = avail_w;
            if (v == Align::stretch && cs.height.is_auto()) cr.height = avail_h;
            cr.perform = true;
            Size sz = e.layout(p.id, cr);
            auto place = [](Align a, float space, float size, float m0, float m1) {
                switch (a) {
                    case Align::end: return space - size - m1;
                    case Align::center: return m0 + (space - m0 - m1 - size) / 2;
                    default: return m0;
                }
            };
            float x = cell_x + place(h, cell_w, sz.width, cs.margin.left, cs.margin.right);
            float y = cell_y + place(v, cell_h, sz.height, cs.margin.top, cs.margin.bottom);
            e.at(p.id).frame = {pb_l + x, pb_t + y, sz.width, sz.height};
            content_w = std::max(content_w, x + sz.width + cs.margin.right);
            content_h = std::max(content_h, y + sz.height + cs.margin.bottom);
        }
        Engine::Node& m = e.at(node);
        m.content = {std::max(content_w, grid_w), std::max(content_h, grid_h)};
        m.baseline = items.empty() ? height : e.at(items.front().id).frame.y + e.at(items.front().id).baseline;
    }
    return {width, height};
}

}  // namespace omui::layout
