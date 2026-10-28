module openmui.ui.canvas;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;

namespace omui::world {

namespace {

// A node spanning more cells than this lives in the large list.
constexpr std::int64_t max_cells = 64;

std::int64_t cell_key(std::int64_t cx, std::int64_t cy) noexcept {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cy) & 0xffffffffull));
}

std::int64_t cell_of(double v, double cell) noexcept { return static_cast<std::int64_t>(std::floor(v / cell)); }

}  // namespace

WorldRect WorldRect::united(const WorldRect& o) const noexcept {
    if (empty()) return o;
    if (o.empty()) return *this;
    const double l = std::min(x, o.x), t = std::min(y, o.y), r = std::max(right(), o.right()), b = std::max(bottom(), o.bottom());
    return {l, t, r - l, b - t};
}

Scene::Scene() : Scene(512) {}

void Scene::changed() {
    ++revision_;
    for (auto& [token, fn] : observers_) fn();
}

std::uint64_t Scene::observe(std::function<void()> fn) {
    observers_.emplace_back(next_observer_, std::move(fn));
    return next_observer_++;
}

void Scene::unobserve(std::uint64_t token) { std::erase_if(observers_, [&](const auto& o) { return o.first == token; }); }

Scene::Scene(double cell) : cell_(cell > 0 ? cell : 512) { kind("box", Kind{}); }

std::uint16_t Scene::kind(std::string_view name, Kind k) {
    if (k.tiers.empty()) k.tiers.push_back({0, Tier::Draw::box});
    std::ranges::sort(k.tiers, {}, &Tier::from);
    changed();
    if (auto i = find_kind(name)) {
        kinds_[*i] = std::move(k);
        return *i;
    }
    kind_names_.emplace_back(name);
    kinds_.push_back(std::move(k));
    return static_cast<std::uint16_t>(kinds_.size() - 1);
}

std::optional<std::uint16_t> Scene::find_kind(std::string_view name) const {
    for (std::size_t i = 0; i < kind_names_.size(); ++i)
        if (kind_names_[i] == name) return static_cast<std::uint16_t>(i);
    return std::nullopt;
}

const Kind& Scene::kind_of(NodeRef n) const { return kinds_[kind_[at(n)]]; }
std::string_view Scene::kind_name(NodeRef n) const { return kind_names_[kind_[at(n)]]; }

std::uint32_t Scene::at(NodeRef n) const {
    if (!valid(n)) fatal("world::Scene: a stale node (a NodeRef used after its node was removed)");
    return n.index;
}

bool Scene::valid(NodeRef n) const noexcept {
    return n.index < alive_.size() && alive_[n.index] && generation_[n.index] == n.generation;
}

NodeRef Scene::add(const NodeDesc& d) {
    // A kind not registered yet is registered as a box, by its name: what
    // reads the kind (hits, behaviours, find) sees the name, and registering
    // it later gives these nodes its look.
    const std::string_view kind_name = d.kind.empty() ? std::string_view("box") : d.kind;
    const auto found = find_kind(kind_name);
    const std::uint16_t k = found ? *found : kind(kind_name, Kind{});
    std::uint32_t i;
    if (!free_.empty()) {
        i = free_.back();
        free_.pop_back();
    } else {
        i = static_cast<std::uint32_t>(x_.size());
        x_.push_back(0), y_.push_back(0), w_.push_back(0), h_.push_back(0), radius_.push_back(0);
        fill_.push_back({}), kind_.push_back(0), generation_.push_back(0), z_.push_back(0), alive_.push_back(false), key_.emplace_back();
        content_.push_back(0);
        in_large_.push_back(false);
        stamp_.push_back(0);
        for (auto& [name, col] : numbers_) col.push_back(0);
        for (auto& [name, col] : texts_) col.emplace_back();
    }
    x_[i] = d.x, y_[i] = d.y, w_[i] = std::max(0.0f, d.width), h_[i] = std::max(0.0f, d.height);
    fill_[i] = d.fill, radius_[i] = d.radius, kind_[i] = k, z_[i] = next_z_++, alive_[i] = true;
    ++content_[i];  // a new node in an old slot: none of the old one's textures
    key_[i] = d.key.empty() ? std::format("n{}", i) : std::string(d.key);
    if (auto old = keys_.find(key_[i]); old != keys_.end()) {
        // A key names one node: the old one goes.
        const NodeRef gone{old->second, generation_[old->second]};
        remove(gone);
    }
    keys_[key_[i]] = i;
    for (auto& [name, col] : numbers_) col[i] = 0;
    for (auto& [name, col] : texts_) col[i].clear();
    ++live_;
    const bool kept = bounds_revision_ == revision_;  // a node more only grows them
    changed();
    index(i);
    if (kept) keep_bounds(bounds_.united({x_[i], y_[i], w_[i], h_[i]}));
    return {i, generation_[i]};
}

