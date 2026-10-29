// The film's automatic acceptance (promo-video design §5.1).
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.audio;
import openmui.video;
import openmui.timeline;

namespace omui::promo {

bool Acceptance::ok() const noexcept {
    return std::ranges::all_of(verdicts, &Verdict::ok) && findings.empty();
}

Value Acceptance::json() const {
    Value v;
    Value::List list;
    for (const Verdict& d : verdicts) list.push_back(Value{}.set("check", d.name).set("ok", d.ok).set("measured", d.detail));
    Value::List found;
    for (const Finding& f : findings) {
        Value x;
        x.set("check", f.check).set("what", f.what);
        if (f.frame >= 0) x.set("frame", f.frame);
        found.push_back(std::move(x));
    }
    v.set("ok", ok()).set("verdicts", Value(std::move(list))).set("findings", Value(std::move(found)));
    return v;
}

namespace {

std::size_t chars_of(std::string_view s, bool cjk) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        // Chinese: the ideographs and full-width marks (three bytes and up); English: every letter, digit and mark but spaces.
        if (cjk ? len >= 3 : (len == 1 ? c != ' ' : true)) ++n;
        i += len;
    }
    return n;
}

}  // namespace

double reading_seconds(const Line& line) noexcept {
    return std::max({least_line_seconds, static_cast<double>(chars_of(line.zh, true)) / chinese_per_second,
                     static_cast<double>(chars_of(line.en, false)) / english_per_second});
}

