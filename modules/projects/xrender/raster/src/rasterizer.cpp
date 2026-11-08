module xrender.raster.api;

import std;

namespace xrender::raster {

// ── paths ─────────────────────────────────────────────────────────────────

PathBuilder& PathBuilder::move_to(Point p) {
    verbs_.push_back(Verb::move);
    points_.push_back(p);
    return *this;
}
PathBuilder& PathBuilder::line_to(Point p) {
    verbs_.push_back(Verb::line);
    points_.push_back(p);
    return *this;
}
PathBuilder& PathBuilder::quad_to(Point c, Point p) {
    verbs_.push_back(Verb::quad);
    points_.push_back(c);
    points_.push_back(p);
    return *this;
}
PathBuilder& PathBuilder::cubic_to(Point c1, Point c2, Point p) {
    verbs_.push_back(Verb::cubic);
    points_.push_back(c1);
    points_.push_back(c2);
    points_.push_back(p);
    return *this;
}
PathBuilder& PathBuilder::close() {
    verbs_.push_back(Verb::close);
    return *this;
}
PathBuilder& PathBuilder::rect(const Rect& r) {
    return move_to({r.x, r.y}).line_to({r.right(), r.y}).line_to({r.right(), r.bottom()}).line_to({r.x, r.bottom()}).close();
}

// Shared geometry conversion; the rasterizer owns only coverage/stroking.
void flatten(PathData path,const Affine& m,Polygons& out,float tolerance) {
    mlibs::geom::flatten_path(path,m,out,tolerance);
}

// ── rasterising ───────────────────────────────────────────────────────────

namespace {

constexpr std::int32_t one = 256;  // subpixels per pixel

std::int32_t to_sub(float v) {
    constexpr float limit = 1e6f;
    if (!(v > -limit)) v = -limit;  // also NaN
    if (v > limit) v = limit;
    return static_cast<std::int32_t>(std::floor(static_cast<double>(v) * one + 0.5));
}

// floor(a / b) for b > 0, exact in 64 bits.
std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

}  // namespace

// A cell joins its row's list in column order (FreeType's ftgrays keeps
// its cells so): the sweep reads each row in order without a sort. A row's
// cells mostly come in rising columns (scanline walks a row left to right),
// so the search starts at the last cell when it lies to the left.
void Rasterizer::cell(std::int32_t ex, std::int32_t ey, std::int32_t cover, std::int32_t area) {
    if (ey < 0 || ey >= height_) return;
    if (ex < 0) ex = 0;
    if (ex > width_) ex = width_;  // the extra column: edges right of the clip
    if (cover == 0 && area == 0) return;
    std::int32_t before = -1, at;
    if (last_row_ == ey) {
        Cell& last = cells_[static_cast<std::size_t>(last_)];
        if (last.x == ex) {
            last.cover += cover;
            last.area += area;
            return;
        }
        if (last.x < ex) before = last_, at = last.next;
        else at = rows_[static_cast<std::size_t>(ey)];
    } else {
        at = rows_[static_cast<std::size_t>(ey)];
    }
    while (at >= 0 && cells_[static_cast<std::size_t>(at)].x < ex) {
        before = at;
        at = cells_[static_cast<std::size_t>(at)].next;
    }
    last_row_ = ey;
    if (at >= 0 && cells_[static_cast<std::size_t>(at)].x == ex) {
        Cell& found = cells_[static_cast<std::size_t>(at)];
        found.cover += cover;
        found.area += area;
        last_ = at;
        return;
    }
    const auto made = static_cast<std::int32_t>(cells_.size());
    cells_.push_back({ex, cover, area, at});
    if (before < 0) rows_[static_cast<std::size_t>(ey)] = made;
    else cells_[static_cast<std::size_t>(before)].next = made;
    last_ = made;
}

void Rasterizer::scanline(std::int32_t ey, std::int32_t x1, std::int32_t fy1, std::int32_t x2, std::int32_t fy2) {
    if (fy1 == fy2) return;
    std::int32_t ex1 = x1 >> 8, ex2 = x2 >> 8;
    std::int32_t fx1 = x1 & (one - 1), fx2 = x2 & (one - 1);
    if (ex1 == ex2) {
        cell(ex1, ey, fy2 - fy1, (fx1 + fx2) * (fy2 - fy1));
        return;
    }
    std::int64_t dx = static_cast<std::int64_t>(x2) - x1, dy = static_cast<std::int64_t>(fy2) - fy1;
    auto y_at = [&](std::int64_t x) { return static_cast<std::int32_t>(fy1 + floor_div((x - x1) * dy, dx)); };
    if (dx > 0) {
        std::int32_t y = y_at(static_cast<std::int64_t>(ex1 + 1) * one);
        cell(ex1, ey, y - fy1, (fx1 + one) * (y - fy1));
        for (std::int32_t ex = ex1 + 1; ex < ex2; ++ex) {
            std::int32_t yn = y_at(static_cast<std::int64_t>(ex + 1) * one);
            cell(ex, ey, yn - y, one * (yn - y));
            y = yn;
        }
        cell(ex2, ey, fy2 - y, fx2 * (fy2 - y));
    } else {
        // Leftwards, its cells given left to right (the row's list grows at
        // the end): the end's cell, the whole ones, the start's — each with
        // the crossings at its two sides, as walking from the start gives.
        std::int32_t y = y_at(static_cast<std::int64_t>(ex2 + 1) * one);  // at the end cell's right side
        cell(ex2, ey, fy2 - y, (fx2 + one) * (fy2 - y));
        for (std::int32_t ex = ex2 + 1; ex < ex1; ++ex) {
            const std::int32_t right = y_at(static_cast<std::int64_t>(ex + 1) * one);
            cell(ex, ey, y - right, one * (y - right));
            y = right;
        }
        cell(ex1, ey, y - fy1, fx1 * (y - fy1));
    }
}

void Rasterizer::line(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    if (y1 == y2) return;
    if (std::max(y1, y2) <= 0 || std::min(y1, y2) >= height_ * one) return;
    // Every crossing of a row boundary is computed from the edge's own
    // endpoints, and rows outside [0, height) are skipped rather than the edge
    // cut: the coverage of a row does not depend on which other rows are
    // rasterised. So a band of rows renders exactly as it does in a whole
    // surface (damage-region rendering relies on it).
    const std::int64_t dx = static_cast<std::int64_t>(x2) - x1, dy = static_cast<std::int64_t>(y2) - y1;
    auto x_on = [&](std::int64_t y) { return static_cast<std::int32_t>(x1 + floor_div((y - y1) * dx, dy)); };
    const std::int32_t ey1 = y1 >> 8, ey2 = y2 >> 8;
    const std::int32_t fy1 = y1 & (one - 1), fy2 = y2 & (one - 1);
    if (ey1 == ey2) {
        if (ey1 >= 0 && ey1 < height_) scanline(ey1, x1, fy1, x2, fy2);
        return;
    }
    if (dy > 0) {
        // Rows ey1 (from y1) … ey2 (to y2), downwards.
        std::int32_t ey = std::max(ey1, 0);
        std::int32_t xb;
        if (ey == ey1) {
            xb = x_on(static_cast<std::int64_t>(ey1 + 1) * one);
            scanline(ey1, x1, fy1, xb, one);
            ++ey;
        } else {
            xb = x_on(static_cast<std::int64_t>(ey) * one);
        }
        const std::int32_t last = std::min(ey2 - 1, height_ - 1);
        for (; ey <= last; ++ey) {
            const std::int32_t xn = x_on(static_cast<std::int64_t>(ey + 1) * one);
            scanline(ey, xb, 0, xn, one);
            xb = xn;
        }
        if (ey2 < height_ && ey == ey2) scanline(ey2, xb, 0, x2, fy2);
    } else {
        // Rows ey1 (from y1) … ey2 (to y2), upwards.
        std::int32_t ey = std::min(ey1, height_ - 1);
        std::int32_t xb;
        if (ey == ey1) {
            xb = x_on(static_cast<std::int64_t>(ey1) * one);
            scanline(ey1, x1, fy1, xb, 0);
            --ey;
        } else {
            xb = x_on(static_cast<std::int64_t>(ey + 1) * one);
        }
        const std::int32_t last = std::max(ey2 + 1, 0);
        for (; ey >= last; --ey) {
            const std::int32_t xn = x_on(static_cast<std::int64_t>(ey) * one);
            scanline(ey, xb, one, xn, 0);
            xb = xn;
        }
        if (ey2 >= 0 && ey == ey2) scanline(ey2, xb, one, x2, fy2);
    }
}

bool Rasterizer::prepare(const Polygons& polygons, PixelRect clip) {
    cells_.clear();
    last_row_ = -1;
    if (polygons.points.empty() || clip.empty()) return false;
    float l = polygons.points[0].x, t = polygons.points[0].y, r = l, b = t;
    for (const Point& p : polygons.points) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    }
    auto floori = [](float v) { return static_cast<std::int32_t>(std::floor(std::clamp(v, -1e6f, 1e6f))); };
    auto ceili = [](float v) { return static_cast<std::int32_t>(std::ceil(std::clamp(v, -1e6f, 1e6f))); };
    std::int32_t x0 = std::max(floori(l), clip.x), y0 = std::max(floori(t), clip.y);
    std::int32_t x1 = std::min(ceili(r), clip.x + clip.width), y1 = std::min(ceili(b), clip.y + clip.height);
    if (x1 <= x0 || y1 <= y0) return false;
    min_x_ = x0;
    min_y_ = y0;
    width_ = x1 - x0;
    height_ = y1 - y0;
    rows_.assign(static_cast<std::size_t>(height_), -1);

