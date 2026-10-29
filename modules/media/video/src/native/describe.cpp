// Reading and writing *.video.omui.toml. toml++ reports a malformed file by
// throwing; this unit — the library's adapter — turns that into a problem at
// its line and column, as it does every other.
module openmui.video;

import std;
import tomlplusplus;
import openmui.audio;

namespace omui::video {

namespace {

constexpr std::array<std::string_view, 4> top_keys{"doc", "video", "frames", "audio"};
constexpr std::array<std::string_view, 5> video_keys{"width", "height", "fps", "seconds", "frames"};
constexpr std::array<std::string_view, 3> frames_keys{"card", "png", "view"};
constexpr std::array<std::string_view, 8> audio_keys{"rate", "music", "intensity", "sounds", "recordings", "cues", "beeps", "doc"};
constexpr std::array<std::string_view, 6> cue_keys{"sound", "at", "frame", "gain", "pan", "pitch"};
constexpr std::array<std::string_view, 1> cards{"sync"};

std::size_t distance(std::string_view a, std::string_view b) {
    const std::size_t w = b.size() + 1;
    std::vector<std::size_t> d((a.size() + 1) * w);
    for (std::size_t i = 0; i <= a.size(); ++i) d[i * w] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) d[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
        {
            std::size_t v = std::min({d[(i - 1) * w + j] + 1, d[i * w + j - 1] + 1, d[(i - 1) * w + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[(i - 2) * w + j - 2] + 1);  // a transposition
            d[i * w + j] = v;
        }
    return d[a.size() * w + b.size()];
}

template <std::size_t N>
std::string nearest(std::string_view name, const std::array<std::string_view, N>& options) {
    std::string_view best;
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) d = x, best = o;
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", o);
    return std::format("one of {}", all);
}

struct Reader {
    std::string          file;
    std::vector<Problem> out;

    void error(const toml::node* n, std::string message, std::string hint = {}) {
        int line = 0, column = 0;
        if (n) line = static_cast<int>(n->source().begin.line), column = static_cast<int>(n->source().begin.column);
        out.push_back({file, line, column, std::move(message), std::move(hint)});
    }
    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end()) {
                const auto s = k.source();
                out.push_back({file, static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::format("{} has no key `{}`", what, k.str()),
                               nearest(k.str(), keys)});
            }
    }
    const toml::table* table(const toml::node* n, std::string_view what) {
        if (!n) return nullptr;
        if (const toml::table* t = n->as_table()) return t;
        error(n, std::format("{} is a table", what));
        return nullptr;
    }
    std::optional<double> number(const toml::node* n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        std::optional<double> v;
        if (auto d = n->value_exact<double>()) v = *d;
        else if (auto i = n->value_exact<std::int64_t>()) v = static_cast<double>(*i);
        if (!v) {
            error(n, std::format("{} is a number", what));
            return std::nullopt;
        }
        if (!(*v >= lo && *v <= hi)) {
            error(n, std::format("{} is {}–{}, not {}", what, lo, hi, *v));
            return std::nullopt;
        }
        return v;
    }
    std::optional<std::int64_t> whole(const toml::node* n, std::string_view what, std::int64_t lo, std::int64_t hi) {
        if (!n) return std::nullopt;
        const auto i = n->value_exact<std::int64_t>();
        if (!i) {
            error(n, std::format("{} is a whole number", what));
            return std::nullopt;
        }
        if (*i < lo || *i > hi) {
            error(n, std::format("{} is {}–{}, not {}", what, lo, hi, *i));
            return std::nullopt;
        }
        return i;
    }
    std::optional<std::string> text(const toml::node* n, std::string_view what, std::string_view example) {
        if (!n) return std::nullopt;
        if (auto s = n->value_exact<std::string>()) {
            if (!s->empty()) return s;
        }
        error(n, std::format("{} is a name", what), std::string(example));
        return std::nullopt;
    }
};

// A number as its shortest text that reads back as itself.
std::string num(double v) { return std::format("{}", v); }
std::string num(float v) { return std::format("{}", v); }

std::string toml_string(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string key(std::string_view k) {
    const bool bare = !k.empty() && std::ranges::all_of(k, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
    return bare ? std::string(k) : toml_string(k);
}

}  // namespace

std::expected<Description, std::vector<Problem>> read_description(std::string_view text, std::string_view file) {
    Reader r{std::string(file), {}};
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(std::vector<Problem>{{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), {}}});
    }
    Description d;
    r.only(root, top_keys, "a video description");
    if (const toml::node* doc = root.get("doc")) {
        if (auto s = doc->value_exact<std::string>()) d.doc = *s;
        else r.error(doc, "`doc` is text");
    }

