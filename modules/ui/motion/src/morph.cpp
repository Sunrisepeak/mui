// A morph: two drawings matched, and drawn between them.
module openmui.motion;

import std;
import hanim;
import openmui.math;
import openmui.geom;
import openmui.render.vector;

namespace omui::motion {

namespace {

using namespace render;
namespace det = ::omui::math::det;

double clamp01(double u) noexcept { return u < 0 ? 0 : (u > 1 ? 1 : u); }
float mixf(float a, float b, double u) noexcept { return static_cast<float>(a + (b - a) * u); }
Point mixp(Point a, Point b, double u) noexcept { return {mixf(a.x, b.x, u), mixf(a.y, b.y, u)}; }
Rect mixr(const Rect& a, const Rect& b, double u) noexcept {
    return {mixf(a.x, b.x, u), mixf(a.y, b.y, u), std::max(0.0f, mixf(a.width, b.width, u)), std::max(0.0f, mixf(a.height, b.height, u))};
}
CornerRadii mixc(const CornerRadii& a, const CornerRadii& b, double u) noexcept {
    return {std::max(0.0f, mixf(a.top_left, b.top_left, u)), std::max(0.0f, mixf(a.top_right, b.top_right, u)),
            std::max(0.0f, mixf(a.bottom_right, b.bottom_right, u)), std::max(0.0f, mixf(a.bottom_left, b.bottom_left, u))};
}
// Colours travel through OKLab (premultiplied by alpha), as every transition of the runtime does (D-60).
Color mix_color(const Color& a, const Color& b, double u) noexcept {
    u = clamp01(u);
    if (u <= 0) return a;
    if (u >= 1) return b;
    const auto la = hanim::lanes<hanim::Rgba>::to(hanim::Rgba{a.r, a.g, a.b, a.a});
    const auto lb = hanim::lanes<hanim::Rgba>::to(hanim::Rgba{b.r, b.g, b.b, b.a});
    std::remove_cvref_t<decltype(la)> l{};
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = la[i] + (lb[i] - la[i]) * u;
    const hanim::Rgba c = hanim::lanes<hanim::Rgba>::from(l);
    return {c.r, c.g, c.b, c.a};
}

// A paint faded by `o`, when fading it is a matter of its colour's alpha.
std::optional<Paint> faded(Paint p, float o) noexcept {
    if (o >= 1) return p;
    if (p.kind == Paint::Kind::gradient) return std::nullopt;
    p.color.a *= std::max(0.0f, o);
    return p;
}
// Two paints between: solid colours mixed; others the second's once it is half way (and both faded by the caller otherwise).
std::optional<Paint> mixed(const Paint& a, const Paint& b, double u) noexcept {
    if (a.kind == Paint::Kind::solid && b.kind == Paint::Kind::solid) return Paint::solid(mix_color(a.color, b.color, u));
    if (a.kind == b.kind && (a.kind == Paint::Kind::image || a.kind == Paint::Kind::image_mask) && a.image == b.image) {
        Paint p = b;
        p.color = mix_color(a.color, b.color, u);
        return p;
    }
    return std::nullopt;
}

Path path_of(const PathView& v) {
    Path p;
    std::size_t k = 0;
    for (const PathVerb verb : v.verbs) {
        switch (verb) {
            case PathVerb::move: p.move_to(v.points[k]), k += 1; break;
            case PathVerb::line: p.line_to(v.points[k]), k += 1; break;
            case PathVerb::quad: p.quad_to(v.points[k], v.points[k + 1]), k += 2; break;
            case PathVerb::cubic: p.cubic_to(v.points[k], v.points[k + 1], v.points[k + 2]), k += 3; break;
            case PathVerb::close: p.close(); break;
        }
    }
    return p;
}
std::optional<Path> mix_paths(const PathView& a, const PathView& b, double u) {
    if (a.verbs.size() != b.verbs.size() || a.points.size() != b.points.size() || !std::ranges::equal(a.verbs, b.verbs)) return std::nullopt;
    Path p;
    std::size_t k = 0;
    auto at = [&](std::size_t i) { return mixp(a.points[i], b.points[i], u); };
    for (const PathVerb verb : b.verbs) {
        switch (verb) {
            case PathVerb::move: p.move_to(at(k)), k += 1; break;
            case PathVerb::line: p.line_to(at(k)), k += 1; break;
            case PathVerb::quad: p.quad_to(at(k), at(k + 1)), k += 2; break;
            case PathVerb::cubic: p.cubic_to(at(k), at(k + 1), at(k + 2)), k += 3; break;
            case PathVerb::close: p.close(); break;
        }
    }
    return p;
}

// What is pushed around a piece, popped in the reverse order.
struct Pushes {
    std::array<std::uint8_t, 34> kinds{};  // 0 transform, 1 clip
    std::size_t                  n = 0;
    bool                         ok = true;
};

struct Emitter {
    DisplayList&      out;
    Affine            base;  // from the range's space to where the morph draws (inside its wrapper)
    const NestedDraw* nested = nullptr;

