// omui-music — pieces of music from the command line (D-61: JSON out and an
// exit code, for people, agents and CI alike).
//
//     omui-music check <file.music.omui.json>   (what a .music.omui.ts evaluates to: omui-gen snapshot)
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
import mcpplibs.cmdline;
import nlohmann.json;
import xengine.audio;
import mlibs.math;

namespace audio = xengine::audio;
namespace cmdline = mcpplibs::cmdline;

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
    for (const auto& p : problems) a.push_back({{"file", p.file}, {"line", p.line}, {"column", p.column}, {"at", p.at}, {"message", p.message}, {"hint", p.hint}, {"text", p.text()}});
    return a;
}

double db(double linear) { return linear > 0 ? 20 * mlibs::math::det::log(linear) / 2.302585092994046 : -200.0; }

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

std::optional<RenderArgs> render_args(const cmdline::ParsedArgs& args) {
    RenderArgs a;
    if (auto out = args.value("out")) a.out = path_of(*out);
    a.seconds = std::clamp(args.value_as<double>("seconds").value_or(60.0), 0.0, 3600.0);
    if (auto intensity = args.value("intensity")) {
        auto c = cues(*intensity);
        if (!c) return std::nullopt;
        a.cues = std::move(*c);
    }
    if (auto solo = args.value("solo")) a.solo = list(*solo);
    a.float32 = args.is_flag_set("float");
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

int takes(const std::filesystem::path& file, const cmdline::ParsedArgs& args) {
    const double threshold = args.value_as<double>("threshold").value_or(-40), gap = args.value_as<double>("gap").value_or(0.12);
    const std::filesystem::path out = path_of(args.value("out").value_or(""));
    const std::string name = args.value("name").value_or("take");
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
        j["peak_db"] = t.peak > 0 ? 20 * mlibs::math::det::log(static_cast<double>(t.peak)) / mlibs::math::det::log(10.0) : -200.0;
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

// The command line (mcpplibs.cmdline): check, render, stats, fmt, takes.
cmdline::App command_line() {
    using cmdline::App, cmdline::Arg, cmdline::Option;
    const auto file = Arg("file").required().help("the piece: a .music.omui.json, as a .music.omui.ts evaluates to (omui-gen snapshot)");
    const auto seconds = Option("seconds").value_type<double>().help("how long (default 60, at most 3600)");
    const auto intensity = Option("intensity").takes_value().value_name("0.8 | 0.2@0,0.9@30").help("the intensity, or its cues");
    return App("omui-music")
        .description("Pieces of music from the command line: JSON out, exit 0 done / 1 problems / 2 a wrong command")
        .subcommand(App("check").description("whether it reads and plays: its problems, or its tempo, metre, bars and seconds").arg(file))
        .subcommand(App("render").description("rendered offline (the same bits everywhere): a WAV, its hash and levels").arg(file)
                        .option(Option("out").takes_value().value_name("X.WAV").help("the WAV written"))
                        .option(seconds).option(intensity)
                        .option(Option("solo").takes_value().value_name("A,B").help("only these instruments"))
                        .option(Option("float").help("32-bit float samples")))
        .subcommand(App("stats").description("the mix's levels, and each instrument's alone").arg(file).option(seconds).option(intensity))
        .subcommand(App("fmt").description("the file as write_music writes it").arg(file))
        .subcommand(App("takes").description("the takes in a recording and what each is like").arg(Arg("recording").required().help("a WAV"))
                        .option(Option("threshold").value_type<double>().value_name("DB").help("the level a take starts at (default -40)"))
                        .option(Option("gap").value_type<double>().value_name("S").help("the silence between takes (default 0.12)"))
                        .option(Option("out").takes_value().value_name("DIR").help("each take written as <name>-<n>.wav"))
                        .option(Option("name").takes_value().help("the takes' name (default take)")));
}

}  // namespace

int main(int argc, char** argv) {
    const cmdline::App app = command_line();
    auto parsed = app.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::println(std::cerr, "omui-music: {}", parsed.error().message);
        return usage();
    }
    if (!parsed->has_subcommand()) return usage();
    const std::string command(parsed->subcommand_name());
    const cmdline::ParsedArgs& args = parsed->subcommand()->get();
    const auto file = path_of(args.positional(0));
    if (command == "check") return check(file);
    if (command == "fmt") return fmt(file);
    if (command == "takes") return takes(file, args);
    const auto a = render_args(args);
    if (!a) return usage();
    return command == "render" ? render(file, *a) : stats(file, *a);
}
