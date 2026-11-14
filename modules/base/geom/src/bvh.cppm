// mlibs.geom.api:bvh — a tree of boxes over things that have boxes: what is near a place, and what a ray meets
// first, without asking every thing (the foundation plan's §5.7, D-184: the ray query is a capability of
// geometry — physics' sweeps, picking, lines of sight, sound's occlusion and a renderer's tracing stand on it).
//
//     const geom::Bvh tree = geom::bvh_of(boxes);                       // any things, by their boxes
//     geom::each_within(tree, region, [&](std::uint32_t item) { … });   // those whose box may touch `region`
//     const auto hit = geom::nearest(tree, ray, [&](std::uint32_t item) { return geom::meet(ray, shapes[item]); });
//
//     const geom::Bvh over = geom::bvh_of(mesh);                        // a mesh's triangles
//     const auto at = geom::meet(ray, mesh, over);                      // the triangle and how far
//
// The same things in the same order make the same tree, bit for bit, on every machine: a node is cut at the
// middle thing along its longest axis, by the things' centres and then their numbers, and a query walks it in a
// stated order — so what a ray meets first is the same answer everywhere, and of two things as near, the one with
// the lower number. A query allocates nothing. The form carries a version: a builder that makes better trees (by
// surface area) fills the same nodes.
export module mlibs.geom.api:bvh;

import std;
import mlibs.math;
import :solid;
import :mesh;

