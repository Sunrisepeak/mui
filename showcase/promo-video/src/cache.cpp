module openmui.showcase.promo_video;

import std;
import openmui.hash;
import openmui.timeline;

namespace omui::promo {

namespace tl = omui::timeline;

namespace {

// A clip's whole definition: a one-clip timeline as the timeline file writes it.
std::string clip_text(const tl::Timeline& of, const tl::Clip& clip) {
    tl::Timeline one;
    one.rate = of.rate;
    one.sample_rate = of.sample_rate;
    one.width = of.width;
    one.height = of.height;
    tl::Track track;
    track.name = "clip";
    track.clips.push_back(clip);
    one.tracks.push_back(std::move(track));
    return tl::write_timeline(one);
}

}  // namespace

Digests::Digests(const Film& film, const Cut& cut, const PlayerOptions& o, std::uint64_t program) : film_(&film), cut_(&cut) {
    constexpr std::array<std::string_view, 2> per_frame{".timeline.omui.toml", ".subtitles.omui.toml"};
    hash::Fnv1a g;
    g.u64(program).u64(film.files.digest(per_frame));
    g.f32(o.size.width).f32(o.size.height).f32(o.scale).u64(o.subtitles).u64(o.timebar).u64(o.chapters);
    const tl::Timeline& t = cut.timeline;
    g.u64(static_cast<std::uint64_t>(t.rate.num)).u64(static_cast<std::uint64_t>(t.rate.den)).u64(static_cast<std::uint64_t>(t.width)).u64(static_cast<std::uint64_t>(t.height));
    global_ = g.value();
    // The overlay's strip and chapter label read the whole cut's layout.
    hash::Fnv1a ov;
    if (o.timebar || o.chapters) {
        ov.u64(static_cast<std::uint64_t>(cut.frames()));
        for (const Chapter& c : chapters(cut)) ov.str(c.name).u64(static_cast<std::uint64_t>(c.frame));
        for (const tl::Marker& m : t.markers) ov.str(m.name).u64(static_cast<std::uint64_t>(m.at));
        if (cut.picture >= 0)
            for (const tl::Clip& c : t.tracks[static_cast<std::size_t>(cut.picture)].clips) ov.str(c.name).u64(static_cast<std::uint64_t>(c.at)).u64(static_cast<std::uint64_t>(c.length));
    }
    overlay_ = ov.value();
}

std::uint64_t Digests::frame(std::int64_t i) {
    if (auto it = known_.find(i); it != known_.end()) return it->second;
    hash::Fnv1a d;
    d.u64(global_).u64(overlay_).u64(static_cast<std::uint64_t>(i));
    if (const auto s = shown(*cut_, i)) {
        d.u64(s->index).str(clip_text(cut_->timeline, *s->clip)).u64(static_cast<std::uint64_t>(s->source)).f32(s->level).u64(s->film);
        // A frame played back: what that frame depends on.
        if (s->film) d.u64(frame(tl::frame_at(cut_->timeline.rate, s->source)));
    } else d.str("nothing shown");
    if (film_)
        for (const Showing& l : lines_at(*cut_, film_->subtitles, i)) {
            d.str(l.line ? l.line->id : "").str(l.line ? l.line->zh : "").str(l.line ? l.line->en : "");
            if (l.clip) d.str(clip_text(cut_->timeline, *l.clip));
        }
    known_.emplace(i, d.value());
    return d.value();
}

std::uint64_t Digests::frames(std::int64_t first, std::int64_t count) {
    hash::Fnv1a d;
    for (std::int64_t i = first; i < first + count; ++i) d.u64(frame(i));
    return d.value();
}

std::uint64_t file_digest(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    hash::Fnv1a d;
    std::array<char, 1 << 16> buffer{};
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
        d.text(std::string_view(buffer.data(), static_cast<std::size_t>(in.gcount())));
        if (!in) break;
    }
    return d.value();
}

}  // namespace omui::promo