    const toml::table* v = r.table(root.get("video"), "[video]");
    if (!v) {
        if (!root.get("video")) r.error(nullptr, "a video description has a [video] table", "[video] width = 1280, height = 720, fps = 30, seconds = 3");
    } else {
        r.only(*v, video_keys, "[video]");
        if (auto w = r.whole(v->get("width"), "`width`", 2, 16384)) d.width = static_cast<std::int32_t>(*w);
        if (auto h = r.whole(v->get("height"), "`height`", 2, 16384)) d.height = static_cast<std::int32_t>(*h);
        if (const toml::node* f = v->get("fps")) {
            std::optional<hanim::FrameRate> rate;
            if (auto i = f->value_exact<std::int64_t>(); i && *i > 0 && *i <= 1000) rate = hanim::FrameRate{*i, 1};
            else if (auto s = f->value_exact<std::string>()) rate = parse_rate(*s);
            if (rate && rate->num > 0 && rate->den > 0 && rate->num <= 1000 * rate->den) d.fps = *rate;
            else r.error(f, "`fps` is a whole number of frames a second or a fraction", "fps = 30, or fps = \"30000/1001\"");
        }
        const toml::node* secs = v->get("seconds");
        const toml::node* frames = v->get("frames");
        if (secs && frames) r.error(frames, "[video] has `seconds` or `frames`, not both");
        else if (secs) {
            if (auto s = r.number(secs, "`seconds`", 0.001, 24 * 3600)) d.seconds = *s;
        } else if (frames) {
            if (auto n = r.whole(frames, "`frames`", 1, 24 * 3600 * 240)) d.frames = *n;
        } else r.error(v, "[video] says how long: `seconds` or `frames`", "seconds = 3");
        if (d.seconds > 0) d.frames = Timing{d.fps, d.rate}.frames_in(d.seconds);
    }

    if (const toml::table* f = r.table(root.get("frames"), "[frames]")) {
        r.only(*f, frames_keys, "[frames]");
        int sources = 0;
        for (const auto [name, kind] : {std::pair{"card", FramesFrom::card}, std::pair{"png", FramesFrom::png}, std::pair{"view", FramesFrom::view}}) {
            const toml::node* n = f->get(name);
            if (!n) continue;
            if (++sources > 1) {
                r.error(n, "[frames] comes from one of `card`, `png` and `view`");
                continue;
            }
            auto s = r.text(n, std::format("`{}`", name), kind == FramesFrom::card ? "card = \"sync\"" : kind == FramesFrom::png ? "png = \"shots/{:05}.png\"" : "view = \"intro.view.omui.json\"");
            if (!s) continue;
            d.from = kind;
            d.source = *s;
            if (kind == FramesFrom::card && std::ranges::find(cards, *s) == cards.end()) r.error(n, std::format("no card `{}`", *s), nearest(*s, cards));
            if (kind == FramesFrom::png && !frame_name(*s, 0)) r.error(n, "`png` is a pattern with the frame's number in it", "png = \"shots/{:05}.png\"");
        }
        if (sources == 0) r.error(f, "[frames] comes from one of `card`, `png` and `view`", "card = \"sync\"");
    }