    // The piece's clips (as `m` holds them) and then its transform.
    Pushes enter(const Matter& m, int clip, const Affine& t) {
        Pushes p;
        std::array<int, 16> chain{};
        std::size_t         n = 0;
        for (int k = clip; k >= 0 && n < chain.size(); k = m.clips()[static_cast<std::size_t>(k)].parent) chain[n++] = k;
        Affine cur{};
        for (std::size_t i = n; i-- > 0;) {
            const ClipEntry& ce = m.clips()[static_cast<std::size_t>(chain[i])];
            const Affine x = base * ce.transform;
            const auto inv = cur.inverse();
            if (!inv) {
                p.ok = false;
                break;
            }
            out.push_transform(*inv * x);
            p.kinds[p.n++] = 0;
            out.append_command(m.list().commands()[ce.command]);
            p.kinds[p.n++] = 1;
            cur = x;
        }
        if (p.ok) {
            if (const auto inv = cur.inverse()) {
                out.push_transform(*inv * (base * t));
                p.kinds[p.n++] = 0;
            } else {
                p.ok = false;
            }
        }
        return p;
    }
    void leave(const Pushes& p) {
        for (std::size_t i = p.n; i-- > 0;) {
            if (p.kinds[i] == 0) out.pop_transform();
            else out.pop_clip();
        }
    }

    // One drawable command at `opacity` (its paint's alpha where it can, else a layer).
    void command(const Command& c, float opacity) {
        if (opacity <= 0) return;
        std::visit(
            [&](const auto& cmd) {
                using T = std::decay_t<decltype(cmd)>;
                auto layered = [&](auto&& draw) {
                    out.push_layer(opacity);
                    draw();
                    out.pop_layer();
                };
                if constexpr (std::same_as<T, FillRect>) {
                    if (auto p = faded(cmd.paint, opacity)) out.fill_rect(cmd.rect, *p);
                    else layered([&] { out.append_command(c); });
                } else if constexpr (std::same_as<T, FillRoundRect>) {
                    if (auto p = faded(cmd.paint, opacity)) out.fill_round_rect(cmd.rect, cmd.radii, *p);
                    else layered([&] { out.append_command(c); });
                } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                    if (auto p = faded(cmd.paint, opacity)) out.stroke_round_rect(cmd.rect, cmd.radii, cmd.width, *p);
                    else layered([&] { out.append_command(c); });
                } else if constexpr (std::same_as<T, BoxShadow>) {
                    out.box_shadow(cmd.rect, cmd.radii, cmd.offset, cmd.blur, cmd.spread, cmd.color.with_alpha(cmd.color.a * opacity), cmd.inset);
                } else if constexpr (std::same_as<T, DrawImage>) {
                    out.draw_image(cmd.image, cmd.source, cmd.dest, cmd.sampling, cmd.opacity * opacity);
                } else if constexpr (std::same_as<T, ExternalTexture>) {
                    out.external_texture(cmd.texture, cmd.dest, cmd.opacity * opacity);
                } else if constexpr (std::same_as<T, DrawGlyphs>) {
                    if (auto p = faded(cmd.paint, opacity)) out.draw_glyphs(cmd.font, cmd.size, cmd.origin, cmd.glyphs, *p, cmd.styles);
                    else layered([&] { out.append_command(c); });
                } else if constexpr (std::same_as<T, FillPath> || std::same_as<T, StrokePath>) {
                    if (opacity >= 1) out.append_command(c);
                    else if (auto p = faded(cmd.paint, opacity)) {
                        if constexpr (std::same_as<T, FillPath>) out.fill_path(path_of(cmd.path), cmd.rule, *p);
                        else out.stroke_path(path_of(cmd.path), cmd.style, *p);
                    } else layered([&] { out.append_command(c); });
                } else {
                    out.append_command(c);
                }
            },
            c);
    }

    // Each piece inside its node's marker (0: none).
    std::uint32_t marker = 0;
    void begin(std::uint32_t m) {
        marker = m;
        if (m) out.begin_node(m);
    }
    void end() {
        if (marker) out.end_node();
        marker = 0;
    }

