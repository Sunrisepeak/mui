module openmui.site;

import std;
import openmui;

namespace omui::site {

namespace {

// Home, then a flight to Features, the language switched, the tree's lens:
// the site's own motions (a camera's spring, glyphs gathering, a fade), each
// pressed on a whole frame (60 a second).
constexpr std::array<ReferenceStep, 3> script{{
    {0.5, "main/body/nav/to_features"},
    {2.7, "main/bar/lang"},
    {4.6, "main/bar/lens_tree"},
}};
constexpr double tail = 1.2;  // seconds after the last step: its motion to rest

}  // namespace

std::span<const ReferenceStep> reference_script() { return script; }

std::int32_t reference_frames() { return static_cast<std::int32_t>(std::lround((script.back().at + tail) * reference_fps)) + 1; }

std::uint64_t frame_hash(const raster::Surface& px) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < px.height(); ++y) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.row(y));
        for (std::size_t i = 0; i < static_cast<std::size_t>(px.width()) * 4; ++i) h = (h ^ bytes[i]) * 1099511628211ull;
    }
    return h;
}

std::span<const std::uint64_t> reference_pinned() {
    static const std::vector<std::uint64_t> pinned = [] {
        std::vector<std::uint64_t> out;
        const auto table = parse_json(files::ci_frames);
        if (!table) return out;
        if (const Value* frames = table->get("frames"); frames && frames->list())
            for (const Value& f : *frames->list()) {
                std::uint64_t v = 0;
                const std::string_view hex = f.as_string();
                if (std::from_chars(hex.data(), hex.data() + hex.size(), v, 16).ec == std::errc{}) out.push_back(v);
            }
        return out;
    }();
    return pinned;
}

struct ReferenceReplay::State {
    typeset::FontCollection   fonts;
    std::unique_ptr<Headless> h;
    std::int32_t              frame = 0;  // the frame played to
    std::size_t               next = 0;   // the next step
};

ReferenceReplay::ReferenceReplay() : s_(std::make_unique<State>()) {
    // Exactly the fonts the page carries, in the order its runtime takes them
    // (dist_toy's Inter, Noto Sans SC, JetBrains Mono): a glyph none of them
    // has falls back the same way here, in CI and in a browser.
    const auto dir = typeset::FontCollection::default_directory();
    std::vector<typeset::FaceId> chain;
    for (const char* name : {"Inter.ttf", "NotoSansSC.ttf", "JetBrainsMono.ttf"})
        if (auto id = s_->fonts.add_file(dir / name)) chain.push_back(*id);
    s_->fonts.set_fallback(std::move(chain));
    restart();
}

ReferenceReplay::~ReferenceReplay() = default;

void ReferenceReplay::restart() {
    s_->h.reset();
    s_->h = std::make_unique<Headless>(HeadlessOptions{.size = reference_size, .fonts = &s_->fonts, .parallel_raster = false, .complete_frames = true});
    AppSpec spec = app("home", "en");
    s_->h->runtime().set_theme(spec.theme);
    spec.setup(s_->h->runtime());
    s_->h->mount("main", spec.view());
    (void)s_->h->step();  // frame 0, at the run's first instant
    s_->frame = 0;
    s_->next = 0;
}

std::uint64_t ReferenceReplay::hash(std::int32_t frame) {
    frame = std::clamp(frame, 0, reference_frames() - 1);
    if (frame < s_->frame) restart();
    Headless& h = *s_->h;
    const auto frames_to = [](double seconds) { return static_cast<std::int64_t>(std::lround(seconds * 60)); };
    std::int64_t at = frames_to(static_cast<double>(s_->frame) / reference_fps);
    const std::int64_t to = frames_to(static_cast<double>(frame) / reference_fps);
    // Played on, a frame at a time, each step's control pressed on its frame.
    for (; at < to; ++at) {
        while (s_->next < script.size() && frames_to(script[s_->next].at) == at) (void)h.click(script[s_->next++].node);
        (void)h.step();
    }
    s_->frame = frame;
    return frame_hash(h.pixels());
}

const raster::Surface& ReferenceReplay::pixels() { return s_->h->pixels(); }

std::expected<std::vector<std::uint8_t>, std::string> ReferenceReplay::png() { return s_->h->png(); }

}  // namespace omui::site