    if (const toml::table* a = r.table(root.get("audio"), "[audio]")) {
        d.audio = true;
        r.only(*a, audio_keys, "[audio]");
        if (auto rate = r.whole(a->get("rate"), "`rate`", 8000, 192000)) d.rate = static_cast<std::int32_t>(*rate);
        if (auto m = r.text(a->get("music"), "`music`", "music = \"theme.music.omui.toml\"")) d.music = *m;
        if (auto s = r.text(a->get("sounds"), "`sounds`", "sounds = \"ui.sound.omui.toml\"")) d.sounds = *s;
        if (const toml::node* b = a->get("beeps")) {
            if (auto x = b->value_exact<bool>()) d.beeps = *x;
            else r.error(b, "`beeps` is true or false");
        }
        if (const toml::node* i = a->get("intensity")) {
            if (const toml::array* list = i->as_array()) {
                for (const toml::node& item : *list) {
                    const toml::table* t = item.as_table();
                    if (!t) {
                        r.error(&item, "an intensity is { at = <seconds>, value = <0–1> }");
                        continue;
                    }
                    r.only(*t, std::array<std::string_view, 2>{"at", "value"}, "an intensity");
                    Intensity c;
                    if (auto at = r.number(t->get("at"), "an intensity's `at`", 0, 24 * 3600)) c.at = *at;
                    if (auto x = r.number(t->get("value"), "an intensity's `value`", 0, 1)) c.value = static_cast<float>(*x);
                    else if (!t->get("value")) r.error(t, "an intensity has a `value`");
                    d.intensity.push_back(c);
                }
            } else if (auto x = r.number(i, "`intensity`", 0, 1)) d.intensity.push_back({0, static_cast<float>(*x)});
        }
        if (const toml::table* rec = r.table(a->get("recordings"), "`recordings`")) {
            for (const auto& [k, f] : *rec)
                if (auto s = r.text(&f, std::format("recording `{}`", k.str()), "voice = \"voice.wav\"")) d.recordings[std::string(k.str())] = *s;
        }
        if (const toml::node* c = a->get("cues")) {
            const toml::array* list = c->as_array();
            if (!list) r.error(c, "`cues` is a list", "cues = [{ sound = \"click\", frame = 30 }]");
            else
                for (const toml::node& item : *list) {
                    const toml::table* t = item.as_table();
                    if (!t) {
                        r.error(&item, "a cue is { sound = <name>, frame = <n> } or { sound = <name>, at = <seconds> }");
                        continue;
                    }
                    r.only(*t, cue_keys, "a cue");
                    SoundCue cue;
                    if (auto s = r.text(t->get("sound"), "a cue's `sound`", "sound = \"click\"")) cue.sound = *s;
                    else if (!t->get("sound")) r.error(t, "a cue has a `sound`");
                    if (t->get("at") && t->get("frame")) r.error(t->get("frame"), "a cue has `at` or `frame`, not both");
                    else if (auto at = r.number(t->get("at"), "a cue's `at`", 0, 24 * 3600)) cue.at = *at;
                    else if (auto fr = r.whole(t->get("frame"), "a cue's `frame`", 0, 24 * 3600 * 240)) cue.frame = *fr;
                    if (auto g = r.number(t->get("gain"), "a cue's `gain`", 0, 8)) cue.gain = static_cast<float>(*g);
                    if (auto p = r.number(t->get("pan"), "a cue's `pan`", -1, 1)) cue.pan = static_cast<float>(*p);
                    if (auto p = r.number(t->get("pitch"), "a cue's `pitch`", 0.125, 8)) cue.pitch = static_cast<float>(*p);
                    d.cues.push_back(std::move(cue));
                }
        }
    }
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return d;
}

std::string write_description(const Description& d) {
    std::string s;
    if (!d.doc.empty()) s += std::format("doc = {}\n\n", toml_string(d.doc));
    s += std::format("[video]\nwidth = {}\nheight = {}\n", d.width, d.height);
    s += d.fps.den == 1 ? std::format("fps = {}\n", d.fps.num) : std::format("fps = \"{}\"\n", rate_text(d.fps));
    s += d.seconds > 0 ? std::format("seconds = {}\n", num(d.seconds)) : std::format("frames = {}\n", d.frames);
    constexpr std::array<std::string_view, 3> from{"card", "png", "view"};
    s += std::format("\n[frames]\n{} = {}\n", from[static_cast<std::size_t>(d.from)], toml_string(d.source));
    if (d.audio) {
        s += "\n[audio]\n";
        if (d.rate != 48000) s += std::format("rate = {}\n", d.rate);
        if (!d.music.empty()) s += std::format("music = {}\n", toml_string(d.music));
        if (d.intensity.size() == 1 && d.intensity[0].at == 0) s += std::format("intensity = {}\n", num(d.intensity[0].value));
        else if (!d.intensity.empty()) {
            s += "intensity = [";
            for (std::size_t i = 0; i < d.intensity.size(); ++i)
                s += std::format("{}{{ at = {}, value = {} }}", i ? ", " : "", num(d.intensity[i].at), num(d.intensity[i].value));
            s += "]\n";
        }
        if (!d.sounds.empty()) s += std::format("sounds = {}\n", toml_string(d.sounds));
        if (!d.recordings.empty()) {
            s += "recordings = { ";
            bool first = true;
            for (const auto& [k, f] : d.recordings) s += std::format("{}{} = {}", std::exchange(first, false) ? "" : ", ", key(k), toml_string(f));
            s += " }\n";
        }
        if (!d.cues.empty()) {
            s += "cues = [\n";
            for (const SoundCue& c : d.cues) {
                s += std::format("    {{ sound = {}, ", toml_string(c.sound));
                s += c.frame >= 0 ? std::format("frame = {}", c.frame) : std::format("at = {}", num(c.at));
                if (c.gain != 1) s += std::format(", gain = {}", num(c.gain));
                if (c.pan != 0) s += std::format(", pan = {}", num(c.pan));
                if (c.pitch != 1) s += std::format(", pitch = {}", num(c.pitch));
                s += " },\n";
            }
            s += "]\n";
        }
        if (d.beeps) s += "beeps = true\n";
    }
    return s;
}

}  // namespace omui::video