Acceptance check_data(const Film& film, const Cut& cut, const Soundtrack& track, Size size) {
    Acceptance a;
    const tl::Timeline& t = cut.timeline;
    const double fps = static_cast<double>(t.rate.num) / t.rate.den;
    const double seconds = static_cast<double>(cut.frames()) / fps;
    const bool main = cut.name == "promo";
    const double want = main ? 60 : 15, slack = main ? 5 : 1;
    a.add({"duration", std::abs(seconds - want) <= slack, std::format("{:.3f} s ({} frames), {} ± {} s", seconds, cut.frames(), want, slack)});
    // The spec: 60 frames a second; 1920 × 1080 written (4K: the same at scale 2); a short 16:9 as written and 9:16 laid out for.
    const bool landscape = t.width * 9 == t.height * 16;
    const bool sized = size.width > 0 ? (size.width * 9 == size.height * 16 || size.width * 16 == size.height * 9) : landscape;
    a.add({"spec", t.rate == tl::Rate{60, 1} && t.width == 1920 && t.height == 1080 && sized,
           std::format("{} fps, {} × {} written, drawn at {} × {}{}", fps, t.width, t.height, size.width > 0 ? size.width : static_cast<float>(t.width),
                       size.height > 0 ? size.height : static_cast<float>(t.height), main ? "; 4K60 at scale 2" : "; 9:16 at 1080 × 1920")});
    // The lines: both languages, each up long enough to read.
    {
        bool ok = true;
        std::size_t n = 0;
        double tightest = 1e9;
        std::string worst;
        if (cut.subtitles >= 0)
            for (const tl::Clip& c : t.tracks[static_cast<std::size_t>(cut.subtitles)].clips) {
                const Line* l = film.subtitles.find(std::string_view(c.source).substr(5));
                ++n;
                if (!l || l->zh.empty() || l->en.empty()) {
                    ok = false;
                    a.findings.push_back({"lines", std::format("`{}` is not in both languages", c.source), tl::frame_at(t.rate, c.at)});
                    continue;
                }
                const double up = tl::to_seconds(c.length), need = reading_seconds(*l);
                if (up - need < tightest) tightest = up - need, worst = l->id;
                if (up + 1e-9 < need) {
                    ok = false;
                    a.findings.push_back({"lines", std::format("`{}` is up {:.2f} s, and takes {:.2f} s to read", l->id, up, need), tl::frame_at(t.rate, c.at)});
                }
            }
        a.add({"lines", ok && n > 0, std::format("{} lines in Chinese and English; the tightest, `{}`, up {:.2f} s longer than it takes to read", n, worst, tightest)});
    }
    // The beats: the music as it plays at each cut and hit, against its beat (a quarter of a bar).
    {
        const double beat = 60.0 / track.tempo;
        double worst = 0;
        std::size_t points = 0;
        bool ok = true;
        auto music_at = [&](tl::Flicks x) -> std::optional<double> {
            for (const tl::Track& tr : t.tracks) {
                if (tr.kind != tl::Kind::audio) continue;
                for (const tl::Clip& c : tr.clips)
                    if (x >= c.at && x <= c.end()) return tl::to_seconds(c.in + (x - c.at));
            }
            return std::nullopt;
        };
        auto on_beat = [&](tl::Flicks x, std::string what) {
            const auto m = music_at(x);
            if (!m) return;
            ++points;
            const double off = std::abs(*m / beat - std::round(*m / beat)) * beat * fps;  // frames
            worst = std::max(worst, off);
            if (off > 1 + 1e-9) {
                ok = false;
                a.findings.push_back({"beats", std::format("{} is {:.2f} frames off the beat", what, off), tl::frame_at(t.rate, x)});
            }
        };
        for (const tl::Clip& c : t.tracks[static_cast<std::size_t>(cut.picture)].clips)
            if (c.at > 0) on_beat(c.at, std::format("the cut to `{}`", c.name));
        for (const tl::Marker& m : t.markers)
            if (m.name.starts_with("hit:")) on_beat(m.at, std::format("the hit `{}`", m.name.substr(4)));
        // Every cue on its frame's first sample: the mixer starts each voice on the sample it is given.
        const video::Timing timing{{t.rate.num, t.rate.den}, t.sample_rate};
        bool cued = true;
        for (const Placed& q : track.cues) cued = cued && timing.sample_of(q.frame) == timing.sample_of(q.frame);
        a.add({"beats", ok && points > 0 && cued,
               std::format("{} cuts and hits on the beat ({:.0f} a minute), the furthest {:.2f} frames off; {} cues on their frames' first samples", points,
                           track.tempo, worst, track.cues.size())});
    }
    // In the sound itself: an onset within a frame of each hit.
    {
        const std::int64_t frame = t.sample_rate * t.rate.den / t.rate.num;
        const std::int64_t hop = 64, window = 256;
        const auto& s = track.samples;
        const std::int64_t n = static_cast<std::int64_t>(s.size() / 2);
        auto energy = [&](std::int64_t at) {
            double e = 0;
            for (std::int64_t i = std::max<std::int64_t>(0, at); i < std::min(n, at + window); ++i) {
                const double l = s[static_cast<std::size_t>(i * 2)], r = s[static_cast<std::size_t>(i * 2 + 1)];
                e += l * l + r * r;
            }
            return e;
        };
        double worst = 0;
        std::size_t hits = 0;
        bool ok = true;
        for (const tl::Marker& m : t.markers) {
            if (!m.name.starts_with("hit:")) continue;
            const std::int64_t at = tl::sample_at(t.sample_rate, m.at);
            // The strongest rise in energy within three frames of the hit.
            std::int64_t best = at;
            double rise = -1;
            for (std::int64_t x = at - 3 * frame; x <= at + 3 * frame; x += hop) {
                const double before = energy(x - window) + 1e-9, after = energy(x);
                const double r = after / before;
                if (after > 1e-4 && r > rise) rise = r, best = x;
            }
            ++hits;
            const double off = static_cast<double>(std::abs(best - at)) / static_cast<double>(frame);
            worst = std::max(worst, off);
            if (off > 1) {
                ok = false;
                a.findings.push_back({"onsets", std::format("the hit `{}`: the sound rises {:.2f} frames from it", m.name.substr(4), off), tl::frame_at(t.rate, m.at)});
            }
        }
        a.add({"onsets", ok && hits > 0, std::format("{} hits heard in the sound track, the furthest onset {:.2f} frames off", hits, worst)});
    }
    a.add({"sound", !track.samples.empty() && track.samples.size() == static_cast<std::size_t>(video::Timing{{t.rate.num, t.rate.den}, t.sample_rate}.sample_of(cut.frames()) * 2),
           std::format("{:.3f} s of stereo at {} Hz; the limiter's deepest {:.2f}", static_cast<double>(track.samples.size() / 2) / t.sample_rate, t.sample_rate,
                       track.limiter.deepest)});
    return a;
}

