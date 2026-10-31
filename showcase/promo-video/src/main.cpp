// omui-promo — openmui's film from the command line (D-61: one JSON object
// out and an exit code — 0 done, 1 failed, 2 a wrong command line).
//
//     omui-promo render CUT --out FILE [--size WxH] [--scale S] [--from N] [--to N] [--workers N]
//                           [--quality Q] [--speed N] [--realtime] [--dir DIR] [--no-subtitles]
//     omui-promo still  CUT (--frame N | --at SECONDS) --out x.png [--size WxH] [--scale S]
//     omui-promo check  CUT [--every N] [--seek N] [--size WxH] [--scale S] [--full]
//     omui-promo sound  CUT --out x.wav
//
// CUT is `promo` (the film, 60 s) or `short` (15 s). FILE's name says the
// format: x.webm (VP9 and Opus), x.y4m (a WAV beside it), shots/{:05}.png,
// or x.txt — each frame's pixel hash, a line a frame (no encoding).
// The frames are drawn by `--workers` players at once (each a clip or a
// stretch of one, from its start: any frame renders on its own), handed to
// the encoder in order. --dir reads edited copies of the film's files.
import std;
import openmui.hash;
import mcpplibs.cmdline;
import mcpplibs.os;
import openmui;
import openmui.video;
import openmui.video.webm;
import openmui.audio;
import openmui.timeline;
import openmui.showcase.promo_video;

namespace {

namespace cmd = mcpplibs::cmdline;
namespace promo = omui::promo;
namespace vid = omui::video;
namespace tl = omui::timeline;
using omui::Value;

int print(const Value& report, int code) {
    std::cout << report.json() << '\n';
    return code;
}

int failed(Value report, std::string message) {
    report.set("ok", false).set("error", std::move(message));
    return print(report, 1);
}

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

// The program's own file (argv[0]): its bytes are part of what every frame depends on.
std::filesystem::path self;

struct Loaded {
    promo::Film          film;
    promo::Cut           cut;
    promo::PlayerOptions options;
};

std::expected<Loaded, int> load(const cmd::ParsedArgs& p, Value& report) {
    promo::Files files;
    if (auto d = p.value("dir")) files = promo::Files(mcpplibs::os::fs::from_utf8(*d));
    auto film = promo::load_film(std::move(files));
    if (!film) {
        std::string all;
        for (const auto& pr : film.error()) all += pr.text() + "; ";
        return std::unexpected(failed(std::move(report), all));
    }
    Loaded l{std::move(*film), {}, {}};
    const std::string name = p.positional(0).empty() ? std::string("promo") : p.positional(0);
    report.set("cut", name);
    auto cut = promo::load_cut(l.film.files, name, l.film.subtitles);
    if (!cut) {
        Value::List problems;
        for (const auto& pr : cut.error()) problems.push_back(Value(pr.text()));
        report.set("problems", Value(std::move(problems)));
        return std::unexpected(failed(std::move(report), "the cut does not read"));
    }
    l.cut = std::move(*cut);
    if (auto s = p.value("size")) {
        int w = 0, h = 0;
        const auto x = s->find('x');
        if (x == std::string::npos || std::from_chars(s->data(), s->data() + x, w).ec != std::errc{} ||
            std::from_chars(s->data() + x + 1, s->data() + s->size(), h).ec != std::errc{} || w < 64 || h < 64) {
            std::cerr << "omui-promo: --size is WIDTHxHEIGHT in logical pixels (1920x1080, 1080x1920)\n";
            return std::unexpected(2);
        }
        l.options.size = {static_cast<float>(w), static_cast<float>(h)};
    }
    if (auto s = p.value("scale")) {
        double v = 0;
        if (std::from_chars(s->data(), s->data() + s->size(), v).ec != std::errc{} || !(v > 0.05 && v <= 4)) {
            std::cerr << "omui-promo: --scale is physical pixels a logical one (0.25 … 4; 2: 4K)\n";
            return std::unexpected(2);
        }
        l.options.scale = static_cast<float>(v);
    }
    l.options.subtitles = !p.is_flag_set("no-subtitles");
    return l;
}

std::int64_t number(const cmd::ParsedArgs& p, std::string_view name, std::int64_t otherwise) {
    std::int64_t v = otherwise;
    if (auto s = p.value(name)) std::from_chars(s->data(), s->data() + s->size(), v);
    return v;
}

// The frames of a range drawn by several players at once: player w draws
// frames w, w + n, w + 2n … of the range in order — each clip's scene going
// on from its frame before, its moments made between (any frame renders on
// its own, so no player needs the frames the others draw) — a few ahead of
// the encoder at most; the frames are handed on in order. The frames a
// `film` clip plays back are shared between them as they are drawn.
class Parallel final : public vid::FrameSource {
public:
    Parallel(const Loaded& l, std::int64_t from, std::int64_t to, int workers)
        : l_(&l), from_(from), to_(to), n_(std::max(1, workers)), consumed_(from) {
        slots_.resize(static_cast<std::size_t>(n_ * ahead));
        store_ = std::make_shared<promo::FrameStore>();
        // The frames played back that this range draws: waited for; the rest drawn afresh.
        std::set<std::int64_t> produced;
        for (std::int64_t f = from; f < to; ++f) produced.insert(f);
        store_->expect(produced);
        for (int w = 0; w < n_; ++w) threads_.emplace_back([this, w] { work(w); });
    }
    ~Parallel() override {
        {
            std::scoped_lock g(lock_);
            stop_ = true;
        }
        changed_.notify_all();
        for (auto& t : threads_) t.join();
    }

