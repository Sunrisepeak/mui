// Effects and cut-ins drawn from a look's pictures: sprites that grow, turn
// and fade; brush strokes laid along a curve and drawn out; droplets thrown
// and falling; a slanted band of art sliding across the screen. Each is a
// pure function of its age, its numbers from a seed (the same everywhere).
module gongfu.present2d;

import std;
import openmui.math;
import gongfu;
import hanim;
import openmui.geom;
import openmui.render.vector;

namespace gongfu::present2d {

namespace {

namespace dm = hanim::dm;
namespace render = omui::render;
using omui::geom::Rect;

// A number in [0, 1) from a seed and an index (splitmix64).
double unit(std::uint64_t seed, std::uint64_t i) {
    return omui::math::unit24(omui::math::mix64(seed * omui::math::golden + i));
}

double mix(std::array<float, 2> r, double u) { return r[0] + (r[1] - r[0]) * u; }

// A point on a Catmull-Rom curve through `p` (its ends repeated), segment `i`, at `u`.
Point catmull(std::span<const Point> p, std::size_t i, double u) {
    const auto at = [&](std::ptrdiff_t k) { return p[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(k, 0, static_cast<std::ptrdiff_t>(p.size()) - 1))]; };
    const auto k = static_cast<std::ptrdiff_t>(i);
    const Point a = at(k - 1), b = at(k), c = at(k + 1), d = at(k + 2);
    const double u2 = u * u, u3 = u2 * u;
    auto one = [&](double x0, double x1, double x2, double x3) {
        return 0.5 * (2 * x1 + (-x0 + x2) * u + (2 * x0 - 5 * x1 + 4 * x2 - x3) * u2 + (-x0 + 3 * x1 - 3 * x2 + x3) * u3);
    };
    return {static_cast<float>(one(a.x, b.x, c.x, d.x)), static_cast<float>(one(a.y, b.y, c.y, d.y))};
}

}  // namespace

std::int32_t Fx::length() const noexcept {
    std::int32_t n = 0;
    for (const FxSprite& s : sprites) n = std::max(n, s.from + s.frames);
    for (const FxStroke& s : strokes) n = std::max(n, s.from + s.draw + s.hold + s.fade);
    for (const FxSpray& s : sprays) n = std::max(n, s.from + s.frames);
    return n;
}

