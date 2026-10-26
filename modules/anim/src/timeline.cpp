module openmui.anim;

import std;

namespace omui::anim {

namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();

double local_length(const TimeMap& map, seconds natural) noexcept {
    if (map.length) return std::max(0.0, map.length->count());
    return std::max(0.0, natural.count() - map.trim.count());
}

bool fills_forwards(Fill f) noexcept { return f == Fill::forwards || f == Fill::both; }
bool fills_backwards(Fill f) noexcept { return f == Fill::backwards || f == Fill::both; }

}  // namespace

seconds mapped_length(const TimeMap& map, seconds natural) noexcept {
    double L = local_length(map, natural);
    double total = map.repeat == infinity ? infinity : L * std::max(0.0, map.repeat);
    double rate = map.rate > 0 ? map.rate : 1.0;
    return seconds(total / rate);
}

std::optional<seconds> map_time(const TimeMap& map, seconds start, seconds natural, seconds t) noexcept {
    double L = local_length(map, natural);
    double repeat = std::max(0.0, map.repeat);
    double total = repeat == infinity ? infinity : L * repeat;
    double rate = map.rate > 0 ? map.rate : 1.0;
    double u = (t - start).count() * rate;

    bool ended = false;
    if (u < 0.0) {
        if (!fills_backwards(map.fill)) return std::nullopt;
        u = 0.0;
    } else if (u >= total) {
        if (!fills_forwards(map.fill)) return std::nullopt;
        u = total;
        ended = true;
    }
    if (L <= 0.0) return map.trim;

    double iteration = 0;
    double within = 0;
    if (ended) {
        // The end of the last (possibly partial) iteration.
        iteration = std::ceil(repeat) - 1.0;
        if (iteration < 0) iteration = 0;
        within = total - iteration * L;
    } else {
        iteration = std::floor(u / L);
        within = u - iteration * L;
    }
    bool odd = std::fmod(iteration, 2.0) == 1.0;
    bool backwards = map.reverse != (map.alternate && odd);
    double local = backwards ? L - within : within;
    return seconds(map.trim.count() + local);
}

Timeline& Timeline::at(seconds start, std::string target, Clip clip, TimeMap map) {
    Entry e;
    e.target = std::move(target);
    e.start = start;
    e.map = map;
    e.natural = clip.length();
    e.clip = std::move(clip);
    entries_.push_back(std::move(e));
    return *this;
}

Timeline& Timeline::then(std::string target, Clip clip, seconds gap, TimeMap map) {
    seconds start = gap;
    if (!entries_.empty()) {
        const Entry& last = entries_.back();
        start = last.start + mapped_length(last.map, last.natural) + gap;
    }
    return at(start, std::move(target), std::move(clip), map);
}

Timeline& Timeline::with(std::string target, Clip clip, seconds offset, TimeMap map) {
    seconds start = entries_.empty() ? offset : entries_.back().start + offset;
    return at(start, std::move(target), std::move(clip), map);
}

Timeline& Timeline::stagger(seconds start, seconds step, std::span<const std::string> targets, const Clip& clip,
                            TimeMap map) {
    for (std::size_t i = 0; i < targets.size(); ++i)
        at(start + step * static_cast<double>(i), targets[i], clip, map);
    return *this;
}

Timeline& Timeline::nest(seconds start, Timeline child, std::string prefix, TimeMap map) {
    Entry e;
    e.target = std::move(prefix);
    e.start = start;
    e.map = map;
    e.natural = child.duration();
    // Not std::make_shared: under MinGW its type-tag object, instantiated in a module
    // unit, collides with the one in libstdc++.a at link time.
    e.child = std::shared_ptr<const Timeline>(new Timeline(std::move(child)));
    entries_.push_back(std::move(e));
    return *this;
}

Timeline& Timeline::marker(seconds t, std::string name) {
    Marker m{t, std::move(name)};
    auto it = std::ranges::upper_bound(markers_, t, {}, &Marker::time);
    markers_.insert(it, std::move(m));
    return *this;
}

seconds Timeline::duration() const noexcept {
    double end = 0;
    for (const Entry& e : entries_) end = std::max(end, (e.start + mapped_length(e.map, e.natural)).count());
    for (const Marker& m : markers_) end = std::max(end, m.time.count());
    return seconds(end);
}

std::optional<seconds> Timeline::marker_time(std::string_view name) const noexcept {
    for (const Marker& m : markers_)
        if (m.name == name) return m.time;
    return std::nullopt;
}

std::vector<Marker> Timeline::all_markers() const {
    std::vector<Marker> out(markers_.begin(), markers_.end());
    for (const Entry& e : entries_) {
        if (!e.child) continue;
        double rate = e.map.rate > 0 ? e.map.rate : 1.0;
        for (Marker m : e.child->all_markers()) {
            // A nested marker in parent time: only the first iteration, played forwards.
            double local = m.time.count() - e.map.trim.count();
            if (local < 0) continue;
            m.time = e.start + seconds(local / rate);
            m.name = e.target + m.name;
            out.push_back(std::move(m));
        }
    }
    std::ranges::stable_sort(out, {}, &Marker::time);
    return out;
}

void Timeline::visit(seconds t, Thunk fn, void* ctx) const {
    std::string prefix;
    visit_prefixed(t, fn, ctx, prefix);
}

void Timeline::visit_prefixed(seconds t, Thunk fn, void* ctx, std::string& prefix) const {
    for (const Entry& e : entries_) {
        auto local = map_time(e.map, e.start, e.natural, t);
        if (!local) continue;
        std::size_t keep = prefix.size();
        if (e.child) {
            prefix += e.target;
            e.child->visit_prefixed(*local, fn, ctx, prefix);
            prefix.resize(keep);
            continue;
        }
        Value v = e.clip->sample(*local);
        if (keep == 0) {
            fn(ctx, e.target, v);
        } else {
            prefix += e.target;
            fn(ctx, prefix, v);
            prefix.resize(keep);
        }
    }
}

std::vector<std::pair<std::string, Value>> Timeline::sample(seconds t) const {
    std::vector<std::pair<std::string, Value>> out;
    sample(t, [&](std::string_view target, const Value& v) { out.emplace_back(std::string(target), v); });
    return out;
}

}  // namespace omui::anim