    std::expected<omui::imageio::View, std::string> frame(std::int64_t index) override {
        const std::int64_t f = from_ + index;
        std::unique_lock g(lock_);
        Slot& s = slots_[static_cast<std::size_t>(f % static_cast<std::int64_t>(slots_.size()))];
        changed_.wait(g, [&] { return s.frame == f || !error_.empty(); });
        if (s.frame != f) return std::unexpected(error_);
        shown_ = std::move(s.pixels);
        s.frame = -1;
        consumed_ = f + 1;
        changed_.notify_all();
        return omui::imageio::View{reinterpret_cast<const std::uint8_t*>(shown_.row(0)), shown_.width(), shown_.height(), shown_.width() * 4,
                                   omui::imageio::Alpha::premultiplied};
    }

private:
    static constexpr int ahead = 3;  // frames each player may be ahead of the encoder
    struct Slot {
        std::int64_t          frame = -1;
        omui::raster::Surface pixels;
    };
    void work(int w) {
        omui::typeset::FontCollection fonts;
        (void)fonts.add_defaults();
        promo::PlayerOptions o = l_->options;
        o.fonts = &fonts;
        o.store = store_;
        // Several players are the parallelism: each draws its frames' rows in one band
        // (bands on top cost more than they give: 360 frames in 20.4 s with 8 players,
        // 23.3 s with their bands too — the same bytes either way, dev-story §6).
        o.parallel_raster = n_ == 1;
        promo::Player player(l_->film, l_->cut, o);
        for (std::int64_t f = from_ + w; f < to_; f += n_) {
            {
                std::unique_lock g(lock_);
                changed_.wait(g, [&] { return stop_ || f - consumed_ < static_cast<std::int64_t>(slots_.size()); });
                if (stop_) return;
            }
            auto px = player.frame(f);
            std::scoped_lock g(lock_);
            if (!px) {
                error_ = std::format("frame {}: {}", f, px.error());
                changed_.notify_all();
                return;
            }
            Slot& s = slots_[static_cast<std::size_t>(f % static_cast<std::int64_t>(slots_.size()))];
            s.pixels = **px;
            s.frame = f;
            changed_.notify_all();
        }
    }

