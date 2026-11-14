// Reading and writing video descriptions: the JSON a `*.video.omui.ts`
// evaluates to (D-139), every problem at its JSON pointer and its line and
// column.
module xengine.video;

import std;
import mlibs.json;
import mlibs.names;
import xengine.audio;

namespace xengine::video {

namespace {

using mlibs::json::Node;

constexpr std::array<std::string_view, 8> video_keys{"doc", "width", "height", "fps", "seconds", "frames", "from", "audio"};
constexpr std::array<std::string_view, 3> from_keys{"card", "png", "view"};
constexpr std::array<std::string_view, 8> audio_keys{"rate", "music", "intensity", "sounds", "recordings", "cues", "beeps", "doc"};
constexpr std::array<std::string_view, 6> cue_keys{"sound", "at", "frame", "gain", "pan", "pitch"};
constexpr std::array<std::string_view, 1> cards{"sync"};

struct Reader {
    const mlibs::json::Document& doc;
    std::vector<Problem>         out;

    void error(const Node& n, std::string message, std::string hint = {}) {
        const auto p = doc.problem(n, std::move(message), std::move(hint));
        out.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at});
    }
    template <std::size_t N>
    void only(const Node& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [key, value] : t.members())
            if (std::ranges::find(keys, key) == keys.end()) error(value, std::format("{} has no key `{}`", what, key), mlibs::names::nearest(key, keys));
    }
    Node object(const Node& n, std::string_view what) {
        if (!n || n.is_object()) return n;
        error(n, std::format("{} is an object", what));
        return Node();
    }
    std::optional<double> number(const Node& n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        const auto v = n.number();
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
    std::optional<std::int64_t> whole(const Node& n, std::string_view what, std::int64_t lo, std::int64_t hi) {
        if (!n) return std::nullopt;
        const auto i = n.integer();
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
    std::optional<std::string> text(const Node& n, std::string_view what, std::string_view example) {
        if (!n) return std::nullopt;
        if (auto s = n.text(); s && !s->empty()) return s;
        error(n, std::format("{} is a name", what), std::string(example));
        return std::nullopt;
    }
};

// A number as its shortest text that reads back as itself.
std::string num(double v) { return std::format("{}", v); }
std::string num(float v) { return std::format("{}", v); }

std::string json_text(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

}  // namespace

std::expected<Description, std::vector<Problem>> read_description(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) {
        const auto& p = doc.error();
        return std::unexpected(std::vector<Problem>{{p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at}});
    }
    Reader r{*doc, {}};
    constexpr std::string_view shape = "{ \"video\": { width, height, fps, seconds, from: { card: \"sync\" }, audio: { … } } }, as defineVideo({ … }) evaluates";
    for (const auto& [key, value] : doc->root().members())
        if (key != "video") r.error(value, std::format("a video description has no key `{}`", key), std::string(shape));
    const Node v = doc->root()["video"];
    Description d;
    if (!v.is_object()) {
        r.error(v, "a video description is an object", std::string(shape));
        return std::unexpected(std::move(r.out));
    }
    r.only(v, video_keys, "a video description");
    if (const Node n = v["doc"]) {
        if (auto s = n.text()) d.doc = *s;
        else r.error(n, "`doc` is text");
    }
    if (auto w = r.whole(v["width"], "`width`", 2, 16384)) d.width = static_cast<std::int32_t>(*w);
    if (auto h = r.whole(v["height"], "`height`", 2, 16384)) d.height = static_cast<std::int32_t>(*h);
    if (const Node f = v["fps"]) {
        std::optional<mlibs::time::Rate> rate;
        if (auto i = f.integer(); i && *i > 0 && *i <= 1000) rate = mlibs::time::Rate{*i, 1};
        else if (auto s = f.text()) rate = parse_rate(*s);
        if (rate && rate->num > 0 && rate->den > 0 && rate->num <= 1000 * rate->den) d.fps = *rate;
        else r.error(f, "`fps` is a whole number of frames a second or a fraction", "fps: 30, or fps: \"30000/1001\"");
    }
    const Node secs = v["seconds"], frames = v["frames"];
    if (secs && frames) r.error(frames, "a video has `seconds` or `frames`, not both");
    else if (secs) {
        if (auto s = r.number(secs, "`seconds`", 0.001, 24 * 3600)) d.seconds = *s;
    } else if (frames) {
        if (auto n = r.whole(frames, "`frames`", 1, 24 * 3600 * 240)) d.frames = *n;
    } else r.error(v, "a video says how long: `seconds` or `frames`", "seconds: 3");
    if (d.seconds > 0) d.frames = Timing{d.fps, d.rate}.frames_in(d.seconds);

    if (const Node f = r.object(v["from"], "`from`")) {
        r.only(f, from_keys, "`from`");
        int sources = 0;
        for (const auto [name, kind] : {std::pair{"card", FramesFrom::card}, std::pair{"png", FramesFrom::png}, std::pair{"view", FramesFrom::view}}) {
            const Node n = f[name];
            if (!n) continue;
            if (++sources > 1) {
                r.error(n, "the frames come from one of `card`, `png` and `view`");
                continue;
            }
            auto s = r.text(n, std::format("`{}`", name), kind == FramesFrom::card ? "card: \"sync\"" : kind == FramesFrom::png ? "png: \"shots/{:05}.png\"" : "view: \"intro.view.omui.json\"");
            if (!s) continue;
            d.from = kind;
            d.source = *s;
            if (kind == FramesFrom::card && std::ranges::find(cards, *s) == cards.end()) r.error(n, std::format("no card `{}`", *s), mlibs::names::nearest(*s, cards));
            if (kind == FramesFrom::png && !frame_name(*s, 0)) r.error(n, "`png` is a pattern with the frame's number in it", "png: \"shots/{:05}.png\"");
        }
        if (sources == 0) r.error(f, "the frames come from one of `card`, `png` and `view`", "from: { card: \"sync\" }");
    }

    if (const Node a = r.object(v["audio"], "`audio`")) {
        d.audio = true;
        r.only(a, audio_keys, "`audio`");
        if (auto rate = r.whole(a["rate"], "`rate`", 8000, 192000)) d.rate = static_cast<std::int32_t>(*rate);
        if (auto m = r.text(a["music"], "`music`", "music: \"theme.music.omui.ts\"")) d.music = *m;
        if (auto s = r.text(a["sounds"], "`sounds`", "sounds: \"ui.sound.omui.ts\"")) d.sounds = *s;
        if (const Node b = a["beeps"]) {
            if (auto x = b.boolean()) d.beeps = *x;
            else r.error(b, "`beeps` is true or false");
        }
        if (const Node i = a["intensity"]) {
            if (i.is_array()) {
                for (const Node& t : i.items()) {
                    if (!t.is_object()) {
                        r.error(t, "an intensity is { at: <seconds>, value: <0–1> }");
                        continue;
                    }
                    r.only(t, std::array<std::string_view, 2>{"at", "value"}, "an intensity");
                    Intensity c;
                    if (auto at = r.number(t["at"], "an intensity's `at`", 0, 24 * 3600)) c.at = *at;
                    if (auto x = r.number(t["value"], "an intensity's `value`", 0, 1)) c.value = static_cast<float>(*x);
                    else if (!t["value"]) r.error(t, "an intensity has a `value`");
                    d.intensity.push_back(c);
                }
            } else if (auto x = r.number(i, "`intensity`", 0, 1)) d.intensity.push_back({0, static_cast<float>(*x)});
        }
        if (const Node rec = r.object(a["recordings"], "`recordings`"))
            for (const auto& [name, f] : rec.members_by_key())
                if (auto s = r.text(f, std::format("recording `{}`", name), "voice: \"voice.wav\"")) d.recordings[name] = *s;
        if (const Node c = a["cues"]) {
            if (!c.is_array()) r.error(c, "`cues` is a list", "cues: [{ sound: \"click\", frame: 30 }]");
            else
                for (const Node& t : c.items()) {
                    if (!t.is_object()) {
                        r.error(t, "a cue is { sound: <name>, frame: <n> } or { sound: <name>, at: <seconds> }");
                        continue;
                    }
                    r.only(t, cue_keys, "a cue");
                    SoundCue cue;
                    if (auto s = r.text(t["sound"], "a cue's `sound`", "sound: \"click\"")) cue.sound = *s;
                    else if (!t["sound"]) r.error(t, "a cue has a `sound`");
                    if (t["at"] && t["frame"]) r.error(t["frame"], "a cue has `at` or `frame`, not both");
                    else if (auto at = r.number(t["at"], "a cue's `at`", 0, 24 * 3600)) cue.at = *at;
                    else if (auto fr = r.whole(t["frame"], "a cue's `frame`", 0, 24 * 3600 * 240)) cue.frame = *fr;
                    if (auto g = r.number(t["gain"], "a cue's `gain`", 0, 8)) cue.gain = static_cast<float>(*g);
                    if (auto p = r.number(t["pan"], "a cue's `pan`", -1, 1)) cue.pan = static_cast<float>(*p);
                    if (auto p = r.number(t["pitch"], "a cue's `pitch`", 0.125, 8)) cue.pitch = static_cast<float>(*p);
                    d.cues.push_back(std::move(cue));
                }
        }
    }
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return d;
}

