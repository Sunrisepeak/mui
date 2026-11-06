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

// A snapshot interpolates its border and metadata independently. The two
// algebraically identical expressions can differ by a handful of float
// roundings. This allowance identifies a carrier only; it is never added to
// layout/paint geometry, which is rebuilt from the exact current allocation.
bool same_carrier(Rect a, Rect b) noexcept {
    auto same = [](float x, float y) {
        constexpr float rounding = 4 * std::numeric_limits<float>::epsilon();
        return x == y || std::abs(x - y) <= rounding * std::max({1.0f, std::abs(x), std::abs(y)});
    };
    return same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height);
}

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
    render::LayerBoundsMap content_bounds;
    bool measured_content=false,valid_content=false;

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
    std::vector<int> parts;
    std::map<std::tuple<std::uint32_t,std::string,std::string,PieceClass>,std::uint32_t> part_counts;
    auto reference_name = [&](std::string_view key) -> std::string_view {
        for (const auto& name : m.reference_names_) if (name == key) return name;
        m.reference_names_.emplace_back(key); return m.reference_names_.back();
    };
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
        if (!parts.empty() && parts.back() >= 0 && m.parts_[static_cast<std::size_t>(parts.back())].node == node) {
            const auto& part = m.parts_[static_cast<std::size_t>(parts.back())];
            m.pieces_.back().part = parts.back();
            m.pieces_.back().part_ordinal = part_counts[{node,part.role,part.id,cls}]++;
        }
    };

    for (std::size_t i = 0; i < c.size(); ++i) {
        const Command& cmd = c[i];
        if (const auto* b = std::get_if<BeginNode>(&cmd)) {
            // A nested root inside the range: drawn whole, where it is.
            if ((i > 0 || !outer_node) && resolver.nested && resolver.nested(b->node)) {
                const std::size_t j = closing(c, i);
                nodes.push_back(node_of(b->node));
                add(PieceClass::group, i, j + 1, {}, b->node);
                if (resolver.nested_placement && resolver.nested_placement(b->node)) {
                    // Find the nested root's real border metadata, not its
                    // first fill or glyph. Sampling its placement here gives
                    // the parent's curve/stagger an allocation envelope;
                    // the live nested callback still owns its state drawing.
                    Affine local;
                    std::vector<Affine> stack;
                    for (std::size_t k = i + 1; k < j; ++k) {
                        if (const auto* transform = std::get_if<PushTransform>(&c[k])) {
                            stack.push_back(local); local = local * transform->transform;
                        } else if (std::holds_alternative<PopTransform>(c[k])) {
                            if (!stack.empty()) { local = stack.back(); stack.pop_back(); }
                        } else if (const auto* frame = std::get_if<NodeFrame>(&c[k]); frame && frame->node == b->node) {
                            if (const auto visual_inverse = frame->visual.inverse()) {
                                const Affine allocation = local * *visual_inverse;
                                if (const auto inverse = allocation.inverse()) {
                                    Piece& piece = m.pieces_.back();
                                    piece.nested_envelope = true;
                                    piece.nested_inverse = *inverse;
                                    piece.nested_parent = transforms.back();
                                    piece.transform = transforms.back() * allocation;
                                    piece.bounds = (transforms.back() * local).map(frame->rect);
                                }
                            }
                            break;
                        }
                    }
                }
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
                if(l->bounds_mode==render::LayerBoundsMode::content) {
                    // Matching needs an allocation envelope even when the
                    // caller deliberately supplied no rectangle. Reuse the
                    // renderer's conservative glyph fallback here; actual
                    // capture measures with the renderer's live GlyphSource.
                    if(!measured_content){valid_content=content_bounds.measure(m.list_,{},1);measured_content=true;}
                    if(valid_content)if(const auto* extent=content_bounds.at(i))m.pieces_.back().bounds=extent->area;
                }
                i = j;
                continue;
            }
            opacity.push_back(opacity.back() * l->opacity);
        } else if (std::holds_alternative<PopLayer>(cmd)) {
            if (opacity.size() > 1) opacity.pop_back();
        } else if (const auto* reference = std::get_if<ReferenceBox>(&cmd)) {
            const auto node = reference->owner ? node_of(reference->owner) : nodes.empty() ? node_of(0) : nodes.back();
            auto found = std::ranges::find_if(m.references_, [&](const Reference& r) { return r.node == node && r.key == reference->key; });
            if (found == m.references_.end()) m.references_.push_back({node,std::string(reference->key),reference->allocation});
            else found->allocation = reference->allocation;
        } else if (const auto* begin = std::get_if<BeginPart>(&cmd)) {
            if (!begin->frame) { parts.push_back(-1); continue; }
            const auto& frame = *begin->frame;
            const auto node = frame.owner ? node_of(frame.owner) : nodes.empty() ? node_of(0) : nodes.back();
            Part part{.node=node,.role=std::string(frame.role),.id=std::string(frame.id),.allocation=frame.allocation,.anchor=frame.anchor,.kind=frame.kind};
            for (const auto& ref : frame.lineage) {
                if (ref.weight == 0) continue;
                auto found = std::ranges::find(part.lineage,ref.key,&render::ReferenceWeight::key);
                if (found == part.lineage.end()) part.lineage.push_back({reference_name(ref.key),ref.weight});
                else found->weight += ref.weight;
            }
            std::erase_if(part.lineage,[](const render::ReferenceWeight& ref){return ref.weight == 0;});
            m.parts_.push_back(std::move(part)); parts.push_back(static_cast<int>(m.parts_.size()-1));
        } else if (std::holds_alternative<EndPart>(cmd)) {
            if (!parts.empty()) parts.pop_back();
        } else if (const auto* frame = std::get_if<NodeFrame>(&cmd)) {
            m.frames_.push_back({node_of(frame->node), frame->rect, transforms.back(), clips.back(), opacity.back() * frame->opacity,
                frame->placement, frame->constraint, frame->visual, frame->sampled_placement_origin.value_or(frame->placement.origin())});
        } else if (const auto cls = class_of(cmd)) {
            add(*cls, i, i + 1, local_bounds(cmd), 0);
        }
    }
    return m;
}

