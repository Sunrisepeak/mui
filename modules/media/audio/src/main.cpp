// omui-music — pieces of music from the command line (D-61: JSON out and an
// exit code, for people, agents and CI alike).
//
//     omui-music check <file.music.omui.toml>
//                       whether it reads and plays: its problems at their line and column, or
//                       its tempo, metre, bars and seconds (exit 1 with problems)
//     omui-music render <file> --out <x.wav> [--seconds 60] [--intensity 0.8 | 0.2@0,0.9@30]
//                       [--solo lead,bass] [--float]
//                       rendered offline (the same bits everywhere): a WAV, its hash and levels
//     omui-music stats <file> [--seconds 60] [--intensity …]
//                       the mix's levels — peak, RMS, loudness (LUFS), bands, the limiter's
//                       work — and each instrument's alone
//     omui-music fmt <file>
//                       the file as write_music writes it (the one way it is written)
//     omui-music takes <recording.wav> [--threshold -40] [--gap 0.12] [--out dir] [--name take]
//                       the takes in a recording (a pack's grunts, a session's lines) and what
//                       each is like — its length, peak, RMS, pitch, how voiced, how bright —
//                       to choose by; with --out, each written as <name>-<n>.wav (mono, its rate)
import std;
import nlohmann.json;
import openmui.audio;
import hanim;

namespace audio = omui::audio;

namespace {

std::filesystem::path path_of(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::optional<std::string> read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), {}};
}

int usage() {
    std::println(std::cerr, "omui-music check <file> | render <file> --out <x.wav> [--seconds s] [--intensity x | x@s,y@s] [--solo a,b] [--float] | "
                            "stats <file> [--seconds s] [--intensity …] | fmt <file> | takes <recording.wav> [--threshold dB] [--gap s] [--out dir] [--name n]");
    return 2;
}

std::vector<std::string> list(std::string_view s) {
    std::vector<std::string> v;
    for (auto part : std::views::split(s, ',')) v.emplace_back(part.begin(), part.end());
    return v;
}

// "0.8", or "0.2@0,0.9@30" (an intensity from a second on).
std::optional<std::vector<audio::Cue>> cues(std::string_view s) {
    std::vector<audio::Cue> v;
    for (const std::string& item : list(s)) {
        const auto at = item.find('@');
        audio::Cue c;
        const std::string value = item.substr(0, at);
        const auto [p, e] = std::from_chars(value.data(), value.data() + value.size(), c.intensity);
        if (e != std::errc{} || p != value.data() + value.size()) return std::nullopt;
        if (at != std::string::npos) {
            const std::string when = item.substr(at + 1);
            const auto [q, f] = std::from_chars(when.data(), when.data() + when.size(), c.at);
            if (f != std::errc{} || q != when.data() + when.size()) return std::nullopt;
        }
        v.push_back(c);
    }
    return v;
}

nlohmann::ordered_json problems_json(std::span<const audio::Problem> problems) {
    nlohmann::ordered_json a = nlohmann::ordered_json::array();
    for (const auto& p : problems) a.push_back({{"file", p.file}, {"line", p.line}, {"column", p.column}, {"message", p.message}, {"hint", p.hint}, {"text", p.text()}});
    return a;
}

double db(double linear) { return linear > 0 ? 20 * hanim::dm::log(linear) / 2.302585092994046 : -200.0; }

// A float as the double its shortest text is (0.35, not 0.3499999940395355).
double tidy(float f) {
    const std::string s = std::format("{}", f);
    double d = 0;
    std::from_chars(s.data(), s.data() + s.size(), d);
    return d;
}

nlohmann::ordered_json levels_json(const audio::Levels& l) {
    return {{"peak", tidy(l.peak)},
            {"peak_db", db(l.peak)},
            {"rms_db", l.rms},
            {"loudness_lufs", l.loudness},
            {"correlation", l.correlation},
            {"bands_db", {{"sub_80", l.bands[0]}, {"low_80_250", l.bands[1]}, {"mid_250_2k", l.bands[2]}, {"presence_2k_6k", l.bands[3]}, {"air_6k", l.bands[4]}}}};
}

nlohmann::ordered_json limiter_json(const audio::LimiterStats& s, std::size_t frames) {
    return {{"deepest_db", db(s.deepest)}, {"frames", s.frames}, {"share", frames ? static_cast<double>(s.frames) / static_cast<double>(frames) : 0.0}};
}

struct Loaded {
    audio::Music   music;
    nlohmann::ordered_json report;
};