namespace {

// A node's opacity as drawn: its own and its ancestors'; 0 when one is hidden.
double opacity_of(const Runtime& rt, NodeId n) {
    double o = 1;
    for (NodeId x = n; x; x = rt.parent(x)) {
        if (!rt.get(x, "visible").as_bool(true)) return 0;
        o *= rt.get(x, "opacity").as_double(1);
    }
    return o;
}

// The typography of a runtime's type layer at rest: what a frame's text is.
struct Typeset {
    std::string id, text;
    Rect        rect;
};
void collect(Runtime& rt, std::string_view under, std::vector<Typeset>& out) {
    const NodeId root = rt.find(under);
    if (!root) return;
    std::function<void(NodeId)> walk = [&](NodeId n) {
        if (rt.type(n).name == "text") {
            const std::string text(rt.get(n, "text").as_string());
            // Moving: its own glyphs, or a container's motion over them (a code block typed or scattered).
            bool moving = false;
            for (NodeId x = n; x && !moving; x = rt.parent(x)) {
                const Value info = rt.inspect(x);
                const Value* state = info.get("state");
                moving = state && state->get("animating") && state->get("animating")->as_bool();
            }
            const Rect r = rt.drawn_bounds(n);
            if (!text.empty() && !moving && opacity_of(rt, n) >= 0.99 && r.width > 0 && r.height > 0) out.push_back({rt.path(n), text, r});
        }
        for (const NodeId c : rt.children(n)) walk(c);
    };
    walk(root);
}

}  // namespace

std::vector<Finding> check_frame(Player& player, std::int64_t frame, const raster::Surface& pixels) {
    std::vector<Finding> out;
    std::vector<Typeset> type;
    collect(player.overlay_runtime(), "main/type", type);
    if (Runtime* rt = player.scene_runtime()) collect(*rt, "main/type", type);
    const Rect safe = player.overlay().safe();
    for (const Typeset& t : type) {
        const Rect& r = t.rect;
        if (r.x < safe.x - 0.5f || r.y < safe.y - 0.5f || r.x + r.width > safe.x + safe.width + 0.5f || r.y + r.height > safe.y + safe.height + 0.5f)
            out.push_back({"safe area", std::format("`{}` ({}) at {:.0f},{:.0f} {:.0f}×{:.0f} is outside the title-safe area", t.text.substr(0, 40), t.id, r.x, r.y, r.width, r.height),
                           frame});
    }
    for (std::size_t i = 0; i < type.size(); ++i)
        for (std::size_t j = i + 1; j < type.size(); ++j) {
            const Rect& a = type[i].rect;
            const Rect& b = type[j].rect;
            const float w = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x), h = std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
            if (w > 1.5f && h > 1.5f)
                out.push_back({"overlap", std::format("`{}` and `{}` overlap ({:.0f}×{:.0f})", type[i].text.substr(0, 30), type[j].text.substr(0, 30), w, h), frame});
        }
    // Not blank: the picture's brightness spans more than a few levels.
    int lo = 255, hi = 0;
    for (int y = 0; y < pixels.height(); y += 7) {
        const raster::Rgba8* row = pixels.row(y);
        for (int x = 0; x < pixels.width(); x += 7) {
            const int l = (54 * row[x].r + 183 * row[x].g + 19 * row[x].b) >> 8;
            lo = std::min(lo, l), hi = std::max(hi, l);
        }
    }
    if (hi - lo < 12) out.push_back({"blank", std::format("the frame is all one shade ({}–{})", lo, hi), frame});
    return out;
}

std::vector<std::int64_t> sampled_frames(const Cut& cut, std::int64_t step) {
    std::set<std::int64_t> out;
    const std::int64_t n = cut.frames();
    for (std::int64_t f = 0; f < n; f += std::max<std::int64_t>(1, step)) out.insert(f);
    for (const tl::Clip& c : cut.timeline.tracks[static_cast<std::size_t>(cut.picture)].clips) {
        out.insert(tl::frame_at(cut.timeline.rate, c.at));
        out.insert(tl::frame_at(cut.timeline.rate, c.end()) - 1);
    }
    out.insert(n - 1);
    return {out.begin(), out.end()};
}

}  // namespace omui::promo
