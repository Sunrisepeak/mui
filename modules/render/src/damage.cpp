module openmui.render;

import std;
import openmui.geom;

namespace omui::render {

namespace {

constexpr std::uint64_t fnv_basis = 0xcbf29ce484222325ull;

std::uint64_t mix(std::uint64_t h, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xff;
        h *= 0x100000001b3ull;
    }
    return h;
}

Rect points_bounds(std::span<const Point> points) noexcept {
    if (points.empty()) return {};
    float l = points[0].x, t = points[0].y, r = l, b = t;
    for (const Point& p : points) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    }
    return Rect::ltrb(l, t, r, b);
}

Rect grow(const Rect& r, float by) noexcept { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }

// A rectangle mapped by `m`: the box around its four corners.
Rect mapped(const Affine& m, const Rect& r) noexcept {
    if (m.b == 0 && m.c == 0) return m.map(r);
    const std::array<Point, 4> corners{m.apply({r.x, r.y}), m.apply({r.right(), r.y}), m.apply({r.x, r.bottom()}),
                                       m.apply({r.right(), r.bottom()})};
    return points_bounds(corners);
}

}  // namespace

std::optional<Rect> device_bounds(const Command& command, const Affine& m, GlyphSource* glyphs) {
    // One device pixel around everything: anti-aliased edges and the rounding
    // of fractional positions.
    constexpr float aa = 1.0f;
    return std::visit(
        [&](const auto& c) -> std::optional<Rect> {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::same_as<T, FillRect> || std::same_as<T, FillRoundRect>) {
                return grow(mapped(m, c.rect), aa);
            } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                return grow(mapped(m, grow(c.rect, c.width / 2)), aa);
            } else if constexpr (std::same_as<T, BoxShadow>) {
                if (c.inset) return grow(mapped(m, c.rect), aa);
                // The blur reaches three standard deviations (σ = blur / 2) and the
                // box-blur approximation a few pixels more.
                const Rect shadow = grow(c.rect.offset(c.offset), std::max(0.0f, c.spread) + 1.5f * c.blur);
                return grow(mapped(m, shadow.united(c.rect)), 4 + aa);
            } else if constexpr (std::same_as<T, FillPath>) {
                return grow(mapped(m, points_bounds(c.path.points)), aa);
            } else if constexpr (std::same_as<T, StrokePath>) {
                // Miter joins reach miter_limit · width / 2; square caps √2 · width / 2.
                const float reach = c.style.width / 2 * std::max({c.style.miter_limit, 1.5f, 1.0f});
                return grow(mapped(m, grow(points_bounds(c.path.points), reach)), aa);
            } else if constexpr (std::same_as<T, DrawImage>) {
                return grow(mapped(m, c.dest), aa);
            } else if constexpr (std::same_as<T, ExternalTexture>) {
                return grow(mapped(m, c.dest), aa);
            } else if constexpr (std::same_as<T, DrawGlyphs>) {
                if (c.glyphs.empty()) return std::nullopt;
                const bool simple = m.b == 0 && m.c == 0 && m.a == m.d && m.a > 0;
                if (glyphs && simple) {
                    // Exactly where the renderer places each glyph's mask.
                    Rect box;
                    for (const Glyph& g : c.glyphs) {
                        const Point d = m.apply({c.origin.x + g.position.x, c.origin.y + g.position.y});
                        const float ix = std::floor(d.x), iy = std::floor(d.y);
                        const GlyphMask gm = glyphs->mask(c.font, g.id, c.size * m.a, {d.x - ix, d.y - iy});
                        geom::PixelRect pr;
                        if (gm.color) pr = gm.color_bounds;
                        else if (gm.coverage) pr = gm.coverage->bounds;
                        else continue;
                        if (pr.empty()) continue;
                        box = box.united({ix + static_cast<float>(pr.x), iy + static_cast<float>(pr.y), static_cast<float>(pr.width),
                                          static_cast<float>(pr.height)});
                    }
                    if (box.empty()) return std::nullopt;
                    return grow(box, aa);
                }
                // Without masks: an em box generous on every side of each origin.
                float l = c.glyphs[0].position.x, t = c.glyphs[0].position.y, r = l, b = t;
                for (const Glyph& g : c.glyphs) {
                    l = std::min(l, g.position.x);
                    r = std::max(r, g.position.x);
                    t = std::min(t, g.position.y);
                    b = std::max(b, g.position.y);
                }
                const Rect em = Rect::ltrb(c.origin.x + l - c.size, c.origin.y + t - 1.5f * c.size, c.origin.x + r + 2 * c.size,
                                           c.origin.y + b + 0.75f * c.size);
                return grow(mapped(m, em), aa);
            } else {
                return std::nullopt;
            }
        },
        command);
}

