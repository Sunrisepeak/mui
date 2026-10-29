// openmui.showcase.promo_video:player — a cut's frames, drawn.
//
// Frame i is what the picture shows there — a shot's live scene on its own
// headless host, drawn at the shot's time; or the cut's own earlier frame
// (a `film` clip: the rewind) — with the overlay over it. Played in order, a
// clip's scene goes on from the frame before; asked out of order, it is
// mounted afresh and its moments replayed. Either way the same bytes: the
// frames are functions of time, and `direct` draws one with nothing kept.
// The frames a `film` clip plays back are kept (as PNG) when they are drawn.
export module openmui.showcase.promo_video:player;

import std;
import openmui;
import openmui.video;
import :film;
import :shot;
import :overlay;

export namespace omui::promo {

// Everything the film is made from: its files and its lines.
struct Film {
    Files     files;
    Subtitles subtitles;
};
[[nodiscard]] std::expected<Film, std::vector<Problem>> load_film(Files files = {});

// The frames `film` clips play back, kept (as PNG) as they are drawn:
// shared by players drawing parts of a cut at once, one waiting for the
// frame another draws — those it was told to expect; the rest are drawn
// afresh by whoever needs them.
class FrameStore {
public:
    void expect(std::set<std::int64_t> frames);
    void put(std::int64_t frame, std::vector<std::uint8_t> png);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(std::int64_t frame);

private:
    std::mutex                                        lock_;
    std::condition_variable                           added_;
    std::set<std::int64_t>                            expected_;
    std::map<std::int64_t, std::vector<std::uint8_t>> frames_;
};

struct PlayerOptions {
    Size  size{};       // logical pixels (none: the cut's own); a portrait size lays every scene out for it
    float scale = 1;    // physical pixels a logical one: 2 draws 4K from 1920 × 1080
    bool  subtitles = true;
    bool  timebar = true;
    // Shared by every host this player makes (one thread's); none: its own.
    typeset::FontCollection* fonts = nullptr;
    // Where the frames `film` clips play back are kept (none: its own).
    std::shared_ptr<FrameStore> store;
    // Each frame's rows rasterised in bands at once (the same bytes); off
    // when several players draw at once and are the parallelism themselves.
    bool parallel_raster = true;
};

class Player {
public:
    Player(const Film& film, const Cut& cut, PlayerOptions options = {});
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Frame i (borrowed until the next call).
    [[nodiscard]] std::expected<const raster::Surface*, std::string> frame(std::int64_t i);
    // Frame i drawn with nothing kept: every scene mounted afresh.
    [[nodiscard]] std::expected<raster::Surface, std::string> direct(std::int64_t i);

    [[nodiscard]] std::int64_t frames() const noexcept;
    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] int width() const noexcept;   // physical
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] const Cut& cut() const noexcept { return *cut_; }
    [[nodiscard]] const Overlay& overlay() const noexcept { return *overlay_; }

    // The last frame's runtimes, for the checks: the scene's (none over a
    // played-back frame) and the overlay's; and its shot.
    [[nodiscard]] Runtime* scene_runtime() noexcept;
    [[nodiscard]] Runtime& overlay_runtime() noexcept;
    [[nodiscard]] Shot* scene_shot() noexcept;
    [[nodiscard]] std::optional<Shown> last_shown() const noexcept { return last_; }

    // A shot made for this cut's clip `index` (the soundtrack asks for their cues).
    [[nodiscard]] std::expected<std::unique_ptr<Shot>, std::string> make(std::size_t index) const;

    struct Scene;  // a clip's live scene

private:
    std::expected<void, std::string> draw_scene(const Shown& s);
    std::expected<std::shared_ptr<const raster::Surface>, std::string> played_back(std::int64_t frame);

    const Film*                              film_;
    const Cut*                               cut_;
    PlayerOptions                            options_;
    Size                                     size_;
    std::unique_ptr<typeset::FontCollection> own_fonts_;
    typeset::FontCollection*                 fonts_ = nullptr;
    std::unique_ptr<Scene>                   scene_;
    std::unique_ptr<Overlay>                 overlay_;
    std::unique_ptr<Headless>                overlay_host_;
    std::optional<render::ImageRef>          shown_image_;
    raster::Surface                          out_;
    std::optional<Shown>                     last_;
    std::set<std::int64_t>                   wanted_;  // the frames `film` clips play back
    std::shared_ptr<FrameStore>              store_;   // … as PNG, once drawn
};

// The cut as an openmui.video frame source (frames asked for in order).
class Frames final : public video::FrameSource {
public:
    explicit Frames(Player& player) : player_(&player) {}
    [[nodiscard]] std::expected<imageio::View, std::string> frame(std::int64_t index) override;

private:
    Player* player_;
};

// A frame's pixels, hashed (FNV-1a over its rows): what tests pin.
[[nodiscard]] std::uint64_t pixel_hash(const raster::Surface& s) noexcept;

}  // namespace omui::promo
