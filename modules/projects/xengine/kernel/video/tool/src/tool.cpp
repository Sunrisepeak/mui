// xengine.video.tool — films without a GUI for people, agents and CI: the subject `video`, its catalog's operations
// (video.ops.xdef.ts). Moved here from omui's command line (convergence plan RC3.3): a film is xengine.video's;
// only a view's frames are omui's, given through Sources by a composition.
module xengine.video.tool;

import std;
import mlibs.agent;
import mlibs.image;
import mlibs.xdef.tool;
import mcpplibs.os;
import openas;
import nlohmann.json;
import xengine.video;
import xengine.video.webm;
import xengine.audio;

namespace xengine::video::tool {

namespace {

namespace vid = xengine::video;
using Json = nlohmann::json;

// A report built a field at a time (the records the operations write).
struct Record {
    Json json = Json::object();
    template <class T>
    Record& set(std::string_view key, T&& value) {
        json[std::string(key)] = std::forward<T>(value);
        return *this;
    }
};

std::filesystem::path path_of(std::string_view text) { return mcpplibs::os::fs::from_utf8(text); }

std::string utf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

// A data file's JSON: a `.xdef.ts` module evaluated through a workspace's registry of kinds (mlibs.xdef) — its own,
// else the one the command runs in — else the file as it is.
std::expected<std::string, std::string> data_text(const std::filesystem::path& file) {
    if (utf8(file.filename()).ends_with(".xdef.ts")) {
        auto workspace = mlibs::xdef::workspace_of(std::filesystem::absolute(file));
        if (!workspace) workspace = mlibs::xdef::workspace_of(std::filesystem::current_path());
        if (!workspace)
            return std::unexpected(std::format("{}: a data module is evaluated with a workspace's kinds, and neither it nor this directory is in one "
                                               "(run it from a workspace, or give its .json)", utf8(file)));
        auto registry = mlibs::xdef::Registry::read(*workspace);
        if (!registry) return std::unexpected(registry.error());
        auto snapshot = mlibs::xdef::snapshot(file, *registry);
        if (!snapshot) return std::unexpected(snapshot.error().text());
        return std::move(*snapshot);
    }
    auto text = mcpplibs::os::fs::read(file);
    if (!text) return std::unexpected(std::format("{}: {}", utf8(file), text.error().message()));
    return std::move(*text);
}

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

Json problems_value(std::span<const vid::Problem> problems) {
    Json list = Json::array();
    for (const vid::Problem& p : problems) {
        Json v{{"file", p.file}, {"line", p.line}, {"column", p.column}, {"at", p.at}, {"message", p.message}};
        if (!p.hint.empty()) v["hint"] = p.hint;
        v["text"] = p.text();
        list.push_back(std::move(v));
    }
    return list;
}

int print(std::ostream& out, const Json& report, int code) {
    out << report.dump() << '\n';
    return code;
}

int print(std::ostream& out, const Record& report, int code) { return print(out, report.json, code); }

int failed(std::ostream& out, Json report, std::string message) {
    report["ok"] = false;
    report["error"] = std::move(message);
    return print(out, report, 1);
}
int failed(std::ostream& out, Record report, std::string message) { return failed(out, std::move(report.json), std::move(message)); }

// No pictures (a WAV alone): nothing drawn.
class NoFrames final : public vid::FrameSource {
public:
    std::expected<mlibs::image::View, std::string> frame(std::int64_t) override { return mlibs::image::View{pixel_.data(), 1, 1, 4, mlibs::image::Alpha::straight}; }

private:
    std::array<std::uint8_t, 4> pixel_{0, 0, 0, 255};
};

struct Loaded {
    vid::Description      d;
    std::filesystem::path file, base;
};

// A description, or a view document taken as one; the size, rate and length
// given on the command line over what the file says.
std::expected<Loaded, int> load(const mlibs::agent::Arguments& p, Record& report, std::ostream& out, std::ostream& err) {
    Loaded l;
    l.file = path_of(p.positional(0));
    l.base = l.file.parent_path();
    report.set("file", utf8(l.file));
    const std::string name = utf8(l.file.filename());
    if (name.ends_with(".view.xdef.json")) {
        l.d.from = vid::FramesFrom::view;
        l.d.source = name;
        l.d.seconds = 3;
    } else {
        // A description module (`*.video.xdef.ts`) evaluated, or its JSON (D-139).
        auto text = data_text(l.file);
        if (!text) return std::unexpected(failed(out, report, std::format("cannot read the file: {}", text.error())));
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
            err << "video: --size is WIDTHxHEIGHT (1280x720)\n";
            return std::unexpected(2);
        }
        l.d.width = w, l.d.height = h;
    }
    if (auto f = p.value("fps")) {
        auto rate = vid::parse_rate(*f);
        if (!rate) {
            err << "video: --fps is a rate (30, 30000/1001, 29.97)\n";
            return std::unexpected(2);
        }
        l.d.fps = *rate;
    }
    if (auto s = p.value("seconds")) {
        double v = 0;
        const auto [q, e] = std::from_chars(s->data(), s->data() + s->size(), v);
        if (e != std::errc{} || q != s->data() + s->size() || !(v > 0)) {
            err << "video: --seconds is a length (2.5)\n";
            return std::unexpected(2);
        }
        l.d.seconds = v;
    }
    if (auto n = p.value("frames")) {
        std::int64_t v = 0;
        const auto [q, e] = std::from_chars(n->data(), n->data() + n->size(), v);
        if (e != std::errc{} || q != n->data() + n->size() || v <= 0) {
            err << "video: --frames is a count\n";
            return std::unexpected(2);
        }
        l.d.seconds = 0;
        l.d.frames = v;
    }
    if (l.d.seconds > 0) l.d.frames = vid::Timing{l.d.fps, l.d.rate}.frames_in(l.d.seconds);
    return l;
}

std::expected<std::unique_ptr<vid::FrameSource>, std::string> frames_of(const Loaded& l, const Sources& sources) {
    if (l.d.from == vid::FramesFrom::view) {
        // A view's frames are omui's: a composition gives them (xtools adds omui's view source).
        if (!sources.view)
            return std::unexpected(std::format("{}: a view's frames are drawn by omui — run this through xtools (`xtools video.render …`), which adds "
                                               "omui's view source, or render the frames to PNG files and describe those",
                                               l.d.source));
        return sources.view(l.d, l.base / path_of(l.d.source));
    }
    auto f = vid::frame_source(l.d, l.base);
    if (!f) return std::unexpected(f.error().front().text());
    return std::move(*f);
}

Record describe(const vid::Description& d) {
    const vid::Format f = d.format();
    Record v;
    constexpr std::array<std::string_view, 3> from{"card", "png", "view"};
    v.set("width", d.width).set("height", d.height).set("fps", vid::rate_text(d.fps)).set("frames", d.frames).set("seconds", f.seconds());
    v.set("frames_from", Json{{std::string(from[static_cast<std::size_t>(d.from)]), d.source}});
    if (d.audio) {
        std::int64_t lo = std::numeric_limits<std::int64_t>::max(), hi = 0;
        for (std::int64_t i = 0; i < std::min<std::int64_t>(d.frames, 100000); ++i) lo = std::min(lo, f.timing.samples_in(i)), hi = std::max(hi, f.timing.samples_in(i));
        v.set("rate", d.rate).set("samples", f.samples()).set("samples_per_frame", Json::array({lo, hi}));
    }
    return v;
}

int render(const mlibs::agent::Arguments& p, std::ostream& out, std::ostream& err, const Sources& sources) {
    Record report;
    report.set("command", "render");
    auto l = load(p, report, out, err);
    if (!l) return l.error();
    const auto out_value = p.value("out");
    if (!out_value) {
        err << "video render: --out names the file (x.y4m, x.wav, shots/{:05}.png, shots/)\n";
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
        err << "video render: the format is webm, y4m, png or wav (from --out's name, or --format)\n";
        return 2;
    }
    vid::WebmOptions webm;
    for (auto [name, field] : {std::pair{"quality", &webm.quality}, std::pair{"bitrate", &webm.bitrate}, std::pair{"speed", &webm.speed},
                               std::pair{"threads", &webm.threads}, std::pair{"audio-bitrate", &webm.audio_bitrate}})
        if (auto v = p.value(name)) {
            const auto [q, e] = std::from_chars(v->data(), v->data() + v->size(), *field);
            if (e != std::errc{} || q != v->data() + v->size()) {
                err << "video render: --" << name << " is a whole number\n";
                return 2;
            }
        }
    webm.realtime = p.flag("realtime");
    webm.lossless = p.flag("lossless");
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
    auto track = vid::soundtrack(l->d, l->base, [](const std::filesystem::path& file) { return data_text(file); });
    if (!track) {
        report.set("ok", false).set("problems", problems_value(track.error()));
        return print(out, report, 1);
    }
    std::unique_ptr<vid::FrameSource> frames;
    if (format == "wav") frames = std::make_unique<NoFrames>();
    else if (auto f = frames_of(*l, sources); f) frames = std::move(*f);
    else return failed(out, report, f.error());
    vid::DirectoryOutput output(dir.empty() ? std::filesystem::path(".") : dir);
    std::vector<vid::Encoder*> list;
    for (auto& e : encoders) list.push_back(e.get());
    const vid::Format f = l->d.format();
    auto done = vid::encode(f, *frames, *track, list, output);
    if (!done) return failed(out, report, done.error());
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Json files = Json::array();
    for (const vid::Written& w : done->files) {
        Record v;
        v.set("name", utf8(output.directory() / path_of(w.name))).set("bytes", w.bytes).set("hash", hex(w.hash));
        files.push_back(std::move(v.json));
    }
    report.set("ok", true).set("format", format);
    report.json.update(describe(l->d).json);
    report.set("files", std::move(files)).set("took_seconds", took).set("realtime", took > 0 ? f.seconds() / took : 0.0);
    return print(out, report, 0);
}

int still(const mlibs::agent::Arguments& p, std::ostream& out, std::ostream& err, const Sources& sources) {
    Record report;
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
        err << "video still: --frame N or --at SECONDS, and --out x.png\n";
        return 2;
    }
    if (index >= l->d.frames) return failed(out, report, std::format("frame {}: the video has {}", index, l->d.frames));
    auto frames = frames_of(*l, sources);
    if (!frames) return failed(out, report, frames.error());
    std::expected<mlibs::image::View, std::string> pixels;
    // A view is stepped there (its clock runs from 0); the card and files are asked directly.
    for (std::int64_t i = l->d.from == vid::FramesFrom::view ? 0 : index; i <= index; ++i) pixels = (*frames)->frame(i);
    if (!pixels) return failed(out, report, pixels.error());
    auto png = mlibs::image::encode_png(*pixels);
    if (!png) return failed(out, report, png.error());
    if (auto w = mcpplibs::os::fs::write_atomic(path_of(*target), std::string_view(reinterpret_cast<const char*>(png->data()), png->size())); !w)
        return failed(out, report, std::format("cannot write {}: {}", *target, w.error().message()));
    const vid::Timing t{l->d.fps, l->d.rate};
    report.set("ok", true).set("frame", index).set("time", static_cast<double>(t.time_of(index, 1'000'000)) / 1e6);
    report.set("out", *target).set("bytes", static_cast<std::int64_t>(png->size())).set("hash", hex(vid::hash(std::span<const std::uint8_t>(*png))));
    return print(out, report, 0);
}

