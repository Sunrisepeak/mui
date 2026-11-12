// Reading and writing sounds: the JSON a `*.sound.omui.ts` evaluates to
// (D-139), every problem at its JSON pointer and its line and column.
module xengine.audio;

import std;
import mlibs.json;
import :detail;

namespace xengine::audio {

std::size_t detail::distance(std::string_view a, std::string_view b) { return mlibs::json::distance(a, b); }

std::string detail::nearest(std::string_view name, std::span<const std::string_view> options) { return mlibs::json::nearest(name, options); }

namespace {

constexpr std::array<std::string_view, 4> sound_keys{"gain", "bus", "layers", "doc"};
constexpr std::array<std::string_view, 14> layer_keys{"wave", "at", "length", "pitch", "envelope", "filter", "gain", "pan", "drive", "seed", "recording", "speed", "doc", "name"};
constexpr std::array<std::string_view, 5> envelope_keys{"attack", "hold", "decay", "sustain", "release"};
constexpr std::array<std::string_view, 3> filter_keys{"kind", "cutoff", "q"};
using detail::filters;
using detail::nearest;
using detail::layer_waves;

using mlibs::json::Node;

struct Reader {
    const mlibs::json::Document& doc;
    std::vector<Problem>         out;

    void error(const Node& n, std::string message, std::string hint = {}) { out.push_back(detail::problem_of(doc.problem(n, std::move(message), std::move(hint)))); }
    template <std::size_t N>
    void only(const Node& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [key, value] : t.members())
            if (std::ranges::find(keys, key) == keys.end()) error(value, std::format("{} has no key `{}`", what, key), nearest(key, keys));
    }
    std::optional<double> number(const Node& n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        const auto v = n.number();
        if (!v) {
            error(n, std::format("{} is a number", what));
            return std::nullopt;
        }
        if (*v < lo || *v > hi) {
            error(n, std::format("{} is {}–{}, not {}", what, lo, hi, *v));
            return std::nullopt;
        }
        return v;
    }
    // A number, or [from, to].
    std::optional<std::array<float, 2>> glide(const Node& n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        if (n.is_array()) {
            if (n.size() != 2) {
                error(n, std::format("{} is a number, or [from, to]", what), "pitch: [180, 60]");
                return std::nullopt;
            }
            auto x = number(n[0], what, lo, hi), y = number(n[1], what, lo, hi);
            if (!x || !y) return std::nullopt;
            return std::array<float, 2>{static_cast<float>(*x), static_cast<float>(*y)};
        }
        auto x = number(n, what, lo, hi);
        if (!x) return std::nullopt;
        return std::array<float, 2>{static_cast<float>(*x), static_cast<float>(*x)};
    }
    template <std::size_t N>
    std::optional<std::size_t> choice(const Node& n, std::string_view what, const std::array<std::string_view, N>& names) {
        if (!n) return std::nullopt;
        const auto s = n.text();
        if (!s) {
            error(n, std::format("{} is a name", what));
            return std::nullopt;
        }
        const auto k = std::ranges::find(names, *s);
        if (k == names.end()) {
            error(n, std::format("`{}` is not {}", *s, what), nearest(*s, names));
            return std::nullopt;
        }
        return static_cast<std::size_t>(k - names.begin());
    }

