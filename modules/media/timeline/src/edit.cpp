module openmui.timeline;

import std;
import hanim;

namespace omui::timeline {

namespace {

// Whether `c` fits at `index` on `track` between its neighbours (`skip`: the
// clip it replaces, not a neighbour).
std::optional<std::string> fits(const Track& track, std::size_t index, const Clip& c, bool replacing) {
    if (c.length <= 0) return std::format("clip `{}` has no length", c.name);
    if (c.at < 0) return std::format("clip `{}` starts before the timeline", c.name);
    if (c.speed == 0) return std::format("clip `{}` has a speed of 0", c.name);
    if (index > 0 && track.clips[index - 1].end() > c.at)
        return std::format("clip `{}` would overlap `{}`", c.name, track.clips[index - 1].name);
    const std::size_t next = replacing ? index + 1 : index;
    if (next < track.clips.size() && c.end() > track.clips[next].at)
        return std::format("clip `{}` would overlap `{}`", c.name, track.clips[next].name);
    return std::nullopt;
}

// The step that undoes `s`, given what `s` replaced or removed.
Step inverse(const Step& s, Clip before) {
    switch (s.kind) {
        case Step::Kind::insert: return {Step::Kind::remove, s.track, s.index, {}};
        case Step::Kind::remove: return {Step::Kind::insert, s.track, s.index, std::move(before)};
        case Step::Kind::replace: return {Step::Kind::replace, s.track, s.index, std::move(before)};
    }
    return s;
}

// One step, or why not (the timeline untouched); fills `undo` with its inverse.
std::optional<std::string> step(Timeline& t, const Step& s, Step& undo) {
    if (s.track >= t.tracks.size()) return std::format("there is no track {}", s.track);
    Track& track = t.tracks[s.track];
    switch (s.kind) {
        case Step::Kind::insert:
            if (s.index > track.clips.size()) return std::format("track `{}` has no place {}", track.name, s.index);
            if (auto why = fits(track, s.index, s.clip, false)) return why;
            track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(s.index), s.clip);
            undo = inverse(s, {});
            return std::nullopt;
        case Step::Kind::remove: {
            if (s.index >= track.clips.size()) return std::format("track `{}` has no clip {}", track.name, s.index);
            Clip before = std::move(track.clips[s.index]);
            track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(s.index));
            undo = inverse(s, std::move(before));
            return std::nullopt;
        }
        case Step::Kind::replace: {
            if (s.index >= track.clips.size()) return std::format("track `{}` has no clip {}", track.name, s.index);
            if (auto why = fits(track, s.index, s.clip, true)) return why;
            Clip before = std::exchange(track.clips[s.index], s.clip);
            undo = inverse(s, std::move(before));
            return std::nullopt;
        }
    }
    return std::string("an unknown step");
}

// Where a clip starting at `at` goes on a track (after every clip starting before it).
std::size_t place(const Track& track, Flicks at) {
    return static_cast<std::size_t>(std::ranges::upper_bound(track.clips, at, {}, &Clip::at) - track.clips.begin());
}

std::expected<const Clip*, std::string> clip_of(const Timeline& t, std::size_t track, std::size_t index) {
    if (track >= t.tracks.size()) return std::unexpected(std::format("there is no track {}", track));
    if (index >= t.tracks[track].clips.size()) return std::unexpected(std::format("track `{}` has no clip {}", t.tracks[track].name, index));
    return &t.tracks[track].clips[index];
}

// A property's keys moved by `by` (the clip's own time starting later: its keys earlier).
hanim::Track<double> shifted(const hanim::Track<double>& keys, Flicks by) {
    hanim::Track<double> out;
    const hanim::seconds d(to_seconds(by));
    for (const auto& k : keys.keys()) {
        if (k.hold) out.hold(k.time - d, k.value);
        else out.key(k.time - d, k.value, k.ease);
    }
    return out;
}

// `c` starting `by` later in its own time: its source and its keys follow.
void advance(Clip& c, Flicks by) {
    c.in = c.source_at(c.at + by);
    for (Property& p : c.properties) p.keys = shifted(p.keys, by);
}

std::string when(Flicks t) { return std::format("{}s", to_seconds(t)); }

}  // namespace