    // A piece as it is in `m`, under `t`, at `opacity`.
    void piece(const Matter& m, const Piece& pc, const Affine& t, float opacity, bool second) {
        if (opacity <= 0) return;
        const Pushes p = enter(m, pc.clip, t);
        if (p.ok) {
            if (pc.cls == PieceClass::group) {
                const bool layer = opacity < 1;
                if (layer) out.push_layer(opacity);
                if (pc.nested && second && nested && *nested) (*nested)(pc.nested, out);
                else
                    for (std::size_t i = pc.command; i < pc.end; ++i) out.append_command(m.list().commands()[i]);
                if (layer) out.pop_layer();
            } else {
                command(m.list().commands()[pc.command], opacity);
            }
        }
        leave(p);
    }
};

// The longest common subsequence of two glyph runs' glyph ids: for each of
// b, the a it keeps (-1: none). Long runs are matched in order, greedily.
std::vector<int> glyph_lcs(std::span<const Glyph> a, std::span<const Glyph> b) {
    std::vector<int> from(b.size(), -1);
    const std::size_t n = a.size(), m = b.size();
    if (n == 0 || m == 0) return from;
    if (n * m > 160000) {
        std::size_t i = 0;
        for (std::size_t j = 0; j < m && i < n; ++j) {
            std::size_t k = i;
            while (k < n && a[k].id != b[j].id) ++k;
            if (k < n) from[j] = static_cast<int>(k), i = k + 1;
        }
        return from;
    }
    std::vector<std::uint16_t> len((n + 1) * (m + 1), 0);
    auto L = [&](std::size_t i, std::size_t j) -> std::uint16_t& { return len[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;)
            L(i, j) = a[i].id == b[j].id ? static_cast<std::uint16_t>(L(i + 1, j + 1) + 1) : std::max(L(i + 1, j), L(i, j + 1));
    std::size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i].id == b[j].id) from[j] = static_cast<int>(i), ++i, ++j;
        else if (L(i + 1, j) >= L(i, j + 1)) ++i;
        else ++j;
    }
    return from;
}

std::string piece_key(const Piece& p, std::string_view node_key) {
    return std::format("{}\x1f{}\x1f{}", node_key, static_cast<int>(p.cls), p.ordinal);
}

// A rectangle travelling from `a` to `b`, drawn out ahead by `stretch` of the distance it has left.
Rect stretched(Rect r, const Rect& a, const Rect& b, double u, float stretch) noexcept {
    if (stretch <= 0) return r;
    const float dx = b.x - a.x;
    if (dx == 0) return r;
    const float extra = std::min(std::abs(dx) * static_cast<float>(std::max(0.0, 1 - u)) * stretch, r.width * 0.4f);
    const float thin = std::min(extra * 0.08f, r.height * 0.12f);
    if (dx < 0) r.x -= extra;
    r.width += extra;
    r.y += thin / 2;
    r.height -= thin;
    return r;
}

double fade_out(double u, double by) noexcept { return 1 - clamp01(by <= 0 ? 1 : u / by); }
double fade_in(double u, double from) noexcept { return clamp01(from >= 1 ? (u >= 1 ? 1 : 0) : (u - from) / (1 - from)); }

}  // namespace

// ── curve ───────────────────────────────────────────────────────────────

namespace {
// A morph's progress rests when what is left of it cannot be seen: a two-hundredth of the way (half a pixel of
// a hundred), slower than a twentieth of the way a second — not the thousandths of a value's own transition.
constexpr hanim::Rest morph_rest{5e-3, 5e-2};
}  // namespace

double Curve::at(double t) const noexcept {
    if (t <= 0) return 0;
    if (kind == Kind::tween) return duration <= 0 || t >= duration ? 1.0 : ease(t / duration);
    const hanim::Oscillator o(0, velocity, 1, spring, morph_rest);
    return t >= o.settle_time() ? 1.0 : o.position(t);
}

double Curve::rate(double t) const noexcept {
    if (t < 0) return 0;
    if (kind == Kind::tween) {
        if (duration <= 0 || t >= duration) return 0;
        const double h = 1e-4;
        return (at(std::min(duration, t + h)) - at(t)) / h;
    }
    const hanim::Oscillator o(0, velocity, 1, spring, morph_rest);
    return t >= o.settle_time() ? 0.0 : o.velocity(t);
}

double Curve::length() const noexcept {
    if (kind == Kind::tween) return std::max(0.0, duration);
    return hanim::Oscillator(0, velocity, 1, spring, morph_rest).settle_time();
}

// ── transforms ──────────────────────────────────────────────────────────

