// omui video: videos from the command line (D-61: one JSON object out and
// an exit code — 0 done, 1 failed, 2 a wrong command line — for people,
// agents and CI alike).
//
//     omui video render FILE --out OUT [--format webm|y4m|png|wav] [--size WxH] [--fps R] [--seconds S | --frames N]
//                       [--quality Q | --bitrate KBPS] [--speed N] [--realtime] [--lossless] [--threads N]
//     omui video still  FILE (--frame N | --at SECONDS) --out x.png
//     omui video check  FILE
//     omui video probe  FILE.webm | FILE.y4m | FILE.wav
//     omui video timing --fps R [--rate HZ] [--frames N]
//
// FILE is a video description (`*.video.omui.toml`: the sync card, PNG files
// or a view, with its sound track) or a view document (`*.view.omui.json`,
// rendered on the headless host's clock, a frame a step). OUT's name says
// the format: `x.webm` (VP9 and Opus, through openmui.video.webm), `x.y4m`
// (and `x.wav` beside it when there is sound), `x.wav`,
// a pattern `shots/{:05}.png`, or a directory `shots/` (frame-00000.png …
// and audio.wav).
module openmui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import openmui.core;
import openmui.viewdoc;
import openmui.host.headless;
import openmui.video;
import openmui.video.webm;
import openmui.audio;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;
namespace vid = omui::video;

namespace {

std::filesystem::path path_of(std::string_view s) { return mcpplibs::os::fs::from_utf8(s); }

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

Value problems_value(std::span<const vid::Problem> problems) {
    Value::List list;
    for (const vid::Problem& p : problems) {
        Value v;
        v.set("file", p.file).set("line", p.line).set("column", p.column).set("message", p.message);
        if (!p.hint.empty()) v.set("hint", p.hint);
        v.set("text", p.text());
        list.push_back(std::move(v));
    }
    return Value(std::move(list));
}

int print(std::ostream& out, const Value& report, int code) {
    out << report.json() << '\n';
    return code;
}

int failed(std::ostream& out, Value report, std::string message) {
    report.set("ok", false).set("error", std::move(message));
    return print(out, report, 1);
}

// The frames a view document draws: a headless host on the video's clock,
// a step a frame (the first at 0 s), CPU pixels.
class ViewFrames final : public vid::FrameSource {
public:
    explicit ViewFrames(const vid::Description& d)
        : headless_(HeadlessOptions{.size = {static_cast<float>(d.width), static_cast<float>(d.height)}, .fps = d.fps, .app = "omui-video"}) {}

    std::expected<void, std::string> load(const std::filesystem::path& file) {
        register_components();
        auto v = viewdoc::View::load_file(headless_.runtime(), file);
        if (!v) {
            std::string all;
            for (const auto& d : v.error()) all += (all.empty() ? "" : "; ") + d.text(utf8(file));
            return std::unexpected(all);
        }
        view_.emplace(std::move(*v));
        view_->mount(headless_.runtime(), "main");
        return {};
    }

