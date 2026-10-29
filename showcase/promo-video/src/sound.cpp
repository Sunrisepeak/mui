// A cut's sound track: its music clips, its cues on their frames, the
// tape running backwards under a `film` clip.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui.audio;
import openmui.video;
import openmui.timeline;

namespace omui::promo {

namespace {

// The music clips into the mixer's "music" bus: each a stretch of its piece,
// rendered once, from its `in`, at its level through its fades.
class Score final : public audio::Source {
public:
    struct Part {
        std::shared_ptr<const std::vector<float>> samples;  // the piece, stereo, from its start
        std::int64_t                              at = 0, in = 0, length = 0;  // samples
        const tl::Clip*                           clip = nullptr;
    };
    Score(std::vector<Part> parts, std::int32_t bus, std::int32_t rate) : parts_(std::move(parts)), bus_(bus), rate_(rate) {}

    void render(std::int64_t clock, std::int32_t frames, std::span<const audio::BusBlock> buses) noexcept override {
        if (bus_ < 0 || static_cast<std::size_t>(bus_) >= buses.size()) return;
        const audio::BusBlock& out = buses[static_cast<std::size_t>(bus_)];
        for (const Part& p : parts_) {
            const std::int64_t from = std::max(clock, p.at), to = std::min(clock + frames, p.at + p.length);
            if (from >= to) continue;
            // The clip's level through its fades, at the block's middle (a fade is far longer than a block).
            const tl::Flicks mid = tl::sample_time(rate_, (from + to) / 2);
            const float level = p.clip->level_at(mid);
            for (std::int64_t s = from; s < to; ++s) {
                const std::int64_t src = p.in + (s - p.at);
                if (src < 0 || static_cast<std::size_t>(src * 2 + 1) >= p.samples->size()) continue;
                out.left[static_cast<std::size_t>(s - clock)] += (*p.samples)[static_cast<std::size_t>(src * 2)] * level;
                out.right[static_cast<std::size_t>(s - clock)] += (*p.samples)[static_cast<std::size_t>(src * 2 + 1)] * level;
            }
        }
    }

private:
    std::vector<Part> parts_;
    std::int32_t      bus_;
    std::int32_t      rate_;
};

}  // namespace

std::expected<Soundtrack, std::string> soundtrack(const Film& film, const Cut& cut, const Player& player) {
    const tl::Timeline& t = cut.timeline;
    Soundtrack out;
    out.rate = t.sample_rate;
    const video::Timing timing{{t.rate.num, t.rate.den}, t.sample_rate};
    const std::int64_t frames = cut.frames();
    const std::int64_t total = timing.sample_of(frames);
    const auto sample_at = [&](tl::Flicks f) { return tl::sample_at(t.sample_rate, f); };
    // What may sound: the film's sounds.
    auto sound_text = film.files.read("sounds/promo.sound.omui.toml");
    if (!sound_text) return std::unexpected(sound_text.error());
    auto sounds = audio::read_sounds(*sound_text, "promo.sound.omui.toml");
    if (!sounds) return std::unexpected(sounds.error().front().text());
    // The cues: the sfx track's, and each shot's of what it does.
    if (cut.sfx >= 0)
        for (const tl::Clip& c : t.tracks[static_cast<std::size_t>(cut.sfx)].clips)
            out.cues.push_back({c.source.substr(6), tl::frame_at(t.rate, c.at), c.level, 0, 1, c.name});
    const tl::Track& pic = t.tracks[static_cast<std::size_t>(cut.picture)];
    for (std::size_t i = 0; i < pic.clips.size(); ++i) {
        const tl::Clip& c = pic.clips[i];
        if (!c.source.starts_with("shot:")) continue;
        auto shot = player.make(i);
        if (!shot) return std::unexpected(shot.error());
        const double in = tl::to_seconds(c.in), length = tl::to_seconds(c.length);
        for (const Cue& q : (*shot)->cues(in + length)) {
            if (q.at < in || q.at >= in + length) continue;
            const std::int64_t f = tl::frame_at(t.rate, c.at + tl::from_seconds(q.at - in));
            out.cues.push_back({q.sound, f, q.gain, q.pan, q.pitch, c.name});
        }
    }
    std::ranges::stable_sort(out.cues, {}, &Placed::frame);
    // The mixer: the cues on their frames' first samples, the music under them.
    audio::Mixer mixer({.rate = t.sample_rate, .voices = 64, .queue = static_cast<std::int32_t>(out.cues.size() + 16)});
    std::map<std::string, std::int32_t, std::less<>> index;
    for (const audio::Sound& s : *sounds) index[s.name] = mixer.add(s);
    mixer.duck("music", {.key = "sfx", .depth = 0.3f, .threshold = 0.3f, .attack = 0.004f, .release = 0.25f});
    // The music clips: each piece rendered once, as long as its clips reach.
    std::map<std::string, std::shared_ptr<const std::vector<float>>> pieces;
    std::vector<Score::Part> parts;
    for (const tl::Track& tr : t.tracks) {
        if (tr.kind != tl::Kind::audio || tr.muted) continue;
        for (const tl::Clip& c : tr.clips) {
            if (!pieces.contains(c.source)) {
                double need = 0;
                for (const tl::Clip& d : tr.clips)
                    if (d.source == c.source) need = std::max(need, tl::to_seconds(d.in + d.length));
                auto text = film.files.read(c.source);
                if (!text) return std::unexpected(text.error());
                auto music = audio::read_music(*text, c.source);
                if (!music) return std::unexpected(music.error().front().text());
                out.tempo = music->tempo;
                auto r = audio::render_music(*music, need + 0.5, {}, {}, {.rate = t.sample_rate});
                if (!r) return std::unexpected(r.error().front().text());
                pieces[c.source] = std::make_shared<const std::vector<float>>(std::move(r->samples));
            }
            parts.push_back({pieces[c.source], sample_at(c.at), sample_at(c.in), sample_at(c.length), &c});
            // Its bars, where they fall on the cut.
            const double bar = 60.0 / out.tempo * 4;
            for (double m = std::ceil(tl::to_seconds(c.in) / bar - 1e-9) * bar; m < tl::to_seconds(c.in + c.length) - 1e-9; m += bar)
                out.bars.push_back(sample_at(c.at + tl::from_seconds(m) - c.in));
        }
    }
    mixer.add_source(std::make_shared<Score>(std::move(parts), mixer.bus("music"), t.sample_rate));
    for (const Placed& q : out.cues) {
        const auto it = index.find(q.sound);
        if (it == index.end()) return std::unexpected(std::format("clip `{}`: no sound `{}` in promo.sound.omui.toml", q.from, q.sound));
        if (!mixer.play(it->second, timing.sample_of(q.frame), q.gain, q.pan, q.pitch))
            return std::unexpected("the mixer's queue is full");
    }
    out.samples = audio::render(mixer, total);
    out.limiter = mixer.limiter();
    // Under a `film` clip, the cut's own sound at its source times: backwards and five times over, the
    // tape rewinding, under what the clip itself sounds (its cues, mixed above).
    for (const tl::Clip& c : pic.clips) {
        if (c.source != "film") continue;
        const std::int64_t from = sample_at(c.at), to = std::min(total, sample_at(c.end()));
        const std::int64_t fade = t.sample_rate / 100;  // 10 ms in and out
        for (std::int64_t s = from; s < to; ++s) {
            const std::int64_t src = sample_at(c.source_at(tl::sample_time(t.sample_rate, s)));
            if (src < 0 || src >= from) continue;
            const float edge = static_cast<float>(std::min<std::int64_t>({s - from, to - 1 - s, fade})) / static_cast<float>(fade);
            const float k = 0.62f * std::clamp(edge, 0.0f, 1.0f);
            for (int ch = 0; ch < 2; ++ch) {
                float& o = out.samples[static_cast<std::size_t>(s * 2 + ch)];
                o = std::clamp(o + out.samples[static_cast<std::size_t>(src * 2 + ch)] * k, -1.0f, 1.0f);
            }
        }
    }
    return out;
}

}  // namespace omui::promo