    Layer layer(const Node& t, std::string_view where) {
        only(t, layer_keys, std::format("{}'s layer", where));
        Layer l;
        if (auto k = choice(t["wave"], "a wave (sine, triangle, square, saw, noise, recording)", layer_waves)) l.wave = static_cast<Wave>(*k);
        if (auto v = number(t["at"], "a layer's `at` (seconds)", 0, 60)) l.at = static_cast<float>(*v);
        if (l.wave == Wave::recording) {  // a recording plays to its end unless cut short, held at its level
            l.length = 0;
            l.envelope.sustain = 1;
            if (const Node rn = t["recording"]) {
                if (auto name = rn.text(); name && !name->empty()) l.recording = *name;
                else error(rn, "a layer's `recording` is a recording's name", "recording: \"fight\"");
            } else {
                error(t, "a recording's layer names its recording", "wave: \"recording\", recording: \"fight\"");
            }
            if (auto v = glide(t["speed"], "a layer's `speed` (1: as recorded)", 0.05, 8)) l.speed = *v;
        } else {
            for (std::string_view k : {"recording", "speed"})
                if (const Node n = t[k]) error(n, std::format("`{}` is a recording's layer's", k), "wave: \"recording\"");
        }
        if (auto v = number(t["length"], "a layer's `length` (seconds)", 0.001, 60)) l.length = static_cast<float>(*v);
        if (auto v = glide(t["pitch"], "a layer's `pitch` (Hz)", 1, 20000)) l.pitch = *v;
        if (const Node e = t["envelope"]) {
            if (e.is_object()) {
                only(e, envelope_keys, "an envelope");
                if (auto v = number(e["attack"], "an envelope's `attack`", 0, 60)) l.envelope.attack = static_cast<float>(*v);
                if (auto v = number(e["hold"], "an envelope's `hold`", 0, 60)) l.envelope.hold = static_cast<float>(*v);
                if (auto v = number(e["decay"], "an envelope's `decay`", 0, 60)) l.envelope.decay = static_cast<float>(*v);
                if (auto v = number(e["sustain"], "an envelope's `sustain` (0–1)", 0, 1)) l.envelope.sustain = static_cast<float>(*v);
                if (auto v = number(e["release"], "an envelope's `release`", 0, 60)) l.envelope.release = static_cast<float>(*v);
            } else {
                error(e, "an envelope is an object", "envelope: { attack: 0.002, decay: 0.1, sustain: 0, release: 0.02 }");
            }
        }
        if (const Node f = t["filter"]) {
            if (f.is_object()) {
                only(f, filter_keys, "a filter");
                if (auto k = choice(f["kind"], "a filter (lowpass, highpass, bandpass)", filters)) l.filter = static_cast<FilterKind>(*k);
                if (auto v = glide(f["cutoff"], "a filter's `cutoff` (Hz)", 10, 24000)) l.cutoff = *v;
                if (auto v = number(f["q"], "a filter's `q`", 0.05, 40)) l.q = static_cast<float>(*v);
            } else {
                error(f, "a filter is an object", "filter: { kind: \"lowpass\", cutoff: [3000, 600], q: 0.7 }");
            }
        }
        if (auto v = number(t["gain"], "a layer's `gain`", 0, 8)) l.gain = static_cast<float>(*v);
        if (auto v = number(t["pan"], "a layer's `pan` (−1–1)", -1, 1)) l.pan = static_cast<float>(*v);
        if (auto v = number(t["drive"], "a layer's `drive` (0–1)", 0, 1)) l.drive = static_cast<float>(*v);
        if (auto v = number(t["seed"], "a layer's `seed`", 1, 4294967295.0)) l.seed = static_cast<std::uint32_t>(*v);
        return l;
    }
};

// A number as JSON writes it: a float's shortest digits ("0.1", not "0.10000000149011612").
std::string num(float v) { return std::format("{}", v); }

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
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

Problem detail::problem_of(const mlibs::json::Problem& p) {
    return {p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at};
}

std::string Problem::text() const {
    return std::format("{}:{}:{}: {}{}{}", file, line, column, at.empty() ? "" : std::format("{}: ", at), message, hint.empty() ? "" : std::format(" ({})", hint));
}

std::expected<std::vector<Sound>, std::vector<Problem>> read_sounds(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) return std::unexpected(std::vector<Problem>{detail::problem_of(doc.error())});
    Reader r{*doc, {}};
    const Node root = doc->root();
    constexpr std::string_view shape = "{ \"sound\": { <name>: { layers: [ … ] } } }, as defineSound({ … }) evaluates";
    for (const auto& [key, value] : root.members())
        if (key != "sound") r.error(value, std::format("a sound file has no key `{}`", key), std::string(shape));
    const Node all = root["sound"];
    if (!all.is_object()) {
        r.error(all, "a sound file is an object of named sounds", std::string(shape));
        return std::unexpected(std::move(r.out));
    }
    std::vector<Sound> sounds;
    // In name order, as they have always been numbered: a file's order changes no sound's id.
    for (const auto& [name, t] : all.members_by_key()) {
        if (!t.is_object()) {
            r.error(t, std::format("sound `{}` is an object", name));
            continue;
        }
        r.only(t, sound_keys, std::format("sound `{}`", name));
        Sound s;
        s.name = name;
        if (auto g = r.number(t["gain"], "a sound's `gain`", 0, 8)) s.gain = static_cast<float>(*g);
        if (const Node b = t["bus"]) {
            if (auto bs = b.text()) s.bus = *bs;
            else r.error(b, "a sound's `bus` is a name", "bus: \"sfx\"");
        }
        if (const Node layers = t["layers"]) {
            if (layers.is_array()) {
                for (const Node& l : layers.items())
                    if (l.is_object()) s.layers.push_back(r.layer(l, std::format("sound `{}`", s.name)));
                    else r.error(l, "a layer is an object", "{ wave: \"sine\", pitch: 220, length: 0.2 }");
            } else {
                r.error(layers, "a sound's `layers` is a list", "layers: [{ wave: \"sine\", pitch: 220, length: 0.2 }]");
            }
        }
        if (s.layers.empty()) r.error(t, std::format("sound `{}` has no layer", s.name), "layers: [{ wave: \"sine\", pitch: 220, length: 0.2 }]");
        sounds.push_back(std::move(s));
    }
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return sounds;
}

