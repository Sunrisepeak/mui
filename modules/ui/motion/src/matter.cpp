// Matter taken from a display list's range.
module openmui.motion;

import std;
import openmui.geom;
import openmui.render.vector;

namespace omui::motion {

namespace {

using namespace render;

// Where `push` (a PushLayer or a BeginNode) is closed: the index of its pop.
std::size_t closing(std::span<const Command> c, std::size_t push) {
    const bool layer = std::holds_alternative<PushLayer>(c[push]);
    int depth = 0;
    for (std::size_t i = push; i < c.size(); ++i) {
        if (layer) {
            if (std::holds_alternative<PushLayer>(c[i])) ++depth;
            else if (std::holds_alternative<PopLayer>(c[i]) && --depth == 0) return i;
        } else {
            if (std::holds_alternative<BeginNode>(c[i])) ++depth;
            else if (std::holds_alternative<EndNode>(c[i]) && --depth == 0) return i;
        }
    }
    return c.size() - 1;
}

bool drawable(const Command& c) noexcept { return class_of(c).has_value(); }

Rect grown(Rect r, float d) noexcept { return {r.x - d, r.y - d, r.width + 2 * d, r.height + 2 * d}; }

Rect path_bounds(const PathView& p) noexcept {
    if (p.points.empty()) return {};
    float l = p.points[0].x, t = p.points[0].y, r = l, b = t;
    for (const Point& q : p.points) l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), b = std::max(b, q.y);
    return {l, t, r - l, b - t};
}

// A drawable command's bounds in its own space.
Rect local_bounds(const Command& c) noexcept {
    return std::visit(
        [](const auto& cmd) -> Rect {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::same_as<T, FillRect> || std::same_as<T, FillRoundRect>) return cmd.rect;
            else if constexpr (std::same_as<T, StrokeRoundRect>) return grown(cmd.rect, cmd.width / 2);
            else if constexpr (std::same_as<T, BoxShadow>) return grown(cmd.rect.offset(cmd.offset), cmd.blur + cmd.spread);
            else if constexpr (std::same_as<T, FillPath>) return path_bounds(cmd.path);
            else if constexpr (std::same_as<T, StrokePath>) return grown(path_bounds(cmd.path), cmd.style.width / 2);
            else if constexpr (std::same_as<T, DrawImage> || std::same_as<T, ExternalTexture>) return cmd.dest;
            else if constexpr (std::same_as<T, DrawGlyphs>) {
                if (cmd.glyphs.empty()) return {cmd.origin.x, cmd.origin.y - cmd.size, 0, cmd.size * 1.25f};
                float l = cmd.glyphs.front().position.x, r = l, t = cmd.glyphs.front().position.y, b = t;
                for (const Glyph& g : cmd.glyphs) l = std::min(l, g.position.x), r = std::max(r, g.position.x), t = std::min(t, g.position.y), b = std::max(b, g.position.y);
                return {cmd.origin.x + l, cmd.origin.y + t - cmd.size, r - l + cmd.size * 0.6f, b - t + cmd.size * 1.25f};
            } else if constexpr (std::same_as<T, PushLayer>) return cmd.bounds;
            else return {};
        },
        c);
}

}  // namespace

std::optional<PieceClass> class_of(const render::Command& c) noexcept {
    return std::visit(
        [](const auto& cmd) -> std::optional<PieceClass> {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::same_as<T, FillRect> || std::same_as<T, FillRoundRect>) return PieceClass::fill;
            else if constexpr (std::same_as<T, StrokeRoundRect>) return PieceClass::stroke;
            else if constexpr (std::same_as<T, BoxShadow>) return PieceClass::shadow;
            else if constexpr (std::same_as<T, FillPath>) return PieceClass::path_fill;
            else if constexpr (std::same_as<T, StrokePath>) return PieceClass::path_stroke;
            else if constexpr (std::same_as<T, DrawImage>) return PieceClass::image;
            else if constexpr (std::same_as<T, DrawGlyphs>) return PieceClass::glyphs;
            else if constexpr (std::same_as<T, ExternalTexture>) return PieceClass::texture;
            else return std::nullopt;
        },
        c);
}

std::optional<std::pair<std::size_t, std::size_t>> find_range(std::span<const render::Command> commands, std::uint32_t marker, std::size_t from) {
    for (std::size_t i = from; i < commands.size(); ++i)
        if (const auto* b = std::get_if<BeginNode>(&commands[i]); b && b->node == marker) return std::pair{i, closing(commands, i) + 1};
    return std::nullopt;
}