    const Loaded*                      l_;
    std::int64_t                       from_, to_;
    int                                n_;
    std::vector<Slot>                  slots_;
    std::shared_ptr<promo::FrameStore> store_;
    std::vector<std::thread>           threads_;
    std::mutex                         lock_;
    std::condition_variable            changed_;
    std::int64_t                       consumed_ = 0;
    bool                               stop_ = false;
    std::string                        error_;
    omui::raster::Surface              shown_;
};

// A text file of each frame's pixel hash (FNV-1a over its rows): the film's
// pictures without encoding them — what two machines compare, and how fast
// the frames alone are drawn.
class Hashes final : public vid::Encoder {
public:
    explicit Hashes(std::string name) : name_(std::move(name)) {}
    std::string_view name() const noexcept override { return "frame hashes"; }
    std::expected<void, std::string> begin(const vid::Format& format, vid::Output& out) override {
        auto f = out.create(name_);
        if (!f) return std::unexpected(f.error());
        file_ = *f;
        out_ = &out;
        (void)format;
        return {};
    }
    std::expected<void, std::string> frame(std::int64_t index, const omui::imageio::View& px, std::span<const float>) override {
        omui::hash::Fnv1a h;
        for (int y = 0; y < px.height; ++y) h.bytes(std::span(px.rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(px.stride), static_cast<std::size_t>(px.width) * 4));
        const std::string line = std::format("{} {}\n", index + first_, h.hex());
        if (!file_->write(std::as_bytes(std::span(line.data(), line.size())))) return std::unexpected("cannot write the hashes");
        return {};
    }
    std::expected<void, std::string> finish() override { return out_->close(file_); }
    void first(std::int64_t f) { first_ = f; }

private:
    std::string   name_;
    vid::File*    file_ = nullptr;
    vid::Output*  out_ = nullptr;
    std::int64_t  first_ = 0;
};

int render(const cmd::ParsedArgs& p) {
    Value report;
    report.set("command", "render");
    auto l = load(p, report);
    if (!l) return l.error();
    const auto target = p.value("out");
    if (!target) {
        std::cerr << "omui-promo render: --out names the file (x.webm, x.y4m, shots/{:05}.png)\n";
        return 2;
    }
    const std::int64_t total = l->cut.frames();
    const std::int64_t from = std::clamp<std::int64_t>(number(p, "from", 0), 0, total), to = std::clamp<std::int64_t>(number(p, "to", total), from, total);
    const int workers = static_cast<int>(number(p, "workers", std::clamp(std::thread::hardware_concurrency() / 4, 1u, 8u)));
    const auto t0 = std::chrono::steady_clock::now();
    promo::Player sounding(l->film, l->cut, l->options);
    auto track = promo::soundtrack(l->film, l->cut, sounding);
    if (!track) return failed(report, track.error());
    const double sound_took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const tl::Timeline& t = l->cut.timeline;
    const vid::Timing timing{{t.rate.num, t.rate.den}, t.sample_rate};
    // The range's samples.
    std::vector<float> samples(track->samples.begin() + timing.sample_of(from) * 2, track->samples.begin() + timing.sample_of(to) * 2);
    const vid::Format format{.width = sounding.width(), .height = sounding.height(), .timing = timing, .frames = to - from, .channels = 2};
    const std::filesystem::path out = mcpplibs::os::fs::from_utf8(*target);
    std::vector<std::unique_ptr<vid::Encoder>> encoders;
    std::filesystem::path dir = out.parent_path();
    const std::string name = omui::utf8(out.filename());
    if (name.ends_with(".webm")) {
        vid::WebmOptions w;
        w.quality = static_cast<int>(number(p, "quality", 24));
        w.speed = static_cast<int>(number(p, "speed", 6));
        w.realtime = p.is_flag_set("realtime");
        w.bitrate = static_cast<int>(number(p, "bitrate", 0));
        w.threads = static_cast<int>(number(p, "threads", 1));
        encoders.push_back(vid::webm_encoder(name, w));
    } else if (name.ends_with(".txt")) {
        auto h = std::make_unique<Hashes>(name);
        h->first(from);
        encoders.push_back(std::move(h));
    } else if (name.ends_with(".y4m")) {
        encoders.push_back(vid::y4m_encoder(name));
        encoders.push_back(vid::wav_encoder(omui::utf8(out.stem()) + ".wav"));
    } else if (name.find('{') != std::string::npos) {
        encoders.push_back(vid::png_encoder(name));
        encoders.push_back(vid::wav_encoder("audio.wav"));
    } else {
        std::cerr << "omui-promo render: --out is x.webm, x.y4m, a pattern (shots/{:05}.png) or x.txt (frame hashes)\n";
        return 2;
    }
    vid::DirectoryOutput output(dir.empty() ? std::filesystem::path(".") : dir);
    // A WebM put together from cached chunks (--cache): only the chunks whose frames' inputs changed are drawn.
    if (auto cache = p.value("cache"); cache && name.ends_with(".webm")) {
        vid::WebmOptions w;
        w.quality = static_cast<int>(number(p, "quality", 24));
        w.speed = static_cast<int>(number(p, "speed", 6));
        w.realtime = p.is_flag_set("realtime");
        w.bitrate = static_cast<int>(number(p, "bitrate", 0));
        w.threads = static_cast<int>(number(p, "threads", 1));
        const auto t_digest = std::chrono::steady_clock::now();
        promo::Digests digests(l->film, l->cut, l->options, promo::file_digest(self));
        for (std::int64_t i = from; i < to; ++i) (void)digests.frame(i);
        const double digest_took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_digest).count();
        vid::ChunkPlan plan;
        plan.frames = std::max<std::int64_t>(1, number(p, "chunk", 120));
        plan.directory = mcpplibs::os::fs::from_utf8(*cache);
        plan.key = [&](std::int64_t first, std::int64_t count) { return digests.frames(from + first, count); };
        // `workers` chunks at once, each encoded by its own VP9 encoder and drawn by
        // its share of the players (a few changed chunks: several players each).
        plan.at_once = workers;
        plan.draw = [&](std::int64_t first, std::int64_t count, int threads) -> std::expected<std::unique_ptr<vid::FrameSource>, std::string> {
            return std::make_unique<Parallel>(*l, from + first, from + first + count, threads);
        };
        auto done = vid::encode_webm_chunked(format, samples, plan, output, name, w);
        if (!done) return failed(report, done.error());
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        Value files{Value::List{}};
        files.list()->push_back(Value{}.set("name", omui::utf8(output.directory() / mcpplibs::os::fs::from_utf8(done->file.name))).set("bytes", done->file.bytes)
                                    .set("hash", hex(done->file.hash)));
        report.set("ok", true).set("width", format.width).set("height", format.height).set("fps", 60).set("frames", to - from).set("from", from);
        report.set("seconds", format.seconds()).set("workers", workers).set("files", std::move(files));
        report.set("chunks", done->chunks).set("reused", done->reused).set("drawn", done->drawn).set("digest_seconds", digest_took);
        report.set("took_seconds", took).set("sound_seconds", sound_took).set("realtime", took > 0 ? format.seconds() / took : 0.0);
        return print(report, 0);
    }
    std::vector<vid::Encoder*> list;
    for (auto& e : encoders) list.push_back(e.get());
    Parallel frames(*l, from, to, workers);
    auto done = vid::encode(format, frames, samples, list, output);
    if (!done) return failed(report, done.error());
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Value files{Value::List{}};
    for (const vid::Written& w : done->files)
        files.list()->push_back(Value{}.set("name", omui::utf8(output.directory() / mcpplibs::os::fs::from_utf8(w.name))).set("bytes", w.bytes).set("hash", hex(w.hash)));
    report.set("ok", true).set("width", format.width).set("height", format.height).set("fps", 60).set("frames", to - from).set("from", from);
    report.set("seconds", format.seconds()).set("workers", workers).set("files", std::move(files));
    report.set("took_seconds", took).set("sound_seconds", sound_took).set("realtime", took > 0 ? format.seconds() / took : 0.0);
    report.set("frames_per_second", took > 0 ? static_cast<double>(to - from) / took : 0.0);
    return print(report, 0);
}