std::string write_description(const Description& d) {
    std::string s = "{\"video\": {\n";
    if (!d.doc.empty()) s += std::format("  \"doc\": {},\n", json_text(d.doc));
    s += std::format("  \"width\": {}, \"height\": {}, \"fps\": {}, ", d.width, d.height,
                     d.fps.den == 1 ? std::format("{}", d.fps.num) : json_text(rate_text(d.fps)));
    s += d.seconds > 0 ? std::format("\"seconds\": {}", num(d.seconds)) : std::format("\"frames\": {}", d.frames);
    constexpr std::array<std::string_view, 3> from{"card", "png", "view"};
    s += std::format(",\n  \"from\": {{\"{}\": {}}}", from[static_cast<std::size_t>(d.from)], json_text(d.source));
    if (d.audio) {
        std::vector<std::string> fields;
        if (d.rate != 48000) fields.push_back(std::format("\"rate\": {}", d.rate));
        if (!d.music.empty()) fields.push_back(std::format("\"music\": {}", json_text(d.music)));
        if (d.intensity.size() == 1 && d.intensity[0].at == 0) fields.push_back(std::format("\"intensity\": {}", num(d.intensity[0].value)));
        else if (!d.intensity.empty()) {
            std::string list = "\"intensity\": [";
            for (std::size_t i = 0; i < d.intensity.size(); ++i)
                list += std::format("{}{{\"at\": {}, \"value\": {}}}", i ? ", " : "", num(d.intensity[i].at), num(d.intensity[i].value));
            fields.push_back(list + "]");
        }
        if (!d.sounds.empty()) fields.push_back(std::format("\"sounds\": {}", json_text(d.sounds)));
        if (!d.recordings.empty()) {
            std::string map = "\"recordings\": {";
            bool first = true;
            for (const auto& [k, f] : d.recordings) map += std::format("{}{}: {}", std::exchange(first, false) ? "" : ", ", json_text(k), json_text(f));
            fields.push_back(map + "}");
        }
        if (!d.cues.empty()) {
            std::string list = "\"cues\": [";
            for (std::size_t i = 0; i < d.cues.size(); ++i) {
                const SoundCue& c = d.cues[i];
                list += std::format("{}\n    {{\"sound\": {}, ", i ? "," : "", json_text(c.sound));
                list += c.frame >= 0 ? std::format("\"frame\": {}", c.frame) : std::format("\"at\": {}", num(c.at));
                if (c.gain != 1) list += std::format(", \"gain\": {}", num(c.gain));
                if (c.pan != 0) list += std::format(", \"pan\": {}", num(c.pan));
                if (c.pitch != 1) list += std::format(", \"pitch\": {}", num(c.pitch));
                list += "}";
            }
            fields.push_back(list + "\n  ]");
        }
        if (d.beeps) fields.push_back("\"beeps\": true");
        s += ",\n  \"audio\": {";
        for (std::size_t i = 0; i < fields.size(); ++i) s += (i ? ", " : "") + fields[i];
        s += "}";
    }
    return s + "\n}}\n";
}

}  // namespace xengine::video