std::string write_sounds(std::span<const Sound> sounds) {
    std::string s = "{\"sound\": {";
    for (std::size_t index = 0; index < sounds.size(); ++index) {
        const Sound& sound = sounds[index];
        s += std::format("{}\n  {}: {{", index ? "," : "", json_text(sound.name));
        if (sound.gain != 1) s += std::format("\"gain\": {}, ", num(sound.gain));
        if (sound.bus != "sfx") s += std::format("\"bus\": {}, ", json_text(sound.bus));
        s += "\"layers\": [";
        for (std::size_t k = 0; k < sound.layers.size(); ++k) {
            const Layer& l = sound.layers[k];
            s += std::format("{}\n    {{\"wave\": \"{}\"", k ? "," : "", layer_waves[static_cast<std::size_t>(l.wave)]);
            if (l.wave == Wave::recording) {
                s += std::format(", \"recording\": {}", json_text(l.recording));
                if (l.speed != std::array<float, 2>{1, 1}) s += std::format(", \"speed\": [{}, {}]", num(l.speed[0]), num(l.speed[1]));
            }
            if (l.at != 0) s += std::format(", \"at\": {}", num(l.at));
            if (l.wave != Wave::recording || l.length > 0) s += std::format(", \"length\": {}", num(l.length));
            if (l.wave != Wave::recording) s += std::format(", \"pitch\": [{}, {}]", num(l.pitch[0]), num(l.pitch[1]));
            const Envelope& e = l.envelope;
            s += std::format(", \"envelope\": {{\"attack\": {}, \"hold\": {}, \"decay\": {}, \"sustain\": {}, \"release\": {}}}", num(e.attack), num(e.hold),
                             num(e.decay), num(e.sustain), num(e.release));
            if (l.filter != FilterKind::none)
                s += std::format(", \"filter\": {{\"kind\": \"{}\", \"cutoff\": [{}, {}], \"q\": {}}}", filters[static_cast<std::size_t>(l.filter)],
                                 num(l.cutoff[0]), num(l.cutoff[1]), num(l.q));
            if (l.gain != 1) s += std::format(", \"gain\": {}", num(l.gain));
            if (l.pan != 0) s += std::format(", \"pan\": {}", num(l.pan));
            if (l.drive != 0) s += std::format(", \"drive\": {}", num(l.drive));
            if (l.seed != 1) s += std::format(", \"seed\": {}", l.seed);
            s += "}";
        }
        s += "\n  ]}";
    }
    return s + "\n}}\n";
}

}  // namespace xengine::audio
