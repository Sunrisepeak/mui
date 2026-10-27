module openmui.ui.canvas;

import std;
import openmui.core;
import openmui.geom;
import openmui.raster;
import openmui.render;
import openmui.text;
import openmui.trace;

namespace omui::world {

// A raster that came back.
struct Textures::Done {
    std::string                      key;
    std::uint64_t                    revision;
    int                              level;
    std::uint64_t                    epoch;
    std::shared_ptr<raster::Surface> surface;
};

// Where jobs put what they made; shared with them, so a job that outlives
// the cache drops its result instead of touching it.
struct Textures::Inbox {
    std::mutex            lock;
    std::vector<Done>     done;
    std::atomic<bool>     closed = false;
    std::atomic<std::uint64_t> epoch = 0;
};

namespace {

// The images a display list draws, by reference.
void images_of(const render::DisplayList& dl, std::vector<render::ImageRef>& out) {
    for (const render::Command& c : dl.commands())
        std::visit(
            [&](const auto& cmd) {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::same_as<T, render::DrawImage>) out.push_back(cmd.image);
                if constexpr (requires { cmd.paint; })
                    if (cmd.paint.kind == render::Paint::Kind::image) out.push_back(cmd.paint.image);
            },
            c);
}

// A glyph cache per worker thread and font collection: glyph caches are not
// shared between threads; the collection's fonts are.
render::GlyphSource* glyphs_for(typeset::FontCollection* fonts) {
    if (!fonts) return nullptr;
    thread_local std::map<typeset::FontCollection*, std::unique_ptr<typeset::GlyphCache>> caches;
    auto& c = caches[fonts];
    if (!c) c = std::make_unique<typeset::GlyphCache>(*fonts);
    return c.get();
}

}  // namespace

Textures::Textures() : inbox_(std::make_shared<Inbox>()) {}

Textures::~Textures() { inbox_->closed = true; }

int Textures::level_for(double texels_per_unit) noexcept {
    if (!(texels_per_unit > 0)) return -8;
    return std::clamp(static_cast<int>(std::ceil(std::log2(texels_per_unit) - 1e-9)), -8, 2);
}

std::optional<Textures::Pick> Textures::pick(std::string_view key, std::uint64_t revision, int level, std::uint64_t frame) {
    auto it = entries_.find(std::string(key));
    if (it == entries_.end() || it->second.levels.empty()) return std::nullopt;
    Entry& e = it->second;
    const bool current = e.revision == revision;
    // The wanted level, then the sharper ones, then the blurrier.
    auto use = [&](Level& l, int at) {
        l.used = frame;
        return Pick{l.image, at, current && at == level};
    };
    if (auto exact = e.levels.find(level); exact != e.levels.end()) return use(exact->second, level);
    if (auto sharper = e.levels.upper_bound(level); sharper != e.levels.end()) return use(sharper->second, sharper->first);
    auto blurrier = std::prev(e.levels.end());
    return use(blurrier->second, blurrier->first);
}

void Textures::want(std::string_view key, std::uint64_t revision, int level, NodeRef node, float priority) {
    if (in_flight_.contains({std::string(key), revision, level})) return;
    candidates_.push_back({std::string(key), revision, level, node, priority});
}

void Textures::issue(const Scene& scene, const render::ImageStore& images, typeset::FontCollection* fonts) {
    const std::size_t limit = options_.jobs ? options_.jobs : 2 * std::max(1u, Tasks::shared().threads());
    std::ranges::sort(candidates_, std::greater{}, &Candidate::priority);
    for (Candidate& c : candidates_) {
        if (in_flight_.size() >= limit) break;
        if (!scene.valid(c.node)) continue;
        const Kind& k = scene.kind_of(c.node);
        if (!k.content) continue;
        const WorldRect r = scene.rect(c.node);
        if (r.empty()) continue;
        // The texture: 2^level texels a world unit, its largest side capped.
        double s = std::ldexp(1.0, c.level);
        const double largest = std::max(r.width, r.height) * s;
        if (largest > options_.max_side) s *= options_.max_side / largest;
        const int pw = std::max(1, static_cast<int>(std::ceil(r.width * s))), ph = std::max(1, static_cast<int>(std::ceil(r.height * s)));
        // Built here, on the scene's thread; drawn on the pool.
        auto dl = std::make_shared<render::DisplayList>();
        k.content(scene, c.node, *dl);
        auto snapshot = std::make_shared<render::ImageStore>();
        std::vector<render::ImageRef> used;
        images_of(*dl, used);
        for (render::ImageRef i : used) snapshot->put(i, images.shared(i));
        in_flight_.insert({c.key, c.revision, c.level});
        const float sx = static_cast<float>(pw / r.width), sy = static_cast<float>(ph / r.height);
        Tasks::shared().spawn([inbox = inbox_, dl, snapshot, fonts, key = c.key, rev = c.revision, level = c.level, epoch = inbox_->epoch.load(), pw, ph, sx,
                               sy](std::stop_token stop) {
            if (inbox->closed || stop.stop_requested()) return;
            trace::Span span("canvas.texture", "canvas");
            auto surface = std::make_shared<raster::Surface>(pw, ph);
            render::CpuRenderer r;
            render::CpuOptions o;
            o.transform = geom::Affine::scale(sx, sy);
            o.background = Color::transparent();
            (void)r.render(*dl, *surface, {snapshot.get(), glyphs_for(fonts)}, o);
            std::scoped_lock g(inbox->lock);
            if (!inbox->closed) inbox->done.push_back({key, rev, level, epoch, std::move(surface)});
        });
    }
    candidates_.clear();
}

