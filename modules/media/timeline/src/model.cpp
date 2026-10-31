module openmui.timeline;

import std;
import hanim;

namespace omui::timeline {

Flicks from_seconds(double s) noexcept { return static_cast<Flicks>(std::llround(s * static_cast<double>(flicks_per_second))); }

Flicks Clip::source_at(Flicks t) const noexcept {
    const Flicks local = t - at;
    if (speed == 1) return in + local;  // the usual case, exact in integers
    return in + static_cast<Flicks>(std::llround(static_cast<double>(local) * speed));
}

float Clip::level_at(Flicks t) const noexcept {
    double k = 1;
    const Flicks local = t - at, left = end() - t;
    if (fade.in > 0 && local < fade.in) k *= fade.ease(std::clamp(static_cast<double>(local) / static_cast<double>(fade.in), 0.0, 1.0));
    if (fade.out > 0 && left < fade.out) k *= fade.ease(std::clamp(static_cast<double>(left) / static_cast<double>(fade.out), 0.0, 1.0));
    return static_cast<float>(level * k);
}

double Clip::value(std::string_view property, Flicks t, double otherwise) const {
    for (const Property& p : properties)
        if (p.name == property) return p.keys.empty() ? otherwise : p.keys.value(hanim::seconds(to_seconds(t - at)));
    return otherwise;
}

Flicks Timeline::duration() const noexcept {
    Flicks d = 0;
    for (const Track& track : tracks)
        if (!track.clips.empty()) d = std::max(d, track.clips.back().end());
    for (const Marker& m : markers) d = std::max(d, m.at);
    return d;
}

std::optional<Flicks> Timeline::marker(std::string_view name) const noexcept {
    for (const Marker& m : markers)
        if (m.name == name) return m.at;
    return std::nullopt;
}

std::vector<std::string> check(const Timeline& timeline) {
    std::vector<std::string> out;
    if (!exact(timeline.rate))
        out.push_back(std::format("the rate {}/{} is not a whole number of flicks a frame", timeline.rate.num, timeline.rate.den));
    if (!exact_samples(timeline.sample_rate)) out.push_back(std::format("the sample rate {} is not a whole number of flicks a sample", timeline.sample_rate));
    for (const Track& track : timeline.tracks)
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            const Clip& c = track.clips[i];
            const std::string where = std::format("track `{}`, clip `{}`", track.name, c.name);
            if (c.length <= 0) out.push_back(where + ": no length");
            if (c.at < 0) out.push_back(where + ": starts before the timeline");
            if (c.speed == 0 || !std::isfinite(c.speed)) out.push_back(where + ": a speed of 0 (or not a number)");
            if (c.fade.in < 0 || c.fade.out < 0 || c.fade.in + c.fade.out > c.length) out.push_back(where + ": fades longer than the clip");
            if (i > 0 && c.at < track.clips[i - 1].end())
                out.push_back(std::format("{}: overlaps `{}` (or comes before it)", where, track.clips[i - 1].name));
        }
    for (std::size_t i = 1; i < timeline.markers.size(); ++i)
        if (timeline.markers[i].at < timeline.markers[i - 1].at) out.push_back(std::format("marker `{}` is out of order", timeline.markers[i].name));
    return out;
}

void playing(const Timeline& timeline, Flicks t, std::vector<Playing>& out) {
    out.clear();
    for (std::size_t k = 0; k < timeline.tracks.size(); ++k) {
        const Track& track = timeline.tracks[k];
        if (track.muted) continue;
        // The last clip starting at or before t; it covers t, or none does.
        const auto it = std::ranges::upper_bound(track.clips, t, {}, &Clip::at);
        if (it == track.clips.begin()) continue;
        const Clip& c = *(it - 1);
        if (!c.covers(t)) continue;
        out.push_back({k, static_cast<std::size_t>(it - 1 - track.clips.begin()), c.source_at(t), c.level_at(t)});
    }
}

}  // namespace omui::timeline