void Painter::fx(const Look& look, const Fx& fx, double age, Point origin, double facing, float scale, const Scene& scene, const Camera& camera,
                 render::DisplayList& out, float alpha, std::optional<Color> paint) {
    auto picture = [&](std::int32_t p) -> const Picture* {
        if (p < 0 || static_cast<std::size_t>(p) >= look.pictures.size()) return nullptr;
        const Picture& pic = look.pictures[static_cast<std::size_t>(p)];
        return pic.ref.id == 0 ? nullptr : &pic;
    };
    // A place of the effect (forward, up; stage units) on the picture.
    auto at = [&](double x, double y) { return on_picture(scene, camera, origin.x + facing * x * scale, origin.y + y * scale); };
    const float zoom = static_cast<float>(camera.zoom * scale);
    // The mesh drawn: in the picture's own colours, or as its shape in one (the effect's `paint`, else the piece's own).
    auto draw = [&](const Picture& pic, const std::optional<Color>& own, float opacity) {
        if (const std::optional<Color> c = paint ? paint : own) {
            out.draw_mesh_mask(pic.ref, mesh_at_, mesh_uv_, mesh_index_, Color{c->r, c->g, c->b, c->a * opacity}, render::Sampling::linear);
        } else {
            out.draw_mesh(pic.ref, mesh_at_, mesh_uv_, mesh_index_, render::Sampling::linear, opacity);
        }
    };
    // A region drawn as a quad about `centre` (picture pixels), `h` high, turned, mirrored with the facing.
    auto quad = [&](const Picture& pic, const Rect& src, Point centre, double h, double turn, float opacity, const std::optional<Color>& own) {
        if (opacity <= 0 || src.height <= 0 || h <= 0) return;
        const double k = h / src.height, hw = src.width * k / 2, hh = h / 2;
        const double a = -turn * dm::pi / 180 * facing, ca = dm::cos(a), sa = dm::sin(a);
        auto corner = [&](double x, double y) {
            x *= facing;
            return Point{static_cast<float>(centre.x + x * ca - y * sa), static_cast<float>(centre.y + x * sa + y * ca)};
        };
        mesh_at_.assign({corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh)});
        mesh_uv_.assign({{src.x, src.y}, {src.x + src.width, src.y}, {src.x + src.width, src.y + src.height}, {src.x, src.y + src.height}});
        mesh_index_.assign({0, 1, 2, 0, 2, 3});
        draw(pic, own, opacity);
    };

    for (const FxStroke& s : fx.strokes) {
        const Picture* pic = picture(s.picture);
        const double t = age - s.from;
        if (!pic || t < 0 || t >= s.draw + s.hold + s.fade || s.points.size() < 2) continue;
        const double drawn = s.draw > 0 ? std::min(1.0, (t + 1) / s.draw) : 1.0;
        const double fading = t < s.draw + s.hold ? 1.0 : 1 - (t - s.draw - s.hold) / std::max(1, s.fade);
        // The curve, sampled evenly by segment, and its length so far at each sample.
        constexpr int per = 8;
        std::array<Point, 8 * 16 + 1> pts{};
        std::array<double, 8 * 16 + 1> len{};
        const std::size_t segs = std::min<std::size_t>(s.points.size() - 1, 16);
        std::size_t n = 0;
        for (std::size_t i = 0; i < segs; ++i)
            for (int j = 0; j < per; ++j) pts[n++] = catmull(s.points, i, static_cast<double>(j) / per);
        pts[n++] = s.points[segs];
        for (std::size_t i = 1; i < n; ++i) len[i] = len[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        const double total = len[n - 1];
        if (total <= 0) continue;
        const double shown = total * drawn;
        mesh_at_.clear();
        mesh_uv_.clear();
        mesh_index_.clear();
        for (std::size_t i = 0; i < n && len[i] <= shown + 1e-9; ++i) {
            const Point a = pts[i == 0 ? 0 : i - 1], b = pts[std::min(i + 1, n - 1)];
            double dx = b.x - a.x, dy = b.y - a.y;
            const double d = std::hypot(dx, dy);
            if (d > 0) dx /= d, dy /= d;
            const double half = s.width / 2;
            // Across the stroke (y up): its picture's top on the left of the way it goes.
            const Point l = at(pts[i].x - dy * half, pts[i].y + dx * half);
            const Point r = at(pts[i].x + dy * half, pts[i].y - dx * half);
            const float u = s.source.x + s.source.width * static_cast<float>(len[i] / total);
            mesh_at_.push_back(l);
            mesh_at_.push_back(r);
            mesh_uv_.push_back({u, s.source.y});
            mesh_uv_.push_back({u, s.source.y + s.source.height});
        }
        const std::size_t rows = mesh_at_.size() / 2;
        for (std::size_t i = 0; i + 1 < rows; ++i) {
            const auto k = static_cast<std::uint16_t>(2 * i);
            for (const std::uint16_t v : {k, static_cast<std::uint16_t>(k + 1), static_cast<std::uint16_t>(k + 3), k, static_cast<std::uint16_t>(k + 3),
                                          static_cast<std::uint16_t>(k + 2)})
                mesh_index_.push_back(v);
        }
        if (!mesh_index_.empty()) draw(*pic, s.color, static_cast<float>(fading * alpha));
    }

    for (const FxSprite& s : fx.sprites) {
        const Picture* pic = picture(s.picture);
        const double t = age - s.from;
        if (!pic || t < 0 || t >= s.frames) continue;
        const double u = s.ease(s.frames > 1 ? t / (s.frames - 1) : 1.0);
        // Where it is: at its place, or along the arc to its destination by the same eased time.
        Point p = s.at;
        if (s.to) {
            const double k = u, arc = 4 * s.lift * k * (1 - k);
            p = {static_cast<float>(s.at.x + (s.to->x - s.at.x) * k), static_cast<float>(s.at.y + (s.to->y - s.at.y) * k + arc)};
        }
        quad(*pic, s.source, at(p.x, p.y), mix(s.size, u) * zoom, mix(s.turn, u), static_cast<float>(mix(s.alpha, u) * alpha), s.color);
    }

    for (const FxSpray& s : fx.sprays) {
        const Picture* pic = picture(s.picture);
        const double t = age - s.from;
        if (!pic || s.sources.empty() || t < 0 || t >= s.frames) continue;
        const double life = t / s.frames;
        const float opacity = static_cast<float>((life < 0.6 ? 1.0 : 1 - (life - 0.6) / 0.4) * alpha);
        for (std::int32_t i = 0; i < s.count; ++i) {
            const auto k = static_cast<std::uint64_t>(i) * 8;
            const double speed = mix(s.speed, unit(s.seed, k)), angle = mix(s.angle, unit(s.seed, k + 1)) * dm::pi / 180;
            const double size = mix(s.size, unit(s.seed, k + 2)) * zoom;
            const Rect& src = s.sources[static_cast<std::size_t>(unit(s.seed, k + 3) * static_cast<double>(s.sources.size())) % s.sources.size()];
            const double x = s.at.x + speed * dm::cos(angle) * t, y = s.at.y + speed * dm::sin(angle) * t - 0.5 * s.gravity * t * t;
            quad(*pic, src, at(x, y), size, unit(s.seed, k + 4) * 360 + t * (unit(s.seed, k + 5) - 0.5) * 20, opacity, s.color);
        }
    }
}

