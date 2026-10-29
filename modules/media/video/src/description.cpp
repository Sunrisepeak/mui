// A description's sound track and frames.
module openmui.video;

import std;
import openmui.image;
import openmui.audio;
import openmui.trace;

namespace omui::video {

namespace {

std::filesystem::path path_of(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::optional<std::string> read_text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), {}};
}

Problem problem(std::string_view file, std::string message, std::string hint = {}) { return {std::string(file), 0, 0, std::move(message), std::move(hint)}; }

// PNG files, one a frame.
class PngFrames final : public FrameSource {
public:
    PngFrames(std::filesystem::path base, std::string pattern, std::int32_t width, std::int32_t height)
        : base_(std::move(base)), pattern_(std::move(pattern)), width_(width), height_(height) {}
    std::expected<imageio::View, std::string> frame(std::int64_t index) override {
        const std::string name = *frame_name(pattern_, index);
        auto img = imageio::load(base_ / path_of(name));
        if (!img) return std::unexpected(std::format("{}: {}", name, img.error()));
        if (img->width != width_ || img->height != height_)
            return std::unexpected(std::format("{} is {} × {}, the video {} × {}", name, img->width, img->height, width_, height_));
        image_ = std::move(*img);
        return imageio::view(image_);
    }

private:
    std::filesystem::path base_;
    std::string           pattern_;
    std::int32_t          width_, height_;
    imageio::Image        image_;
};

}  // namespace

std::optional<std::string> frame_name(std::string_view pattern, std::int64_t index) {
    const auto open = pattern.find('{');
    if (open == std::string_view::npos) return std::nullopt;
    const auto close = pattern.find('}', open);
    if (close == std::string_view::npos || pattern.find('{', open + 1) != std::string_view::npos || pattern.find('}', close + 1) != std::string_view::npos)
        return std::nullopt;
    std::string_view spec = pattern.substr(open + 1, close - open - 1);
    std::size_t width = 0;
    bool zero = false;
    if (!spec.empty()) {
        if (spec[0] != ':') return std::nullopt;
        spec.remove_prefix(1);
        if (spec.starts_with('0')) zero = true, spec.remove_prefix(1);
        if (!spec.empty()) {
            const auto [p, e] = std::from_chars(spec.data(), spec.data() + spec.size(), width);
            if (e != std::errc{} || p != spec.data() + spec.size() || width > 12) return std::nullopt;
        }
    }
    std::string digits = std::to_string(index);
    if (digits.size() < width) digits.insert(0, width - digits.size(), zero ? '0' : ' ');
    return std::string(pattern.substr(0, open)) + digits + std::string(pattern.substr(close + 1));
}

std::expected<std::unique_ptr<FrameSource>, std::vector<Problem>> frame_source(const Description& d, const std::filesystem::path& base) {
    switch (d.from) {
        case FramesFrom::card: return std::make_unique<Card>(d.format());
        case FramesFrom::png:
            if (!frame_name(d.source, 0)) return std::unexpected(std::vector{problem(d.source, "not a frame pattern", "png = \"shots/{:05}.png\"")});
            return std::make_unique<PngFrames>(base, d.source, d.width, d.height);
        case FramesFrom::view: break;
    }
    return std::unexpected(std::vector{problem(d.source, "a view's frames are rendered by a host", "omui video render")});
}

std::expected<std::vector<float>, std::vector<Problem>> soundtrack(const Description& d, const std::filesystem::path& base) {
    if (!d.audio) return std::vector<float>{};
    trace::Span span("video.soundtrack", "video");
    const Format f = d.format();
    std::vector<Problem> problems;
    audio::Mixer mixer({.rate = d.rate, .queue = 1024});
    for (const auto& [name, file] : d.recordings) {
        const auto text = read_text(base / path_of(file));
        if (!text) {
            problems.push_back(problem(file, "cannot read the recording"));
            continue;
        }
        auto r = audio::read_wav(std::as_bytes(std::span(text->data(), text->size())));
        if (!r) problems.push_back(problem(file, r.error()));
        else mixer.add_recording(name, r->samples, r->rate);
    }
    if (!d.sounds.empty()) {
        const auto text = read_text(base / path_of(d.sounds));
        if (!text) problems.push_back(problem(d.sounds, "cannot read the sounds"));
        else if (auto sounds = audio::read_sounds(*text, d.sounds); !sounds) problems.insert(problems.end(), sounds.error().begin(), sounds.error().end());
        else
            for (const audio::Sound& s : *sounds) mixer.add(s);
    }
    std::shared_ptr<audio::Sequencer> sequencer;
    if (!d.music.empty()) {
        const auto text = read_text(base / path_of(d.music));
        if (!text) problems.push_back(problem(d.music, "cannot read the music"));
        else if (auto music = audio::read_music(*text, d.music); !music) problems.insert(problems.end(), music.error().begin(), music.error().end());
        else if (auto s = audio::Sequencer::attach(*music, mixer); !s) problems.insert(problems.end(), s.error().begin(), s.error().end());
        else sequencer = std::move(*s);
    }
    // Every cue on its sample.
    struct Play {
        std::int64_t   at;
        std::int32_t   sound;
        const SoundCue* cue;
    };
    std::vector<Play> plays;
    for (const SoundCue& c : d.cues) {
        const std::int32_t id = mixer.find(c.sound);
        if (id < 0) {
            problems.push_back(problem(d.sounds.empty() ? std::string_view("[audio]") : std::string_view(d.sounds), std::format("no sound `{}` to cue", c.sound),
                                       d.sounds.empty() ? "sounds = \"<file>.sound.omui.toml\"" : ""));
            continue;
        }
        plays.push_back({c.frame >= 0 ? f.timing.sample_of(c.frame) : std::llround(c.at * d.rate), id, &c});
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    std::ranges::stable_sort(plays, {}, &Play::at);
    std::vector<Intensity> intensity = d.intensity;
    std::ranges::stable_sort(intensity, {}, &Intensity::at);

    const std::int64_t total = f.samples();
    std::vector<float> out(static_cast<std::size_t>(2 * total), 0.0f);
    if (sequencer) sequencer->start(0);
    std::size_t next_play = 0, next_level = 0;
    constexpr std::int64_t chunk = 4096;
    for (std::int64_t done = 0; done < total;) {
        while (next_level < intensity.size() && std::llround(intensity[next_level].at * d.rate) <= done) {
            if (sequencer) sequencer->set_intensity(intensity[next_level].value);
            ++next_level;
        }
        std::int64_t until = std::min(total, done + chunk);
        if (next_level < intensity.size()) until = std::clamp<std::int64_t>(std::llround(intensity[next_level].at * d.rate), done + 1, until);
        // The plays that begin in this stretch: the mixer starts each on its sample.
        while (next_play < plays.size() && plays[next_play].at < until) {
            const Play& p = plays[next_play++];
            mixer.play(p.sound, p.at, p.cue->gain, p.cue->pan, p.cue->pitch);
        }
        mixer.render(std::span(out).subspan(static_cast<std::size_t>(2 * done), static_cast<std::size_t>(2 * (until - done))));
        done = until;
    }
    if (d.beeps) {
        const std::vector<float> beeps = card_track({.width = d.width, .height = d.height, .timing = f.timing, .frames = f.frames, .channels = 2});
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += beeps[i];
    }
    return out;
}

}  // namespace omui::video