int still(const cmd::ParsedArgs& p) {
    Value report;
    report.set("command", "still");
    auto l = load(p, report);
    if (!l) return l.error();
    std::int64_t frame = number(p, "frame", -1);
    if (auto a = p.value("at")) {
        double s = -1;
        std::from_chars(a->data(), a->data() + a->size(), s);
        if (s >= 0) frame = static_cast<std::int64_t>(std::floor(s * 60 + 1e-9));
    }
    const auto target = p.value("out");
    if (frame < 0 || !target) {
        std::cerr << "omui-promo still: --frame N or --at SECONDS, and --out x.png\n";
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    promo::Player player(l->film, l->cut, l->options);
    auto px = player.frame(frame);
    if (!px) return failed(report, px.error());
    const omui::raster::Surface& s = **px;
    auto png = omui::imageio::encode_png({reinterpret_cast<const std::uint8_t*>(s.row(0)), s.width(), s.height(), s.width() * 4, omui::imageio::Alpha::premultiplied});
    if (!png) return failed(report, png.error());
    if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(*target), std::string_view(reinterpret_cast<const char*>(png->data()), png->size())); !w)
        return failed(report, w.error().message());
    report.set("ok", true).set("frame", frame).set("out", *target).set("hash", hex(promo::pixel_hash(s)));
    report.set("took_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return print(report, 0);
}

