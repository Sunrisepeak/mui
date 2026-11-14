module mlibs.geom.api;

import std;
import mlibs.math;

namespace mlibs::geom {

namespace {

float along(Vec3 value, int axis) noexcept { return axis == 0 ? value.x : axis == 1 ? value.y : value.z; }

}  // namespace

void BvhBuild::begin(std::span<const Box3> boxes, const BvhOptions& options, Bvh room) {
    boxes_ = boxes, leaf_ = std::max<std::uint32_t>(1, options.leaf), begun_ = true;
    if (room.nodes.capacity() || room.order.capacity()) tree_ = std::move(room);
    tree_.version = bvh_version, tree_.depth = 0;
    tree_.nodes.clear(), tree_.order.clear(), waiting_.clear();
    if (boxes.empty()) return;
    tree_.order.resize(boxes.size());
    std::iota(tree_.order.begin(), tree_.order.end(), std::uint32_t{0});
    tree_.nodes.reserve(boxes.size() * 2);
    tree_.nodes.emplace_back();
    waiting_.reserve(64);
    waiting_.push_back({0, 0, static_cast<std::uint32_t>(boxes.size()), 1});
}

// A node over order[first … first + count): a leaf if small enough, else cut at the middle thing along the longest
// axis of the centres' box — by centre, then by number, so equal centres still fall one way. Its two children are
// made next, the first before the second and all that is under it: the nodes are numbered as one walk down the
// tree numbers them, however many steps it is made in.
bool BvhBuild::step(std::size_t things) {
    std::size_t looked = 0;
    while (!waiting_.empty() && (looked == 0 || looked < things)) {
        const Part part = waiting_.back();
        waiting_.pop_back();
        looked += part.count;
        Box3 bounds, centres;
        for (std::uint32_t index = part.first; index < part.first + part.count; ++index) {
            const Box3& box = boxes_[tree_.order[index]];
            bounds.join(box);
            if (!box.empty()) centres.grow(box.centre());
        }
        tree_.nodes[part.node].bounds = bounds;
        tree_.depth = std::max(tree_.depth, part.depth);
        if (part.count <= leaf_) {
            tree_.nodes[part.node].first = part.first, tree_.nodes[part.node].count = part.count;
            continue;
        }
        int axis = 0;
        if (!centres.empty()) {
            const Vec3 size = centres.high - centres.low;
            axis = size.y > size.x ? (size.z > size.y ? 2 : 1) : (size.z > size.x ? 2 : 0);
        }
        const auto begin = tree_.order.begin() + part.first, middle = begin + part.count / 2, end = begin + part.count;
        std::nth_element(begin, middle, end, [&](std::uint32_t one, std::uint32_t other) {
            const float left = boxes_[one].empty() ? 0.0f : along(boxes_[one].centre(), axis), right = boxes_[other].empty() ? 0.0f : along(boxes_[other].centre(), axis);
            return left != right ? left < right : one < other;
        });
        // nth_element leaves each half in an order of its own making: by number, so the tree is the same wherever
        // it is built.
        std::sort(begin, middle), std::sort(middle, end);
        const std::uint32_t children = static_cast<std::uint32_t>(tree_.nodes.size());
        tree_.nodes.emplace_back(), tree_.nodes.emplace_back();
        tree_.nodes[part.node].first = children, tree_.nodes[part.node].count = 0;
        waiting_.push_back({children + 1, part.first + part.count / 2, part.count - part.count / 2, part.depth + 1});
        waiting_.push_back({children, part.first, part.count / 2, part.depth + 1});
    }
    return waiting_.empty();
}

Bvh BvhBuild::take() noexcept {
    Bvh made = std::move(tree_);
    tree_ = {}, boxes_ = {}, begun_ = false;
    waiting_.clear();
    return made;
}

// The whole tree at once: the same steps, all of them.
Bvh bvh_of(std::span<const Box3> boxes, const BvhOptions& options) {
    BvhBuild build(boxes, options);
    static_cast<void>(build.step(std::numeric_limits<std::size_t>::max()));
    return build.take();
}

Bvh bvh_of(const Mesh& mesh, const BvhOptions& options) {
    std::vector<Box3> boxes(mesh.triangles());
    for (std::size_t triangle = 0; triangle < boxes.size(); ++triangle)
        for (int corner = 0; corner < 3; ++corner) {
            const std::uint32_t index = mesh.indices[triangle * 3 + static_cast<std::size_t>(corner)];
            if (index < mesh.positions.size()) boxes[triangle].grow(mesh.positions[index]);
        }
    return bvh_of(boxes, options);
}

std::expected<void, std::string> check(const Bvh& tree, std::span<const Box3> boxes, const BvhOptions& options) {
    if (tree.version != bvh_version) return std::unexpected(std::format("a tree of version {}: this reads {}", tree.version, bvh_version));
    if (boxes.empty()) return tree.nodes.empty() && tree.order.empty() ? std::expected<void, std::string>{} : std::unexpected(std::string("nodes over no things"));
    if (tree.nodes.empty() || tree.order.size() != boxes.size()) return std::unexpected(std::format("{} things in order for {} boxes", tree.order.size(), boxes.size()));
    std::vector<std::uint8_t> seen(boxes.size(), 0), reached(tree.nodes.size(), 0);
    const auto holds = [](const Box3& outer, const Box3& inner) {
        return inner.empty() || (!outer.empty() && outer.low.x <= inner.low.x && outer.low.y <= inner.low.y && outer.low.z <= inner.low.z && outer.high.x >= inner.high.x &&
                                 outer.high.y >= inner.high.y && outer.high.z >= inner.high.z);
    };
    std::vector<std::uint32_t> stack{0};
    while (!stack.empty()) {
        const std::uint32_t at = stack.back();
        stack.pop_back();
        if (at >= tree.nodes.size() || reached[at]) return std::unexpected(std::format("node {} is reached twice, or is not there", at));
        reached[at] = 1;
        const Bvh::Node& node = tree.nodes[at];
        if (node.count) {
            if (node.count > std::max<std::uint32_t>(1, options.leaf) || std::size_t{node.first} + node.count > tree.order.size()) return std::unexpected(std::format("leaf {} holds {} things", at, node.count));
            for (std::uint32_t index = node.first; index < node.first + node.count; ++index) {
                const std::uint32_t item = tree.order[index];
                if (item >= boxes.size() || seen[item]) return std::unexpected(std::format("thing {} is in two leaves, or is not a thing", item));
                seen[item] = 1;
                if (!holds(node.bounds, boxes[item])) return std::unexpected(std::format("leaf {} does not hold thing {}", at, item));
            }
            continue;
        }
        if (std::size_t{node.first} + 1 >= tree.nodes.size()) return std::unexpected(std::format("node {} has no children", at));
        if (!holds(node.bounds, tree.nodes[node.first].bounds) || !holds(node.bounds, tree.nodes[node.first + 1].bounds)) return std::unexpected(std::format("node {} does not hold its children", at));
        stack.push_back(node.first), stack.push_back(node.first + 1);
    }
    if (std::ranges::find(seen, std::uint8_t{0}) != seen.end()) return std::unexpected(std::string("a thing is in no leaf"));
    if (std::ranges::find(reached, std::uint8_t{0}) != reached.end()) return std::unexpected(std::string("a node is not reached"));
    return {};
}

std::optional<RayHit> meet(const Ray& ray, const Mesh& mesh, const Bvh& tree, float farthest) {
    return nearest(
        tree, ray,
        [&](std::uint32_t triangle) -> std::optional<float> {
            const std::size_t at = static_cast<std::size_t>(triangle) * 3;
            if (at + 2 >= mesh.indices.size()) return std::nullopt;
            const std::uint32_t first = mesh.indices[at], second = mesh.indices[at + 1], third = mesh.indices[at + 2];
            if (first >= mesh.positions.size() || second >= mesh.positions.size() || third >= mesh.positions.size()) return std::nullopt;
            return meet(ray, mesh.positions[first], mesh.positions[second], mesh.positions[third]);
        },
        farthest);
}

}  // namespace mlibs::geom