bool Textures::collect(render::ImageStore& images, std::uint64_t frame) {
    std::vector<Done> done;
    {
        std::scoped_lock g(inbox_->lock);
        done.swap(inbox_->done);
    }
    last_.entered = last_.evicted = 0;
    bool changed = false;
    std::size_t entered = 0;
    std::vector<Done> later;
    for (Done& d : done) {
        if (d.epoch != inbox_->epoch) continue;  // from before clear()
        const std::size_t bytes = static_cast<std::size_t>(d.surface->width()) * static_cast<std::size_t>(d.surface->height()) * 4;
        // The frame's budget of bytes entering the store (a GPU renderer uploads each once).
        if (entered > 0 && entered + bytes > options_.upload) {
            later.push_back(std::move(d));
            continue;
        }
        in_flight_.erase({d.key, d.revision, d.level});
        Entry& e = entries_[d.key];
        if (e.revision != d.revision) {
            // A newer content: the old textures go.
            for (auto& [lv, l] : e.levels) {
                images.remove(l.image);
                bytes_ -= l.bytes;
            }
            e.levels.clear();
            e.revision = d.revision;
        }
        if (auto old = e.levels.find(d.level); old != e.levels.end()) {
            images.remove(old->second.image);
            bytes_ -= old->second.bytes;
        }
        e.levels[d.level] = {images.add(std::shared_ptr<const raster::Surface>(std::move(d.surface))), bytes, frame};
        bytes_ += bytes;
        entered += bytes;
        ++last_.entered;
        changed = true;
    }
    if (!later.empty()) {
        std::scoped_lock g(inbox_->lock);
        inbox_->done.insert(inbox_->done.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
    }
    // Within the budget: what was drawn longest ago goes first, never what this frame drew.
    while (bytes_ > options_.budget) {
        Entry* oldest_entry = nullptr;
        int oldest_level = 0;
        std::uint64_t oldest = frame;
        for (auto& [key, e] : entries_)
            for (auto& [lv, l] : e.levels)
                if (l.used < oldest) {
                    oldest = l.used;
                    oldest_entry = &e;
                    oldest_level = lv;
                }
        if (!oldest_entry) break;
        const Level l = oldest_entry->levels[oldest_level];
        images.remove(l.image);
        bytes_ -= l.bytes;
        oldest_entry->levels.erase(oldest_level);
        ++last_.evicted;
    }
    std::erase_if(entries_, [](const auto& kv) { return kv.second.levels.empty(); });
    return changed;
}

void Textures::clear(render::ImageStore& images) {
    ++inbox_->epoch;
    for (auto& [key, e] : entries_)
        for (auto& [lv, l] : e.levels) images.remove(l.image);
    entries_.clear();
    in_flight_.clear();
    candidates_.clear();
    bytes_ = 0;
    std::scoped_lock g(inbox_->lock);
    inbox_->done.clear();
}

bool Textures::busy() const {
    if (!in_flight_.empty()) return true;
    std::scoped_lock g(inbox_->lock);
    return !inbox_->done.empty();
}

Textures::Stats Textures::stats() const {
    Stats s = last_;
    s.bytes = bytes_;
    s.jobs = in_flight_.size();
    for (const auto& [key, e] : entries_) s.textures += e.levels.size();
    std::scoped_lock g(inbox_->lock);
    s.waiting = inbox_->done.size();
    return s;
}

}  // namespace omui::world
