// openmui.geom — the geometry the independent modules share.
//
// Points, sizes, rectangles, insets, corner radii, colours and affine
// transforms in logical pixels, and the rounding to physical pixels. Standard
// library only, so layout, raster, render, text and their users agree on one
// set of types without depending on openmui. The specification keeps its own
// wire types (openmui.spec); the runtime converts at that boundary, and the
// two have the same fields and the same rounding rule.
export module openmui.geom;

import std;

export namespace omui::geom {

struct Point {
    float x = 0, y = 0;
    friend constexpr bool operator==(const Point& a, const Point& b) noexcept { return a.x == b.x && a.y == b.y; }
    friend constexpr Point operator+(Point a, Point b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Point operator-(Point a, Point b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Point operator*(Point a, float s) noexcept { return {a.x * s, a.y * s}; }
};

struct Size {
    float width = 0, height = 0;
    [[nodiscard]] constexpr bool empty() const noexcept { return !(width > 0 && height > 0); }
    friend constexpr bool operator==(const Size& a, const Size& b) noexcept {
        return a.width == b.width && a.height == b.height;
    }
};

struct Insets {
    float top = 0, right = 0, bottom = 0, left = 0;

    [[nodiscard]] static constexpr Insets all(float v) noexcept { return {v, v, v, v}; }
    [[nodiscard]] static constexpr Insets symmetric(float vertical, float horizontal) noexcept {
        return {vertical, horizontal, vertical, horizontal};
    }
    [[nodiscard]] constexpr float horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr float vertical() const noexcept { return top + bottom; }
    friend constexpr bool operator==(const Insets& a, const Insets& b) noexcept {
        return a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.left == b.left;
    }
};

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;

    [[nodiscard]] static constexpr Rect from(Point origin, Size size) noexcept {
        return {origin.x, origin.y, size.width, size.height};
    }
    [[nodiscard]] static constexpr Rect ltrb(float l, float t, float r, float b) noexcept { return {l, t, r - l, b - t}; }

    [[nodiscard]] constexpr float left() const noexcept { return x; }
    [[nodiscard]] constexpr float top() const noexcept { return y; }
    [[nodiscard]] constexpr float right() const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr Point origin() const noexcept { return {x, y}; }
    [[nodiscard]] constexpr Size size() const noexcept { return {width, height}; }
    [[nodiscard]] constexpr Point center() const noexcept { return {x + width / 2, y + height / 2}; }
    [[nodiscard]] constexpr bool empty() const noexcept { return !(width > 0 && height > 0); }

    // Half-open: the right and bottom edges are outside.
    [[nodiscard]] constexpr bool contains(Point p) const noexcept {
        return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
    }
    [[nodiscard]] constexpr bool contains(const Rect& r) const noexcept {
        return r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
    }
    [[nodiscard]] constexpr bool intersects(const Rect& o) const noexcept {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
    [[nodiscard]] constexpr Rect intersection(const Rect& o) const noexcept {
        float l = std::max(x, o.x), t = std::max(y, o.y);
        float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return r > l && b > t ? ltrb(l, t, r, b) : Rect{l, t, 0, 0};
    }
    // The smallest rectangle covering both; an empty one contributes nothing.
    [[nodiscard]] constexpr Rect united(const Rect& o) const noexcept {
        if (empty()) return o;
        if (o.empty()) return *this;
        return ltrb(std::min(x, o.x), std::min(y, o.y), std::max(right(), o.right()), std::max(bottom(), o.bottom()));
    }
    [[nodiscard]] constexpr Rect inset(const Insets& i) const noexcept {
        return {x + i.left, y + i.top, std::max(0.0f, width - i.horizontal()), std::max(0.0f, height - i.vertical())};
    }
    [[nodiscard]] constexpr Rect outset(const Insets& i) const noexcept {
        return {x - i.left, y - i.top, width + i.horizontal(), height + i.vertical()};
    }
    [[nodiscard]] constexpr Rect offset(Point d) const noexcept { return {x + d.x, y + d.y, width, height}; }

    friend constexpr bool operator==(const Rect& a, const Rect& b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
};

// A rectangle of physical pixels.
struct PixelRect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    friend constexpr bool operator==(const PixelRect& a, const PixelRect& b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
};

// A logical coordinate v at scale s is the physical floor(v·s + 0.5), in
// double precision, the same for negative values. Rectangles snap their
// edges, never their sizes: rectangles that share an edge in logical pixels
// share it in physical pixels at every scale (the spec's rule, V-2).
[[nodiscard]] constexpr std::int32_t snap(float v, float scale) noexcept {
    double p = static_cast<double>(v) * static_cast<double>(scale) + 0.5;
    double f = static_cast<double>(static_cast<std::int64_t>(p));
    if (f > p) f -= 1.0;
    return static_cast<std::int32_t>(f);
}

[[nodiscard]] constexpr PixelRect snap(const Rect& r, float scale) noexcept {
    std::int32_t l = snap(r.x, scale), t = snap(r.y, scale);
    std::int32_t rr = snap(r.right(), scale), b = snap(r.bottom(), scale);
    return {l, t, std::max(0, rr - l), std::max(0, b - t)};
}

struct CornerRadii {
    float top_left = 0, top_right = 0, bottom_right = 0, bottom_left = 0;

    [[nodiscard]] static constexpr CornerRadii all(float r) noexcept { return {r, r, r, r}; }
    [[nodiscard]] constexpr bool zero() const noexcept {
        return top_left == 0 && top_right == 0 && bottom_right == 0 && bottom_left == 0;
    }
    [[nodiscard]] constexpr bool uniform() const noexcept {
        return top_left == top_right && top_right == bottom_right && bottom_right == bottom_left;
    }
    // Radii larger than the sides scaled down together (CSS backgrounds §5.5).
    [[nodiscard]] constexpr CornerRadii fitted(Size s) const noexcept {
        float f = 1.0f;
        auto fit = [&](float side, float a, float b) {
            if (a + b > side && a + b > 0) f = std::min(f, side / (a + b));
        };
        fit(s.width, top_left, top_right);
        fit(s.width, bottom_left, bottom_right);
        fit(s.height, top_left, bottom_left);
        fit(s.height, top_right, bottom_right);
        return {top_left * f, top_right * f, bottom_right * f, bottom_left * f};
    }
    friend constexpr bool operator==(const CornerRadii& a, const CornerRadii& b) noexcept {
        return a.top_left == b.top_left && a.top_right == b.top_right && a.bottom_right == b.bottom_right &&
               a.bottom_left == b.bottom_left;
    }
};

// An sRGB colour with straight alpha, channels in [0, 1].
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    [[nodiscard]] static constexpr Color rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
    [[nodiscard]] static constexpr Color hex(std::uint32_t rgb, float alpha = 1.0f) noexcept {
        return {((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, alpha};
    }
    [[nodiscard]] static constexpr Color transparent() noexcept { return {0, 0, 0, 0}; }
    [[nodiscard]] static constexpr Color white() noexcept { return {1, 1, 1, 1}; }
    [[nodiscard]] static constexpr Color black() noexcept { return {0, 0, 0, 1}; }

    // #rgb, #rgba, #rrggbb, #rrggbbaa
    [[nodiscard]] static constexpr std::optional<Color> parse(std::string_view text) noexcept {
        if (!text.empty() && text.front() == '#') text.remove_prefix(1);
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::array<int, 4> v{0, 0, 0, 255};
        if (text.size() == 3 || text.size() == 4) {
            for (std::size_t i = 0; i < text.size(); ++i) {
                int d = digit(text[i]);
                if (d < 0) return std::nullopt;
                v[i] = d * 17;
            }
        } else if (text.size() == 6 || text.size() == 8) {
            for (std::size_t i = 0; i < text.size() / 2; ++i) {
                int hi = digit(text[2 * i]), lo = digit(text[2 * i + 1]);
                if (hi < 0 || lo < 0) return std::nullopt;
                v[i] = hi * 16 + lo;
            }
        } else {
            return std::nullopt;
        }
        return rgba8(static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2]),
                     static_cast<std::uint8_t>(v[3]));
    }

    [[nodiscard]] constexpr Color with_alpha(float alpha) const noexcept { return {r, g, b, alpha}; }

    // 8-bit premultiplied (R, G, B, A), rounded half up.
    [[nodiscard]] constexpr std::array<std::uint8_t, 4> premultiplied8() const noexcept {
        auto q = [](float v) {
            float c = v < 0 ? 0 : (v > 1 ? 1 : v);
            return static_cast<std::uint8_t>(static_cast<int>(c * 255.0f + 0.5f));
        };
        float alpha = a < 0 ? 0 : (a > 1 ? 1 : a);
        return {q(r * alpha), q(g * alpha), q(b * alpha), q(alpha)};
    }

    friend constexpr bool operator==(const Color& x, const Color& y) noexcept {
        return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
    }
};

// x' = a·x + c·y + e, y' = b·x + d·y + f
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    [[nodiscard]] static constexpr Affine translate(float x, float y) noexcept { return {1, 0, 0, 1, x, y}; }
    [[nodiscard]] static constexpr Affine scale(float sx, float sy) noexcept { return {sx, 0, 0, sy, 0, 0}; }
    // From a rotation's cosine and sine (callers that need identical results
    // everywhere compute them with deterministic functions).
    [[nodiscard]] static constexpr Affine rotation(float cos_a, float sin_a) noexcept { return {cos_a, sin_a, -sin_a, cos_a, 0, 0}; }

    // This transform after `o`: (this ∘ o)(p) = this(o(p)).
    [[nodiscard]] constexpr Affine operator*(const Affine& o) const noexcept {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e,
                b * o.e + d * o.f + f};
    }
    [[nodiscard]] constexpr Point apply(Point p) const noexcept { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    [[nodiscard]] constexpr std::optional<Affine> inverse() const noexcept {
        float det = a * d - b * c;
        if (det == 0 || det != det) return std::nullopt;
        float id = 1.0f / det;
        return Affine{d * id, -b * id, -c * id, a * id, (c * f - d * e) * id, (b * e - a * f) * id};
    }
    [[nodiscard]] constexpr bool identity() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
    [[nodiscard]] constexpr bool translation_only() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1; }
    // The scale factor along the larger axis (for stroke widths, tolerances).
    [[nodiscard]] constexpr float max_scale() const noexcept {
        float sx = a * a + b * b, sy = c * c + d * d;
        float m = sx > sy ? sx : sy;
        // √m by Newton from a positive start: constexpr and exact enough for tolerances.
        float x = m > 1 ? m : 1;
        for (int i = 0; i < 20; ++i) x = 0.5f * (x + m / x);
        return x;
    }
    // The bounding box of a transformed rectangle.
    [[nodiscard]] constexpr Rect map(const Rect& r) const noexcept {
        Point p[4] = {apply({r.x, r.y}), apply({r.right(), r.y}), apply({r.x, r.bottom()}), apply({r.right(), r.bottom()})};
        float l = p[0].x, t = p[0].y, rr = p[0].x, bb = p[0].y;
        for (const Point& q : p) {
            l = std::min(l, q.x);
            t = std::min(t, q.y);
            rr = std::max(rr, q.x);
            bb = std::max(bb, q.y);
        }
        return Rect::ltrb(l, t, rr, bb);
    }

    friend constexpr bool operator==(const Affine& x, const Affine& y) noexcept {
        return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.f == y.f;
    }
};

}  // namespace omui::geom