// The piece, or 1 with its problems in the report.
std::expected<Loaded, int> load(std::string_view command, const std::filesystem::path& file) {
    nlohmann::ordered_json report = {{"command", command}, {"file", utf8(file)}};
    const auto text = read(file);
    if (!text) {
        report["ok"] = false;
        report["error"] = "cannot read the file";
        std::println("{}", report.dump(2));
        return std::unexpected(1);
    }
    auto music = audio::read_music(*text, utf8(file.filename()));
    if (!music) {
        report["ok"] = false;
        report["problems"] = problems_json(music.error());
        std::println("{}", report.dump(2));
        return std::unexpected(1);
    }
    return Loaded{std::move(*music), std::move(report)};
}

int check(const std::filesystem::path& file) {
    auto loaded = load("check", file);
    if (!loaded) return loaded.error();
    auto& [m, report] = *loaded;
    // It reads: does it play (every name it plays)?
    audio::Mixer mixer;
    if (auto s = audio::Sequencer::attach(m, mixer); !s) {
        report["ok"] = false;
        report["problems"] = problems_json(s.error());
        std::println("{}", report.dump(2));
        return 1;
    }
    const double bar = m.bar_seconds();
    const auto intro = m.bars(m.arrangement.intro), loop = m.bars(m.arrangement.loop), outro = m.bars(m.arrangement.outro);
    report["ok"] = true;
    report["name"] = m.name;
    report["tempo"] = tidy(m.tempo);
    report["meter"] = m.meter;
    report["bars"] = {{"intro", intro}, {"loop", loop}, {"outro", outro}};
    report["seconds"] = {{"bar", bar}, {"intro", intro * bar}, {"loop", loop * bar}, {"outro", outro * bar}};
    nlohmann::ordered_json instruments = nlohmann::ordered_json::array();
    for (const auto& in : m.instruments) instruments.push_back({{"name", in.name}, {"patch", in.patch}});
    report["instruments"] = instruments;
    report["patterns"] = m.patterns.size();
    report["sections"] = m.sections.size();
    nlohmann::ordered_json sections = nlohmann::ordered_json::array();
    for (const auto& s : m.sections) {
        // The intensities its layers change at.
        std::set<double> edges;
        for (const auto& l : s.layers) edges.insert(tidy(l.intensity[0])), edges.insert(tidy(l.intensity[1]));
        sections.push_back({{"name", s.name}, {"bars", s.bars}, {"layers", s.layers.size()}, {"intensity_edges", edges}});
    }
    report["section_list"] = sections;
    std::println("{}", report.dump(2));
    return 0;
}

struct RenderArgs {
    std::filesystem::path   out;
    double                  seconds = 60;
    std::vector<audio::Cue> cues;
    std::vector<std::string> solo;
    bool                    float32 = false;
};

std::optional<RenderArgs> args(int argc, char** argv, int from) {
    RenderArgs a;
    for (int i = from; i < argc; ++i) {
        const std::string_view s = argv[i];
        const bool more = i + 1 < argc;
        if (s == "--out" && more) a.out = path_of(argv[++i]);
        else if (s == "--seconds" && more) a.seconds = std::clamp(std::atof(argv[++i]), 0.0, 3600.0);
        else if (s == "--intensity" && more) {
            auto c = cues(argv[++i]);
            if (!c) return std::nullopt;
            a.cues = std::move(*c);
        } else if (s == "--solo" && more) a.solo = list(argv[++i]);
        else if (s == "--float") a.float32 = true;
        else return std::nullopt;
    }
    return a;
}

