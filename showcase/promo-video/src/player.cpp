// A cut's frames: its shots' scenes on their headless hosts, the film's own
// frames played back, the overlay over them.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.video;

namespace omui::promo {

void FrameStore::expect(std::set<std::int64_t> frames) {
    std::scoped_lock g(lock_);
    expected_ = std::move(frames);
}

void FrameStore::put(std::int64_t frame, std::vector<std::uint8_t> png) {
    {
        std::scoped_lock g(lock_);
        frames_.emplace(frame, std::move(png));
    }
    added_.notify_all();
}

std::optional<std::vector<std::uint8_t>> FrameStore::get(std::int64_t frame) {
    std::unique_lock g(lock_);
    if (expected_.contains(frame)) added_.wait(g, [&] { return frames_.contains(frame); });
    const auto it = frames_.find(frame);
    if (it == frames_.end()) return std::nullopt;
    return it->second;
}

std::expected<Film, std::vector<Problem>> load_film(Files files) {
    prepare();
    constexpr std::string_view file = "promo.subtitles.omui.toml";
    auto text = files.read(file);
    if (!text) return std::unexpected(std::vector<Problem>{{std::string(file), 0, 0, text.error()}});
    auto subs = read_subtitles(*text, file);
    if (!subs) return std::unexpected(subs.error());
    return Film{std::move(files), std::move(*subs)};
}

// ── a clip's live scene ─────────────────────────────────────────────────

// The rule both ways of drawing keep: mounted at its time 0; before a
// moment, the frame before it drawn; the frame asked for drawn last.
struct Player::Scene {
    std::size_t               index = 0;  // its clip, in the picture track
    std::unique_ptr<Shot>     shot;
    std::unique_ptr<Headless> host;
    std::vector<std::int64_t> moments;
    std::size_t               next = 0;
    std::int64_t              drawn = -1;
    double                    rate = 60;
    bool                      every = false;