    // An edge is cut where it crosses the clip's sides: what lies left of the
    // clip runs down its first column (keeping its winding), what lies right
    // of it down the extra column (changing no visible pixel), and the part
    // between is the edge itself — not a line to its clamped end, which
    // would tilt it.
    const std::int64_t max_sub = static_cast<std::int64_t>(width_) * one;
    auto sx = [&](float v) { return static_cast<std::int64_t>(to_sub(v)) - static_cast<std::int64_t>(min_x_) * one; };
    auto sy = [&](float v) { return static_cast<std::int64_t>(to_sub(v)) - static_cast<std::int64_t>(min_y_) * one; };
    auto piece = [&](std::int64_t x1, std::int64_t y1, std::int64_t x2, std::int64_t y2) {
        line(static_cast<std::int32_t>(std::clamp<std::int64_t>(x1, 0, max_sub)), static_cast<std::int32_t>(y1),
             static_cast<std::int32_t>(std::clamp<std::int64_t>(x2, 0, max_sub)), static_cast<std::int32_t>(y2));
    };
    auto edge = [&](std::int64_t ax, std::int64_t ay, std::int64_t cx, std::int64_t cy) {
        std::array<std::int64_t, 2> sides{0, max_sub};
        if (cx < ax) std::swap(sides[0], sides[1]);  // crossed in the order the edge runs
        std::int64_t px = ax, py = ay;
        for (const std::int64_t side : sides)
            if ((px < side && side < cx) || (cx < side && side < px)) {
                const std::int64_t y = ay + floor_div((side - ax) * (cy - ay), cx - ax);
                piece(px, py, side, y);
                px = side, py = y;
            }
        piece(px, py, cx, cy);
    };
    // Each point to subpixels once (it ends one edge and starts the next).
    for (std::size_t pg = 0; pg < polygons.count(); ++pg) {
        auto poly = polygons.polygon(pg);
        if (poly.size() < 2) continue;
        const std::int64_t first_x = sx(poly[0].x), first_y = sy(poly[0].y);
        std::int64_t from_x = first_x, from_y = first_y;
        for (std::size_t i = 1; i < poly.size(); ++i) {
            const std::int64_t to_x = sx(poly[i].x), to_y = sy(poly[i].y);
            edge(from_x, from_y, to_x, to_y);
            from_x = to_x, from_y = to_y;
        }
        edge(from_x, from_y, first_x, first_y);
    }
    return true;
}