std::expected<Change, std::string> apply(Timeline& timeline, const Change& change) {
    Change undo{change.what, {}};
    undo.steps.reserve(change.steps.size());
    for (const Step& s : change.steps) {
        Step back;
        if (auto why = step(timeline, s, back)) {
            // Not made at all: what was done, undone (its inverses always apply).
            for (auto it = undo.steps.rbegin(); it != undo.steps.rend(); ++it) {
                Step ignored;
                (void)step(timeline, *it, ignored);
            }
            return std::unexpected(std::format("{}: {}", change.what.empty() ? "the change" : change.what, *why));
        }
        undo.steps.push_back(std::move(back));
    }
    std::ranges::reverse(undo.steps);
    return undo;
}

std::expected<Change, std::string> insert(const Timeline& timeline, std::size_t track, Clip clip) {
    if (track >= timeline.tracks.size()) return std::unexpected(std::format("there is no track {}", track));
    const std::size_t at = place(timeline.tracks[track], clip.at);
    std::string what = std::format("insert `{}` at {}", clip.name, when(clip.at));
    return Change{std::move(what), {{Step::Kind::insert, track, at, std::move(clip)}}};
}

std::expected<Change, std::string> remove(const Timeline& timeline, std::size_t track, std::size_t index) {
    auto c = clip_of(timeline, track, index);
    if (!c) return std::unexpected(c.error());
    return Change{std::format("remove `{}`", (*c)->name), {{Step::Kind::remove, track, index, {}}}};
}

std::expected<Change, std::string> ripple_remove(const Timeline& timeline, std::size_t track, std::size_t index) {
    auto c = clip_of(timeline, track, index);
    if (!c) return std::unexpected(c.error());
    Change change{std::format("ripple remove `{}`", (*c)->name), {{Step::Kind::remove, track, index, {}}}};
    const auto& clips = timeline.tracks[track].clips;
    const Flicks gap = (*c)->length;
    // After the removal the later clips sit one place earlier; each moves back by the gap, in order.
    for (std::size_t j = index + 1; j < clips.size(); ++j) {
        Clip moved = clips[j];
        moved.at -= gap;
        change.steps.push_back({Step::Kind::replace, track, j - 1, std::move(moved)});
    }
    return change;
}

std::expected<Change, std::string> move(const Timeline& timeline, std::size_t track, std::size_t index, std::size_t to_track, Flicks at) {
    auto c = clip_of(timeline, track, index);
    if (!c) return std::unexpected(c.error());
    if (to_track >= timeline.tracks.size()) return std::unexpected(std::format("there is no track {}", to_track));
    if (timeline.tracks[to_track].kind != timeline.tracks[track].kind)
        return std::unexpected(std::format("track `{}` carries another kind than `{}`", timeline.tracks[to_track].name, timeline.tracks[track].name));
    Clip moved = **c;
    moved.at = at;
    // Where it lands, counted as the destination is once the clip has left its place.
    std::size_t to = place(timeline.tracks[to_track], at);
    if (to_track == track && to > index) --to;
    return Change{std::format("move `{}` to {}", moved.name, when(at)), {{Step::Kind::remove, track, index, {}}, {Step::Kind::insert, to_track, to, std::move(moved)}}};
}

std::expected<Change, std::string> trim(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at, Flicks length) {
    auto c = clip_of(timeline, track, index);
    if (!c) return std::unexpected(c.error());
    Clip t = **c;
    advance(t, at - t.at);
    t.at = at;
    t.length = length;
    t.fade.in = std::min(t.fade.in, length);
    t.fade.out = std::min(t.fade.out, length - t.fade.in);
    return Change{std::format("trim `{}` to {}–{}", t.name, when(at), when(at + length)), {{Step::Kind::replace, track, index, std::move(t)}}};
}

std::expected<Change, std::string> split(const Timeline& timeline, std::size_t track, std::size_t index, Flicks at) {
    auto c = clip_of(timeline, track, index);
    if (!c) return std::unexpected(c.error());
    const Clip& whole = **c;
    if (at <= whole.at || at >= whole.end()) return std::unexpected(std::format("{} is not inside `{}`", when(at), whole.name));
    Clip first = whole, second = whole;
    first.length = at - whole.at;
    first.fade.out = 0;
    first.fade.in = std::min(first.fade.in, first.length);
    advance(second, at - whole.at);
    second.at = at;
    second.length = whole.end() - at;
    second.fade.in = 0;
    second.fade.out = std::min(second.fade.out, second.length);
    second.name = whole.name + " (2)";
    return Change{std::format("split `{}` at {}", whole.name, when(at)),
                  {{Step::Kind::replace, track, index, std::move(first)}, {Step::Kind::insert, track, index + 1, std::move(second)}}};
}

}  // namespace omui::timeline
