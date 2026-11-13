module xrender.scene;

import std;

namespace xrender::scene {

namespace {

bool finite(const Mat4& matrix) noexcept {
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            if (!std::isfinite(matrix.at(column, row))) return false;
    return true;
}
bool finite(Vec3 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

template <class Thing>
auto place(std::vector<Thing>& things, Id id) noexcept {
    return std::ranges::lower_bound(things, id, {}, &Thing::id);
}
template <class Thing>
const Thing* found(const std::vector<Thing>& things, Id id) noexcept {
    const auto at = std::ranges::lower_bound(things, id, {}, &Thing::id);
    return at != things.end() && at->id == id ? &*at : nullptr;
}
std::expected<void, Errc> checked(const Instance& instance) noexcept {
    if (instance.id == 0) return std::unexpected(Errc::no_id);
    if (instance.bounds.empty()) return std::unexpected(Errc::empty_bounds);
    if (!finite(instance.world) || !finite(instance.bounds.low) || !finite(instance.bounds.high)) return std::unexpected(Errc::not_finite);
    return {};
}
std::expected<void, Errc> checked(const Light& light) noexcept {
    if (light.id == 0) return std::unexpected(Errc::no_id);
    if (!finite(light.at) || !finite(light.toward) || !finite(light.colour) || !std::isfinite(light.intensity) || !std::isfinite(light.range) || !std::isfinite(light.cone))
        return std::unexpected(Errc::not_finite);
    return {};
}

// One instance against a view: why it is no draw, or that it is one.
Unseen judged(const Instance& instance, const View& view, const Frustum& frustum) noexcept {
    if (instance.hidden) return Unseen::hidden;
    if (!(instance.layers & view.layers)) return Unseen::other_layer;
    if (!frustum.holds(mlibs::geom::moved(instance.world, instance.bounds))) return Unseen::outside;
    return Unseen::seen;
}
Draw draw_of(const Instance& instance, std::uint32_t index, const View& view) noexcept {
    const Vec3 middle = mlibs::math::point(view.view, mlibs::math::point(instance.world, instance.bounds.centre()));
    return {instance.id, index, instance.mesh, instance.material, -middle.z};
}
// The draws sorted and cut into batches; the lights that reach the view.
void finish(const Scene& scene, const View& view, const Frustum& frustum, Extracted& out) {
    std::ranges::sort(out.draws, [](const Draw& one, const Draw& other) {
        if (one.material != other.material) return one.material < other.material;
        if (one.mesh != other.mesh) return one.mesh < other.mesh;
        if (one.depth != other.depth) return one.depth < other.depth;
        return one.id < other.id;
    });
    for (std::uint32_t at = 0; at < out.draws.size(); ++at) {
        const Draw& draw = out.draws[at];
        if (!out.batches.empty() && out.batches.back().mesh == draw.mesh && out.batches.back().material == draw.material) ++out.batches.back().count;
        else out.batches.push_back({draw.mesh, draw.material, at, 1});
    }
    const std::span<const Light> lights = scene.lights();
    for (std::uint32_t index = 0; index < lights.size(); ++index) {
        const Light& light = lights[index];
        if (light.hidden || !(light.layers & view.layers)) continue;
        if (light.kind != LightKind::sun && !frustum.holds(mlibs::geom::Sphere{light.at, light.range})) continue;
        out.lights.push_back(index);
    }
}
void begin(Extracted& out) noexcept {
    out.draws.clear(), out.batches.clear(), out.lights.clear();
    out.looked = out.hidden = out.other_layer = out.outside = 0;
}
void count(Extracted& out, Unseen why) noexcept {
    ++out.looked;
    out.hidden += why == Unseen::hidden, out.other_layer += why == Unseen::other_layer, out.outside += why == Unseen::outside;
}

}  // namespace

std::string_view text(Errc code) noexcept {
    switch (code) {
        case Errc::no_id: return "an id is never 0";
        case Errc::duplicate: return "that id is in the scene already";
        case Errc::absent: return "no such id in the scene";
        case Errc::empty_bounds: return "its bounds hold nothing";
        case Errc::not_finite: return "a number of it is not finite";
    }
    return "";
}
std::string_view text(Unseen why) noexcept {
    switch (why) {
        case Unseen::seen: return "seen";
        case Unseen::absent: return "absent";
        case Unseen::hidden: return "hidden";
        case Unseen::other_layer: return "on none of the view's layers";
        case Unseen::outside: return "outside the view's frustum";
    }
    return "";
}

std::expected<void, Errc> Scene::add(const Instance& instance) {
    if (const auto ok = checked(instance); !ok) return ok;
    const auto at = place(instances_, instance.id);
    if (at != instances_.end() && at->id == instance.id) return std::unexpected(Errc::duplicate);
    instances_.insert(at, instance);
    changes_.push_back({ChangeKind::added, instance.id});
    return {};
}
std::expected<void, Errc> Scene::remove(Id id) {
    const auto at = place(instances_, id);
    if (at == instances_.end() || at->id != id) return std::unexpected(Errc::absent);
    instances_.erase(at);
    changes_.push_back({ChangeKind::removed, id});
    return {};
}
std::expected<void, Errc> Scene::move(Id id, const Mat4& world) {
    const auto at = place(instances_, id);
    if (at == instances_.end() || at->id != id) return std::unexpected(Errc::absent);
    if (!finite(world)) return std::unexpected(Errc::not_finite);
    at->world = world;
    changes_.push_back({ChangeKind::moved, id});
    return {};
}
std::expected<void, Errc> Scene::set(const Instance& instance) {
    if (const auto ok = checked(instance); !ok) return ok;
    const auto at = place(instances_, instance.id);
    if (at == instances_.end() || at->id != instance.id) return std::unexpected(Errc::absent);
    const Mat4 world = at->world;
    *at = instance;
    at->world = world;
    changes_.push_back({ChangeKind::changed, instance.id});
    return {};
}
std::expected<void, Errc> Scene::add(const Light& light) {
    if (const auto ok = checked(light); !ok) return ok;
    const auto at = place(lights_, light.id);
    if (at != lights_.end() && at->id == light.id) return std::unexpected(Errc::duplicate);
    lights_.insert(at, light);
    changes_.push_back({ChangeKind::light_added, light.id});
    return {};
}
std::expected<void, Errc> Scene::remove_light(Id id) {
    const auto at = place(lights_, id);
    if (at == lights_.end() || at->id != id) return std::unexpected(Errc::absent);
    lights_.erase(at);
    changes_.push_back({ChangeKind::light_removed, id});
    return {};
}
std::expected<void, Errc> Scene::set(const Light& light) {
    if (const auto ok = checked(light); !ok) return ok;
    const auto at = place(lights_, light.id);
    if (at == lights_.end() || at->id != light.id) return std::unexpected(Errc::absent);
    *at = light;
    changes_.push_back({ChangeKind::light_changed, light.id});
    return {};
}
const Instance* Scene::find(Id id) const noexcept { return found(instances_, id); }
const Light* Scene::find_light(Id id) const noexcept { return found(lights_, id); }
Box3 Scene::world_bounds(Id id) const noexcept {
    const Instance* instance = find(id);
    return instance ? mlibs::geom::moved(instance->world, instance->bounds) : Box3{};
}
void Scene::settle() noexcept {
    settled_ += changes_.size();
    changes_.clear();
    ++revision_;
}

void Spatial::rebuild(const Scene& scene) {
    ids_.clear(), boxes_.clear();
    for (const Instance& instance : scene.instances()) ids_.push_back(instance.id), boxes_.push_back(mlibs::geom::moved(instance.world, instance.bounds));
}
// The same tree over the boxes as they are now: each leaf the box of its things, each node within the box of its
// two children — which come after it, so one pass from the last node to the first. A tree refitted many times
// over things that moved far is a poor one (its boxes overlap): after so many it is made again.
bool Spatial::refit() {
    constexpr std::uint32_t most_refits = 64;
    if (tree_.nodes.empty() || tree_.order.size() != boxes_.size() || ++refits_ > most_refits) return false;
    for (std::size_t index = tree_.nodes.size(); index-- > 0;) {
        mlibs::geom::Bvh::Node& node = tree_.nodes[index];
        Box3 bounds;
        if (node.count) {
            for (std::uint32_t at = node.first; at < node.first + node.count; ++at) bounds.join(boxes_[tree_.order[at]]);
        } else {
            if (node.first <= index || node.first + 1 >= tree_.nodes.size()) return false;  // not the order it was made in
            bounds.join(tree_.nodes[node.first].bounds), bounds.join(tree_.nodes[node.first + 1].bounds);
        }
        node.bounds = bounds;
    }
    return true;
}

Spatial::Followed Spatial::follow(const Scene& scene) {
    Followed did = Followed::nothing;
    bool reshaped = false;  // something came or went: the tree is made again; things only moved: its boxes are refitted
    if (!made_ || followed_ < scene.settled()) {
        // Made new, or a frame it never followed was settled: what it missed is not on the list any more.
        rebuild(scene);
        did = Followed::rebuilt, reshaped = true;
    } else if (followed_ < scene.changed()) {
        for (const Change& change : scene.changes().subspan(static_cast<std::size_t>(followed_ - scene.settled()))) {
            const auto at = std::ranges::lower_bound(ids_, change.id);
            const std::size_t index = static_cast<std::size_t>(at - ids_.begin());
            const bool here = at != ids_.end() && *at == change.id;
            switch (change.kind) {
                case ChangeKind::added:
                case ChangeKind::moved:
                case ChangeKind::changed: {
                    // As the scene has it now: one added and removed again within the frame is not there to add.
                    const Instance* instance = scene.find(change.id);
                    if (!instance) break;
                    const Box3 box = mlibs::geom::moved(instance->world, instance->bounds);
                    if (here) boxes_[index] = box;
                    else ids_.insert(at, change.id), boxes_.insert(boxes_.begin() + static_cast<std::ptrdiff_t>(index), box), reshaped = true;
                    break;
                }
                case ChangeKind::removed:
                    if (here && !scene.find(change.id)) ids_.erase(at), boxes_.erase(boxes_.begin() + static_cast<std::ptrdiff_t>(index)), reshaped = true;
                    break;
                case ChangeKind::light_added:
                case ChangeKind::light_removed:
                case ChangeKind::light_changed: continue;  // no box of a light here
            }
            did = Followed::changes;
        }
    }
    if (did != Followed::nothing && (reshaped || !refit())) tree_ = mlibs::geom::bvh_of(boxes_), refits_ = 0;
    followed_ = scene.changed();
    made_ = true;
    return did;
}

void extract(const Scene& scene, const View& view, Extracted& out) {
    begin(out);
    const Frustum frustum = Frustum::of(view.projection * view.view);
    const std::span<const Instance> instances = scene.instances();
    for (std::uint32_t index = 0; index < instances.size(); ++index) {
        const Unseen why = judged(instances[index], view, frustum);
        count(out, why);
        if (why == Unseen::seen) out.draws.push_back(draw_of(instances[index], index, view));
    }
    finish(scene, view, frustum, out);
}

void extract(const Scene& scene, const Spatial& spatial, const View& view, Extracted& out) {
    begin(out);
    const Frustum frustum = Frustum::of(view.projection * view.view);
    const std::span<const Instance> instances = scene.instances();
    const mlibs::geom::Bvh& tree = spatial.tree();
    if (!tree.nodes.empty() && spatial.ids().size() == instances.size()) {
        std::array<std::uint32_t, mlibs::geom::bvh_stack> stack{};
        std::size_t held = 0;
        stack[held++] = 0;
        while (held) {
            const mlibs::geom::Bvh::Node& node = tree.nodes[stack[--held]];
            if (!frustum.holds(node.bounds)) continue;
            if (node.count) {
                for (std::uint32_t at = node.first; at < node.first + node.count; ++at) {
                    const std::uint32_t index = tree.order[at];
                    const Unseen why = judged(instances[index], view, frustum);
                    count(out, why);
                    if (why == Unseen::seen) out.draws.push_back(draw_of(instances[index], index, view));
                }
            } else if (held + 2 <= stack.size()) {
                stack[held++] = node.first + 1, stack[held++] = node.first;
            }
        }
    }
    finish(scene, view, frustum, out);
}

Unseen why_unseen(const Scene& scene, const View& view, Id id) noexcept {
    const Instance* instance = scene.find(id);
    if (!instance) return Unseen::absent;
    return judged(*instance, view, Frustum::of(view.projection * view.view));
}

}  // namespace xrender::scene