Affine mix(const Affine& a, const Affine& b, double u) noexcept {
    if (u <= 0) return a;
    if (u == 1) return b;
    struct Parts {
        double tx, ty, rot, sx, sy, shear;
    };
    auto parts = [](const Affine& m, Parts& p) {
        p.tx = m.e, p.ty = m.f;
        p.sx = det::sqrt(static_cast<double>(m.a) * m.a + static_cast<double>(m.b) * m.b);
        if (p.sx < 1e-9) return false;
        p.rot = det::atan2(m.b, m.a);
        const double c = m.a / p.sx, s = m.b / p.sx;
        p.shear = c * m.c + s * m.d;
        p.sy = (static_cast<double>(m.a) * m.d - static_cast<double>(m.b) * m.c) / p.sx;
        return true;
    };
    Parts pa{}, pb{};
    if (!parts(a, pa) || !parts(b, pb)) {
        return {mixf(a.a, b.a, u), mixf(a.b, b.b, u), mixf(a.c, b.c, u), mixf(a.d, b.d, u), mixf(a.e, b.e, u), mixf(a.f, b.f, u)};
    }
    double dr = pb.rot - pa.rot;
    constexpr double pi = std::numbers::pi;
    while (dr > pi) dr -= 2 * pi;
    while (dr < -pi) dr += 2 * pi;
    const double rot = pa.rot + dr * u, sx = pa.sx + (pb.sx - pa.sx) * u, sy = pa.sy + (pb.sy - pa.sy) * u,
                 shear = pa.shear + (pb.shear - pa.shear) * u;
    const double c = det::cos(rot), s = det::sin(rot);
    return {static_cast<float>(c * sx), static_cast<float>(s * sx), static_cast<float>(c * shear - s * sy), static_cast<float>(s * shear + c * sy),
            static_cast<float>(pa.tx + (pb.tx - pa.tx) * u), static_cast<float>(pa.ty + (pb.ty - pa.ty) * u)};
}

// ── matching ────────────────────────────────────────────────────────────