    std::expected<imageio::View, std::string> frame(std::int64_t index) override {
        if (index != next_) return std::unexpected(std::format("a view's frames come in order (frame {} after {})", index, next_ - 1));
        headless_.step();
        ++next_;
        const raster::Surface& s = headless_.pixels();
        return imageio::View{reinterpret_cast<const std::uint8_t*>(s.row(0)), s.width(), s.height(), s.width() * 4, imageio::Alpha::premultiplied};
    }

private:
    Headless                     headless_;
    std::optional<viewdoc::View> view_;
    std::int64_t                 next_ = 0;
};

// No pictures (a WAV alone): nothing drawn.
class NoFrames final : public vid::FrameSource {
public:
    std::expected<imageio::View, std::string> frame(std::int64_t) override { return imageio::View{pixel_.data(), 1, 1, 4, imageio::Alpha::straight}; }

private:
    std::array<std::uint8_t, 4> pixel_{0, 0, 0, 255};
};

struct Loaded {
    vid::Description      d;
    std::filesystem::path file, base;
};

// A description, or a view document taken as one; the size, rate and length
// given on the command line over what the file says.
std::expected<Loaded, int> load(const cmd::ParsedArgs& p, Value& report, std::ostream& out, std::ostream& err) {
    Loaded l;
    l.file = path_of(p.positional(0));
    l.base = l.file.parent_path();
    report.set("file", utf8(l.file));
    const std::string name = utf8(l.file.filename());
    if (name.ends_with(".view.omui.json")) {
        l.d.from = vid::FramesFrom::view;
        l.d.source = name;
        l.d.seconds = 3;
    } else {
        auto text = mcpplibs::os::fs::read(l.file);
        if (!text) return std::unexpected(failed(out, report, std::format("cannot read the file: {}", text.error().message())));
        auto d = vid::read_description(*text, name);
        if (!d) {
            report.set("ok", false).set("problems", problems_value(d.error()));
            return std::unexpected(print(out, report, 1));
        }
        l.d = std::move(*d);
    }
    if (auto s = p.value("size")) {
        int w = 0, h = 0;
        const auto x = s->find('x');
        const auto [pw, ew] = std::from_chars(s->data(), s->data() + (x == std::string::npos ? 0 : x), w);
        const auto [ph, eh] = x == std::string::npos ? std::from_chars_result{} : std::from_chars(s->data() + x + 1, s->data() + s->size(), h);
        if (x == std::string::npos || ew != std::errc{} || eh != std::errc{} || w < 2 || h < 2 || w > 16384 || h > 16384) {
            err << "omui video: --size is WIDTHxHEIGHT (1280x720)\n";
            return std::unexpected(2);
        }
        l.d.width = w, l.d.height = h;
    }
    if (auto f = p.value("fps")) {
        auto rate = vid::parse_rate(*f);
        if (!rate) {
            err << "omui video: --fps is a rate (30, 30000/1001, 29.97)\n";
            return std::unexpected(2);
        }
        l.d.fps = *rate;
    }
    if (auto s = p.value("seconds")) {
        double v = 0;
        const auto [q, e] = std::from_chars(s->data(), s->data() + s->size(), v);
        if (e != std::errc{} || q != s->data() + s->size() || !(v > 0)) {
            err << "omui video: --seconds is a length (2.5)\n";
            return std::unexpected(2);
        }
        l.d.seconds = v;
    }
    if (auto n = p.value("frames")) {
        std::int64_t v = 0;
        const auto [q, e] = std::from_chars(n->data(), n->data() + n->size(), v);
        if (e != std::errc{} || q != n->data() + n->size() || v <= 0) {
            err << "omui video: --frames is a count\n";
            return std::unexpected(2);
        }
        l.d.seconds = 0;
        l.d.frames = v;
    }
    if (l.d.seconds > 0) l.d.frames = vid::Timing{l.d.fps, l.d.rate}.frames_in(l.d.seconds);
    return l;
}

std::expected<std::unique_ptr<vid::FrameSource>, std::string> frames_of(const Loaded& l) {
    if (l.d.from == vid::FramesFrom::view) {
        auto v = std::make_unique<ViewFrames>(l.d);
        if (auto r = v->load(l.base / path_of(l.d.source)); !r) return std::unexpected(r.error());
        return v;
    }
    auto f = vid::frame_source(l.d, l.base);
    if (!f) return std::unexpected(f.error().front().text());
    return std::move(*f);
}

Value describe(const vid::Description& d) {
    const vid::Format f = d.format();
    Value v;
    constexpr std::array<std::string_view, 3> from{"card", "png", "view"};
    v.set("width", d.width).set("height", d.height).set("fps", vid::rate_text(d.fps)).set("frames", d.frames).set("seconds", f.seconds());
    v.set("frames_from", Value(Value::Map{{std::string(from[static_cast<std::size_t>(d.from)]), Value(d.source)}}));
    if (d.audio) {
        std::int64_t lo = std::numeric_limits<std::int64_t>::max(), hi = 0;
        for (std::int64_t i = 0; i < std::min<std::int64_t>(d.frames, 100000); ++i) lo = std::min(lo, f.timing.samples_in(i)), hi = std::max(hi, f.timing.samples_in(i));
        v.set("rate", d.rate).set("samples", f.samples()).set("samples_per_frame", Value(Value::List{Value(lo), Value(hi)}));
    }
    return v;
}

cmd::App with_file(std::string_view name, std::string_view what) {
    cmd::App a{name};
    (void)a.description(what);
    a.arg("file").required().help("a video description (*.video.omui.toml) or a view document (*.view.omui.json)");
    a.option("size").takes_value().value_name("WxH").help("the size (a view's default: 1280x720)");
    a.option("fps").takes_value().value_name("RATE").help("frames a second: 30, 30000/1001, 29.97 (a view's default: 30)");
    a.option("seconds").takes_value().value_name("S").help("how long (a view's default: 3)");
    a.option("frames").takes_value().value_name("N").help("how many frames");
    return a;
}

int render(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    Value report;
    report.set("command", "render");
    auto l = load(p, report, out, err);
    if (!l) return l.error();
    const auto out_value = p.value("out");
    if (!out_value) {
        err << "omui video render: --out names the file (x.y4m, x.wav, shots/{:05}.png, shots/)\n";
        return 2;
    }
    const std::string target = *out_value;
    std::string format = p.value("format").value_or("");
    if (format.empty()) {
        if (target.ends_with(".y4m")) format = "y4m";
        else if (target.ends_with(".wav")) format = "wav";
        else if (target.ends_with(".webm")) format = "webm";
        else if (std::error_code ec; target.ends_with('/') || target.find('{') != std::string::npos || std::filesystem::is_directory(path_of(target), ec))
            format = "png";
    }
    if (format != "webm" && format != "y4m" && format != "wav" && format != "png") {
        err << "omui video render: the format is webm, y4m, png or wav (from --out's name, or --format)\n";
        return 2;
    }
    vid::WebmOptions webm;
    for (auto [name, field] : {std::pair{"quality", &webm.quality}, std::pair{"bitrate", &webm.bitrate}, std::pair{"speed", &webm.speed},
                               std::pair{"threads", &webm.threads}, std::pair{"audio-bitrate", &webm.audio_bitrate}})
        if (auto v = p.value(name)) {
            const auto [q, e] = std::from_chars(v->data(), v->data() + v->size(), *field);
            if (e != std::errc{} || q != v->data() + v->size()) {
                err << "omui video render: --" << name << " is a whole number\n";
                return 2;
            }
        }
    webm.realtime = p.is_flag_set("realtime");
    webm.lossless = p.is_flag_set("lossless");
    // Where the files go, and what writes them.
    const std::filesystem::path out_path = path_of(target);
    std::filesystem::path dir = out_path.parent_path();
    std::vector<std::unique_ptr<vid::Encoder>> encoders;
    if (format == "webm") {
        encoders.push_back(vid::webm_encoder(utf8(out_path.filename()), webm));
    } else if (format == "y4m") {
        encoders.push_back(vid::y4m_encoder(utf8(out_path.filename())));
        if (l->d.audio) encoders.push_back(vid::wav_encoder(utf8(out_path.stem()) + ".wav"));
    } else if (format == "wav") {
        if (!l->d.audio) return failed(out, report, "the video has no sound track ([audio])");
        encoders.push_back(vid::wav_encoder(utf8(out_path.filename())));
    } else {
        const bool pattern = target.find('{') != std::string::npos;
        if (!pattern) dir = out_path;
        encoders.push_back(vid::png_encoder(pattern ? utf8(out_path.filename()) : "frame-{:05}.png"));
        if (l->d.audio) encoders.push_back(vid::wav_encoder("audio.wav"));
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto track = vid::soundtrack(l->d, l->base);
    if (!track) {
        report.set("ok", false).set("problems", problems_value(track.error()));
        return print(out, report, 1);
    }
    std::unique_ptr<vid::FrameSource> frames;
    if (format == "wav") frames = std::make_unique<NoFrames>();
    else if (auto f = frames_of(*l); f) frames = std::move(*f);
    else return failed(out, report, f.error());
    vid::DirectoryOutput output(dir.empty() ? std::filesystem::path(".") : dir);
    std::vector<vid::Encoder*> list;
    for (auto& e : encoders) list.push_back(e.get());
    const vid::Format f = l->d.format();
    auto done = vid::encode(f, *frames, *track, list, output);
    if (!done) return failed(out, report, done.error());
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Value files{Value::List{}};
    for (const vid::Written& w : done->files) {
        Value v;
        v.set("name", utf8(output.directory() / path_of(w.name))).set("bytes", w.bytes).set("hash", hex(w.hash));
        files.list()->push_back(std::move(v));
    }
    report.set("ok", true).set("format", format);
    for (const auto& [k, v] : *describe(l->d).map()) report.set(k, v);
    report.set("files", std::move(files)).set("took_seconds", took).set("realtime", took > 0 ? f.seconds() / took : 0.0);
    return print(out, report, 0);
}

int still(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    Value report;
    report.set("command", "still");
    auto l = load(p, report, out, err);
    if (!l) return l.error();
    const auto target = p.value("out");
    std::int64_t index = -1;
    if (auto n = p.value("frame")) std::from_chars(n->data(), n->data() + n->size(), index);
    else if (auto a = p.value("at")) {
        double t = -1;
        std::from_chars(a->data(), a->data() + a->size(), t);
        if (t >= 0) index = vid::Timing{l->d.fps, l->d.rate}.frame_at(t);
    }
    if (!target || index < 0) {
        err << "omui video still: --frame N or --at SECONDS, and --out x.png\n";
        return 2;
    }
    if (index >= l->d.frames) return failed(out, report, std::format("frame {}: the video has {}", index, l->d.frames));
    auto frames = frames_of(*l);
    if (!frames) return failed(out, report, frames.error());
    std::expected<imageio::View, std::string> pixels;
    // A view is stepped there (its clock runs from 0); the card and files are asked directly.
    for (std::int64_t i = l->d.from == vid::FramesFrom::view ? 0 : index; i <= index; ++i) pixels = (*frames)->frame(i);
    if (!pixels) return failed(out, report, pixels.error());
    auto png = imageio::encode_png(*pixels);
    if (!png) return failed(out, report, png.error());
    if (auto w = mcpplibs::os::fs::write_atomic(path_of(*target), std::string_view(reinterpret_cast<const char*>(png->data()), png->size())); !w)
        return failed(out, report, std::format("cannot write {}: {}", *target, w.error().message()));
    const vid::Timing t{l->d.fps, l->d.rate};
    report.set("ok", true).set("frame", index).set("time", static_cast<double>(t.time_of(index, 1'000'000)) / 1e6);
    report.set("out", *target).set("bytes", static_cast<std::int64_t>(png->size())).set("hash", hex(vid::hash(std::span<const std::uint8_t>(*png))));
    return print(out, report, 0);
}

int check_video(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    Value report;
    report.set("command", "check");
    auto l = load(p, report, out, err);
    if (!l) return l.error();
    report.set("ok", true);
    for (const auto& [k, v] : *describe(l->d).map()) report.set(k, v);
    return print(out, report, 0);
}

int probe(const cmd::ParsedArgs& p, std::ostream& out) {
    Value report;
    const std::filesystem::path file = path_of(p.positional(0));
    report.set("command", "probe").set("file", utf8(file));
    auto text = mcpplibs::os::fs::read(file);
    if (!text) return failed(out, report, std::format("cannot read the file: {}", text.error().message()));
    const auto bytes = std::as_bytes(std::span(text->data(), text->size()));
    report.set("bytes", static_cast<std::int64_t>(bytes.size())).set("hash", hex(vid::hash(bytes)));
    if (text->starts_with("YUV4MPEG2 ")) {
        auto y = vid::read_y4m(bytes);
        if (!y) return failed(out, report, y.error());
        const double seconds = static_cast<double>(y->frames.size()) * static_cast<double>(y->fps.den) / static_cast<double>(y->fps.num);
        report.set("ok", true).set("format", "y4m").set("width", y->width).set("height", y->height).set("fps", vid::rate_text(y->fps));
        report.set("frames", static_cast<std::int64_t>(y->frames.size())).set("seconds", seconds).set("colour", y->colour);
        return print(out, report, 0);
    }
    if (bytes.size() >= 4 && text->starts_with("\x1a\x45\xdf\xa3")) {
        auto w = vid::decode_webm(bytes);
        if (!w) return failed(out, report, w.error());
        report.set("ok", true).set("format", "webm").set("width", w->width).set("height", w->height).set("video", w->video_codec);
        report.set("frames", static_cast<std::int64_t>(w->frames.size())).set("seconds", static_cast<double>(w->duration_ns) / 1e9);
        report.set("bt709", w->bt709).set("limited_range", w->limited_range);
        if (!w->audio_codec.empty())
            report.set("audio", w->audio_codec).set("rate", w->rate).set("channels", w->channels)
                .set("samples", static_cast<std::int64_t>(w->samples.size() / static_cast<std::size_t>(std::max(1, w->channels))));
        return print(out, report, 0);
    }
    if (text->starts_with("RIFF")) {
        auto w = audio::read_wav(bytes);
        if (!w) return failed(out, report, w.error());
        report.set("ok", true).set("format", "wav").set("rate", w->rate).set("samples", static_cast<std::int64_t>(w->samples.size()));
        report.set("seconds", static_cast<double>(w->samples.size()) / w->rate);
        return print(out, report, 0);
    }
    return failed(out, report, "neither WebM, Y4M nor WAV");
}

int timing(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    const auto rate = vid::parse_rate(p.value("fps").value_or("30"));
    std::int32_t hz = 48000;
    if (auto r = p.value("rate")) std::from_chars(r->data(), r->data() + r->size(), hz);
    if (!rate || hz <= 0) {
        err << "omui video timing: --fps RATE (30, 30000/1001) [--rate HZ]\n";
        return 2;
    }
    const vid::Timing t{*rate, hz};
    std::int64_t n = (rate->num + rate->den - 1) / rate->den;
    if (auto f = p.value("frames")) std::from_chars(f->data(), f->data() + f->size(), n);
    n = std::clamp<std::int64_t>(n, 1, 100000);
    Value frames{Value::List{}};
    for (std::int64_t i = 0; i < n; ++i) {
        Value v;
        v.set("frame", i).set("first_sample", t.sample_of(i)).set("samples", t.samples_in(i)).set("start_ns", t.time_of(i, 1'000'000'000));
        frames.list()->push_back(std::move(v));
    }
    Value report;
    report.set("command", "timing").set("ok", true).set("fps", vid::rate_text(*rate)).set("rate", hz).set("frames", n).set("samples", t.sample_of(n));
    report.set("each", std::move(frames));
    return print(out, report, 0);
}

}  // namespace

cmd::App video_command() {
    cmd::App a{"video"};
    (void)a.description("Videos: frames and a sound track on exact frame and sample counts (openmui.video)");
    cmd::App r = with_file("render", "Render to files: WebM (VP9 and Opus), Y4M (and a WAV beside it), a PNG sequence, or a WAV");
    r.option("out").takes_value().value_name("OUT").help("x.webm, x.y4m, x.wav, shots/{:05}.png or shots/");
    r.option("format").takes_value().value_name("FORMAT").help("webm, y4m, png or wav (default: from --out)");
    r.option("quality").takes_value().value_name("Q").help("WebM: VP9 at constant quality, 0 (best) … 63 (default 24)");
    r.option("bitrate").takes_value().value_name("KBPS").help("WebM: a target bit rate instead of a quality");
    r.option("speed").takes_value().value_name("N").help("WebM: libvpx's speed, 0 (smallest) … 9 (fastest) (default 5)");
    r.option("realtime").help("WebM: libvpx's real-time mode (the fastest encodes)");
    r.option("lossless").help("WebM: every Y'CbCr sample kept");
    r.option("threads").takes_value().value_name("N").help("WebM: encoder threads (1, the default, gives the same bytes everywhere)");
    r.option("audio-bitrate").takes_value().value_name("KBPS").help("WebM: Opus (default 128)");
    (void)a.subcommand(std::move(r));
    cmd::App s = with_file("still", "One frame as a PNG");
    s.option("frame").takes_value().value_name("N").help("the frame");
    s.option("at").takes_value().value_name("SECONDS").help("the frame shown at a time");
    s.option("out").takes_value().value_name("FILE.png").help("where");
    (void)a.subcommand(std::move(s));
    (void)a.subcommand(with_file("check", "Whether a description reads: its problems, or its size, rate, frames and samples"));
    cmd::App pr{"probe"};
    (void)pr.description("What a WebM, Y4M or WAV file holds (a WebM decoded: its frames and samples counted)");
    pr.arg("file").required().help("a .webm, .y4m or .wav file");
    (void)a.subcommand(std::move(pr));
    cmd::App t{"timing"};
    (void)t.description("Each frame's first sample and length, and its start in nanoseconds");
    t.option("fps").takes_value().value_name("RATE").help("30, 30000/1001, 29.97");
    t.option("rate").takes_value().value_name("HZ").help("samples a second (48000)");
    t.option("frames").takes_value().value_name("N").help("how many (default: a second's)");
    (void)a.subcommand(std::move(t));
    return a;
}

int video(const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    if (!p.has_subcommand()) {
        err << "usage: omui video <render|still|check|probe|timing> …\n";
        return 2;
    }
    const cmd::ParsedArgs& sub = p.subcommand()->get();
    const std::string_view name = p.subcommand_name();
    if (name == "render") return render(sub, out, err);
    if (name == "still") return still(sub, out, err);
    if (name == "check") return check_video(sub, out, err);
    if (name == "probe") return probe(sub, out);
    if (name == "timing") return timing(sub, out, err);
    err << "omui video: no command `" << name << "`\n";
    return 2;
}

}  // namespace omui::cli