void Painter::panel(const Look& look, const CinematicLook::Panel& p, std::int32_t t, double facing, const Scene& scene, render::DisplayList& out) {
    if (t < p.from || t > p.to || p.picture < 0 || static_cast<std::size_t>(p.picture) >= look.pictures.size()) return;
    const Picture& pic = look.pictures[static_cast<std::size_t>(p.picture)];
    if (pic.ref.id == 0 || p.source.width <= 0 || p.source.height <= 0) return;
    // In: the band slides across from behind the attacker; out: it closes to a line.
    const double in = p.in > 0 ? hanim::ease::out_cubic(std::min(1.0, static_cast<double>(t - p.from + 1) / p.in)) : 1.0;
    const double close = p.out > 0 ? hanim::ease::in_cubic(std::clamp(static_cast<double>(t - (p.to - p.out)) / p.out, 0.0, 1.0)) : 0.0;
    const float w = scene.width, h = scene.height * p.height * static_cast<float>(1 - close), cy = scene.height * p.y;
    if (h < 0.5f) return;
    const float lean = scene.width * p.slant, shift = static_cast<float>((1 - in) * (w + lean) * -facing);
    path_.clear();
    path_.move_to({shift - lean, cy + h / 2})
        .line_to({shift + lean, cy - h / 2})
        .line_to({shift + w + lean * 2, cy - h / 2})
        .line_to({shift + w, cy + h / 2})
        .close();
    // A dark edge band a little taller than the art: the cut-in's frame.
    const float edge = std::max(2.0f, h * 0.035f);
    render::Path band;
    band.move_to({shift - lean, cy + h / 2 + edge}).line_to({shift + lean, cy - h / 2 - edge}).line_to({shift + w + lean * 2, cy - h / 2 - edge}).line_to({shift + w, cy + h / 2 + edge}).close();
    out.fill_path(band, render::FillRule::nonzero, render::Paint::solid(p.band));
    out.push_clip(path_);
    // The art covering the band, drifting by `pan`.
    const double k = std::max(static_cast<double>(w + lean * 2) / p.source.width, static_cast<double>(scene.height * p.height) / p.source.height);
    const double age = t - p.from;
    const float sx = p.source.x + static_cast<float>(p.pan.x * age), sy = p.source.y + static_cast<float>(p.pan.y * age);
    const float dw = static_cast<float>(p.source.width * k), dh = static_cast<float>(p.source.height * k);
    out.draw_image(pic.ref, {sx, sy, p.source.width, p.source.height}, {shift + (w + lean) / 2 - dw / 2, cy - dh / 2, dw, dh}, render::Sampling::linear);
    out.pop_clip();
}

}  // namespace gongfu::present2d