Morph::Morph(Matter from, Matter to, Options options, double start)
    : from_(std::move(from)), to_(std::move(to)), options_(std::move(options)), start_(start) {
    const auto A = from_.pieces();
    const auto B = to_.pieces();
    std::vector<int> b_from(B.size(), -1), a_to(A.size(), -1);
    std::vector<char> by_meaning(B.size(), 0);

    // By node and place.
    std::unordered_map<std::string, int> b_index;
    b_index.reserve(B.size());
    for (std::size_t j = 0; j < B.size(); ++j) b_index.emplace(piece_key(B[j], to_.nodes()[B[j].node].key), static_cast<int>(j));
    for (std::size_t i = 0; i < A.size(); ++i) {
        const auto it = b_index.find(piece_key(A[i], from_.nodes()[A[i].node].key));
        if (it != b_index.end() && b_from[static_cast<std::size_t>(it->second)] < 0) {
            b_from[static_cast<std::size_t>(it->second)] = static_cast<int>(i);
            a_to[i] = it->second;
        }
    }
    // By meaning: a node of the first whose key the second does not have, the one node of the second with its role and name.
    if (options_.semantic) {
        std::set<std::string_view> a_keys, b_keys;
        for (const NodeKeys& n : from_.nodes()) a_keys.insert(n.key);
        for (const NodeKeys& n : to_.nodes()) b_keys.insert(n.key);
        std::map<std::string_view, int> a_meaning, b_meaning;  // meaning → node (−1: more than one)
        for (std::size_t k = 0; k < from_.nodes().size(); ++k) {
            const NodeKeys& n = from_.nodes()[k];
            if (n.semantic.empty() || b_keys.contains(n.key)) continue;
            auto [it, fresh] = a_meaning.emplace(n.semantic, static_cast<int>(k));
            if (!fresh) it->second = -1;
        }
        for (std::size_t k = 0; k < to_.nodes().size(); ++k) {
            const NodeKeys& n = to_.nodes()[k];
            if (n.semantic.empty() || a_keys.contains(n.key)) continue;
            auto [it, fresh] = b_meaning.emplace(n.semantic, static_cast<int>(k));
            if (!fresh) it->second = -1;
        }
        std::map<std::uint32_t, std::uint32_t> node_map;  // a node → b node
        for (const auto& [meaning, an] : a_meaning)
            if (an >= 0)
                if (const auto it = b_meaning.find(meaning); it != b_meaning.end() && it->second >= 0)
                    node_map[static_cast<std::uint32_t>(an)] = static_cast<std::uint32_t>(it->second);
        for (std::size_t i = 0; i < A.size(); ++i) {
            if (a_to[i] >= 0) continue;
            const auto nm = node_map.find(A[i].node);
            if (nm == node_map.end()) continue;
            const auto it = b_index.find(piece_key(A[i], to_.nodes()[nm->second].key));
            if (it != b_index.end() && b_from[static_cast<std::size_t>(it->second)] < 0) {
                b_from[static_cast<std::size_t>(it->second)] = static_cast<int>(i);
                a_to[i] = it->second;
                by_meaning[static_cast<std::size_t>(it->second)] = 1;
            }
        }
    }

    // The drawing order: the second's, each piece of only the first after the piece its predecessor became.
    std::vector<std::vector<Pair>> after(B.size() + 1);
    {
        int last = -1;  // the b of the last matched a met
        for (std::size_t i = 0; i < A.size(); ++i) {
            if (a_to[i] >= 0) last = a_to[i];
            else after[static_cast<std::size_t>(last + 1)].push_back(Pair{.a = static_cast<int>(i)});
        }
    }
    for (Pair& p : after[0]) pairs_.push_back(std::move(p));
    for (std::size_t j = 0; j < B.size(); ++j) {
        Pair p{.a = b_from[j], .b = static_cast<int>(j)};
        // Glyph by glyph inside two matched runs of one font.
        if (p.a >= 0 && B[j].cls == PieceClass::glyphs && options_.glyphs) {
            const auto& ga = std::get<DrawGlyphs>(from_.list().commands()[A[static_cast<std::size_t>(p.a)].command]);
            const auto& gb = std::get<DrawGlyphs>(to_.list().commands()[B[j].command]);
            if (ga.font == gb.font) p.glyph_from = glyph_lcs(ga.glyphs, gb.glyphs);
            else p.glyph_from.assign(gb.glyphs.size(), -1);
            p.glyph_kept.assign(ga.glyphs.size(), 0);
            for (const int k : p.glyph_from)
                if (k >= 0) p.glyph_kept[static_cast<std::size_t>(k)] = 1, ++stats_.glyphs_kept;
            stats_.glyphs_new += static_cast<std::size_t>(std::ranges::count(p.glyph_from, -1));
            stats_.glyphs_gone += static_cast<std::size_t>(std::ranges::count(p.glyph_kept, 0));
        }
        if (p.a >= 0) ++stats_.matched;
        else ++stats_.to_only;
        if (by_meaning[j]) ++stats_.by_meaning;
        pairs_.push_back(std::move(p));
        for (Pair& q : after[j + 1]) pairs_.push_back(std::move(q));
    }
    for (const Pair& p : pairs_)
        if (p.b < 0) ++stats_.from_only;

    // Staggered starts.
    if (options_.stagger > 0 && options_.order != Order::together) {
        std::vector<std::size_t> rank(pairs_.size());
        std::iota(rank.begin(), rank.end(), std::size_t{0});
        if (options_.order == Order::distance) {
            const Rect whole = to_.empty() ? from_.bounds() : to_.bounds();
            const Point c = whole.center();
            std::vector<double> d(pairs_.size());
            for (std::size_t k = 0; k < pairs_.size(); ++k) {
                const Piece& pc = pairs_[k].b >= 0 ? B[static_cast<std::size_t>(pairs_[k].b)] : A[static_cast<std::size_t>(pairs_[k].a)];
                const Point q = pc.bounds.center();
                d[k] = (q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y);
            }
            std::ranges::stable_sort(rank, [&](std::size_t x, std::size_t y) { return d[x] < d[y]; });
        }
        for (std::size_t r = 0; r < rank.size(); ++r) pairs_[rank[r]].delay = static_cast<double>(r) * options_.stagger;
    }
    double longest = 0;
    for (const Pair& p : pairs_) longest = std::max(longest, p.delay);
    end_ = start_ + longest + options_.curve.length();
}

// ── drawing ─────────────────────────────────────────────────────────────

void Morph::draw(double t, DisplayList& out, const NestedDraw& nested, const PushLayer* live) const { draw(t, out, nested, false, live); }

Matter Morph::snapshot(double t, const NestedDraw& nested) const {
    DisplayList list;
    draw(t, list, nested, true, nullptr);
    // Its markers: the second drawing's nodes, and the first's (their index with the high bit) for what only it has.
    constexpr std::uint32_t first = 0x80000000u;
    std::set<std::uint32_t> nested_markers;
    for (const Piece& p : to_.pieces())
        if (p.nested) nested_markers.insert(p.nested);
    Resolver r{.keys = [this](std::uint32_t m, std::string& key, std::string& meaning) {
                   const NodeKeys* n = nullptr;
                   if (m & first) {
                       const std::size_t i = m & ~first;
                       if (i < from_.nodes().size()) n = &from_.nodes()[i];
                   } else {
                       for (const NodeKeys& k : to_.nodes())
                           if (k.marker == m) n = &k;
                   }
                   if (n) key = n->key, meaning = n->semantic;
               },
               .nested = [&](std::uint32_t m) { return nested_markers.contains(m); }};
    return Matter::extract(list, r);
}