std::vector<NodeRef> Scene::push(std::span<const double> x, std::span<const double> y, std::span<const float> width,
                                 std::span<const float> height, std::span<const std::string> kinds, std::span<const std::string> keys) {
    const std::size_t n = std::min({x.size(), y.size(), width.size(), height.size()});
    std::vector<NodeRef> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        NodeDesc d;
        d.kind = kinds.empty() ? std::string_view("box") : kinds.size() == 1 ? std::string_view(kinds[0]) : std::string_view(kinds[i]);
        d.x = x[i], d.y = y[i], d.width = width[i], d.height = height[i];
        if (keys.size() == n) d.key = keys[i];
        out.push_back(add(d));
    }
    return out;
}

bool Scene::remove(NodeRef n) {
    if (!valid(n)) return false;
    const std::uint32_t i = n.index;
    const bool kept = bounds_revision_ == revision_ && interior({x_[i], y_[i], w_[i], h_[i]});
    std::erase_if(edges_, [&](const Edge& e) { return e.from == key_[i] || e.to == key_[i]; });
    unindex(i);
    keys_.erase(key_[i]);
    alive_[i] = false;
    ++generation_[i];
    free_.push_back(i);
    --live_;
    changed();
    if (kept) keep_bounds(bounds_);
    return true;
}

void Scene::clear() {
    for (std::uint32_t i = 0; i < alive_.size(); ++i)
        if (alive_[i]) remove({i, generation_[i]});
}

void Scene::move(NodeRef n, double x, double y) {
    const std::uint32_t i = at(n);
    if (x_[i] == x && y_[i] == y) return;
    const bool kept = bounds_revision_ == revision_ && interior({x_[i], y_[i], w_[i], h_[i]});
    unindex(i);
    x_[i] = x, y_[i] = y;
    index(i);
    changed();
    if (kept) keep_bounds(bounds_.united({x_[i], y_[i], w_[i], h_[i]}));
}

void Scene::resize(NodeRef n, float width, float height) {
    const std::uint32_t i = at(n);
    const bool kept = bounds_revision_ == revision_ && interior({x_[i], y_[i], w_[i], h_[i]});
    unindex(i);
    w_[i] = std::max(0.0f, width), h_[i] = std::max(0.0f, height);
    index(i);
    changed();
    if (kept) keep_bounds(bounds_.united({x_[i], y_[i], w_[i], h_[i]}));
}

void Scene::set_fill(NodeRef n, Color fill, float radius) {
    const std::uint32_t i = at(n);
    fill_[i] = fill, radius_[i] = radius;
    changed();
}

void Scene::touch(NodeRef n) {
    ++content_[at(n)];
    changed();
}

std::uint64_t Scene::content_revision(NodeRef n) const { return content_[at(n)] ^ (static_cast<std::uint64_t>(n.index) << 40); }

void Scene::raise(NodeRef n) {
    z_[at(n)] = next_z_++;
    changed();
}

WorldRect Scene::rect(NodeRef n) const {
    const std::uint32_t i = at(n);
    return {x_[i], y_[i], w_[i], h_[i]};
}
Color Scene::fill(NodeRef n) const { return fill_[at(n)]; }
float Scene::radius(NodeRef n) const { return radius_[at(n)]; }
std::string_view Scene::key(NodeRef n) const { return key_[at(n)]; }