    void draw(std::int64_t f) {
        const double t = static_cast<double>(f) / rate;
        shot->at(host->runtime(), t);
        host->runtime().frame({hanim::seconds(t), {}});
        drawn = f;
    }
    void to(std::int64_t target) {
        if (drawn < 0) draw(0);
        if (every) {
            while (drawn < target) {
                shot->moment(host->runtime(), drawn + 1);
                draw(drawn + 1);
            }
            return;
        }
        // A moment's change is made after the frame before it and seen in its own frame, both drawn
        // whichever way the frame asked for is reached.
        while (next < moments.size() && moments[next] <= target) {
            const std::int64_t m = moments[next++];
            if (m <= drawn) continue;  // (moments are frames after the first)
            if (drawn < m - 1) draw(m - 1);
            shot->moment(host->runtime(), m);
            draw(m);
        }
        if (drawn < target) draw(target);
    }
};

Player::Player(const Film& film, const Cut& cut, PlayerOptions options) : film_(&film), cut_(&cut), options_(options) {
    prepare();
    size_ = options_.size.width > 0 && options_.size.height > 0
                ? options_.size
                : Size{static_cast<float>(cut.timeline.width), static_cast<float>(cut.timeline.height)};
    fonts_ = options_.fonts;
    if (!fonts_) {
        own_fonts_ = std::make_unique<typeset::FontCollection>();
        if (auto r = own_fonts_->add_defaults(); !r) warn("promo: the default fonts did not load: {}", r.error());
        fonts_ = own_fonts_.get();
    }
    overlay_ = std::make_unique<Overlay>(cut, film.subtitles, size_, OverlayOptions{.subtitles = options_.subtitles, .timebar = options_.timebar, .chapters = options_.chapters});
    overlay_host_ = std::make_unique<Headless>(HeadlessOptions{.size = size_,
                                                               .scale = options_.scale,
                                                               .fps = {cut.timeline.rate.num, cut.timeline.rate.den},
                                                               .background = Color::transparent(),
                                                               .theme = blueprint(),
                                                               .fonts = fonts_,
                                                               .app = "openmui-promo",
                                                               .parallel_raster = options_.parallel_raster,
                                                               .complete_frames = true});
    overlay_host_->mount("main", overlay_->view());
    out_ = raster::Surface(width(), height());
    store_ = options_.store ? options_.store : std::make_shared<FrameStore>();
    // The frames the `film` clips play back, kept as they are drawn.
    const tl::Flicks frame = tl::frame_length(cut.timeline.rate);
    for (const tl::Clip& c : cut.timeline.tracks[static_cast<std::size_t>(cut.picture)].clips)
        if (c.source == "film")
            for (tl::Flicks t = c.at; t < c.end(); t += frame) wanted_.insert(tl::frame_at(cut.timeline.rate, c.source_at(t)));
}

Player::~Player() = default;

std::int64_t Player::frames() const noexcept { return cut_->frames(); }
int Player::width() const noexcept { return static_cast<int>(std::lround(size_.width * options_.scale)); }
int Player::height() const noexcept { return static_cast<int>(std::lround(size_.height * options_.scale)); }

Runtime* Player::scene_runtime() noexcept { return scene_ && last_ && !last_->film ? &scene_->host->runtime() : nullptr; }
Runtime& Player::overlay_runtime() noexcept { return overlay_host_->runtime(); }
Shot* Player::scene_shot() noexcept { return scene_ && last_ && !last_->film ? scene_->shot.get() : nullptr; }

std::expected<std::unique_ptr<Shot>, std::string> Player::make(std::size_t index) const {
    const tl::Track& pic = cut_->timeline.tracks[static_cast<std::size_t>(cut_->picture)];
    if (index >= pic.clips.size()) return std::unexpected(std::format("no clip {}", index));
    const tl::Clip& clip = pic.clips[index];
    if (!clip.source.starts_with("shot:")) return std::unexpected(std::format("clip `{}` is no shot", clip.name));
    const ShotContext ctx{&film_->files, cut_, &clip, size_, options_.scale};
    return make_shot(std::string_view(clip.source).substr(5), ctx);
}

std::expected<void, std::string> Player::draw_scene(const Shown& s) {
    const double rate = static_cast<double>(cut_->timeline.rate.num) / cut_->timeline.rate.den;
    const std::int64_t local = tl::frame_at(cut_->timeline.rate, s.source);
    if (!scene_ || scene_->index != s.index || scene_->drawn > local) {
        auto shot = make(s.index);
        if (!shot) return std::unexpected(shot.error());
        auto scene = std::make_unique<Scene>();
        scene->index = s.index;
        scene->shot = std::move(*shot);
        scene->rate = rate;
        scene->every = scene->shot->simulates();
        scene->moments = scene->shot->moments();
        std::ranges::sort(scene->moments);
        scene->moments.erase(std::ranges::unique(scene->moments).begin(), scene->moments.end());
        scene->host = std::make_unique<Headless>(HeadlessOptions{.size = size_,
                                                                 .scale = options_.scale,
                                                                 .fps = {cut_->timeline.rate.num, cut_->timeline.rate.den},
                                                                 .background = hex(ink::paper),
                                                                 .theme = blueprint(),
                                                                 .fonts = fonts_,
                                                                 .app = "openmui-promo",
                                                                 .parallel_raster = options_.parallel_raster,
                                                                 .complete_frames = true});
        scene_.reset();  // the one before goes first (its scenes, its timers)
        scene->host->mount("main", scene->shot->view());
        scene->shot->mounted(scene->host->runtime());
        scene_ = std::move(scene);
    }
    scene_->to(local);
    return {};
}

std::expected<std::shared_ptr<const raster::Surface>, std::string> Player::played_back(std::int64_t frame) {
    if (const auto kept = store_->get(frame)) {
        auto img = imageio::decode_png(*kept);
        if (!img) return std::unexpected(img.error());
        imageio::premultiply(*img);
        auto s = std::make_shared<raster::Surface>(img->width, img->height);
        std::memcpy(s->pixels().data(), img->rgba.data(), img->rgba.size());
        return s;
    }
    // Not kept (asked for out of order): drawn afresh.
    Player again(*film_, *cut_, PlayerOptions{options_.size, options_.scale, options_.subtitles, options_.timebar, options_.chapters, fonts_, {}, options_.parallel_raster});
    auto f = again.frame(frame);
    if (!f) return std::unexpected(f.error());
    return std::make_shared<raster::Surface>(**f);
}

namespace {

// a·b/255, rounded to nearest, exactly.
constexpr std::uint32_t mul255(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint32_t t = a * b + 128;
    return (t + (t >> 8)) >> 8;
}

}  // namespace

std::expected<const raster::Surface*, std::string> Player::frame(std::int64_t i) {
    if (i < 0 || i >= frames()) return std::unexpected(std::format("frame {}: the cut has {}", i, frames()));
    const auto s = shown(*cut_, i);
    last_ = s;
    const int w = width(), h = height();
    std::optional<render::ImageRef> picture;
    Runtime& ort = overlay_host_->runtime();
    if (shown_image_) {
        ort.images().remove(*shown_image_);
        shown_image_.reset();
    }
    if (!s) {
        out_.clear(raster::Rgba8{0, 0, 0, 255});
    } else if (s->film) {
        auto pic = played_back(tl::frame_at(cut_->timeline.rate, s->source));
        if (!pic) return std::unexpected(pic.error());
        shown_image_ = ort.images().add(*pic);
        picture = shown_image_;
        out_.clear(raster::Rgba8{0, 0, 0, 255});
    } else {
        if (auto r = draw_scene(*s); !r) return std::unexpected(r.error());
        const raster::Surface& px = scene_->host->pixels();
        const auto level = static_cast<std::uint32_t>(std::lround(std::clamp(s->level, 0.0f, 1.0f) * 255));
        for (int y = 0; y < h; ++y) {
            const raster::Rgba8* from = px.row(y);
            raster::Rgba8* to = out_.row(y);
            if (level == 255) {
                std::memcpy(to, from, static_cast<std::size_t>(w) * 4);
                continue;
            }
            // Faded: towards black.
            for (int x = 0; x < w; ++x)
                to[x] = {static_cast<std::uint8_t>(mul255(from[x].r, level)), static_cast<std::uint8_t>(mul255(from[x].g, level)),
                         static_cast<std::uint8_t>(mul255(from[x].b, level)), 255};
        }
    }
    overlay_->at(ort, i, s, picture);
    ort.frame({hanim::seconds(cut_->seconds_of(i)), {}});
    const raster::Surface& o = overlay_host_->pixels();
    // The overlay over the picture (both premultiplied; the picture opaque).
    for (int y = 0; y < h; ++y) {
        const raster::Rgba8* over = o.row(y);
        raster::Rgba8* to = out_.row(y);
        for (int x = 0; x < w; ++x) {
            const raster::Rgba8 v = over[x];
            if (v.a == 0) continue;
            if (v.a == 255) {
                to[x] = v;
                continue;
            }
            const std::uint32_t k = 255u - v.a;
            to[x] = {static_cast<std::uint8_t>(v.r + mul255(to[x].r, k)), static_cast<std::uint8_t>(v.g + mul255(to[x].g, k)),
                     static_cast<std::uint8_t>(v.b + mul255(to[x].b, k)), 255};
        }
    }
    if (wanted_.contains(i)) {
        auto png = imageio::encode_png(imageio::View{reinterpret_cast<const std::uint8_t*>(out_.row(0)), w, h, w * 4, imageio::Alpha::premultiplied});
        if (png) store_->put(i, std::move(*png));
    }
    return &out_;
}

std::expected<raster::Surface, std::string> Player::direct(std::int64_t i) {
    Player fresh(*film_, *cut_, PlayerOptions{options_.size, options_.scale, options_.subtitles, options_.timebar, options_.chapters, fonts_, {}, options_.parallel_raster});
    auto f = fresh.frame(i);
    if (!f) return std::unexpected(f.error());
    return **f;
}

std::expected<imageio::View, std::string> Frames::frame(std::int64_t index) {
    auto f = player_->frame(index);
    if (!f) return std::unexpected(f.error());
    const raster::Surface& s = **f;
    return imageio::View{reinterpret_cast<const std::uint8_t*>(s.row(0)), s.width(), s.height(), s.width() * 4, imageio::Alpha::premultiplied};
}

std::uint64_t pixel_hash(const raster::Surface& s) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int y = 0; y < s.height(); ++y) {
        const auto* p = reinterpret_cast<const unsigned char*>(s.row(y));
        for (std::size_t i = 0; i < static_cast<std::size_t>(s.width()) * 4; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    }
    return h;
}

}  // namespace omui::promo