void Matter::follow_layout(Matter& target, std::span<const std::string> scopes) {
    if (frames_.empty() || target.frames_.empty()) return;
    // The transfer is between semantic owners, not arbitrary drawable ordinals.
    // A node's local visual transform (pressed/rotation/scale) remains the source
    // state. Only its layout/ancestor allocation is replaced by the new one.
    const auto original_frames = frames_;
    std::vector<int> match(original_frames.size(), -1);
    std::vector<Affine> delta(original_frames.size());
    std::vector<render::Command> source(list_.commands().begin(), list_.commands().end());
    std::vector<render::Command> destination(target.list_.commands().begin(), target.list_.commands().end());
    auto in_scope = [&](std::string_view key) {
        return scopes.empty() || std::ranges::any_of(scopes, [&](const std::string& scope) {
            return key == scope || (key.starts_with(scope) && key.size() > scope.size() && key[scope.size()] == '/');
        });
    };
    auto same_owner = [&](std::uint32_t a, std::uint32_t b) {
        return !nodes_[a].key.empty() && in_scope(nodes_[a].key) && nodes_[a].key == target.nodes_[b].key;
    };
    for (std::size_t a = 0; a < original_frames.size(); ++a) {
        const Frame& before = original_frames[a];
        for (std::size_t b = 0; b < target.frames_.size(); ++b) {
            const Frame& after = target.frames_[b];
            if (!same_owner(before.node, after.node)) continue;
            const auto old_inverse = before.transform.inverse(), visual_inverse = after.visual.inverse();
            if (!old_inverse || !visual_inverse) break;
            match[a] = static_cast<int>(b);
            delta[a] = after.transform * *visual_inverse * before.visual * *old_inverse;
            Frame& frame = frames_[a];
            frame.transform = delta[a] * before.transform;
            frame.rect = after.rect;
            frame.placement = after.placement;
            frame.constraint = after.constraint;
            frame.sampled_placement_origin = after.sampled_placement_origin;
            break;
        }
    }
    auto owner = [&](std::uint32_t node) -> int {
        for (std::size_t f = 0; f < original_frames.size(); ++f)
            if (original_frames[f].node == node && match[f] >= 0) return static_cast<int>(f);
        return -1;
    };
    auto current_clip = [&](int clip) {
        // Inherited clipping belongs to its current allocation, not to the
        // moving child's old offset. Copy target clip commands into this
        // matter's backing list rather than translating an ancestor clip by
        // each owner's delta (which would clip pixels and input differently).
        std::vector<int> chain;
        for (int c = clip; c >= 0; c = target.clips_[static_cast<std::size_t>(c)].parent) chain.push_back(c);
        int parent = -1;
        for (auto c = chain.rbegin(); c != chain.rend(); ++c) {
            ClipEntry entry = target.clips_[static_cast<std::size_t>(*c)];
            entry.parent = parent;
            const auto command = source.size();
            source.push_back(destination[entry.command]);
            source.emplace_back(render::PopClip{});
            entry.command = command;
            clips_.push_back(entry);
            parent = static_cast<int>(clips_.size() - 1);
        }
        return parent;
    };
    auto border_clip = [](std::vector<render::Command>& commands, std::vector<ClipEntry>& clips,
                          int parent, const Frame& frame, Rect rect) {
        const auto command = commands.size();
        commands.emplace_back(render::PushClipRect{rect, {}});
        commands.emplace_back(render::PopClip{}); // keep the backing display list balanced
        clips.push_back({parent, frame.transform, command, true});
        return static_cast<int>(clips.size() - 1);
    };
    for (std::size_t f = 0; f < frames_.size(); ++f)
        if (match[f] >= 0) frames_[f].clip = current_clip(target.frames_[static_cast<std::size_t>(match[f])].clip);
    std::vector<char> text_clipped(target.frames_.size(), false);
    std::vector<std::optional<Rect>> part_allocations(parts_.size());
    for (std::size_t p = 0; p < parts_.size(); ++p) {
        const Part& part = parts_[p];
        if (owner(part.node) < 0 || part.lineage.empty()) continue;
        std::array<double,4> resolved{}; bool complete = true, changed = false;
        for (const auto& basis : part.lineage) {
            const auto found = std::ranges::find_if(target.references_, [&](const Reference& reference) {
                return same_owner(part.node,reference.node) && reference.key == basis.key;
            });
            if (found == target.references_.end()) { complete = false; break; }
            const auto previous = std::ranges::find_if(references_,[&](const Reference& reference) {
                return reference.node == part.node && reference.key == basis.key;
            });
            changed |= previous == references_.end() || previous->allocation != found->allocation;
            resolved[0] += found->allocation.x * basis.weight; resolved[1] += found->allocation.y * basis.weight;
            resolved[2] += found->allocation.width * basis.weight; resolved[3] += found->allocation.height * basis.weight;
        }
        if (complete) {
            // A purely internal state retarget must not acquire a different
            // rounding just by re-resolving its unchanged reference basis.
            part_allocations[p] = changed ? Rect{static_cast<float>(resolved[0]),static_cast<float>(resolved[1]),
                std::max(0.0f,static_cast<float>(resolved[2])),std::max(0.0f,static_cast<float>(resolved[3]))} : part.allocation;
        }
    }
    std::vector<std::vector<Point>> shifted_paths;
    for (Piece& piece : pieces_) {
        const int f = owner(piece.node);
        if (f < 0) {
            // Nested roots draw their current allocation live. They are not
            // scaled as a parent-sized bitmap and keep their own state morph.
            if (piece.nested)
                for (const Piece& next : target.pieces_)
                    if (next.nested && same_owner(piece.node, next.node)) {
                        piece.transform = next.transform; piece.bounds = next.bounds;
                        if (piece.nested_envelope) piece.nested_parent = next.transform * piece.nested_inverse;
                        piece.clip = current_clip(next.clip); break;
                    }
            continue;
        }
        const auto index = static_cast<std::size_t>(f);
        const Frame& before = original_frames[index];
        const Frame& after = target.frames_[static_cast<std::size_t>(match[index])];
        piece.transform = delta[index] * piece.transform;
        int clip = after.clip;
        const Piece* corresponding = nullptr;
        for (const Piece& next : target.pieces_) {
            bool same_part = piece.ordinal == next.ordinal;
            if (piece.part >= 0 && next.part >= 0) {
                const auto& a = parts_[static_cast<std::size_t>(piece.part)];
                const auto& b = target.parts_[static_cast<std::size_t>(next.part)];
                same_part = a.role == b.role && a.id == b.id && piece.part_ordinal == next.part_ordinal;
            }
            if (same_owner(piece.node, next.node) && piece.cls == next.cls && same_part) {
                corresponding = &next; clip = next.clip; break;
            }
        }
        piece.clip = current_clip(clip);
        const Part* part = piece.part >= 0 ? &parts_[static_cast<std::size_t>(piece.part)] : nullptr;
        const auto allocation = piece.part >= 0 ? part_allocations[static_cast<std::size_t>(piece.part)] : std::nullopt;
        Point part_shift;
        if (part && allocation) {
            const Point shift{allocation->x + allocation->width*part->anchor.x - part->allocation.x - part->allocation.width*part->anchor.x,
                allocation->y + allocation->height*part->anchor.y - part->allocation.y - part->allocation.height*part->anchor.y};
            const Affine base = frames_[index].transform;
            part_shift = shift;
            if (shift.x != 0 || shift.y != 0)
                if (const auto inverse = base.inverse()) {
                    piece.transform = base * Affine::translate(shift.x,shift.y) * *inverse * piece.transform;
                    if (piece.cls != PieceClass::group) piece.transform = piece.transform * Affine::translate(-shift.x,-shift.y);
                }
        }
        // Only a full owner border changes size. A toggle thumb, icon, glyph,
        // focus decoration or path retains its local state geometry.
        render::Command& command = source[piece.command];
        std::visit([&](auto& draw) {
            using T = std::decay_t<decltype(draw)>;
            if (part && allocation) {
                if constexpr (std::same_as<T,render::FillRect> || std::same_as<T,render::FillRoundRect> ||
                              std::same_as<T,render::StrokeRoundRect> || std::same_as<T,render::BoxShadow>) {
                    draw.rect = draw.rect.offset(part_shift);
                    if (part->kind == render::PartKind::box) {
                        const float dw = allocation->width-part->allocation.width, dh = allocation->height-part->allocation.height;
                        draw.rect = {draw.rect.x-dw*part->anchor.x,draw.rect.y-dh*part->anchor.y,
                            std::max(0.0f,draw.rect.width+dw),std::max(0.0f,draw.rect.height+dh)};
                    }
                } else if constexpr (std::same_as<T,render::DrawGlyphs>) draw.origin = draw.origin + part_shift;
                else if constexpr (std::same_as<T,render::DrawImage> || std::same_as<T,render::ExternalTexture>) draw.dest = draw.dest.offset(part_shift);
                else if constexpr (std::same_as<T,render::FillPath> || std::same_as<T,render::StrokePath>) {
                    shifted_paths.emplace_back(draw.path.points.begin(),draw.path.points.end());
                    for (auto& point : shifted_paths.back()) point = point + part_shift;
                    draw.path.points = shifted_paths.back();
                }
                if constexpr (requires { draw.paint; }) {
                    if (draw.paint.kind == render::Paint::Kind::gradient) {
                        draw.paint.gradient.start = draw.paint.gradient.start + part_shift;
                        draw.paint.gradient.end = draw.paint.gradient.end + part_shift;
                    }
                    if (draw.paint.kind == render::Paint::Kind::image || draw.paint.kind == render::Paint::Kind::image_mask)
                        draw.paint.image_transform = Affine::translate(part_shift.x,part_shift.y) * draw.paint.image_transform;
                }
                return; // fonts and local state styles are not stretched to new allocation
            }
            if constexpr (std::same_as<T, render::FillRect> || std::same_as<T, render::FillRoundRect> ||
                          std::same_as<T, render::StrokeRoundRect> || std::same_as<T, render::BoxShadow>) {
                if (same_carrier(draw.rect, before.rect)) draw.rect = after.rect;
                else if constexpr (std::same_as<T, render::StrokeRoundRect>) {
                    const float half = draw.width / 2;
                    if (same_carrier(draw.rect, before.rect.inset(geom::Insets{half, half, half, half}))) {
                        // Real component borders are normally inset half a
                        // stroke. They are carriers, not an old narrow outline
                        // to interpolate inside the new allocated surface.
                        draw.rect = after.rect.inset(geom::Insets{half, half, half, half});
                    }
                }
            } else if constexpr (std::same_as<T, render::DrawGlyphs>) {
                if (before.rect != after.rect && corresponding)
                    if (const auto* next = std::get_if<render::DrawGlyphs>(&destination[corresponding->command])) {
                        // Center/right alignment belongs to the new allocation.
                        // Retain source font, glyph positions and GlyphStyle:
                        // only the whole run's aligned origin is rebased. A
                        // content edit in an unchanged box keeps its old path.
                        draw.origin = next->origin;
                    }
            }
        }, command);
        piece.bounds = piece.transform.map(local_bounds(command));
        if (piece.cls == PieceClass::glyphs && before.rect != after.rect) {
            // A rewrapped old run may have extra rows. Crop it to the currently
            // allocated owner, while retaining the target's intentional ink
            // overhang; font size and glyph transforms are never width-scaled.
            Rect crop = after.rect;
            if (const auto inverse = after.transform.inverse())
                for (const Piece& next : target.pieces_)
                    if (next.node == after.node && next.cls == PieceClass::glyphs)
                        crop = crop.united(inverse->map(next.bounds));
            Frame source_frame = after;
            source_frame.transform = frames_[index].transform;
            piece.clip = border_clip(source, clips_, piece.clip, source_frame, crop);
            const auto target_frame = static_cast<std::size_t>(match[index]);
            if (!text_clipped[target_frame]) {
                text_clipped[target_frame] = true;
                for (Piece& next : target.pieces_)
                    if (next.node == after.node && next.cls == PieceClass::glyphs)
                        next.clip = border_clip(destination, target.clips_, next.clip, after, crop);
            }
        }
    }
    for (std::size_t p = 0; p < parts_.size(); ++p) if (part_allocations[p]) parts_[p].allocation = *part_allocations[p];
    for (Reference& reference : references_)
        for (const Reference& current : target.references_)
            if (same_owner(reference.node,current.node) && reference.key == current.key) { reference.allocation = current.allocation; break; }
    // The style wrapper is a carrier too, rather than a differently sized
    // offscreen sheet surrounding correctly rebased children.
    std::vector<render::EffectShape> wrapper_shapes; // borrowed by `source` until it is copied below
    if (scopes.empty() && wrapper_.present && target.wrapper_.present) {
        auto& before = std::get<render::PushLayer>(source[wrapper_.command]);
        const auto& after = std::get<render::PushLayer>(destination[target.wrapper_.command]);
        before.bounds = after.bounds;
        // Shape geometry is copied through arena ownership below; material
        // params/opacity remain old and keep their ordinary interpolation.
        // Like drawables, only a shape that carries an owner's allocation
        // adopts the new one; any other shape is state drawn in that
        // allocation (a tab bar's lens under the selected tab) and keeps its
        // old local geometry, so its own change still morphs.
        if (before.shapes.size() == after.shapes.size()) {
            wrapper_shapes.assign(before.shapes.begin(), before.shapes.end());
            for (std::size_t i = 0; i < wrapper_shapes.size(); ++i) {
                const bool carrier = std::ranges::any_of(original_frames, [&](const Frame& f) { return same_carrier(wrapper_shapes[i].rect, f.rect); });
                if (carrier) wrapper_shapes[i] = after.shapes[i];
            }
            before.shapes = wrapper_shapes;
        } else {
            before.shapes = after.shapes;
        }
        wrapper_.transform = target.wrapper_.transform;
    }
    render::DisplayList rebased, clipped;
    rebased.reserve(source.size()); clipped.reserve(destination.size());
    for (const auto& command : source) rebased.append_command(command);
    for (const auto& command : destination) clipped.append_command(command);
    list_ = std::move(rebased);
    target.list_ = std::move(clipped);
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