std::optional<NodeRef> Scene::find(std::string_view key) const {
    const auto it = keys_.find(std::string(key));
    if (it == keys_.end()) return std::nullopt;
    return NodeRef{it->second, generation_[it->second]};
}

// The bounds are kept across changes when they can be: a node that comes,
// or one that moves or changes size or goes from strictly inside them, does
// not need every node visited again; one that held an edge does (the next
// bounds() finds them). A drag inside a large scene costs nothing here.
WorldRect Scene::bounds() const {
    if (bounds_revision_ == revision_) return bounds_;
    WorldRect r;
    for (std::uint32_t i = 0; i < alive_.size(); ++i)
        if (alive_[i]) r = r.united({x_[i], y_[i], w_[i], h_[i]});
    bounds_ = r;
    bounds_revision_ = revision_;
    return r;
}

bool Scene::interior(const WorldRect& r) const noexcept {
    return !r.empty() && r.x > bounds_.x && r.y > bounds_.y && r.right() < bounds_.right() && r.bottom() < bounds_.bottom();
}

void Scene::keep_bounds(const WorldRect& b) {
    bounds_ = b;
    bounds_revision_ = revision_;
}

void Scene::set(NodeRef n, std::string_view column, double value) {
    const std::uint32_t i = at(n);
    auto [it, made] = numbers_.try_emplace(std::string(column));
    if (made) it->second.assign(x_.size(), 0);
    it->second[i] = value;
    changed();
}

void Scene::set(NodeRef n, std::string_view column, std::string value) {
    const std::uint32_t i = at(n);
    auto [it, made] = texts_.try_emplace(std::string(column));
    if (made) it->second.assign(x_.size(), std::string{});
    it->second[i] = std::move(value);
    changed();
}

double Scene::number(NodeRef n, std::string_view column, double fallback) const {
    const std::uint32_t i = at(n);
    const auto it = numbers_.find(std::string(column));
    return it == numbers_.end() ? fallback : it->second[i];
}

std::string_view Scene::text(NodeRef n, std::string_view column) const {
    const std::uint32_t i = at(n);
    const auto it = texts_.find(std::string(column));
    return it == texts_.end() ? std::string_view{} : std::string_view(it->second[i]);
}

// ── the grid ─────────────────────────────────────────────────────────────

void Scene::index(std::uint32_t i) {
    const std::int64_t x0 = cell_of(x_[i], cell_), y0 = cell_of(y_[i], cell_);
    const std::int64_t x1 = cell_of(x_[i] + w_[i], cell_), y1 = cell_of(y_[i] + h_[i], cell_);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > max_cells) {
        large_.push_back(i);
        in_large_[i] = true;
        return;
    }
    for (std::int64_t cy = y0; cy <= y1; ++cy)
        for (std::int64_t cx = x0; cx <= x1; ++cx) cells_[cell_key(cx, cy)].push_back(i);
}

void Scene::unindex(std::uint32_t i) {
    if (in_large_[i]) {
        std::erase(large_, i);
        in_large_[i] = false;
        return;
    }
    const std::int64_t x0 = cell_of(x_[i], cell_), y0 = cell_of(y_[i], cell_);
    const std::int64_t x1 = cell_of(x_[i] + w_[i], cell_), y1 = cell_of(y_[i] + h_[i], cell_);
    for (std::int64_t cy = y0; cy <= y1; ++cy)
        for (std::int64_t cx = x0; cx <= x1; ++cx) {
            auto it = cells_.find(cell_key(cx, cy));
            if (it == cells_.end()) continue;
            std::erase(it->second, i);
            if (it->second.empty()) cells_.erase(it);
        }
}