int check(const cmd::ParsedArgs& p) {
    Value report;
    report.set("command", "check");
    auto l = load(p, report);
    if (!l) return l.error();
    if (!p.value("scale")) l->options.scale = 0.25f;  // the checks read layout and a coarse picture
    const auto t0 = std::chrono::steady_clock::now();
    promo::Player player(l->film, l->cut, l->options);
    auto track = promo::soundtrack(l->film, l->cut, player);
    if (!track) return failed(report, track.error());
    promo::Acceptance a = promo::check_data(l->film, l->cut, *track, l->options.size);
    // The frames: every one with --full, else every Nth and each clip's first and last; seek: some drawn directly.
    const std::int64_t every = p.is_flag_set("full") ? 1 : std::max<std::int64_t>(1, number(p, "every", 10));
    const auto frames = promo::sampled_frames(l->cut, every);
    std::map<std::int64_t, std::uint64_t> drawn;
    std::size_t looked = 0;
    for (const std::int64_t f : frames) {
        auto px = player.frame(f);
        if (!px) return failed(report, px.error());
        drawn[f] = promo::pixel_hash(**px);
        for (auto& finding : promo::check_frame(player, f, **px)) a.findings.push_back(std::move(finding));
        ++looked;
    }
    std::size_t bad = 0;
    for (const auto& fnd : a.findings) bad += fnd.check == "safe area" || fnd.check == "overlap" || fnd.check == "blank";
    a.add({"frames", bad == 0, std::format("{} frames looked at: their typography at rest inside the title-safe area and apart; none blank", looked)});
    // Seek: frames drawn directly against the same frames played to.
    const std::int64_t seeks = number(p, "seek", 12);
    std::uint64_t r = 0x2545f4914f6cdd1dull;
    std::size_t same = 0, tried = 0;
    std::vector<std::int64_t> pool(frames.begin(), frames.end());
    for (std::int64_t k = 0; k < seeks && !pool.empty(); ++k) {
        r ^= r << 13, r ^= r >> 7, r ^= r << 17;
        const std::int64_t f = pool[r % pool.size()];
        auto direct = player.direct(f);
        ++tried;
        if (direct && promo::pixel_hash(*direct) == drawn[f]) ++same;
        else a.findings.push_back({"seek", std::format("frame {} drawn directly differs from it played to", f), f});
    }
    a.add({"seek", same == tried, std::format("{} of {} frames drawn directly are the frames played to", same, tried)});
    // Determinism: three frames drawn again by a player of their own.
    {
        promo::Player again(l->film, l->cut, l->options);
        std::size_t equal = 0, n = 0;
        for (const std::int64_t f : {frames[frames.size() / 5], frames[frames.size() / 2], frames[frames.size() * 4 / 5]}) {
            auto px = again.frame(f);
            ++n;
            equal += px && promo::pixel_hash(**px) == drawn[f];
        }
        a.add({"determinism", equal == n, std::format("{} of {} frames the same bytes drawn again", equal, n)});
    }
    report.set("took_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const auto& [k, v] : *a.json().map()) report.set(k, v);
    return print(report, a.ok() ? 0 : 1);
}

int sound(const cmd::ParsedArgs& p) {
    Value report;
    report.set("command", "sound");
    auto l = load(p, report);
    if (!l) return l.error();
    const auto target = p.value("out");
    if (!target) {
        std::cerr << "omui-promo sound: --out x.wav\n";
        return 2;
    }
    promo::Player player(l->film, l->cut, l->options);
    auto track = promo::soundtrack(l->film, l->cut, player);
    if (!track) return failed(report, track.error());
    const auto bytes = omui::audio::wav(track->samples, track->rate);
    if (auto w = mcpplibs::os::fs::write_atomic(mcpplibs::os::fs::from_utf8(*target), std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())); !w)
        return failed(report, w.error().message());
    report.set("ok", true).set("out", *target).set("cues", static_cast<std::int64_t>(track->cues.size())).set("hash", hex(omui::audio::hash(track->samples)));
    return print(report, 0);
}