int render(const std::filesystem::path& file, const RenderArgs& a) {
    auto loaded = load("render", file);
    if (!loaded) return loaded.error();
    auto& [m, report] = *loaded;
    const auto t0 = std::chrono::steady_clock::now();
    auto r = audio::render_music(m, a.seconds, a.cues, a.solo);
    if (!r) {
        report["ok"] = false;
        report["problems"] = problems_json(r.error());
        std::println("{}", report.dump(2));
        return 1;
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const std::size_t frames = r->samples.size() / 2;
    report["ok"] = true;
    report["seconds"] = a.seconds;
    report["rate"] = 48000;
    report["hash"] = std::format("{:016x}", audio::hash(r->samples));
    report["levels"] = levels_json(audio::levels(r->samples, 48000));
    report["limiter"] = limiter_json(r->limiter, frames);
    report["realtime"] = took > 0 ? a.seconds / took : 0.0;
    if (!a.out.empty()) {
        const auto w = audio::wav(r->samples, 48000, a.float32);
        std::ofstream f(a.out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        if (!f) {
            report["ok"] = false;
            report["error"] = "cannot write the WAV";
            std::println("{}", report.dump(2));
            return 1;
        }
        report["out"] = utf8(a.out);
        report["bytes"] = w.size();
    }
    std::println("{}", report.dump(2));
    return 0;
}

int stats(const std::filesystem::path& file, const RenderArgs& a) {
    auto loaded = load("stats", file);
    if (!loaded) return loaded.error();
    auto& [m, report] = *loaded;
    auto mix = audio::render_music(m, a.seconds, a.cues);
    if (!mix) {
        report["ok"] = false;
        report["problems"] = problems_json(mix.error());
        std::println("{}", report.dump(2));
        return 1;
    }
    report["ok"] = true;
    report["seconds"] = a.seconds;
    report["mix"] = levels_json(audio::levels(mix->samples, 48000));
    report["limiter"] = limiter_json(mix->limiter, mix->samples.size() / 2);
    nlohmann::ordered_json each = nlohmann::ordered_json::object();
    for (const auto& in : m.instruments) {
        const std::array<std::string, 1> solo{in.name};
        auto r = audio::render_music(m, a.seconds, a.cues, solo);
        if (r) each[in.name] = levels_json(audio::levels(r->samples, 48000));
    }
    report["instruments"] = each;
    std::println("{}", report.dump(2));
    return 0;
}

int takes(const std::filesystem::path& file, int argc, char** argv) {
    double threshold = -40, gap = 0.12;
    std::filesystem::path out;
    std::string name = "take";
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string_view k = argv[i], v = argv[i + 1];
        auto number = [&](double& to) { return std::from_chars(v.data(), v.data() + v.size(), to).ec == std::errc{}; };
        if (k == "--threshold" && number(threshold)) continue;
        if (k == "--gap" && number(gap)) continue;
        if (k == "--out") { out = path_of(v); continue; }
        if (k == "--name") { name = std::string(v); continue; }
        return usage();
    }
    if (argc % 2 == 0) return usage();
    nlohmann::ordered_json report;
    report["command"] = "takes";
    report["file"] = utf8(file);
    const auto bytes = read(file);
    auto r = bytes ? audio::read_wav(std::as_bytes(std::span(bytes->data(), bytes->size())))
                   : std::expected<audio::Recording, std::string>(std::unexpected(std::string("cannot read it")));
    if (!r) {
        report["ok"] = false;
        report["error"] = r.error();
        std::println("{}", report.dump(2));
        return 1;
    }
    if (!out.empty()) std::filesystem::create_directories(out);
    report["ok"] = true;
    report["rate"] = r->rate;
    report["seconds"] = static_cast<double>(r->samples.size()) / r->rate;
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    const auto found = audio::takes(*r, threshold, gap);
    for (std::size_t n = 0; n < found.size(); ++n) {
        const audio::Take& t = found[n];
        nlohmann::ordered_json j;
        j["take"] = n + 1;
        j["at"] = static_cast<double>(t.begin) / r->rate;
        j["seconds"] = static_cast<double>(t.end - t.begin) / r->rate;
        j["peak_db"] = t.peak > 0 ? 20 * std::log10(static_cast<double>(t.peak)) : -200.0;
        j["rms_db"] = t.rms;
        j["pitch_hz"] = t.pitch;
        j["voiced"] = t.voiced;
        j["brightness_hz"] = t.brightness;
        if (!out.empty()) {
            const auto part = std::span(r->samples).subspan(static_cast<std::size_t>(t.begin), static_cast<std::size_t>(t.end - t.begin));
            const auto w = audio::wav(part, r->rate, false, 1);
            const auto to = out / std::format("{}-{}.wav", name, n + 1);
            std::ofstream f(to, std::ios::binary);
            f.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
            j["out"] = utf8(to);
        }
        list.push_back(j);
    }
    report["takes"] = list;
    std::println("{}", report.dump(2));
    return 0;
}

int fmt(const std::filesystem::path& file) {
    const auto text = read(file);
    if (!text) return usage();
    auto music = audio::read_music(*text, utf8(file.filename()));
    if (!music) {
        for (const auto& p : music.error()) std::println(std::cerr, "{}", p.text());
        return 1;
    }
    std::print("{}", audio::write_music(*music));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string_view command = argv[1];
    const auto file = path_of(argv[2]);
    if (command == "check" && argc == 3) return check(file);
    if (command == "fmt" && argc == 3) return fmt(file);
    if (command == "takes") return takes(file, argc, argv);
    if (command == "render" || command == "stats") {
        const auto a = args(argc, argv, 3);
        if (!a) return usage();
        return command == "render" ? render(file, *a) : stats(file, *a);
    }
    return usage();
}