void Scene::query(const WorldRect& r, std::vector<NodeRef>& out) const {
    out.clear();
    if (r.empty() || live_ == 0) return;
    if (++query_ == 0) {
        std::ranges::fill(stamp_, 0);
        query_ = 1;
    }
    std::vector<std::uint32_t> found;
    auto take = [&](std::uint32_t i) {
        if (stamp_[i] == query_ || !alive_[i]) return;
        stamp_[i] = query_;
        if (r.overlaps({x_[i], y_[i], w_[i], h_[i]})) found.push_back(i);
    };
    const std::int64_t x0 = cell_of(r.x, cell_), y0 = cell_of(r.y, cell_), x1 = cell_of(r.right(), cell_), y1 = cell_of(r.bottom(), cell_);
    // A query wider than the occupied cells walks the cells it has; one
    // smaller walks its own: either way, work in proportion to what it finds.
    const double span = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1);
    if (span > static_cast<double>(cells_.size())) {
        for (const auto& [key, nodes] : cells_)
            for (std::uint32_t i : nodes) take(i);
    } else {
        for (std::int64_t cy = y0; cy <= y1; ++cy)
            for (std::int64_t cx = x0; cx <= x1; ++cx)
                if (auto it = cells_.find(cell_key(cx, cy)); it != cells_.end())
                    for (std::uint32_t i : it->second) take(i);
    }
    for (std::uint32_t i : large_) take(i);
    std::ranges::sort(found, [&](std::uint32_t a, std::uint32_t b) { return z_[a] < z_[b]; });
    out.reserve(found.size());
    for (std::uint32_t i : found) out.push_back({i, generation_[i]});
}

std::optional<Hit> Scene::hit(double x, double y) const {
    std::vector<NodeRef> under;
    query({x, y, 1e-9, 1e-9}, under);
    for (auto it = under.rbegin(); it != under.rend(); ++it) {
        const WorldRect r = rect(*it);
        if (!r.contains(x, y)) continue;
        const Kind& k = kind_of(*it);
        return Hit{*it, k.title_bar > 0 && y < r.y + k.title_bar};
    }
    return std::nullopt;
}

void Scene::each(const std::function<void(NodeRef)>& f) const {
    std::vector<std::uint32_t> order;
    for (std::uint32_t i = 0; i < alive_.size(); ++i)
        if (alive_[i]) order.push_back(i);
    std::ranges::sort(order, [&](std::uint32_t a, std::uint32_t b) { return z_[a] < z_[b]; });
    for (std::uint32_t i : order) f({i, generation_[i]});
}

void Scene::each_unordered(const std::function<void(NodeRef)>& f) const {
    for (std::uint32_t i = 0; i < alive_.size(); ++i)
        if (alive_[i]) f({i, generation_[i]});
}

std::string Scene::connect(Edge e) {
    if (e.key.empty()) e.key = std::format("e{}", next_edge_++);
    std::erase_if(edges_, [&](const Edge& x) { return x.key == e.key; });
    std::string key = e.key;
    edges_.push_back(std::move(e));
    changed();
    return key;
}

bool Scene::disconnect(std::string_view key) {
    const auto n = std::erase_if(edges_, [&](const Edge& x) { return x.key == key; });
    if (n) changed();
    return n > 0;
}

std::vector<const Edge*> Scene::edges_of(std::string_view node_key) const {
    std::vector<const Edge*> out;
    for (const Edge& e : edges_)
        if (e.from == node_key || e.to == node_key) out.push_back(&e);
    return out;
}

void Scene::preset(std::string name, Preset p) {
    presets_[std::move(name)] = p;
    changed();
}

std::optional<Scene::Preset> Scene::preset(std::string_view name) const {
    const auto it = presets_.find(name);
    if (it == presets_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> Scene::presets() const {
    std::vector<std::string> out;
    for (const auto& [name, p] : presets_) out.push_back(name);
    return out;
}

// ── scenes by name ───────────────────────────────────────────────────────

namespace {
std::map<std::string, std::unique_ptr<Scene>, std::less<>>& registry() {
    static std::map<std::string, std::unique_ptr<Scene>, std::less<>> r;
    return r;
}
}  // namespace

Scene& scene(std::string_view name) {
    auto& r = registry();
    auto it = r.find(name);
    if (it == r.end()) it = r.emplace(std::string(name), std::make_unique<Scene>()).first;
    return *it->second;
}

Scene* find_scene(std::string_view name) {
    auto& r = registry();
    const auto it = r.find(name);
    return it == r.end() ? nullptr : it->second.get();
}

void drop_scene(std::string_view name) {
    auto& r = registry();
    if (auto it = r.find(name); it != r.end()) r.erase(it);
}

}  // namespace omui::world