void DamageTracker::build(const DisplayList& list, float scale, std::int32_t width, std::int32_t height, const Resources& resources,
                          std::vector<Segment>& out) {
    out.clear();
    stack_.clear();
    hashers_.clear();
    transforms_.clear();
    clips_.clear();
    transforms_.push_back(Affine::scale(scale, scale));
    clips_.push_back({0, 0, static_cast<float>(width), static_cast<float>(height)});
    // The root: whatever is drawn outside every node.
    out.push_back({});
    stack_.push_back(0);
    hashers_.push_back(fnv_basis);
    std::uint32_t order = 0;
    for (const Command& command : list.commands()) {
        const std::uint32_t at = stack_.back();
        if (const auto* begin = std::get_if<BeginNode>(&command)) {
            // The parent's hash records the child, in order.
            hashers_.back() = mix(hashers_.back(), 0x9e3779b97f4a7c15ull ^ begin->node);
            Segment s;
            s.node = begin->node;
            s.order = ++order;
            s.parent = at;
            out.push_back(s);
            stack_.push_back(static_cast<std::uint32_t>(out.size() - 1));
            hashers_.push_back(fnv_basis);
            continue;
        }
        if (std::holds_alternative<EndNode>(command)) {
            if (stack_.size() <= 1) continue;  // unbalanced: the root never ends
            Segment& s = out[stack_.back()];
            s.hash = hashers_.back();
            s.subtree = s.subtree.united(s.own);
            out[s.parent].subtree = out[s.parent].subtree.united(s.subtree);
            stack_.pop_back();
            hashers_.pop_back();
            continue;
        }
        hashers_.back() = hash_command(command, hashers_.back());
        const Affine& m = transforms_.back();
        if (const auto* t = std::get_if<PushTransform>(&command)) {
            transforms_.push_back(m * t->transform);
        } else if (std::holds_alternative<PopTransform>(command)) {
            if (transforms_.size() > 1) transforms_.pop_back();
        } else if (const auto* cr = std::get_if<PushClipRect>(&command)) {
            clips_.push_back(clips_.back().intersection(grow(mapped(m, cr->rect), 1)));
        } else if (const auto* cp = std::get_if<PushClipPath>(&command)) {
            clips_.push_back(clips_.back().intersection(grow(mapped(m, points_bounds(cp->path.points)), 1)));
        } else if (std::holds_alternative<PopClip>(command)) {
            if (clips_.size() > 1) clips_.pop_back();
        } else if (auto b = device_bounds(command, m, resources.glyphs)) {
            const Rect visible = b->intersection(clips_.back());
            if (!visible.empty()) out[at].own = out[at].own.united(visible);
        }
    }
    // Close what the list left open, the root last.
    while (!stack_.empty()) {
        Segment& s = out[stack_.back()];
        s.hash = hashers_.back();
        s.subtree = s.subtree.united(s.own);
        if (stack_.size() > 1) out[s.parent].subtree = out[s.parent].subtree.united(s.subtree);
        stack_.pop_back();
        hashers_.pop_back();
    }
    // Keys: the node, and which of its occurrences (in list order).
    std::ranges::sort(out, [](const Segment& a, const Segment& b) { return a.node != b.node ? a.node < b.node : a.order < b.order; });
    std::uint64_t occurrence = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        occurrence = i > 0 && out[i - 1].node == out[i].node ? occurrence + 1 : 0;
        out[i].key = (occurrence << 32) | out[i].node;
    }
    std::ranges::sort(out, {}, &Segment::key);
}

void DamageTracker::add(const Rect& r) {
    if (!r.empty()) rects_.push_back(r);
}

std::span<const RowSpan> DamageTracker::update(const DisplayList& list, std::int32_t width, std::int32_t height, float scale,
                                               const Resources& resources) {
    rects_.clear();
    rows_.clear();
    build(list, scale, width, height, resources, current_);
    full_ = !valid_ || width != width_ || height != height_ || scale != scale_;
    if (full_) {
        add({0, 0, static_cast<float>(width), static_cast<float>(height)});
    } else {
        std::size_t i = 0, j = 0;
        while (i < previous_.size() || j < current_.size()) {
            if (j == current_.size() || (i < previous_.size() && previous_[i].key < current_[j].key)) {
                add(previous_[i++].subtree);  // gone
            } else if (i == previous_.size() || current_[j].key < previous_[i].key) {
                add(current_[j++].subtree);  // new
            } else {
                const Segment& a = previous_[i++];
                const Segment& b = current_[j++];
                if (a.hash != b.hash) {
                    add(a.subtree);
                    add(b.subtree);
                } else if (!(a.own == b.own)) {
                    add(a.own);
                    add(b.own);
                }
            }
        }
    }
    // Rectangles to bands of whole rows, merged when they touch or nearly do.
    for (const Rect& r : rects_) {
        const auto top = static_cast<std::int32_t>(std::clamp(std::floor(r.y), 0.0f, static_cast<float>(height)));
        const auto bottom = static_cast<std::int32_t>(std::clamp(std::ceil(r.bottom()), 0.0f, static_cast<float>(height)));
        if (bottom > top) rows_.push_back({top, bottom});
    }
    std::ranges::sort(rows_, {}, &RowSpan::top);
    std::size_t n = 0;
    for (const RowSpan& r : rows_) {
        if (n > 0 && r.top <= rows_[n - 1].bottom + merge_gap) rows_[n - 1].bottom = std::max(rows_[n - 1].bottom, r.bottom);
        else rows_[n++] = r;
    }
    rows_.resize(n);
    previous_.swap(current_);
    width_ = width;
    height_ = height;
    scale_ = scale;
    valid_ = true;
    return rows_;
}

}  // namespace omui::render