cmd::App with_cut(std::string_view name, std::string_view what) {
    cmd::App a{name};
    (void)a.description(what);
    a.arg("cut").help("promo (the film, 60 s) or short (15 s)");
    a.option("size").takes_value().value_name("WxH").help("logical pixels (default the cut's: 1920x1080; 1080x1920 for 9:16)");
    a.option("scale").takes_value().value_name("S").help("physical pixels a logical one (2: 4K)");
    a.option("dir").takes_value().value_name("DIR").help("read the film's files from here (edited copies) before the carried ones");
    a.option("no-subtitles").help("without the lines");
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 0) {
        self = mcpplibs::os::fs::from_utf8(argv[0]);
        std::error_code ec;
        if (!std::filesystem::exists(self, ec))
            if (auto found = mcpplibs::os::env::find_executable(argv[0])) self = *found;
    }
    cmd::App app{"omui-promo"};
    (void)app.description("openmui's film: render it, draw a frame, hear it, check it");
    cmd::App r = with_cut("render", "Render a cut: WebM (VP9 and Opus), Y4M and WAV, or PNG files and a WAV");
    r.option("out").takes_value().value_name("FILE").help("x.webm, x.y4m, shots/{:05}.png, or x.txt (each frame's pixel hash)");
    r.option("from").takes_value().value_name("N").help("the first frame");
    r.option("to").takes_value().value_name("N").help("the frame after the last");
    r.option("workers").takes_value().value_name("N").help("players drawing at once (default: a quarter of the cores, at most 8)");
    r.option("quality").takes_value().value_name("Q").help("WebM: VP9's constant quality, 0 (best) … 63 (default 24)");
    r.option("speed").takes_value().value_name("N").help("WebM: libvpx's speed, 0 … 9 (default 6)");
    r.option("realtime").help("WebM: libvpx's real-time mode");
    r.option("bitrate").takes_value().value_name("KBPS").help("WebM: a target bit rate instead of a quality");
    r.option("threads").takes_value().value_name("N").help("WebM: encoder threads (1, the default, gives the same bytes everywhere)");
    r.option("cache").takes_value().value_name("DIR").help("WebM: keep the encoded chunks here; draw again only those whose frames' inputs changed");
    r.option("chunk").takes_value().value_name("N").help("WebM with --cache: frames a chunk, each begun with a key frame (default 120: 2 s)");
    (void)app.subcommand(std::move(r));
    cmd::App s = with_cut("still", "One frame as a PNG");
    s.option("frame").takes_value().value_name("N").help("the frame");
    s.option("at").takes_value().value_name("SECONDS").help("the frame shown at a time");
    s.option("out").takes_value().value_name("FILE.png").help("where");
    (void)app.subcommand(std::move(s));
    cmd::App c = with_cut("check", "The automatic acceptance: duration, spec, lines, beats, onsets, every frame's typography, seeking, determinism");
    c.option("every").takes_value().value_name("N").help("look at every Nth frame (default 10) and each clip's first and last");
    c.option("full").help("look at every frame");
    c.option("seek").takes_value().value_name("N").help("frames drawn directly against the same played to (default 12)");
    (void)app.subcommand(std::move(c));
    cmd::App so = with_cut("sound", "The sound track as a WAV");
    so.option("out").takes_value().value_name("FILE.wav").help("where");
    (void)app.subcommand(std::move(so));
    cmd::Argv args(argv, argv + argc);
    auto parsed = app.parse_from(args);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::cerr << "omui-promo: " << parsed.error().message << '\n';
        return 2;
    }
    if (!parsed->has_subcommand()) {
        std::cout << "omui-promo render | still | check | sound CUT …   (omui-promo <command> --help)\n";
        return 0;
    }
    const cmd::ParsedArgs& sub = parsed->subcommand()->get();
    const std::string_view name = parsed->subcommand_name();
    if (name == "render") return render(sub);
    if (name == "still") return still(sub);
    if (name == "check") return check(sub);
    if (name == "sound") return sound(sub);
    return 2;
}