int check_video(const mlibs::agent::Arguments& p, std::ostream& out, std::ostream& err) {
    Record report;
    report.set("command", "check");
    auto l = load(p, report, out, err);
    if (!l) return l.error();
    report.set("ok", true);
    report.json.update(describe(l->d).json);
    return print(out, report, 0);
}

int probe(const mlibs::agent::Arguments& p, std::ostream& out) {
    Record report;
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
        auto w = xengine::audio::read_wav(bytes);
        if (!w) return failed(out, report, w.error());
        report.set("ok", true).set("format", "wav").set("rate", w->rate).set("samples", static_cast<std::int64_t>(w->samples.size()));
        report.set("seconds", static_cast<double>(w->samples.size()) / w->rate);
        return print(out, report, 0);
    }
    return failed(out, report, "neither WebM, Y4M nor WAV");
}

int timing(const mlibs::agent::Arguments& p, std::ostream& out, std::ostream& err) {
    const auto rate = vid::parse_rate(p.value("fps").value_or("30"));
    std::int32_t hz = 48000;
    if (auto r = p.value("rate")) std::from_chars(r->data(), r->data() + r->size(), hz);
    if (!rate || hz <= 0) {
        err << "video timing: --fps RATE (30, 30000/1001) [--rate HZ]\n";
        return 2;
    }
    const vid::Timing t{*rate, hz};
    std::int64_t n = (rate->num + rate->den - 1) / rate->den;
    if (auto f = p.value("frames")) std::from_chars(f->data(), f->data() + f->size(), n);
    n = std::clamp<std::int64_t>(n, 1, 100000);
    Json frames = Json::array();
    for (std::int64_t i = 0; i < n; ++i) {
        Record v;
        v.set("frame", i).set("first_sample", t.sample_of(i)).set("samples", t.samples_in(i)).set("start_ns", t.time_of(i, 1'000'000'000));
        frames.push_back(std::move(v.json));
    }
    Record report;
    report.set("command", "timing").set("ok", true).set("fps", vid::rate_text(*rate)).set("rate", hz).set("frames", n).set("samples", t.sample_of(n));
    report.set("each", std::move(frames));
    return print(out, report, 0);
}

}  // namespace

int run(std::string_view operation, const mlibs::agent::Arguments& arguments, std::ostream& out, std::ostream& err, const Sources& sources) {
    if (operation == "render") return render(arguments, out, err, sources);
    if (operation == "still") return still(arguments, out, err, sources);
    if (operation == "check") return check_video(arguments, out, err);
    if (operation == "probe") return probe(arguments, out);
    if (operation == "timing") return timing(arguments, out, err);
    err << "video: no operation `" << operation << "` (render, still, check, probe, timing)\n";
    return 2;
}

mlibs::control::Guide Video::guide() const noexcept {
    static constexpr std::array<std::string_view, 2> first{R"({"op":"video.timing","fps":"30000/1001","frames":3})", R"({"op":"video.check","file":"card.video.xdef.ts"})"};
    static constexpr std::array<mlibs::control::Task, 1> tasks{{{"a-first-film", "How frames and samples line up, then a description checked before it is rendered", first}}};
    return {"Films on exact frame and sample counts: rendered to WebM, Y4M, PNG or WAV, a still, a description checked, a file probed.", tasks};
}

void Video::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    std::ostringstream out, err;
    const std::string_view last = operation.substr(operation.find('.') == std::string_view::npos ? 0 : operation.find('.') + 1);
    const int code = run(last, mlibs::agent::Arguments(request, last == "timing" ? "" : "file"), out, err, sources_);
    // Each record it wrote is a data record of the answer.
    std::istringstream lines(out.str());
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with('{')) answer.data(line);
    if (code != 0) {
        std::string said = err.str();
        while (!said.empty() && said.back() == '\n') said.pop_back();
        if (said.empty()) {
            // A failed record says why (its "error", or its problems).
            const Json last_record = Json::parse(out.str().substr(0, out.str().find('\n')), nullptr, false);
            said = last_record.is_object() && last_record.contains("error") ? last_record["error"].get<std::string>() : std::format("video {} failed: its problems are listed", last);
        }
        answer.fail(code == 2 ? openas::error_code::invalid_argument : std::string_view("failed"), said);
    }
}

}  // namespace xengine::video::tool
