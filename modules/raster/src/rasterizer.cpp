module openmui.raster;

import std;

namespace omui::raster {

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

// ── flattening ────────────────────────────────────────────────────────────

namespace {

float length(Point v) { return std::sqrt(v.x * v.x + v.y * v.y); }

int segments_for(float deviation, float tolerance) {
    if (!(deviation > tolerance)) return 1;
    float n = std::ceil(std::sqrt(deviation / tolerance));
    return n > 256 ? 256 : static_cast<int>(n);
}

}  // namespace

void flatten(PathData path, const Affine& m, Polygons& out, float tolerance) {
    std::size_t pi = 0;
    bool open = false;
    Point current{};
    auto start = [&](Point p) {
        out.begin();
        out.points.push_back(p);
        open = true;
        current = p;
    };
    for (Verb v : path.verbs) {
        switch (v) {
            case Verb::move: {
                start(m.apply(path.points[pi++]));
                break;
            }
            case Verb::line: {
                Point p = m.apply(path.points[pi++]);
                if (!open) start(current);
                out.points.push_back(p);
                current = p;
                break;
            }
            case Verb::quad: {
                Point c = m.apply(path.points[pi]), p = m.apply(path.points[pi + 1]);
                pi += 2;
                if (!open) start(current);
                Point p0 = current;
                Point dd{p0.x - 2 * c.x + p.x, p0.y - 2 * c.y + p.y};
                int n = segments_for(length(dd) / 4, tolerance);
                for (int i = 1; i <= n; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(n), u = 1 - t;
                    out.points.push_back({u * u * p0.x + 2 * u * t * c.x + t * t * p.x, u * u * p0.y + 2 * u * t * c.y + t * t * p.y});
                }
                current = p;
                break;
            }
            case Verb::cubic: {
                Point c1 = m.apply(path.points[pi]), c2 = m.apply(path.points[pi + 1]), p = m.apply(path.points[pi + 2]);
                pi += 3;
                if (!open) start(current);
                Point p0 = current;
                Point d1{p0.x - 2 * c1.x + c2.x, p0.y - 2 * c1.y + c2.y};
                Point d2{c1.x - 2 * c2.x + p.x, c1.y - 2 * c2.y + p.y};
                int n = segments_for(0.75f * std::max(length(d1), length(d2)), tolerance);
                for (int i = 1; i <= n; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(n), u = 1 - t;
                    float a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t, d = t * t * t;
                    out.points.push_back({a * p0.x + b * c1.x + cc * c2.x + d * p.x, a * p0.y + b * c1.y + cc * c2.y + d * p.y});
                }
                current = p;
                break;
            }
            case Verb::close: {
                if (open) {
                    current = out.points[out.starts.back()];
                    out.closed.back() = true;
                }
                open = false;
                break;
            }
        }
    }
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

void Rasterizer::cell(std::int32_t ex, std::int32_t ey, std::int32_t cover, std::int32_t area) {
    if (ey < 0 || ey >= height_) return;
    if (ex < 0) ex = 0;
    if (ex > width_) ex = width_;
    std::size_t i = static_cast<std::size_t>(ey) * static_cast<std::size_t>(width_ + 1) + static_cast<std::size_t>(ex);
    cover_[i] += cover;
    area_[i] += area;
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
        std::int32_t y = y_at(static_cast<std::int64_t>(ex1) * one);
        cell(ex1, ey, y - fy1, fx1 * (y - fy1));
        for (std::int32_t ex = ex1 - 1; ex > ex2; --ex) {
            std::int32_t yn = y_at(static_cast<std::int64_t>(ex) * one);
            cell(ex, ey, yn - y, one * (yn - y));
            y = yn;
        }
        cell(ex2, ey, fy2 - y, (fx2 + one) * (fy2 - y));
    }
}

void Rasterizer::line(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    if (y1 == y2) return;
    std::int32_t top = 0, bottom = height_ * one;
    // Clip to the rows, keeping the orientation.
    std::int32_t lo = std::min(y1, y2), hi = std::max(y1, y2);
    if (hi <= top || lo >= bottom) return;
    {
        // Both ends from the original edge, so clipping does not drift.
        const std::int64_t ox = x1, oy = y1;
        const std::int64_t odx = static_cast<std::int64_t>(x2) - x1, ody = static_cast<std::int64_t>(y2) - y1;
        auto x_at = [&](std::int64_t y) { return static_cast<std::int32_t>(ox + floor_div((y - oy) * odx, ody)); };
        std::int32_t nx1 = x1, ny1 = y1, nx2 = x2, ny2 = y2;
        if (y1 < top) { nx1 = x_at(top); ny1 = top; }
        else if (y1 > bottom) { nx1 = x_at(bottom); ny1 = bottom; }
        if (y2 < top) { nx2 = x_at(top); ny2 = top; }
        else if (y2 > bottom) { nx2 = x_at(bottom); ny2 = bottom; }
        x1 = nx1; y1 = ny1; x2 = nx2; y2 = ny2;
    }
    std::int64_t dx = static_cast<std::int64_t>(x2) - x1, dy = static_cast<std::int64_t>(y2) - y1;
    if (dy == 0) return;

    std::int32_t ey1 = y1 >> 8, ey2 = y2 >> 8;
    std::int32_t fy1 = y1 & (one - 1), fy2 = y2 & (one - 1);
    if (ey1 == ey2) {
        scanline(ey1, x1, fy1, x2, fy2);
        return;
    }
    auto x_on = [&](std::int64_t y) { return static_cast<std::int32_t>(x1 + floor_div((y - y1) * dx, dy)); };
    if (dy > 0) {
        std::int32_t xb = x_on(static_cast<std::int64_t>(ey1 + 1) * one);
        scanline(ey1, x1, fy1, xb, one);
        for (std::int32_t ey = ey1 + 1; ey < ey2; ++ey) {
            std::int32_t xn = x_on(static_cast<std::int64_t>(ey + 1) * one);
            scanline(ey, xb, 0, xn, one);
            xb = xn;
        }
        scanline(ey2, xb, 0, x2, fy2);
    } else {
        std::int32_t xb = x_on(static_cast<std::int64_t>(ey1) * one);
        scanline(ey1, x1, fy1, xb, 0);
        for (std::int32_t ey = ey1 - 1; ey > ey2; --ey) {
            std::int32_t xn = x_on(static_cast<std::int64_t>(ey) * one);
            scanline(ey, xb, one, xn, 0);
            xb = xn;
        }
        scanline(ey2, xb, one, x2, fy2);
    }
}

void Rasterizer::rasterize(const Polygons& polygons, FillRule rule, PixelRect clip, Coverage& out) {
    out.values.clear();
    out.bounds = {};
    if (polygons.points.empty() || clip.empty()) return;
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
    if (x1 <= x0 || y1 <= y0) return;
    min_x_ = x0;
    min_y_ = y0;
    width_ = x1 - x0;
    height_ = y1 - y0;
    std::size_t cells = static_cast<std::size_t>(width_ + 1) * static_cast<std::size_t>(height_);
    cover_.assign(cells, 0);
    area_.assign(cells, 0);

    // Edges left of the clip keep their winding in the first column; edges
    // right of it fall in the extra column and change no visible pixel.
    std::int32_t max_sub = width_ * one;
    auto sx = [&](float v) { return std::clamp(to_sub(v) - min_x_ * one, 0, max_sub); };
    auto sy = [&](float v) { return to_sub(v) - min_y_ * one; };
    for (std::size_t pg = 0; pg < polygons.count(); ++pg) {
        auto poly = polygons.polygon(pg);
        if (poly.size() < 2) continue;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Point& a = poly[i];
            const Point& c = poly[(i + 1) % poly.size()];
            line(sx(a.x), sy(a.y), sx(c.x), sy(c.y));
        }
    }

    out.bounds = {x0, y0, width_, height_};
    out.values.resize(static_cast<std::size_t>(width_) * height_);
    for (std::int32_t y = 0; y < height_; ++y) {
        const std::int32_t* cv = cover_.data() + static_cast<std::size_t>(y) * (width_ + 1);
        const std::int32_t* ar = area_.data() + static_cast<std::size_t>(y) * (width_ + 1);
        std::uint8_t* dst = out.values.data() + static_cast<std::size_t>(y) * width_;
        std::int64_t acc = 0;
        for (std::int32_t x = 0; x < width_; ++x) {
            acc += cv[x];
            std::int64_t v = (acc << 9) - ar[x];
            if (v < 0) v = -v;
            std::int64_t c = v >> 9;
            if (rule == FillRule::even_odd) {
                c &= 511;
                if (c > 256) c = 512 - c;
            }
            dst[x] = static_cast<std::uint8_t>(c > 255 ? 255 : c);
        }
    }
}

void fill_coverage(PathData path, const Affine& m, FillRule rule, PixelRect clip, Coverage& out) {
    Polygons polys;
    flatten(path, m, polys);
    Rasterizer r;
    r.rasterize(polys, rule, clip, out);
}

}  // namespace omui::raster