export namespace mlibs::geom {

[[nodiscard]] constexpr bool overlap(const Box3& one, const Box3& other) noexcept {
    return !one.empty() && !other.empty() && one.low.x <= other.high.x && other.low.x <= one.high.x && one.low.y <= other.high.y && other.low.y <= one.high.y &&
           one.low.z <= other.high.z && other.low.z <= one.high.z;
}

inline constexpr std::uint32_t bvh_version = 1;
struct Bvh {
    // A leaf (count > 0) holds `order[first … first + count)`; a node within (count == 0) has its two children at
    // `first` and `first + 1`. The root is node 0; of no things there are no nodes.
    struct Node {
        Box3          bounds;
        std::uint32_t first = 0, count = 0;
    };
    std::uint32_t              version = bvh_version;
    std::vector<Node>          nodes;
    std::vector<std::uint32_t> order;  // the things' numbers, leaf by leaf
    std::uint32_t              depth = 0;  // of its deepest leaf (the root alone: 1)
};
struct BvhOptions {
    std::uint32_t leaf = 4;  // things in a leaf, at most
};
[[nodiscard]] Bvh bvh_of(std::span<const Box3> boxes, const BvhOptions& options = {});
[[nodiscard]] Bvh bvh_of(const Mesh& mesh, const BvhOptions& options = {});
// A tree made a part at a time: when it is done it is the tree `bvh_of` makes of the same boxes, node for node —
// so a tree of many things can be made over several frames, the same on every machine (the work is counted in
// things looked at, not in time). The boxes are the caller's and stay as they are until it is done.
class BvhBuild {
public:
    BvhBuild() = default;
    explicit BvhBuild(std::span<const Box3> boxes, const BvhOptions& options = {}) { begin(boxes, options); }
    // Begins a tree over `boxes` (one being made is given up). `room`: a tree no longer needed, in whose room the
    // new one is made — of as many things, nothing is allocated.
    void begin(std::span<const Box3> boxes, const BvhOptions& options = {}, Bvh room = {});
    // Gives up the tree being made.
    void drop() noexcept { begun_ = false, waiting_.clear(); }
    // Makes nodes until `things` things have been looked at — one node at the least, and a node is never made in
    // two parts (the root looks at every thing). True when the tree is whole.
    bool step(std::size_t things);
    [[nodiscard]] bool begun() const noexcept { return begun_; }
    [[nodiscard]] bool done() const noexcept { return begun_ && waiting_.empty(); }
    // The tree — whole once done — leaving this as it was before it began.
    [[nodiscard]] Bvh take() noexcept;

private:
    struct Part {
        std::uint32_t node = 0, first = 0, count = 0, depth = 0;
    };
    std::span<const Box3> boxes_;
    std::uint32_t         leaf_ = 4;
    Bvh                   tree_;
    std::vector<Part>     waiting_;  // the nodes still to make, the next one last
    bool                  begun_ = false;
};
// What is wrong with a tree over these boxes, or nothing: every thing in one leaf once, each node's box holding
// what is under it, no leaf over the size, no node unreached.
[[nodiscard]] std::expected<void, std::string> check(const Bvh& tree, std::span<const Box3> boxes, const BvhOptions& options = {});

// The deepest a walk goes: a tree of 2^32 things cut at the middle is no deeper.
inline constexpr std::size_t bvh_stack = 64;

// Every thing in a leaf whose box touches `region`, in the tree's order (a thing's own box may still miss it:
// the caller's test).
template <class Visit>
requires std::invocable<Visit&, std::uint32_t>
void each_within(const Bvh& tree, const Box3& region, Visit&& visit) {
    if (tree.nodes.empty()) return;
    std::array<std::uint32_t, bvh_stack> stack{};
    std::size_t held = 0;
    stack[held++] = 0;
    while (held) {
        const Bvh::Node& node = tree.nodes[stack[--held]];
        if (!overlap(node.bounds, region)) continue;
        if (node.count) {
            for (std::uint32_t at = node.first; at < node.first + node.count; ++at) std::invoke(visit, tree.order[at]);
        } else if (held + 2 <= stack.size()) {
            stack[held++] = node.first + 1, stack[held++] = node.first;  // the first child first
        }
    }
}

struct RayHit {
    std::uint32_t item = 0;
    float         distance = 0;
};
// The thing a ray meets first, no further than `farthest`: `meet_item(item)` is how far along the ray it meets
// that thing, or nothing. Of two as near, the lower number.
template <class Meet>
requires std::invocable<Meet&, std::uint32_t>
[[nodiscard]] std::optional<RayHit> nearest(const Bvh& tree, const Ray& ray, Meet&& meet_item, float farthest = std::numeric_limits<float>::max()) {
    std::optional<RayHit> best;
    if (tree.nodes.empty()) return best;
    struct Waiting {
        std::uint32_t node;
        float         distance;
    };
    std::array<Waiting, bvh_stack> stack{};
    std::size_t held = 0;
    if (const auto root = meet(ray, tree.nodes[0].bounds); root && *root <= farthest) stack[held++] = {0, *root};
    while (held) {
        const Waiting next = stack[--held];
        if (best && next.distance > best->distance) continue;
        const Bvh::Node& node = tree.nodes[next.node];
        if (node.count) {
            for (std::uint32_t at = node.first; at < node.first + node.count; ++at) {
                const std::uint32_t item = tree.order[at];
                const std::optional<float> distance = std::invoke(meet_item, item);
                if (!distance || *distance > farthest) continue;
                if (!best || *distance < best->distance || (*distance == best->distance && item < best->item)) best = RayHit{item, *distance};
            }
            continue;
        }
        const auto first = meet(ray, tree.nodes[node.first].bounds), second = meet(ray, tree.nodes[node.first + 1].bounds);
        const bool take_first = first && *first <= farthest, take_second = second && *second <= farthest;
        if (held + 2 > stack.size()) continue;
        // The nearer child is walked first (it is pushed last); as near, the first.
        if (take_first && take_second) {
            const bool first_nearer = *first <= *second;
            stack[held++] = first_nearer ? Waiting{node.first + 1, *second} : Waiting{node.first, *first};
            stack[held++] = first_nearer ? Waiting{node.first, *first} : Waiting{node.first + 1, *second};
        } else if (take_first) {
            stack[held++] = {node.first, *first};
        } else if (take_second) {
            stack[held++] = {node.first + 1, *second};
        }
    }
    return best;
}

// The triangle of a mesh a ray meets first (its front or its back), through a tree made of that mesh.
[[nodiscard]] std::optional<RayHit> meet(const Ray& ray, const Mesh& mesh, const Bvh& tree, float farthest = std::numeric_limits<float>::max());

}  // namespace mlibs::geom