void Morph::draw(double t, DisplayList& out, const NestedDraw& nested, bool snapshot, const PushLayer* live) const {
    const double since = t - start_;
    auto marker_a = [&](const Piece& pa) -> std::uint32_t { return snapshot ? (0x80000000u | pa.node) : 0; };
    auto marker_b = [&](const Piece& pb) -> std::uint32_t { return to_.nodes()[pb.node].marker; };
    // Appearing and vanishing: `away` 1 is all the way (lower by rise, scaled by grow about the middle), 0 in place.
    const Point middle = (to_.empty() ? from_.bounds() : to_.bounds()).center();
    auto shaped = [&](double away) -> Affine {
        if (away <= 0 || (options_.rise == 0 && options_.grow == 1)) return Affine{};
        const float g = mixf(1, options_.grow, away);
        return Affine::translate(middle.x, middle.y + mixf(0, options_.rise, away)) * Affine::scale(g, g) * Affine::translate(-middle.x, -middle.y);
    };
    const auto A = from_.pieces();
    const auto B = to_.pieces();
    // The second drawing's wrapper (a style's effect on the node) around everything, its numbers between the two.
    const Wrapper& wb = to_.wrapper();
    const Wrapper& wa = from_.wrapper();
    Emitter e{out, {}, &nested};
    if (wb.present) {
        const auto& lb = std::get<PushLayer>(to_.list().commands()[wb.command]);
        const double u = clamp01(options_.curve.at(since));
        std::vector<float>       params(lb.params.begin(), lb.params.end());
        std::vector<EffectShape> shapes(lb.shapes.begin(), lb.shapes.end());
        Rect                     bounds = lb.bounds;
        float                    opacity = lb.opacity;
        if (wa.present) {
            const auto& la = std::get<PushLayer>(from_.list().commands()[wa.command]);
            if (la.effect.id == lb.effect.id && la.params.size() == lb.params.size())
                for (std::size_t i = 0; i < params.size(); ++i) params[i] = mixf(la.params[i], lb.params[i], u);
            if (la.shapes.size() == lb.shapes.size())
                for (std::size_t i = 0; i < shapes.size(); ++i) {
                    const EffectShape& sa = la.shapes[i];
                    const EffectShape& sb = lb.shapes[i];
                    const Rect plain = mixr(sa.rect, sb.rect, u);
                    const Rect r = stretched(plain, sa.rect, sb.rect, u, options_.stretch);
                    const bool capsule = sb.radii.uniform() && sb.radii.top_left * 2 >= std::min(sb.rect.width, sb.rect.height) - 0.5f;
                    shapes[i] = {r, r == plain || !capsule ? mixc(sa.radii, sb.radii, u) : CornerRadii::all(std::min(r.width, r.height) / 2)};
                }
            bounds = mixr(la.bounds, lb.bounds, u);
            opacity = mixf(la.opacity, lb.opacity, u);
        }
        // The effect's numbers as the node draws them now (its own transition, say), when it is the same effect.
        if (live && live->effect.id == lb.effect.id && live->params.size() == params.size()) params.assign(live->params.begin(), live->params.end());
        out.push_transform(wb.transform);
        out.push_layer(opacity, lb.blend, lb.effect, params, bounds, shapes);
        e.base = wb.transform.inverse().value_or(Affine{});
    }

    for (const Pair& p : pairs_) {
        const double u = options_.curve.at(since - p.delay);
        const double k = clamp01(u);
        if (p.b < 0) {  // only in the first: fading away where it was (vanishing, when it is asked to)
            const Piece& pa = A[static_cast<std::size_t>(p.a)];
            const double gone = fade_out(u, options_.fade_out);
            const float o = static_cast<float>(pa.opacity * gone);
            if (o <= 0) continue;
            e.begin(marker_a(pa));
            e.piece(from_, pa, shaped(1 - gone) * pa.transform, o, false);
            e.end();
            continue;
        }
        const Piece& pb = B[static_cast<std::size_t>(p.b)];
        // A group of a nested root is inside its own marker already.
        e.begin(pb.cls == PieceClass::group && pb.nested ? 0 : marker_b(pb));
        if (p.a < 0) {  // only in the second: appearing where it is (from below, smaller, when it is asked to)
            e.piece(to_, pb, shaped(1 - clamp01(u)) * pb.transform, static_cast<float>(pb.opacity * fade_in(u, options_.fade_in)), true);
            e.end();
            continue;
        }
        const Piece& pa = A[static_cast<std::size_t>(p.a)];
        if (since <= 0) {  // not begun: the first drawing as it was
            e.piece(from_, pa, pa.transform, pa.opacity, false);
            e.end();
            continue;
        }
        if (k >= 1 && u == 1) {  // at rest: the second drawing exactly
            e.piece(to_, pb, pb.transform, pb.opacity, true);
            e.end();
            continue;
        }
        const Affine tf = mix(pa.transform, pb.transform, u);
        const float  op = mixf(pa.opacity, pb.opacity, k);
        const Command& ca = from_.list().commands()[pa.command];
        const Command& cb = to_.list().commands()[pb.command];
        // Two of a kind that do not mix: the first fading out as the second fades in, both travelling.
        auto crossfade = [&] {
            e.piece(from_, pa, tf, static_cast<float>(op * (1 - k)), false);
            e.piece(to_, pb, tf, static_cast<float>(op * k), true);
        };
        if (pb.cls == PieceClass::group) {
            e.piece(to_, pb, tf, op, true);
            e.end();
            continue;
        }
        const Pushes pushed = e.enter(to_, pb.clip, tf);
        if (!pushed.ok) {
            e.leave(pushed);
            e.end();
            continue;
        }
        bool drawn = true;
        // A rectangle's numbers: plain or rounded alike.
        auto rect_of = [](const Command& c, Rect& r, CornerRadii& radii, Paint& paint) {
            if (const auto* f = std::get_if<FillRect>(&c)) return r = f->rect, radii = {}, paint = f->paint, true;
            if (const auto* f = std::get_if<FillRoundRect>(&c)) return r = f->rect, radii = f->radii, paint = f->paint, true;
            return false;
        };
        switch (pb.cls) {
            case PieceClass::fill: {
                Rect ra, rb;
                CornerRadii xa, xb;
                Paint fa, fb;
                if (rect_of(ca, ra, xa, fa) && rect_of(cb, rb, xb, fb)) {
                    const auto paint = mixed(fa, fb, k);
                    const Rect plain = mixr(ra, rb, u);
                    const Rect r = stretched(plain, ra, rb, u, options_.stretch);
                    // Drawn out, a capsule stays a capsule.
                    const bool capsule = xb.uniform() && xb.top_left * 2 >= std::min(rb.width, rb.height) - 0.5f;
                    const CornerRadii radii = r == plain || !capsule ? mixc(xa, xb, u) : CornerRadii::all(std::min(r.width, r.height) / 2);
                    if (paint) {
                        if (auto f = faded(*paint, op)) {
                            if (radii.zero()) out.fill_rect(r, *f);
                            else out.fill_round_rect(r, radii, *f);
                        } else drawn = false;
                    } else {
                        for (const auto& [pt, w] : {std::pair{fa, 1 - k}, std::pair{fb, k}})
                            if (auto f = faded(pt, static_cast<float>(op * w))) out.fill_round_rect(r, radii, *f);
                    }
                } else drawn = false;
                break;
            }
            case PieceClass::stroke: {
                const auto& sa = std::get<StrokeRoundRect>(ca);
                const auto& sb = std::get<StrokeRoundRect>(cb);
                if (auto paint = mixed(sa.paint, sb.paint, k))
                    if (auto f = faded(*paint, op)) {
                        out.stroke_round_rect(mixr(sa.rect, sb.rect, u), mixc(sa.radii, sb.radii, u), std::max(0.0f, mixf(sa.width, sb.width, u)), *f);
                        break;
                    }
                drawn = false;
                break;
            }
            case PieceClass::shadow: {
                const auto& sa = std::get<BoxShadow>(ca);
                const auto& sb = std::get<BoxShadow>(cb);
                const Color c = mix_color(sa.color, sb.color, k);
                out.box_shadow(mixr(sa.rect, sb.rect, u), mixc(sa.radii, sb.radii, u), mixp(sa.offset, sb.offset, u), std::max(0.0f, mixf(sa.blur, sb.blur, u)),
                               mixf(sa.spread, sb.spread, u), c.with_alpha(c.a * op), sb.inset);
                break;
            }
            case PieceClass::path_fill: {
                const auto& fa = std::get<FillPath>(ca);
                const auto& fb = std::get<FillPath>(cb);
                const auto path = mix_paths(fa.path, fb.path, u);
                const auto paint = mixed(fa.paint, fb.paint, k);
                if (path && paint)
                    if (auto f = faded(*paint, op)) {
                        out.fill_path(*path, fb.rule, *f);
                        break;
                    }
                drawn = false;
                break;
            }
            case PieceClass::path_stroke: {
                const auto& fa = std::get<StrokePath>(ca);
                const auto& fb = std::get<StrokePath>(cb);
                const auto path = mix_paths(fa.path, fb.path, u);
                const auto paint = mixed(fa.paint, fb.paint, k);
                if (path && paint)
                    if (auto f = faded(*paint, op)) {
                        StrokeStyle style = fb.style;
                        style.width = std::max(0.0f, mixf(fa.style.width, fb.style.width, u));
                        out.stroke_path(*path, style, *f);
                        break;
                    }
                drawn = false;
                break;
            }
            case PieceClass::image: {
                const auto& ia = std::get<DrawImage>(ca);
                const auto& ib = std::get<DrawImage>(cb);
                if (ia.image == ib.image) out.draw_image(ib.image, mixr(ia.source, ib.source, k), mixr(ia.dest, ib.dest, u), ib.sampling, mixf(ia.opacity, ib.opacity, k) * op);
                else drawn = false;
                break;
            }
            case PieceClass::texture: {
                const auto& ta = std::get<ExternalTexture>(ca);
                const auto& tb = std::get<ExternalTexture>(cb);
                if (ta.texture == tb.texture) out.external_texture(tb.texture, mixr(ta.dest, tb.dest, u), mixf(ta.opacity, tb.opacity, k) * op);
                else drawn = false;
                break;
            }
            case PieceClass::glyphs: {
                const auto& ga = std::get<DrawGlyphs>(ca);
                const auto& gb = std::get<DrawGlyphs>(cb);
                const auto paint = mixed(ga.paint, gb.paint, k);
                const auto inv = tf.inverse();
                if (!paint || !inv || p.glyph_from.size() != gb.glyphs.size()) {
                    drawn = false;
                    break;
                }
                // The second run's glyphs, each from where its glyph of the first was; the new ones appearing.
                const float scale = gb.size > 0 ? mixf(ga.size, gb.size, u) / gb.size : 1;
                std::vector<GlyphStyle> styles(gb.glyphs.size());
                for (std::size_t j = 0; j < gb.glyphs.size(); ++j) {
                    GlyphStyle s = gb.styles.empty() ? GlyphStyle{} : gb.styles[j];
                    const int from = p.glyph_from[j];
                    const Point at_b{gb.origin.x + gb.glyphs[j].position.x, gb.origin.y + gb.glyphs[j].position.y};
                    if (from >= 0) {
                        const Glyph& g = ga.glyphs[static_cast<std::size_t>(from)];
                        const Point wa = pa.transform.apply({ga.origin.x + g.position.x, ga.origin.y + g.position.y});
                        const Point wb = pb.transform.apply(at_b);
                        const Point here = inv->apply(mixp(wa, wb, u));
                        s.transform = Affine::translate(here.x - at_b.x, here.y - at_b.y) * Affine::scale(scale, scale) * s.transform;
                    } else {
                        s.opacity *= static_cast<float>(fade_in(u, options_.fade_in));
                    }
                    styles[j] = s;
                }
                if (auto f = faded(*paint, op)) out.draw_glyphs(gb.font, gb.size, gb.origin, gb.glyphs, *f, styles);
                // The first run's glyphs that no glyph of the second keeps, fading where they were.
                if (std::ranges::count(p.glyph_kept, 0) > 0) {
                    std::vector<GlyphStyle> gone(ga.glyphs.size());
                    const auto inv_b = pb.transform.inverse();
                    for (std::size_t i = 0; i < ga.glyphs.size(); ++i) {
                        GlyphStyle s = ga.styles.empty() ? GlyphStyle{} : ga.styles[i];
                        s.opacity *= p.glyph_kept[i] ? 0.0f : static_cast<float>(fade_out(u, options_.fade_out));
                        // Drawn under tf: where it was, mapped from the first run's space.
                        const Point at_a{ga.origin.x + ga.glyphs[i].position.x, ga.origin.y + ga.glyphs[i].position.y};
                        const Point here = inv->apply(pa.transform.apply(at_a));
                        s.transform = Affine::translate(here.x - at_a.x, here.y - at_a.y) * s.transform;
                        gone[i] = s;
                    }
                    (void)inv_b;
                    if (auto f = faded(ga.paint, op)) out.draw_glyphs(ga.font, ga.size, ga.origin, ga.glyphs, *f, gone);
                }
                break;
            }
            default: drawn = false; break;
        }
        e.leave(pushed);
        if (!drawn) crossfade();
        e.end();
    }

    if (wb.present) {
        out.pop_layer();
        out.pop_transform();
    }
}

}  // namespace omui::motion