void Rasterizer::rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Coverage& out) {
    out.values.clear();
    out.bounds = {};
    if (!prepare(polygons, clip)) return;
    out.bounds = {min_x_, min_y_, width_, height_};
    out.values.resize(static_cast<std::size_t>(width_) * height_);
    sweep(rule, [&](std::int32_t y, std::int32_t x, std::int32_t count, std::uint8_t v) {
        std::fill_n(out.values.data() + static_cast<std::size_t>(y) * width_ + x, count, v);
    });
}

void Rasterizer::rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Spans& out) {
    out.runs.clear();
    out.bounds = {};
    if (!prepare(polygons, clip)) return;
    out.bounds = {min_x_, min_y_, width_, height_};
    sweep(rule, [&](std::int32_t y, std::int32_t x, std::int32_t count, std::uint8_t v) {
        if (v == 0) return;
        const std::int32_t gy = y + min_y_, gx = x + min_x_;
        if (!out.runs.empty()) {
            Span& last = out.runs.back();
            if (last.y == gy && last.value == v && last.x + last.length == gx) {
                last.length += count;
                return;
            }
        }
        out.runs.push_back({gy, gx, count, v});
    });
}

void fill_coverage(PathData path, const Affine& m, FillRule rule, PixelRect clip, Coverage& out) {
    Polygons polys;
    flatten(path, m, polys);
    Rasterizer r;
    r.rasterize(polys, rule, clip, out);
}

}  // namespace xrender::raster
