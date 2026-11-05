// A morph: two drawings matched, and drawn between them.
module openmui.motion;

import std;
import hanim.core;
import hanim.object;
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

// Glyph tint is straight RGB plus an independent overlay strength, not a
// premultiplied paint. Keep its RGB continuous even at zero strength: an
// interrupted snapshot must retain the complete source style, including
// when the next transition makes that tint visible again.
Color mix_tint(const Color& a, const Color& b, double u) noexcept {
    u = clamp01(u);
    if (u <= 0) return a;
    if (u >= 1) return b;
    Color rgb = mix_color({a.r, a.g, a.b, 1}, {b.r, b.g, b.b, 1}, u);
    rgb.a = mixf(a.a, b.a, u);
    return rgb;
}

// A paint faded by `o`, when fading it is a matter of its colour's alpha.
std::optional<Paint> faded(Paint p, float o) noexcept {
    if (o >= 1) return p;
    if (p.kind == Paint::Kind::gradient) return std::nullopt;
    if (p.kind == Paint::Kind::mix) p.mix_opacity *= std::max(0.0f, o);
    else p.color.a *= std::max(0.0f, o);
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
bool compatible_paths(const PathView& a, const PathView& b) {
    return a.verbs.size() == b.verbs.size() && a.points.size() == b.points.size() && std::ranges::equal(a.verbs, b.verbs);
}
struct ShapeInputFailure {
    PathMorphIssue issue;
    std::optional<hanim::ShapeMorphError> geometry;
};
std::expected<hanim::Contour, ShapeInputFailure> shape_input(PathView path) {
    auto imported=hanim::import_path(path,{.max_contours=1,.max_segments=256,.source={.max_contours=1}});
    if(!imported) {
        const auto& error=imported.error();
        if(error.path && error.path->code==mlibs::geom::PathError::contour_budget)
            return std::unexpected(ShapeInputFailure{PathMorphIssue::unsupported_contours});
        if(error.code==hanim::ObjectError::capacity)
            return std::unexpected(ShapeInputFailure{PathMorphIssue::geometry,hanim::ShapeMorphError::segment_budget});
        if(error.path && error.path->code==mlibs::geom::PathError::nonfinite_coordinate)
            return std::unexpected(ShapeInputFailure{PathMorphIssue::geometry,hanim::ShapeMorphError::nonfinite_coordinate});
        return std::unexpected(ShapeInputFailure{PathMorphIssue::malformed_path});
    }
    if(imported->contours.empty())return std::unexpected(ShapeInputFailure{PathMorphIssue::malformed_path});
    auto contour=std::move(imported->contours.front().geometry);
    if(!contour.view()->closed)return std::unexpected(ShapeInputFailure{PathMorphIssue::unsupported_contours});
    return contour;
}

// Caller-owned arena storage, never a shared Morph scratch or per-frame Path
// vector. Material alternatives can reuse this exact immutable path recording.
std::optional<RecordedPath> sampled_path(DisplayList& out, const hanim::PreparedShapeMorph* plan,
                                        const PathView& a, const PathView& b, double u, hanim::PointPath point_path) {
    const std::size_t verbs = plan ? plan->segment_count() + 2 : b.verbs.size();
    const std::size_t points = plan ? plan->segment_count() * 3 + 1 : b.points.size();
    bool sampled = true;
    const auto path = out.record_path(verbs, points, [&](MutablePathView buffer) {
        if (plan) {
            buffer.verbs.front() = PathVerb::move; buffer.verbs.back() = PathVerb::close;
            for (std::size_t i = 0; i < plan->segment_count(); ++i) {
                const auto c = plan->sample_segment(u, i);
                if (!c) { sampled = false; return; }
                auto point = [](hanim::Vec2 p) { return Point{static_cast<float>(p.x), static_cast<float>(p.y)}; };
                if (i == 0) buffer.points[0] = point(c->start);
                buffer.verbs[i + 1] = PathVerb::cubic;
                buffer.points[i * 3 + 1] = point(c->control1);
                buffer.points[i * 3 + 2] = point(c->control2);
                buffer.points[i * 3 + 3] = point(c->end);
            }
        } else {
            std::ranges::copy(b.verbs, buffer.verbs.begin());
            for (std::size_t i = 0; i < b.points.size(); ++i) {
                if (point_path.linear()) buffer.points[i] = mixp(a.points[i], b.points[i], u);
                else {
                    const auto p = point_path.interpolate({a.points[i].x, a.points[i].y}, {b.points[i].x, b.points[i].y}, u);
                    buffer.points[i] = {static_cast<float>(p.x), static_cast<float>(p.y)};
                }
            }
        }
    });
    if (!sampled || !path) return std::nullopt;
    return *path;
}
// The renderer stores float points. Check the entire modified [0,1] domain
// before accepting a path, rather than overflow a cast while emitting pixels.
// Each prepared cubic stays within the source controls' convex hull.
bool path_range(const PathView& a, const PathView& b, hanim::PointPath path) {
    if (!path.valid()) return false;
    if (path.linear()) return true; // retain the existing compatible path policy
    double bound = 0;
    for (const auto& points : {a.points, b.points}) for (auto p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        bound = std::max({bound, std::abs(static_cast<double>(p.x)), std::abs(static_cast<double>(p.y))});
    }
    const double x = std::max(std::abs(path.control1.x), std::abs(path.control2.x));
    const double y = std::max(std::abs(path.control1.y), std::abs(path.control2.y));
    return bound * (1 + 2 * (x + y)) <= std::numeric_limits<float>::max();
}
// Different material programs/resources remain a material transition on ONE
// current mask. Image lighting cannot silently jump merely because image ids
// match. Ordinary compatible paints keep the existing cheaper interpolation.
std::optional<Paint> mixed_path_paint(const Paint& a, const Paint& b, double u) {
    if ((a.material == nullptr) != (b.material == nullptr) ||
        (a.material && b.material && !(*a.material == *b.material))) return std::nullopt;
    if ((a.kind == Paint::Kind::image || a.kind == Paint::Kind::image_mask) &&
        (a.image_transform != b.image_transform || a.sampling != b.sampling)) return std::nullopt;
    return mixed(a, b, u);
}
// What is pushed around a piece, popped in the reverse order.
struct Pushes {
    std::array<std::uint8_t, 34> kinds{};  // 0 transform, 1 clip
    std::size_t                  n = 0;
    bool                         ok = true;
};

// Proof scratch has eight independent slots per thread. A normal Morph
// carries no large proof arrays on its stack; recursive deferred draws lease
// another slot. Exhaustion skips only that draw's correction, never drawing.
struct NestedRange { std::size_t first = 0, last = 0; Affine transform; };
struct InkPatch {
    std::size_t command = 0; Rect bounds; Color ink;
    Affine transform; Color from, to; double threshold = 0;
    bool composite = false;
};
struct InkCandidate { InkPatch patch; Rect carrier; CornerRadii radii; Affine inverse; int carrier_clip = -1, text_clip = -1; };
struct DrawProof {
    std::array<NestedRange, 64> groups;
    std::array<InkPatch, 128> patches;
    std::size_t group_count = 0, patch_count = 0, first = 0;
    bool overflow = false;
    void group(std::size_t first, std::size_t last, const Affine& transform) {
        if (group_count == groups.size()) { overflow = true; return; }
        groups[group_count++] = {first, last, transform};
    }
    void patch(const InkPatch& patch) {
        if (patch_count == patches.size()) { overflow = true; return; }
        patches[patch_count++] = patch;
    }
};

struct ProofPool {
    std::array<DrawProof, 8> slots;
    std::size_t depth = 0;
};
thread_local ProofPool proof_pool;
class ProofLease {
public:
    ProofLease() {
        if (proof_pool.depth == proof_pool.slots.size()) return;
        proof_ = &proof_pool.slots[proof_pool.depth++];
        proof_->group_count = 0; proof_->patch_count = 0; proof_->overflow = false;
    }
    ~ProofLease() { if (proof_) --proof_pool.depth; }
    ProofLease(const ProofLease&) = delete;
    ProofLease& operator=(const ProofLease&) = delete;
    [[nodiscard]] DrawProof* get() const noexcept { return proof_; }
private:
    DrawProof* proof_ = nullptr;
};

struct Emitter {
    DisplayList&      out;
    Affine            base;  // from the range's space to where the morph draws (inside its wrapper)
    const NestedDraw* nested = nullptr;
    DrawProof*        proof = nullptr;

    // The piece's clips (as `m` holds them) and then its transform.
    Pushes enter(const Matter& m, int clip, const Affine& t, bool transient = true) {
        Pushes p;
        std::array<int, 16> chain{};
        std::size_t         n = 0;
        for (int k = clip; k >= 0 && n < chain.size(); k = m.clips()[static_cast<std::size_t>(k)].parent) chain[n++] = k;
        Affine cur{};
        for (std::size_t i = n; i-- > 0;) {
            const ClipEntry& ce = m.clips()[static_cast<std::size_t>(chain[i])];
            if (ce.transient && !transient) continue;
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
    const Part* part_a = nullptr;
    const Part* part_b = nullptr;
    std::uint32_t part_owner = 0;
    double part_progress = 0;
    bool part_open = false;
    void begin(std::uint32_t m) {
        marker = m;
        if (m) out.begin_node(m);
        part_open = part_a || part_b;
        if (part_a && part_b && part_a->role == part_b->role && part_a->id == part_b->id)
            out.begin_part(part_a->view(part_owner),part_b->view(part_owner),part_progress);
        else if (part_b) out.begin_part(part_b->view(part_owner));
        else if (part_a) out.begin_part(part_a->view(part_owner));
    }
    void end() {
        if (part_open) out.end_part();
        if (marker) out.end_node();
        marker = 0; part_open = false;
    }

    // A piece as it is in `m`, under `t`, at `opacity`.
    void piece(const Matter& m, const Piece& pc, const Affine& t, float opacity, bool second, bool transient = true) {
        if (opacity <= 0) return;
        const bool endpoint = pc.nested_envelope && t == pc.transform;
        const Pushes p = enter(m, pc.clip, endpoint ? pc.nested_parent : t, transient);
        if (p.ok) {
            if (pc.cls == PieceClass::group) {
                const bool layer = opacity < 1;
                if (layer) out.push_layer(opacity);
                const std::size_t first = out.size();
                // The parent samples allocation; the nested root draws its
                // own state in that allocation. Cancel the callback's target
                // layout translation, rather than animating placement twice.
                const bool normalize = pc.nested_envelope && !endpoint;
                if (normalize) out.push_transform(pc.nested_inverse);
                if (pc.nested && second && nested && *nested) (*nested)(pc.nested, out);
                else
                    for (std::size_t i = pc.command; i < pc.end; ++i) out.append_command(m.list().commands()[i]);
                if (normalize) out.pop_transform();
                if (proof && pc.nested) proof->group(first, out.size(), endpoint ? pc.nested_parent : t);
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

// Conservative automatic matching: retain only a substantial unchanged
// prefix/suffix, never scattered coincident letters in an unrelated phrase.
// A cluster can contain several glyphs; cut the retained spans only at a
// cluster boundary. Equal ids on different lines are a reflow, not a local edit.
std::optional<std::vector<int>> related_text(const DrawGlyphs& a, const DrawGlyphs& b) {
    if (a.font != b.font || a.glyphs.empty() || b.glyphs.empty()) return std::nullopt;
    const auto A = a.glyphs, B = b.glyphs;
    auto same = [](const Glyph& x, const Glyph& y) { return x.id == y.id && x.line == y.line; };
    std::size_t prefix = 0, suffix = 0;
    while (prefix < std::min(A.size(), B.size()) && same(A[prefix], B[prefix])) ++prefix;
    if (prefix == A.size() && prefix == B.size()) {
        std::vector<int> from(B.size());
        std::iota(from.begin(), from.end(), 0);
        return from;
    }
    // Glyph clusters are local to the source; compare boundaries, not their
    // numeric byte offsets between texts.
    auto boundary = [](std::span<const Glyph> run, std::size_t at) {
        return at == 0 || at == run.size() || run[at - 1].cluster != run[at].cluster;
    };
    while (prefix && (!boundary(A, prefix) || !boundary(B, prefix))) --prefix;
    while (suffix < std::min(A.size(), B.size()) - prefix && same(A[A.size() - suffix - 1], B[B.size() - suffix - 1])) ++suffix;
    while (suffix && (!boundary(A, A.size() - suffix) || !boundary(B, B.size() - suffix))) --suffix;
    const std::size_t kept = prefix + suffix;
    if (std::max(prefix, suffix) < 2 || kept * 5 < std::max(A.size(), B.size()) * 3) return std::nullopt;
    std::vector<int> from(B.size(), -1);
    for (std::size_t i = 0; i < prefix; ++i) from[i] = static_cast<int>(i);
    for (std::size_t i = 0; i < suffix; ++i) from[B.size() - 1 - i] = static_cast<int>(A.size() - 1 - i);
    return from;
}

// The guard knows solid display-list carriers, not a composited screen.
// It deliberately rejects unknown image/effect/clip/opacity contexts.
bool fill_shape(const Command& command, Rect& rect, CornerRadii& radii, Paint& paint) {
    if (const auto* f = std::get_if<FillRect>(&command)) { rect = f->rect; radii = {}; paint = f->paint; return true; }
    if (const auto* f = std::get_if<FillRoundRect>(&command)) { rect = f->rect; radii = f->radii; paint = f->paint; return true; }
    return false;
}
bool positive_axis(const Affine& t) {
    return t.b == 0 && t.c == 0 && t.a > 0 && t.d > 0 &&
        std::isfinite(t.a) && std::isfinite(t.d) && std::isfinite(t.e) && std::isfinite(t.f);
}
bool same_basis(const Affine& a, const Affine& b) { return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d; }
bool rounded_contains(const Rect& rect, CornerRadii radii, const Rect& inside) {
    if (rect.empty() || inside.empty() || !rect.contains(inside) ||
        !std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        !std::isfinite(radii.top_left) || !std::isfinite(radii.top_right) || !std::isfinite(radii.bottom_left) || !std::isfinite(radii.bottom_right) ||
        radii.top_left < 0 || radii.top_right < 0 || radii.bottom_left < 0 || radii.bottom_right < 0) return false;
    radii = radii.fitted(rect.size());
    auto corner = [](Point p, Point center, float radius) {
        if (radius <= 0) return true;
        const float dx = p.x - center.x, dy = p.y - center.y;
        return dx * dx + dy * dy <= radius * radius;
    };
    const float l = rect.x, r = rect.right(), t = rect.y, b = rect.bottom();
    if (inside.x < l + radii.top_left && inside.y < t + radii.top_left &&
        !corner(inside.origin(), {l + radii.top_left, t + radii.top_left}, radii.top_left)) return false;
    if (inside.right() > r - radii.top_right && inside.y < t + radii.top_right &&
        !corner({inside.right(), inside.y}, {r - radii.top_right, t + radii.top_right}, radii.top_right)) return false;
    if (inside.right() > r - radii.bottom_right && inside.bottom() > b - radii.bottom_right &&
        !corner({inside.right(), inside.bottom()}, {r - radii.bottom_right, b - radii.bottom_right}, radii.bottom_right)) return false;
    if (inside.x < l + radii.bottom_left && inside.bottom() > b - radii.bottom_left &&
        !corner({inside.x, inside.bottom()}, {l + radii.bottom_left, b - radii.bottom_left}, radii.bottom_left)) return false;
    return true;
}
bool clips_cover(const Matter& matter, int clip, const Rect& bounds) {
    for (int i = clip; i >= 0; i = matter.clips()[static_cast<std::size_t>(i)].parent) {
        const auto& entry = matter.clips()[static_cast<std::size_t>(i)];
        const auto* rect = std::get_if<PushClipRect>(&matter.list().commands()[entry.command]);
        if (!rect || !positive_axis(entry.transform)) return false;
        const auto inv = entry.transform.inverse();
        if (!inv || !rounded_contains(rect->rect, rect->radii, inv->map(bounds))) return false;
    }
    return true;
}
bool unobscured_by(const Matter& matter, const Piece& piece, const Rect& text) {
    if (piece.cls == PieceClass::group) return piece.nested != 0; // deferred: only this draw's expanded contents can prove non-occlusion
    if (!piece.bounds.intersects(text)) return true;
    // A frame's stroke has a rectangular bounding box, but its empty
    // interior is not an overlay. Prove the text lies inside that hole.
    if (const auto* stroke = std::get_if<StrokeRoundRect>(&matter.list().commands()[piece.command]);
        stroke && positive_axis(piece.transform) && std::isfinite(stroke->width) && stroke->width >= 0) {
        const float half = stroke->width / 2;
        const Rect inner{stroke->rect.x + half, stroke->rect.y + half, stroke->rect.width - stroke->width, stroke->rect.height - stroke->width};
        const auto r = stroke->radii;
        const CornerRadii radii{std::max(0.0f, r.top_left - half), std::max(0.0f, r.top_right - half),
                               std::max(0.0f, r.bottom_right - half), std::max(0.0f, r.bottom_left - half)};
        if (const auto inv = piece.transform.inverse()) return rounded_contains(inner, radii, inv->map(text));
    }
    return false;
}
std::optional<std::size_t> layer_end(std::span<const Command> commands, std::size_t first, std::size_t end) {
    int depth = 1;
    for (std::size_t i = first + 1; i < end; ++i) {
        if (std::holds_alternative<PushLayer>(commands[i])) ++depth;
        else if (std::holds_alternative<PopLayer>(commands[i]) && --depth == 0) return i;
    }
    return std::nullopt;
}
std::optional<Rect> nested_ink_bounds(const DisplayList&, const NestedRange&, const GlyphBounds&, float, unsigned = 0);
// Capture reach can be wider than the pixels an effect generates. The
// registry's optional output contract refines only generated coverage;
// actual source drawing is always unioned, never clipped to layer.bounds.
std::optional<Rect> effect_ink_bounds(const DisplayList& list, std::size_t first, std::size_t last, const Affine& transform,
                                     const GlyphBounds& glyph_bounds, float scale, unsigned depth) {
    if (depth >= 8) return std::nullopt;
    const auto& layer = std::get<PushLayer>(list.commands()[first]);
    const auto* info = render::effects().info(layer.effect);
    if (!info) return std::nullopt;
    std::array<float, 64> params;
    const std::size_t count = std::max(layer.params.size(), info->params.size());
    if (count > params.size()) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) params[i] = i < layer.params.size() ? layer.params[i] : info->params[i].value;
    const auto reach = render::effects().output_reach(layer.effect, std::span(params.data(), count));
    if (!reach || !std::isfinite(*reach) || *reach < 0) return std::nullopt;
    auto source = nested_ink_bounds(list, {first + 1, last, transform}, glyph_bounds, scale, depth + 1);
    if (!source) return std::nullopt;
    Rect generated = transform.map(layer.bounds);
    for (const auto& shape : layer.shapes) generated = generated.united(transform.map(shape.rect));
    const float margin = std::ceil(*reach * scale) + 1; // renderer's device reach and AA convention
    generated = {std::floor(generated.x - margin), std::floor(generated.y - margin),
                 std::ceil(generated.right() + margin) - std::floor(generated.x - margin),
                 std::ceil(generated.bottom() + margin) - std::floor(generated.y - margin)};
    return source->empty() ? generated : generated.united(*source);
}
// Bound an already emitted range; the nested callback is never invoked again.
std::optional<Rect> nested_ink_bounds(const DisplayList& list, const NestedRange& range, const GlyphBounds& glyph_bounds, float scale, unsigned layers) {
    std::array<Affine, 64> transforms;
    transforms[0] = range.transform;
    std::size_t depth = 0;
    Rect result; bool any = false;
    for (std::size_t i = range.first; i < range.last; ++i) {
        const auto& command = list.commands()[i];
        std::optional<Rect> bounds;
        if (const auto* push = std::get_if<PushTransform>(&command)) {
            if (depth + 1 == transforms.size()) return std::nullopt;
            transforms[depth + 1] = transforms[depth] * push->transform; ++depth; continue;
        }
        if (std::holds_alternative<PopTransform>(command)) {
            if (depth == 0) return std::nullopt;
            --depth; continue;
        }
        if (const auto* layer = std::get_if<PushLayer>(&command); layer && layer->effect.id != 0) {
            const auto end = layer_end(list.commands(), i, range.last);
            if (!end) return std::nullopt;
            bounds = effect_ink_bounds(list, i, *end, transforms[depth], glyph_bounds, scale, layers);
            if (!bounds) return std::nullopt;
            i = *end;
        } else if (const auto* run = std::get_if<DrawGlyphs>(&command)) {
            if (!glyph_bounds) return std::nullopt;
            bounds = glyph_bounds(*run, transforms[depth]);
            if (!bounds) return std::nullopt;
        } else bounds = render::device_bounds(command, transforms[depth]);
        if (bounds && !bounds->empty()) { result = any ? result.united(*bounds) : *bounds; any = true; }
        else if (!bounds && class_of(command)) return std::nullopt;
    }
    if (depth != 0) return std::nullopt;
    return result;
}
std::optional<Affine> transform_at(const DisplayList& list, std::size_t end, float scale) {
    std::array<Affine, 64> transforms;
    transforms[0] = Affine::scale(scale, scale); std::size_t depth = 0;
    for (std::size_t i = 0; i < end; ++i) {
        if (const auto* push = std::get_if<PushTransform>(&list.commands()[i])) {
            if (depth + 1 == transforms.size()) return std::nullopt;
            transforms[depth + 1] = transforms[depth] * push->transform; ++depth;
        } else if (std::holds_alternative<PopTransform>(list.commands()[i])) {
            if (depth == 0) return std::nullopt;
            --depth;
        }
    }
    return transforms[depth];
}
bool stroke_misses(const StrokeRoundRect& stroke, const Affine& transform, const Rect& text) {
    if (!positive_axis(transform) || !std::isfinite(stroke.width) || stroke.width < 0) return false;
    const float half = stroke.width / 2;
    const Rect inner{stroke.rect.x + half, stroke.rect.y + half, stroke.rect.width - stroke.width, stroke.rect.height - stroke.width};
    const auto r = stroke.radii;
    const CornerRadii radii{std::max(0.0f, r.top_left - half), std::max(0.0f, r.top_right - half),
                           std::max(0.0f, r.bottom_right - half), std::max(0.0f, r.bottom_left - half)};
    const auto inv = transform.inverse();
    return inv && rounded_contains(inner, radii, inv->map(text));
}
// The actual output prefix establishes a uniform opaque underpaint, never a
// presumed white screen. A partial/image/effect region invalidates that
// proof; a later fully covering opaque solid may establish it again.
std::optional<Color> uniform_underpaint(const DisplayList& list, std::size_t glyph, const Rect& text, const GlyphBounds& glyph_bounds, float scale) {
    enum class Clip : std::uint8_t { full, partial, clear };
    std::array<Affine, 64> transforms;
    std::array<Clip, 64> clips;
    transforms[0] = Affine::scale(scale, scale); clips[0] = Clip::full;
    std::size_t depth = 0, clip_depth = 0;
    std::optional<Color> background;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& command = list.commands()[i];
        if (const auto* push = std::get_if<PushTransform>(&command)) {
            if (depth + 1 == transforms.size()) return std::nullopt;
            transforms[depth + 1] = transforms[depth] * push->transform; ++depth; continue;
        }
        if (std::holds_alternative<PopTransform>(command)) {
            if (depth == 0) return std::nullopt;
            --depth; continue;
        }
        if (const auto* clip = std::get_if<PushClipRect>(&command)) {
            if (clip_depth + 1 == clips.size()) return std::nullopt;
            Clip state = Clip::partial;
            if (!transforms[depth].map(clip->rect).intersects(text)) state = Clip::clear;
            else if (positive_axis(transforms[depth]))
                if (const auto inv = transforms[depth].inverse(); inv && rounded_contains(clip->rect, clip->radii, inv->map(text))) state = Clip::full;
            if (clips[clip_depth] == Clip::clear) state = Clip::clear;
            else if (clips[clip_depth] == Clip::partial && state != Clip::clear) state = Clip::partial;
            clips[++clip_depth] = state; continue;
        }
        if (std::holds_alternative<PushClipPath>(command)) {
            if (clip_depth + 1 == clips.size()) return std::nullopt;
            const Clip state = clips[clip_depth] == Clip::clear ? Clip::clear : Clip::partial;
            clips[++clip_depth] = state; continue;
        }
        if (std::holds_alternative<PopClip>(command)) {
            if (clip_depth == 0) return std::nullopt;
            --clip_depth; continue;
        }
        if (i == glyph) {
            if (clips[clip_depth] != Clip::full) return std::nullopt;
            continue;
        }
        if (const auto* layer = std::get_if<PushLayer>(&command);
            layer && (layer->effect.id != 0 || layer->blend != BlendMode::normal || layer->opacity != 1)) {
            const auto end = layer_end(list.commands(), i, list.size());
            if (!end || (i < glyph && *end > glyph)) return std::nullopt;
            std::optional<Rect> bounds = layer->effect.id != 0
                ? effect_ink_bounds(list, i, *end, transforms[depth], glyph_bounds, scale, 0)
                : nested_ink_bounds(list, {i + 1, *end, transforms[depth]}, glyph_bounds, scale);
            if (clips[clip_depth] != Clip::clear && (!bounds || (!bounds->empty() && bounds->intersects(text)))) {
                if (i > glyph) return std::nullopt;
                background.reset();
            }
            i = *end; continue;
        }
        if (clips[clip_depth] == Clip::clear) continue;
        std::optional<Rect> bounds;
        if (const auto* run = std::get_if<DrawGlyphs>(&command)) {
            if (!glyph_bounds) { if (i > glyph) return std::nullopt; background.reset(); continue; }
            bounds = glyph_bounds(*run, transforms[depth]);
            if (!bounds) { if (i > glyph) return std::nullopt; background.reset(); continue; }
        } else bounds = render::device_bounds(command, transforms[depth]);
        if (!bounds || bounds->empty() || !bounds->intersects(text)) continue;
        if (const auto* stroke = std::get_if<StrokeRoundRect>(&command); stroke && stroke_misses(*stroke, transforms[depth], text)) continue;
        if (i > glyph) return std::nullopt; // this draw actually paints over the selected text
        Rect rect; CornerRadii radii; Paint paint;
        const auto inverse = transforms[depth].inverse();
        if (clips[clip_depth] != Clip::full || !positive_axis(transforms[depth]) || !inverse ||
            !fill_shape(command, rect, radii, paint) || paint.kind != Paint::Kind::solid ||
            !rounded_contains(rect, radii, inverse->map(text))) { background.reset(); continue; }
        const Color color = paint.color;
        if (color.a == 1) background = color;
        else if (color.a > 0 && color.a < 1 && background) {
            const float rest = 1 - color.a;
            background = Color{color.r * color.a + background->r * rest, color.g * color.a + background->g * rest,
                               color.b * color.a + background->b * rest, 1};
        } else if (color.a != 0) background.reset();
    }
    return background;
}
Color readable_ink(Color, Color, Color, Color, double);
void apply_deferred_ink(DisplayList& out, const DrawProof& proof, const GlyphBounds& glyph_bounds, float scale) {
    if (proof.overflow || proof.patch_count == 0) return;
    const auto prefix = transform_at(out, proof.first, scale);
    if (!prefix) return;
    std::array<std::optional<Rect>, 64> bounds;
    for (std::size_t i = 0; i < proof.group_count; ++i) {
        auto range = proof.groups[i]; range.transform = *prefix * range.transform;
        bounds[i] = nested_ink_bounds(out, range, glyph_bounds, scale);
    }
    for (std::size_t p = 0; p < proof.patch_count; ++p) {
        const auto& patch = proof.patches[p];
        const auto* run = std::get_if<DrawGlyphs>(&out.commands()[patch.command]);
        if (!run || !glyph_bounds) continue;
        const auto actual = glyph_bounds(*run, *prefix * patch.transform);
        if (!actual || actual->empty()) continue;
        bool clear = true;
        for (std::size_t i = 0; i < proof.group_count; ++i)
            if (!bounds[i] || (!bounds[i]->empty() && bounds[i]->intersects(*actual))) { clear = false; break; }
        if (!clear) continue;
        const auto background = uniform_underpaint(out, patch.command, *actual, glyph_bounds, scale);
        if (!background) continue;
        const Color ink = readable_ink(run->paint.color, *background, patch.from, patch.to, patch.threshold);
        out.recolor(patch.command, patch.command + 1, [&](Color) { return ink; });
    }
}
bool ancestor_key(std::string_view ancestor, std::string_view key) {
    return !ancestor.empty() && (ancestor == key || (key.starts_with(ancestor) && key.size() > ancestor.size() && key[ancestor.size()] == '/'));
}
bool opaque_color(const Color& color) {
    return color.a == 1 && std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
        color.r >= 0 && color.r <= 1 && color.g >= 0 && color.g <= 1 && color.b >= 0 && color.b <= 1;
}
bool bounded_solid(const Color& color) {
    return color.a > 0 && color.a <= 1 && std::isfinite(color.a) &&
        opaque_color(Color{color.r, color.g, color.b, 1});
}
bool finite_affine(const Affine& transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) && std::isfinite(transform.d) &&
        std::isfinite(transform.e) && std::isfinite(transform.f);
}
bool opaque_text(const Piece& piece, const DrawGlyphs& run) {
    return piece.opacity == 1 && run.paint.kind == Paint::Kind::solid && opaque_color(run.paint.color) && std::isfinite(run.size) && run.size > 0 &&
        !run.glyphs.empty() && positive_axis(piece.transform) &&
        std::ranges::all_of(run.styles, [](const GlyphStyle& style) {
            return style.opacity == 1 && style.tint.a == 0 && std::isfinite(style.tint.r) && std::isfinite(style.tint.g) && std::isfinite(style.tint.b) &&
                finite_affine(style.transform);
        });
}
bool styled_geometry(const DrawGlyphs& run) {
    return std::ranges::any_of(run.styles, [](const GlyphStyle& style) { return !style.transform.identity(); });
}
bool unchanged_text(const DrawGlyphs& a, const DrawGlyphs& b) {
    return a.font == b.font && a.glyphs.size() == b.glyphs.size() &&
        std::ranges::equal(a.glyphs, b.glyphs, [](const Glyph& x, const Glyph& y) { return x.id == y.id && x.line == y.line; });
}
double luminance(const Color& color) {
    return ::omui::math::color::relative_luminance(color.r, color.g, color.b);
}
double contrast(const Color& ink, const Color& background) {
    return ::omui::math::color::contrast_ratio(luminance(ink), luminance(background));
}
// Endpoint qualification does not assume the missing backdrop is white.
// A translucent solid over any opaque RGB backdrop occupies this monotone
// luminance interval. An ink inside it has worst-case contrast one.
double worst_carrier_contrast(const Color& ink, const Color& carrier) {
    const float a = carrier.a;
    const double low = luminance({carrier.r * a, carrier.g * a, carrier.b * a, 1});
    const double high = luminance({carrier.r * a + 1 - a, carrier.g * a + 1 - a, carrier.b * a + 1 - a, 1});
    const double value = luminance(ink);
    if (value >= low && value <= high) return 1;
    return std::min(::omui::math::color::contrast_ratio(value, low), ::omui::math::color::contrast_ratio(value, high));
}
// Resolve ink polarity when foreground and carrier swap light/dark. Merely
// easing both endpoint colours together makes them equal halfway through.
Color readable_ink(Color ink, Color background, Color a, Color b, double target) {
    if (contrast(ink, background) >= target) return ink;
    const std::array<Color, 4> choices{a, b, Color::black(), Color::white()};
    Color anchor = choices.front();
    double best_distance = std::numeric_limits<double>::infinity(), strongest = 0;
    bool found = false;
    for (const Color choice : choices) {
        const double ratio = contrast(choice, background);
        const double dr = choice.r - ink.r, dg = choice.g - ink.g, db = choice.b - ink.b;
        const double distance = dr * dr + dg * dg + db * db;
        if (ratio >= target && (!found || distance < best_distance)) {
            anchor = choice; best_distance = distance; found = true;
        } else if (!found && ratio > strongest) anchor = choice;
        strongest = std::max(strongest, ratio);
    }
    if (!found) { anchor.a = ink.a; return anchor; } // an impossible high target: strongest available, not a false guarantee
    // Find the smallest correction toward this readable branch. A fixed
    // iteration budget keeps seeking deterministic and adds no allocation.
    double low = 0, high = 1;
    Color result = anchor;
    for (int i = 0; i < 18; ++i) {
        const double middle = (low + high) * 0.5;
        Color candidate = mix_color(ink, anchor, middle);
        if (contrast(candidate, background) >= target) { high = middle; result = candidate; }
        else low = middle;
    }
    result.a = ink.a;
    return result;
}

std::string piece_key(const Matter& matter, const Piece& p, std::string_view node_key) {
    if (p.part >= 0) {
        const auto& part = matter.parts()[static_cast<std::size_t>(p.part)];
        return std::format("{}\x1fpart\x1f{}\x1f{}\x1f{}\x1f{}",node_key,part.role,part.id,static_cast<int>(p.cls),p.part_ordinal);
    }
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

std::optional<Rect> glyph_ink_bounds(const render::DrawGlyphs& run, const Affine& transform, render::GlyphSource& source) {
    if (run.glyphs.empty() || run.paint.color.a <= 0) return Rect{};
    if (!std::isfinite(run.size) || run.size <= 0 || (!run.styles.empty() && run.styles.size() != run.glyphs.size()) ||
        !std::isfinite(transform.a) || !std::isfinite(transform.b) || !std::isfinite(transform.c) || !std::isfinite(transform.d) ||
        !std::isfinite(transform.e) || !std::isfinite(transform.f)) return std::nullopt;
    Rect result; bool any = false;
    auto include = [&](Rect bounds) {
        if (bounds.empty()) return;
        bounds = {bounds.x - 1, bounds.y - 1, bounds.width + 2, bounds.height + 2};
        result = any ? result.united(bounds) : bounds; any = true;
    };
    for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
        const auto style = run.styles.empty() ? GlyphStyle{} : run.styles[i];
        if (style.opacity <= 0) continue;
        const auto& glyph = run.glyphs[i];
        const Point pen{run.origin.x + glyph.position.x, run.origin.y + glyph.position.y};
        const bool simple = positive_axis(transform) && transform.a == transform.d && style.translation_only();
        if (simple) {
            const Point placed = transform.apply({pen.x + style.transform.e, pen.y + style.transform.f});
            if (!std::isfinite(placed.x) || !std::isfinite(placed.y)) return std::nullopt;
            const float x = std::floor(placed.x), y = std::floor(placed.y);
            const auto mask = source.mask(run.font, glyph.id, run.size * transform.a, {placed.x - x, placed.y - y});
            geom::PixelRect box;
            if (mask.color) box = mask.color_bounds;
            else if (mask.coverage) box = mask.coverage->bounds;
            else return std::nullopt;
            if (!box.empty()) include({x + static_cast<float>(box.x), y + static_cast<float>(box.y), static_cast<float>(box.width), static_cast<float>(box.height)});
            continue;
        }
        // The CPU renderer takes the actual em outline under this complete
        // affine for a styled glyph. Bezier curves lie in the convex hull
        // of their control points: their mapped bounds plus AA are a proof,
        // unlike an assumed em-sized rectangle.
        const auto outline = source.outline(run.font, glyph.id);
        if (!outline.verbs.empty() && !outline.points.empty()) {
            const Affine placed = transform * Affine::translate(pen.x, pen.y) * style.transform * Affine::scale(run.size, run.size);
            Point first = placed.apply(outline.points.front());
            if (!std::isfinite(first.x) || !std::isfinite(first.y)) return std::nullopt;
            float l = first.x, r = first.x, t = first.y, b = first.y;
            for (const auto point : outline.points) {
                const Point actual = placed.apply(point);
                if (!std::isfinite(actual.x) || !std::isfinite(actual.y)) return std::nullopt;
                l = std::min(l, actual.x); r = std::max(r, actual.x); t = std::min(t, actual.y); b = std::max(b, actual.y);
            }
            include(Rect::ltrb(l, t, r, b));
            continue;
        }
        // Match the renderer's transformed colour-bitmap fallback exactly.
        // A nonempty coverage-only bitmap with no outline is unsupported,
        // and cannot certify clear space. Known empty coverage is a blank.
        const float scale = transform.max_scale();
        if (!std::isfinite(scale) || scale <= 0) return std::nullopt;
        const auto mask = source.mask(run.font, glyph.id, run.size * scale, {0, 0});
        if (mask.color) {
            const auto& box = mask.color_bounds;
            const Affine placed = transform * Affine::translate(pen.x, pen.y) * style.transform * Affine::scale(1 / scale, 1 / scale);
            include(placed.map({static_cast<float>(box.x), static_cast<float>(box.y), static_cast<float>(box.width), static_cast<float>(box.height)}));
        } else if (!mask.coverage || !mask.coverage->empty()) return std::nullopt;
    }
    return result;
}

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
    auto& threshold = options_.minimum_text_contrast;
    threshold = !std::isfinite(threshold) || threshold <= 0 ? 0 : std::clamp(threshold, 1.0, 4.5);
    if (!std::isfinite(options_.proof_scale) || options_.proof_scale <= 0) options_.proof_scale = 1;
    if (options_.rebase_layout) from_.follow_layout(to_, options_.layout_scopes);
    const auto A = from_.pieces();
    const auto B = to_.pieces();
    std::vector<int> b_from(B.size(), -1), a_to(A.size(), -1);
    std::vector<char> by_meaning(B.size(), 0);

    // By node and place.
    std::unordered_map<std::string, int> b_index;
    b_index.reserve(B.size());
    for (std::size_t j = 0; j < B.size(); ++j) b_index.emplace(piece_key(to_, B[j], to_.nodes()[B[j].node].key), static_cast<int>(j));
    for (std::size_t i = 0; i < A.size(); ++i) {
        const auto it = b_index.find(piece_key(from_, A[i], from_.nodes()[A[i].node].key));
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
            const auto it = b_index.find(piece_key(from_, A[i], to_.nodes()[nm->second].key));
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
        if (p.a >= 0 && (B[j].cls == PieceClass::path_fill || B[j].cls == PieceClass::path_stroke)) {
            const auto& before = from_.list().commands()[A[static_cast<std::size_t>(p.a)].command];
            const auto& after = to_.list().commands()[B[j].command];
            const auto a_path = B[j].cls == PieceClass::path_fill ? std::get<FillPath>(before).path : std::get<StrokePath>(before).path;
            const auto b_path = B[j].cls == PieceClass::path_fill ? std::get<FillPath>(after).path : std::get<StrokePath>(after).path;
            PathMorphStatus status{static_cast<std::size_t>(p.a), j};
            const auto& a_paint = B[j].cls == PieceClass::path_fill ? std::get<FillPath>(before).paint : std::get<StrokePath>(before).paint;
            const auto& b_paint = B[j].cls == PieceClass::path_fill ? std::get<FillPath>(after).paint : std::get<StrokePath>(after).paint;
            if (!mixed_path_paint(a_paint, b_paint, 0.5)) {
                auto material = PreparedPaintMix::prepare(a_paint, b_paint);
                if (material) { p.paint = std::move(*material); status.material_mode = PathMaterialMode::prepared_mix; }
                else { status.material_mode = PathMaterialMode::unsupported; status.material_error = material.error(); }
            }
            const bool valid_path = path_range(a_path, b_path, options_.vertex_path);
            p.path_compatible = valid_path && compatible_paths(a_path, b_path);
            if (!valid_path) {
                status.issue = PathMorphIssue::geometry;
                status.geometry_error = options_.vertex_path.valid() ? hanim::ShapeMorphError::numerical_range
                                                                    : hanim::ShapeMorphError::invalid_options;
                ++stats_.paths_unsupported;
            } else if (p.path_compatible) {
                status.mode = PathMorphMode::compatible; ++stats_.paths_compatible;
            } else {
                auto aa = shape_input(a_path), bb = shape_input(b_path);
                if (!aa || !bb) {
                    const auto& error=!aa?aa.error():bb.error();
                    status.issue=error.issue;status.geometry_error=error.geometry;
                }
                else {
                    hanim::ShapeMorphOptions shape_options; shape_options.point_path = options_.vertex_path;
                    auto prepared = hanim::PreparedShapeMorph::prepare(*aa->view(), *bb->view(), shape_options);
                    if (prepared) {
                        p.path = std::move(*prepared); status.mode = PathMorphMode::prepared; ++stats_.paths_prepared;
                    } else { status.issue = PathMorphIssue::geometry; status.geometry_error = prepared.error(); }
                }
                if (status.mode == PathMorphMode::unsupported) ++stats_.paths_unsupported;
            }
            path_status_.push_back(status);
        }
        // Glyph by glyph inside two matched runs of one font.
        if (p.a >= 0 && B[j].cls == PieceClass::glyphs && options_.glyphs) {
            const auto& ga = std::get<DrawGlyphs>(from_.list().commands()[A[static_cast<std::size_t>(p.a)].command]);
            const auto& gb = std::get<DrawGlyphs>(to_.list().commands()[B[j].command]);
            if (options_.text == TextTransition::replace) p.replace_text = true;
            else if (options_.text == TextTransition::automatic) {
                if (auto related = related_text(ga, gb)) p.glyph_from = std::move(*related);
                else p.replace_text = true;
            } else if (ga.font == gb.font) p.glyph_from = glyph_lcs(ga.glyphs, gb.glyphs);
            else p.glyph_from.assign(gb.glyphs.size(), -1);
            if (p.replace_text) ++stats_.text_runs_replaced;
            else {
                p.glyph_kept.assign(ga.glyphs.size(), 0);
                for (const int k : p.glyph_from)
                    if (k >= 0) p.glyph_kept[static_cast<std::size_t>(k)] = 1, ++stats_.glyphs_kept;
                stats_.glyphs_new += static_cast<std::size_t>(std::ranges::count(p.glyph_from, -1));
                stats_.glyphs_gone += static_cast<std::size_t>(std::ranges::count(p.glyph_kept, 0));
            }
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
    // Cache only matched, opaque solid fills. No per-frame fill search and
    // no glyph-by-glyph scene scan. A candidate must be behind this run in
    // both drawings, owned by the node or one of its keyed ancestors.
    if (options_.minimum_text_contrast > 0 && std::isfinite(options_.minimum_text_contrast) &&
        !options_.travel && !from_.wrapper().present && !to_.wrapper().present) {
        const bool nested_groups = std::ranges::any_of(A, [](const Piece& p) { return p.cls == PieceClass::group && p.nested; }) ||
            std::ranges::any_of(B, [](const Piece& p) { return p.cls == PieceClass::group && p.nested; });
        std::vector<int> candidates;
        for (std::size_t i = 0; i < pairs_.size(); ++i) {
            const auto& pair = pairs_[i];
            if (pair.a < 0 || pair.b < 0) continue;
            const auto& a = A[static_cast<std::size_t>(pair.a)];
            const auto& b = B[static_cast<std::size_t>(pair.b)];
            Rect ra, rb; CornerRadii xa, xb; Paint ca, cb;
            if (a.cls == PieceClass::fill && b.cls == PieceClass::fill && a.opacity == 1 && b.opacity == 1 &&
                positive_axis(a.transform) && positive_axis(b.transform) && same_basis(a.transform, b.transform) &&
                fill_shape(from_.list().commands()[a.command], ra, xa, ca) && fill_shape(to_.list().commands()[b.command], rb, xb, cb) &&
                ca.kind == Paint::Kind::solid && cb.kind == Paint::Kind::solid && bounded_solid(ca.color) && bounded_solid(cb.color))
                candidates.push_back(static_cast<int>(i));
        }
        auto carrier_for = [&](const Matter& matter, const Piece& run, const Rect& bounds, bool second) {
            if (!clips_cover(matter, run.clip, bounds)) return -1;
            int chosen = -1;
            std::size_t latest = 0;
            const auto pieces = matter.pieces();
            for (const int candidate : candidates) {
                const auto& pair = pairs_[static_cast<std::size_t>(candidate)];
                const auto& fill = pieces[static_cast<std::size_t>(second ? pair.b : pair.a)];
                if (fill.command >= run.command || (chosen >= 0 && fill.command <= latest) ||
                    !ancestor_key(matter.nodes()[fill.node].key, matter.nodes()[run.node].key)) continue;
                Rect rect; CornerRadii radii; Paint paint;
                const auto inv = fill.transform.inverse();
                if (!inv || !fill_shape(matter.list().commands()[fill.command], rect, radii, paint) ||
                    !rounded_contains(rect, radii, inv->map(bounds)) || !clips_cover(matter, fill.clip, bounds)) continue;
                chosen = candidate; latest = fill.command;
            }
            if (chosen < 0) return -1;
            // An image, translucent overlay, sibling fill or unknown group
            // over this carrier makes the effective background unprovable.
            for (const auto& piece : pieces) {
                if (piece.command <= latest || piece.command == run.command) continue;
                // Different fallback runs can have overlapping coarse em
                // boxes even when their actual ink does not overlap. With
                // a font witness, only this draw's emitted ink can decide.
                if (piece.cls == PieceClass::glyphs && options_.glyph_bounds) continue;
                if (!unobscured_by(matter, piece, bounds)) return -1;
            }
            return chosen;
        };
        for (auto& pair : pairs_) {
            if (pair.a < 0 || pair.b < 0 || pair.replace_text || (nested_groups && !options_.glyph_bounds)) continue;
            const auto& a = A[static_cast<std::size_t>(pair.a)];
            const auto& b = B[static_cast<std::size_t>(pair.b)];
            if (a.cls != PieceClass::glyphs || b.cls != PieceClass::glyphs) continue;
            const auto& ga = std::get<DrawGlyphs>(from_.list().commands()[a.command]);
            const auto& gb = std::get<DrawGlyphs>(to_.list().commands()[b.command]);
            if (!opaque_text(a, ga) || !opaque_text(b, gb) || !unchanged_text(ga, gb) ||
                !same_basis(a.transform, b.transform) || pair.glyph_from.size() != gb.glyphs.size() ||
                std::ranges::any_of(pair.glyph_from, [](int source) { return source < 0; })) continue;
            const bool affine_styles = styled_geometry(ga) || styled_geometry(gb);
            if (affine_styles && !options_.glyph_bounds) continue;
            // A snapshot's own glyph affine belongs to its visible source.
            // Endpoint carrier/clip qualification uses actual ink when that
            // witness is available, never the unstyled pen approximation.
            const auto before_bounds = options_.glyph_bounds ? options_.glyph_bounds(ga, a.transform) : std::optional<Rect>(a.bounds);
            const auto after_bounds = options_.glyph_bounds ? options_.glyph_bounds(gb, b.transform) : std::optional<Rect>(b.bounds);
            if (!before_bounds || !after_bounds || before_bounds->empty() || after_bounds->empty()) continue;
            const int carrier = carrier_for(from_, a, *before_bounds, false);
            if (carrier < 0 || carrier_for(to_, b, *after_bounds, true) != carrier) continue;
            const auto& fill = pairs_[static_cast<std::size_t>(carrier)];
            Rect rect; CornerRadii radii; Paint ca, cb;
            (void)fill_shape(from_.list().commands()[A[static_cast<std::size_t>(fill.a)].command], rect, radii, ca);
            (void)fill_shape(to_.list().commands()[B[static_cast<std::size_t>(fill.b)].command], rect, radii, cb);
            if (worst_carrier_contrast(ga.paint.color, ca.color) < options_.minimum_text_contrast ||
                worst_carrier_contrast(gb.paint.color, cb.color) < options_.minimum_text_contrast) continue;
            pair.text_composite = ca.color.a < 1 || cb.color.a < 1;
            if (pair.text_composite && !options_.glyph_bounds) continue;
            pair.text_carrier = carrier;
            pair.text_exact_geometry = static_cast<bool>(options_.glyph_bounds);
            // Every skipped glyph-occlusion decision must reach the exact
            // current-output proof, including multi-run text without nested
            // controls or an alpha carrier. A styled snapshot is not static.
            pair.text_deferred_groups = nested_groups || pair.text_composite || pair.text_exact_geometry;
            deferred_contrast_ = deferred_contrast_ || pair.text_deferred_groups;
            pair.text_static_bounds = !affine_styles && ga.size == gb.size && a.transform == b.transform && ga.origin == gb.origin &&
                std::ranges::equal(ga.glyphs, gb.glyphs, [](const Glyph& x, const Glyph& y) { return x.position == y.position; });
            ++stats_.text_contrast_runs;
        }
    }
    double longest = 0;
    for (const Pair& p : pairs_) longest = std::max(longest, p.delay);
    end_ = start_ + longest + options_.curve.length();

    // Presentation frames never participate in piece matching, statistics,
    // visual bounds, budget or staggering. Reuse a visible owner's carrier
    // delay, preferring its surface; a transparent layout-only owner follows
    // the root layout curve (zero delay), without introducing another rank.
    const auto fa = from_.frames(), fb = to_.frames();
    std::vector<bool> used(fa.size(), false);
    for (std::size_t j = 0; j < fb.size(); ++j) {
        FramePair frame{.b = static_cast<int>(j)};
        const auto& owner = to_.nodes()[fb[j].node];
        for (std::size_t i = 0; i < fa.size(); ++i)
            if (!used[i] && from_.nodes()[fa[i].node].key == owner.key) { frame.a = static_cast<int>(i); break; }
        if (frame.a < 0 && options_.semantic && !owner.semantic.empty() &&
            std::ranges::count_if(fb, [&](const Frame& f) { return to_.nodes()[f.node].semantic == owner.semantic; }) == 1) {
            int match = -1;
            for (std::size_t i = 0; i < fa.size(); ++i) if (!used[i] && from_.nodes()[fa[i].node].semantic == owner.semantic) {
                if (match >= 0) { match = -1; break; } match = static_cast<int>(i);
            }
            frame.a = match;
        }
        if (frame.a >= 0) used[static_cast<std::size_t>(frame.a)] = true;
        const Pair* carrier = nullptr;
        for (const Pair& pair : pairs_) if (pair.b >= 0 && B[static_cast<std::size_t>(pair.b)].node == fb[j].node) {
            if (!carrier) carrier = &pair;
            if (B[static_cast<std::size_t>(pair.b)].cls == PieceClass::fill) { carrier = &pair; break; }
        }
        if (carrier) {
            frame.delay = carrier->delay;
            if (frame.a >= 0 && carrier->a >= 0 && B[static_cast<std::size_t>(carrier->b)].cls == PieceClass::fill) {
                auto rect = [](const render::Command& command) -> std::optional<Rect> {
                    if (const auto* fill = std::get_if<render::FillRect>(&command)) return fill->rect;
                    if (const auto* fill = std::get_if<render::FillRoundRect>(&command)) return fill->rect;
                    return std::nullopt;
                };
                const auto a = rect(from_.list().commands()[A[static_cast<std::size_t>(carrier->a)].command]);
                const auto b = rect(to_.list().commands()[B[static_cast<std::size_t>(carrier->b)].command]);
                frame.stretch = a && b && *a == fa[static_cast<std::size_t>(frame.a)].rect && *b == fb[j].rect;
            }
        }
        frames_.push_back(frame);
    }
    for (std::size_t i = 0; i < fa.size(); ++i) if (!used[i]) {
        FramePair frame{.a = static_cast<int>(i)};
        for (const Pair& pair : pairs_) if (pair.a >= 0 && A[static_cast<std::size_t>(pair.a)].node == fa[i].node) { frame.delay = pair.delay; break; }
        frames_.push_back(frame);
    }
}

// ── drawing ─────────────────────────────────────────────────────────────

double Morph::travel(double t) const noexcept {
    if (!options_.travel || t <= start_) return 0;
    return det::sin(std::numbers::pi * clamp01(options_.curve.at(t - start_)));
}

void Morph::draw(double t, DisplayList& out, const NestedDraw& nested, const PushLayer* live) const {
    const double on = travel(t);
    if (on <= 1e-3 || options_.travel->effect.id == 0) return draw(t, out, nested, false, live);
    // Drawn aside, then inside the travel effect's layer, its shapes the fills where they are now.
    // NestedDraw may reenter this very Morph while its parent is recording
    // into an aside. A single TLS list would then be both source and output:
    // append_command could invalidate the command currently being copied.
    // Keep one stable slot per active depth, shared by neither recursive
    // calls nor threads. First use of a depth may grow the pool; warmed draws
    // reuse each list's arena and parameter storage without a depth cap.
    struct Scratch { DisplayList list; std::vector<float> params; };
    struct Pool { std::deque<Scratch> slots; std::size_t depth = 0; };
    thread_local Pool pool;
    if (pool.depth == pool.slots.size()) pool.slots.emplace_back();
    Scratch& scratch = pool.slots[pool.depth++];
    struct Lease {
        Pool& pool;
        ~Lease() { --pool.depth; }
    } lease{pool};
    auto& aside = scratch.list;
    auto& params = scratch.params;
    aside.clear();
    draw(t, aside, nested, false, live);
    std::array<EffectShape, 8> shapes{};
    std::size_t                count = 0;
    std::array<Affine, 64>     stack{};
    std::size_t                depth = 0;
    Affine                     at{};
    Rect                       all = from_.bounds().united(to_.bounds());
    for (const Command& c : aside.commands()) {
        if (const auto* p = std::get_if<PushTransform>(&c)) {
            if (depth < stack.size()) stack[depth++] = at;
            at = at * p->transform;
        } else if (std::holds_alternative<PopTransform>(c)) {
            if (depth > 0) at = stack[--depth];
        } else if (const auto* f = std::get_if<FillRect>(&c)) {
            const Rect r = at.map(f->rect);
            all = all.united(r);
            if (count < shapes.size()) shapes[count++] = {r, {}};
        } else if (const auto* f = std::get_if<FillRoundRect>(&c)) {
            const Rect r = at.map(f->rect);
            all = all.united(r);
            if (count < shapes.size()) shapes[count++] = {r, f->radii};
        }
    }
    const Options::Travel& tr = *options_.travel;
    params.assign(tr.params.begin(), tr.params.end());
    if (tr.param >= 0 && static_cast<std::size_t>(tr.param) < params.size()) {
        float& p = params[static_cast<std::size_t>(tr.param)];
        p = mixf(p, tr.peak, on);
    }
    // What the effect draws beyond the shapes (a waver, a drop's bridge): its own reach at these numbers.
    const float reach = render::effects().reach(tr.effect, params) + 2;
    out.push_layer(1, BlendMode::normal, tr.effect, params, {all.x - reach, all.y - reach, all.width + 2 * reach, all.height + 2 * reach},
                   std::span(shapes.data(), count));
    for (const Command& c : aside.commands()) out.append_command(c);
    out.pop_layer();
}

Matter Morph::snapshot(double t, const NestedDraw& nested) const {
    return snapshot(t, nested, SnapshotContext{});
}

Matter Morph::snapshot(double t, const NestedDraw& nested, const SnapshotContext& context) const {
    DisplayList list;
    if (deferred_contrast_ && !context.prefix.empty()) {
        list.reserve(context.prefix.size() + 64);
        for (const auto& command : context.prefix) list.append_command(command);
    }
    const std::size_t range_begin = list.size();
    draw(t, list, nested, true, nullptr);
    // Its markers: the second drawing's nodes, and the first's (their index with the high bit) for what only it has.
    constexpr std::uint32_t first = 0x80000000u;
    std::set<std::uint32_t> nested_markers, nested_placements;
    for (const Piece& p : to_.pieces()) if (p.nested) {
        nested_markers.insert(p.nested);
        if (p.nested_envelope) nested_placements.insert(p.nested);
    }
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
               .nested = [&](std::uint32_t m) { return nested_markers.contains(m); },
               .nested_placement = [&](std::uint32_t m) { return nested_placements.contains(m); }};
    return Matter::extract(list, range_begin, list.size(), r);
}

void Morph::draw(double t, DisplayList& out, const NestedDraw& nested, bool snapshot, const PushLayer* live) const {
    const double since = t - start_;
    auto marker_a = [&](const Piece& pa) -> std::uint32_t { return snapshot ? (0x80000000u | pa.node) : 0; };
    auto marker_b = [&](const Piece& pb) -> std::uint32_t { return to_.nodes()[pb.node].marker; };
    // Appearing and vanishing: `away` 1 is all the way (lower by rise, scaled by grow about the middle), 0 in place.
    const Point middle = (to_.empty() ? from_.bounds() : to_.bounds()).center();
    auto shaped = [&](double away, const Rect& bounds) -> Affine {
        if (away <= 0 || (options_.rise == 0 && options_.grow == 1)) return Affine{};
        const float g = mixf(1, options_.grow, away);
        const Point origin = options_.local_presence ? bounds.center() : middle;
        return Affine::translate(origin.x, origin.y + mixf(0, options_.rise, away)) * Affine::scale(g, g) * Affine::translate(-origin.x, -origin.y);
    };
    const auto A = from_.pieces();
    const auto B = to_.pieces();
    // The second drawing's wrapper (a style's effect on the node) around everything, its numbers between the two.
    const Wrapper& wb = to_.wrapper();
    const Wrapper& wa = from_.wrapper();
    const float proof_scale = options_.current_proof_scale ? options_.current_proof_scale() : options_.proof_scale;
    const bool valid_proof_scale = std::isfinite(proof_scale) && proof_scale > 0;
    std::optional<ProofLease> lease;
    if (deferred_contrast_ && valid_proof_scale) lease.emplace();
    DrawProof* proof = lease ? lease->get() : nullptr;
    if (proof) proof->first = out.size();
    Emitter e{out, {}, &nested, proof};
    // Canonical current layout references accompany sampled paint. They do
    // not inherit a particular part's in-flight state geometry, nor create
    // drawable pieces, identities or extra stagger ranks.
    for (const auto& reference : to_.references())
        out.reference_box(to_.nodes()[reference.node].marker,reference.key,reference.allocation);
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

    // Metadata follows the very same sampled curve/clip/transform as drawing,
    // but emits no paint and cannot influence the visual morph's cadence.
    for (const FramePair& pair : frames_) {
        if (pair.b < 0 && !snapshot) continue; // vanished ink must not invent a current input owner
        const auto af = from_.frames(), bf = to_.frames();
        const double u = options_.curve.at(since - pair.delay), k = clamp01(u);
        const Frame& target = pair.b >= 0 ? bf[static_cast<std::size_t>(pair.b)] : af[static_cast<std::size_t>(pair.a)];
        const Matter& matter = pair.b >= 0 ? to_ : from_;
        const Matter* clipping = &matter;
        int clip = target.clip;
        Rect rect = target.rect;
        Affine transform = target.transform;
        float opacity = target.opacity;
        Point placement_origin = target.sampled_placement_origin;
        if (pair.a >= 0 && pair.b >= 0) {
            const Frame& source = af[static_cast<std::size_t>(pair.a)];
            rect = mixr(source.rect, target.rect, u);
            if (pair.stretch) rect = stretched(rect, source.rect, target.rect, u, options_.stretch);
            transform = mix(source.transform, target.transform, u);
            opacity = mixf(source.opacity, target.opacity, k);
            placement_origin = {mixf(source.sampled_placement_origin.x, target.sampled_placement_origin.x, u),
                                mixf(source.sampled_placement_origin.y, target.sampled_placement_origin.y, u)};
            if (u <= 0) { clipping = &from_; clip = source.clip; }
        } else if (pair.b >= 0) {
            const double present = fade_in(u, options_.fade_in);
            transform = shaped(1 - k, target.transform.map(rect)) * transform;
            opacity *= static_cast<float>(present);
        } else {
            const double present = fade_out(u, options_.fade_out);
            transform = shaped(1 - present, target.transform.map(rect)) * transform;
            opacity *= static_cast<float>(present);
        }
        const auto marker = pair.b >= 0 ? matter.nodes()[target.node].marker : snapshot ? (0x80000000u | target.node) : 0;
        const Pushes pushed = e.enter(*clipping, clip, transform);
        if (pushed.ok) out.node_frame(marker, rect, opacity, target.placement, target.constraint,
            pair.a >= 0 && pair.b >= 0 ? mix(af[static_cast<std::size_t>(pair.a)].visual, target.visual, u) : target.visual,
            placement_origin);
        e.leave(pushed);
    }

    for (const Pair& p : pairs_) {
        const double u = options_.curve.at(since - p.delay);
        const double k = clamp01(u);
        const Piece* source_piece = p.a >= 0 ? &A[static_cast<std::size_t>(p.a)] : nullptr;
        const Piece* target_piece = p.b >= 0 ? &B[static_cast<std::size_t>(p.b)] : nullptr;
        e.part_a = source_piece && source_piece->part >= 0 ? &from_.parts()[static_cast<std::size_t>(source_piece->part)] : nullptr;
        e.part_b = target_piece && target_piece->part >= 0 ? &to_.parts()[static_cast<std::size_t>(target_piece->part)] : nullptr;
        if (target_piece && !e.part_b && k >= 1 && u == 1) e.part_a = nullptr;
        e.part_owner = target_piece ? marker_b(*target_piece) : source_piece ? marker_a(*source_piece) : 0;
        e.part_progress = u;
        if (p.b < 0) {  // only in the first: fading away where it was (vanishing, when it is asked to)
            const Piece& pa = A[static_cast<std::size_t>(p.a)];
            const double gone = fade_out(u, options_.fade_out);
            const float o = static_cast<float>(pa.opacity * gone);
            if (o <= 0) continue;
            e.begin(marker_a(pa));
            e.piece(from_, pa, shaped(1 - gone, pa.bounds) * pa.transform, o, false);
            e.end();
            continue;
        }
        const Piece& pb = B[static_cast<std::size_t>(p.b)];
        // A group of a nested root is inside its own marker already.
        e.begin(pb.cls == PieceClass::group && pb.nested ? 0 : marker_b(pb));
        if (p.a < 0) {  // only in the second: appearing where it is (from below, smaller, when it is asked to)
            e.piece(to_, pb, shaped(1 - clamp01(u), pb.bounds) * pb.transform, static_cast<float>(pb.opacity * fade_in(u, options_.fade_in)), true, !(k >= 1 && u == 1));
            e.end();
            continue;
        }
        const Piece& pa = A[static_cast<std::size_t>(p.a)];
        if (since <= 0 && !options_.rebase_layout) {  // not begun: the first drawing as it was
            e.piece(from_, pa, pa.transform, pa.opacity, false);
            e.end();
            continue;
        }
        if (k >= 1 && u == 1) {  // at rest: the second drawing exactly
            e.piece(to_, pb, pb.transform, pb.opacity, true, false);
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
        if (p.replace_text) {
            const auto& ga = std::get<DrawGlyphs>(ca);
            const auto& gb = std::get<DrawGlyphs>(cb);
            // One legible run at a time. The vertical offset is a whole-run
            // transform, never independent letters or a component timer.
            // Clamp only this text travel: spring overshoot must not bring
            // a departed sentence back over the new one. Original clips
            // remain attached to each drawing (including a snapshot's).
            if (k < 0.5) {
                const double away = k * 2;
                const Affine move = Affine::translate(0, -ga.size * options_.text_rise * static_cast<float>(away));
                e.piece(from_, pa, move * tf, static_cast<float>(pa.opacity * (1 - away)), false);
            } else {
                const double show = k * 2 - 1;
                const Affine move = Affine::translate(0, gb.size * options_.text_rise * static_cast<float>(1 - show));
                e.piece(to_, pb, move * tf, static_cast<float>(pb.opacity * show), true);
            }
            e.end();
            continue;
        }
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
        switch (pb.cls) {
            case PieceClass::fill: {
                Rect ra, rb;
                CornerRadii xa, xb;
                Paint fa, fb;
                if (fill_shape(ca, ra, xa, fa) && fill_shape(cb, rb, xb, fb)) {
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
                if (!p.path && !p.path_compatible) { drawn = false; break; }
                const auto path = sampled_path(out, p.path ? &*p.path : nullptr, fa.path, fb.path, u, options_.vertex_path);
                if (!path) { drawn = false; break; }
                if (p.paint) {
                    if (auto paint = p.paint->record(k, out, std::clamp(op, 0.0f, 1.0f))) (void)out.fill_path(*path, fb.rule, *paint);
                } else {
                    const auto mixed_paint = mixed_path_paint(fa.paint, fb.paint, k);
                    // Legacy Morph cannot return a construction error. A failed
                    // strict public material preparation is exposed in status;
                    // retain source material on current geometry until exact B.
                    // Engine/Runtime callers may reject before replacing a handle.
                    const auto paint = mixed_paint.value_or(fa.paint);
                    if (auto f = faded(paint, op)) (void)out.fill_path(*path, fb.rule, *f);
                    else { out.push_layer(op, BlendMode::normal, {}, {}, path->bounds());
                           (void)out.fill_path(*path, fb.rule, paint); out.pop_layer(); }
                }
                break;
            }
            case PieceClass::path_stroke: {
                const auto& fa = std::get<StrokePath>(ca);
                const auto& fb = std::get<StrokePath>(cb);
                if (!p.path && !p.path_compatible) { drawn = false; break; }
                const auto path = sampled_path(out, p.path ? &*p.path : nullptr, fa.path, fb.path, u, options_.vertex_path);
                if (!path) { drawn = false; break; }
                StrokeStyle style = fb.style;
                style.width = std::max(0.0f, mixf(fa.style.width, fb.style.width, u));
                if (p.paint) {
                    if (auto paint = p.paint->record(k, out, std::clamp(op, 0.0f, 1.0f))) (void)out.stroke_path(*path, style, *paint);
                } else {
                    const auto paint = mixed_path_paint(fa.paint, fb.paint, k).value_or(fa.paint);
                    if (auto f = faded(paint, op)) (void)out.stroke_path(*path, style, *f);
                    else {
                        const float reach = style.width * std::max(1.0f, style.miter_limit) * 0.5f + 1;
                        const auto b = path->bounds();
                        out.push_layer(op, BlendMode::normal, {}, {}, {b.x - reach, b.y - reach, b.width + 2 * reach, b.height + 2 * reach});
                        (void)out.stroke_path(*path, style, paint); out.pop_layer();
                    }
                }
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
                auto paint = mixed(ga.paint, gb.paint, k);
                std::optional<InkCandidate> pending_ink;
                if (paint && p.text_carrier >= 0 && (p.text_exact_geometry || (u >= 0 && u <= 1) || p.text_static_bounds)) {
                    const auto& carrier = pairs_[static_cast<std::size_t>(p.text_carrier)];
                    const auto& a = A[static_cast<std::size_t>(carrier.a)];
                    const auto& b = B[static_cast<std::size_t>(carrier.b)];
                    const double cu = options_.curve.at(since - carrier.delay);
                    Rect ra, rb; CornerRadii xa, xb; Paint fa, fb;
                    (void)fill_shape(from_.list().commands()[a.command], ra, xa, fa);
                    (void)fill_shape(to_.list().commands()[b.command], rb, xb, fb);
                    const Rect plain = mixr(ra, rb, cu);
                    const Rect rect = stretched(plain, ra, rb, cu, options_.stretch);
                    const bool capsule = xb.uniform() && xb.top_left * 2 >= std::min(rb.width, rb.height) - 0.5f;
                    const CornerRadii radii = rect == plain || !capsule ? mixc(xa, xb, cu) : CornerRadii::all(std::min(rect.width, rect.height) / 2);
                    const auto inverse = mix(a.transform, b.transform, cu).inverse();
                    // Stable em size/basis makes this the logical run's
                    // moving rectangle. Recheck coverage and rect clips at
                    // this exact time: differently staggered geometry need
                    // not cover the text just because both endpoints did.
                    const Rect text = mixr(pa.bounds, pb.bounds, u);
                    if (inverse && (p.text_exact_geometry || (rounded_contains(rect, radii, inverse->map(text)) &&
                        clips_cover(to_, b.clip, text) && clips_cover(to_, pb.clip, text)))) {
                        const Color background = mix_color(fa.color, fb.color, clamp01(cu));
                        const Color ink = readable_ink(paint->color, background, ga.paint.color, gb.paint.color, options_.minimum_text_contrast);
                        if (p.text_deferred_groups) {
                            pending_ink = InkCandidate{{0, text, ink, tf, ga.paint.color, gb.paint.color, options_.minimum_text_contrast, p.text_composite},
                                                       rect, radii, *inverse, b.clip, pb.clip};
                        }
                        else paint->color = ink;
                    }
                }
                const auto inv = tf.inverse();
                if (!paint || !inv || ga.size <= 0 || gb.size <= 0 || p.glyph_from.size() != gb.glyphs.size()) {
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
                        const Point at_a{ga.origin.x + g.position.x, ga.origin.y + g.position.y};
                        const GlyphStyle sa = ga.styles.empty() ? GlyphStyle{} : ga.styles[static_cast<std::size_t>(from)];
                        if (sa.plain() && s.plain()) {
                            // Preserve the existing plain-glyph trajectory.
                            const Point wa = pa.transform.apply(at_a);
                            const Point wb = pb.transform.apply(at_b);
                            const Point here = inv->apply(mixp(wa, wb, u));
                            s.transform = Affine::translate(here.x - at_b.x, here.y - at_b.y) * Affine::scale(scale, scale);
                        } else if (gb.size > 0) {
                            // GlyphStyle acts about its own pen, before font
                            // size and the piece transform. A snapshot's pen
                            // alone is not where that glyph was drawn. Mix
                            // complete outline matrices, then express the
                            // result about B's pen in the current run space.
                            const Affine a = pa.transform * Affine::translate(at_a.x, at_a.y) * sa.transform * Affine::scale(ga.size, ga.size);
                            const Affine b = pb.transform * Affine::translate(at_b.x, at_b.y) * s.transform * Affine::scale(gb.size, gb.size);
                            s.transform = Affine::translate(-at_b.x, -at_b.y) * *inv * mix(a, b, u) * Affine::scale(1 / gb.size, 1 / gb.size);
                        }
                        s.opacity = mixf(sa.opacity, s.opacity, k);
                        s.tint = mix_tint(sa.tint, s.tint, k);
                    } else {
                        s.opacity *= static_cast<float>(fade_in(u, options_.fade_in));
                    }
                    styles[j] = s;
                }
                if (auto f = faded(*paint, op)) {
                    const std::size_t command = out.size();
                    out.draw_glyphs(gb.font, gb.size, gb.origin, gb.glyphs, *f, styles);
                    if (pending_ink && proof && options_.glyph_bounds) {
                        const auto& actual = std::get<DrawGlyphs>(out.commands()[command]);
                        if (const auto bounds = options_.glyph_bounds(actual, tf);
                            bounds && !bounds->empty() && rounded_contains(pending_ink->carrier, pending_ink->radii, pending_ink->inverse.map(*bounds)) &&
                            clips_cover(to_, pending_ink->carrier_clip, *bounds) && clips_cover(to_, pending_ink->text_clip, *bounds)) {
                            pending_ink->patch.command = command;
                            pending_ink->patch.bounds = *bounds;
                            proof->patch(pending_ink->patch);
                        }
                    }
                }
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

    if (proof) apply_deferred_ink(out, *proof, options_.glyph_bounds, proof_scale);

    if (wb.present) {
        out.pop_layer();
        out.pop_transform();
    }
}

}  // namespace omui::motion