Matter Matter::extract(const render::DisplayList& source, std::size_t begin, std::size_t end, const Resolver& resolver) {
    Matter m;
    const auto all = source.commands();
    end = std::min(end, all.size());
    begin = std::min(begin, end);
    m.list_.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) m.list_.append_command(all[i]);
    const auto c = m.list_.commands();

    // The node index of a marker (its keys asked for once).
    std::vector<std::array<std::uint32_t, static_cast<std::size_t>(PieceClass::count_)>> counts;
    auto node_of = [&](std::uint32_t marker) -> std::uint32_t {
        for (std::size_t k = 0; k < m.nodes_.size(); ++k)
            if (m.nodes_[k].marker == marker) return static_cast<std::uint32_t>(k);
        NodeKeys keys{.marker = marker};
        if (resolver.keys) resolver.keys(marker, keys.key, keys.semantic);
        m.nodes_.push_back(std::move(keys));
        counts.push_back({});
        return static_cast<std::uint32_t>(m.nodes_.size() - 1);
    };
    std::vector<Affine>        transforms{Affine{}};
    std::vector<int>           clips{-1};
    // A layer as pushed: a plain one multiplies the opacity; the wrapper does not.
    std::vector<float>         opacity{1};
    std::vector<std::uint32_t> nodes;
    const bool outer_node = !c.empty() && std::holds_alternative<BeginNode>(c.front());

    auto add = [&](PieceClass cls, std::size_t first, std::size_t last, Rect local, std::uint32_t nested) {
        if (nodes.empty()) nodes.push_back(node_of(0));
        const std::uint32_t node = nodes.back();
        auto& n = counts[node][static_cast<std::size_t>(cls)];
        m.pieces_.push_back({.cls = cls,
                             .node = node,
                             .ordinal = n++,
                             .command = first,
                             .end = last,
                             .transform = transforms.back(),
                             .clip = clips.back(),
                             .opacity = opacity.back(),
                             .bounds = transforms.back().map(local),
                             .nested = nested});
    };

    for (std::size_t i = 0; i < c.size(); ++i) {
        const Command& cmd = c[i];
        if (const auto* b = std::get_if<BeginNode>(&cmd)) {
            // A nested root inside the range: drawn whole, where it is.
            if ((i > 0 || !outer_node) && resolver.nested && resolver.nested(b->node)) {
                const std::size_t j = closing(c, i);
                nodes.push_back(node_of(b->node));
                add(PieceClass::group, i, j + 1, {}, b->node);
                nodes.pop_back();
                i = j;
                continue;
            }
            nodes.push_back(node_of(b->node));
        } else if (std::holds_alternative<EndNode>(cmd)) {
            if (!nodes.empty()) nodes.pop_back();
        } else if (const auto* t = std::get_if<PushTransform>(&cmd)) {
            transforms.push_back(transforms.back() * t->transform);
        } else if (std::holds_alternative<PopTransform>(cmd)) {
            if (transforms.size() > 1) transforms.pop_back();
        } else if (std::holds_alternative<PushClipRect>(cmd) || std::holds_alternative<PushClipPath>(cmd)) {
            m.clips_.push_back({clips.back(), transforms.back(), i});
            clips.push_back(static_cast<int>(m.clips_.size() - 1));
        } else if (std::holds_alternative<PopClip>(cmd)) {
            if (clips.size() > 1) clips.pop_back();
        } else if (const auto* l = std::get_if<PushLayer>(&cmd)) {
            if (l->effect.id != 0 || l->blend != BlendMode::normal) {
                const std::size_t j = closing(c, i);
                // Around everything the range draws (nothing drawn before it, nothing after its pop): the wrapper.
                const bool before = m.pieces_.empty();
                const bool after = std::none_of(c.begin() + static_cast<std::ptrdiff_t>(j) + 1, c.end(), [](const Command& x) { return drawable(x); });
                if (!m.wrapper_.present && before && after) {
                    m.wrapper_ = {true, i, transforms.back()};
                    opacity.push_back(opacity.back());
                    continue;
                }
                add(PieceClass::group, i, j + 1, l->bounds, 0);
                i = j;
                continue;
            }
            opacity.push_back(opacity.back() * l->opacity);
        } else if (std::holds_alternative<PopLayer>(cmd)) {
            if (opacity.size() > 1) opacity.pop_back();
        } else if (const auto cls = class_of(cmd)) {
            add(*cls, i, i + 1, local_bounds(cmd), 0);
        }
    }
    return m;
}

std::optional<std::size_t> wrapper_of(std::span<const render::Command> c, std::size_t begin, std::size_t end) {
    end = std::min(end, c.size());
    for (std::size_t i = begin; i < end; ++i) {
        if (drawable(c[i])) return std::nullopt;
        const auto* l = std::get_if<PushLayer>(&c[i]);
        if (!l || (l->effect.id == 0 && l->blend == BlendMode::normal)) continue;
        const std::size_t j = closing(c, i);
        for (std::size_t k = j + 1; k < end; ++k)
            if (drawable(c[k])) return std::nullopt;
        return i;
    }
    return std::nullopt;
}

Rect Matter::bounds() const noexcept {
    Rect out;
    bool any = false;
    for (const Piece& p : pieces_) {
        if (p.bounds.width <= 0 && p.bounds.height <= 0) continue;
        out = any ? out.united(p.bounds) : p.bounds;
        any = true;
    }
    return out;
}

}  // namespace omui::motion
