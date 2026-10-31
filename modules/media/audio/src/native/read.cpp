// Reading and writing *.sound.omui.toml. toml++ reports a malformed file by
// throwing; this unit — the library's adapter — turns that into a problem at
// its line and column, as it does every other.
module openmui.audio;

import std;
import tomlplusplus;
import :detail;

namespace omui::audio {

std::size_t detail::distance(std::string_view a, std::string_view b) {
    const std::size_t w = b.size() + 1;
    std::vector<std::size_t> d((a.size() + 1) * w);
    for (std::size_t i = 0; i <= a.size(); ++i) d[i * w] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) d[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t v = std::min({d[(i - 1) * w + j] + 1, d[i * w + j - 1] + 1, d[(i - 1) * w + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[(i - 2) * w + j - 2] + 1);
            d[i * w + j] = v;
        }
    return d[a.size() * w + b.size()];
}

std::string detail::nearest(std::string_view name, std::span<const std::string_view> options) {
    std::string_view best;
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) d = x, best = o;
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", o);
    return std::format("one of {}", all);
}

namespace {

constexpr std::array<std::string_view, 4> sound_keys{"gain", "bus", "layer", "doc"};
constexpr std::array<std::string_view, 14> layer_keys{"wave", "at", "length", "pitch", "envelope", "filter", "gain", "pan", "drive", "seed", "recording", "speed", "doc", "name"};
constexpr std::array<std::string_view, 5> envelope_keys{"attack", "hold", "decay", "sustain", "release"};
constexpr std::array<std::string_view, 3> filter_keys{"kind", "cutoff", "q"};
using detail::filters;
using detail::nearest;
using detail::layer_waves;

struct Reader {
    std::string          file;
    std::vector<Problem> out;

    std::pair<int, int> at(const toml::node* n) const {
        if (!n) return {0, 0};
        const auto s = n->source();
        return {static_cast<int>(s.begin.line), static_cast<int>(s.begin.column)};
    }
    void error(const toml::node* n, std::string message, std::string hint = {}) {
        const auto [l, c] = at(n);
        out.push_back({file, l, c, std::move(message), std::move(hint)});
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
    std::optional<double> number(const toml::node* n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        std::optional<double> v;
        if (auto d = n->value_exact<double>()) v = *d;
        else if (auto i = n->value_exact<std::int64_t>()) v = static_cast<double>(*i);
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
    std::optional<std::array<float, 2>> glide(const toml::node* n, std::string_view what, double lo, double hi) {
        if (!n) return std::nullopt;
        if (const toml::array* a = n->as_array()) {
            if (a->size() != 2) {
                error(n, std::format("{} is a number, or [from, to]", what), "pitch = [180, 60]");
                return std::nullopt;
            }
            auto x = number(a->get(0), what, lo, hi), y = number(a->get(1), what, lo, hi);
            if (!x || !y) return std::nullopt;
            return std::array<float, 2>{static_cast<float>(*x), static_cast<float>(*y)};
        }
        auto x = number(n, what, lo, hi);
        if (!x) return std::nullopt;
        return std::array<float, 2>{static_cast<float>(*x), static_cast<float>(*x)};
    }
    template <std::size_t N>
    std::optional<std::size_t> choice(const toml::node* n, std::string_view what, const std::array<std::string_view, N>& names) {
        if (!n) return std::nullopt;
        const auto s = n->value_exact<std::string>();
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

    Layer layer(const toml::table& t, std::string_view where) {
        only(t, layer_keys, std::format("{}'s layer", where));
        Layer l;
        if (auto k = choice(t.get("wave"), "a wave (sine, triangle, square, saw, noise, recording)", layer_waves)) l.wave = static_cast<Wave>(*k);
        if (auto v = number(t.get("at"), "a layer's `at` (seconds)", 0, 60)) l.at = static_cast<float>(*v);
        if (l.wave == Wave::recording) {  // a recording plays to its end unless cut short, held at its level
            l.length = 0;
            l.envelope.sustain = 1;
            if (const toml::node* rn = t.get("recording")) {
                if (auto name = rn->value_exact<std::string>(); name && !name->empty()) l.recording = *name;
                else error(rn, "a layer's `recording` is a recording's name", "recording = \"fight\"");
            } else {
                error(&t, "a recording's layer names its recording", "wave = \"recording\", recording = \"fight\"");
            }
            if (auto v = glide(t.get("speed"), "a layer's `speed` (1: as recorded)", 0.05, 8)) l.speed = *v;
        } else {
            for (std::string_view k : {"recording", "speed"})
                if (const toml::node* n = t.get(k)) error(n, std::format("`{}` is a recording's layer's", k), "wave = \"recording\"");
        }
        if (auto v = number(t.get("length"), "a layer's `length` (seconds)", 0.001, 60)) l.length = static_cast<float>(*v);
        if (auto v = glide(t.get("pitch"), "a layer's `pitch` (Hz)", 1, 20000)) l.pitch = *v;
        if (const toml::node* en = t.get("envelope")) {
            if (const toml::table* e = en->as_table()) {
                only(*e, envelope_keys, "an envelope");
                if (auto v = number(e->get("attack"), "an envelope's `attack`", 0, 60)) l.envelope.attack = static_cast<float>(*v);
                if (auto v = number(e->get("hold"), "an envelope's `hold`", 0, 60)) l.envelope.hold = static_cast<float>(*v);
                if (auto v = number(e->get("decay"), "an envelope's `decay`", 0, 60)) l.envelope.decay = static_cast<float>(*v);
                if (auto v = number(e->get("sustain"), "an envelope's `sustain` (0–1)", 0, 1)) l.envelope.sustain = static_cast<float>(*v);
                if (auto v = number(e->get("release"), "an envelope's `release`", 0, 60)) l.envelope.release = static_cast<float>(*v);
            } else {
                error(en, "an envelope is a table", "envelope = { attack = 0.002, decay = 0.1, sustain = 0, release = 0.02 }");
            }
        }
        if (const toml::node* fn = t.get("filter")) {
            if (const toml::table* f = fn->as_table()) {
                only(*f, filter_keys, "a filter");
                if (auto k = choice(f->get("kind"), "a filter (lowpass, highpass, bandpass)", filters)) l.filter = static_cast<FilterKind>(*k);
                if (auto v = glide(f->get("cutoff"), "a filter's `cutoff` (Hz)", 10, 24000)) l.cutoff = *v;
                if (auto v = number(f->get("q"), "a filter's `q`", 0.05, 40)) l.q = static_cast<float>(*v);
            } else {
                error(fn, "a filter is a table", "filter = { kind = \"lowpass\", cutoff = [3000, 600], q = 0.7 }");
            }
        }
        if (auto v = number(t.get("gain"), "a layer's `gain`", 0, 8)) l.gain = static_cast<float>(*v);
        if (auto v = number(t.get("pan"), "a layer's `pan` (−1–1)", -1, 1)) l.pan = static_cast<float>(*v);
        if (auto v = number(t.get("drive"), "a layer's `drive` (0–1)", 0, 1)) l.drive = static_cast<float>(*v);
        if (auto v = number(t.get("seed"), "a layer's `seed`", 1, 4294967295.0)) l.seed = static_cast<std::uint32_t>(*v);
        return l;
    }
};

std::string num(float v) {
    std::string s = std::format("{}", v);
    return s;
}

}  // namespace

std::string Problem::text() const {
    std::string s = std::format("{}:{}:{}: {}", file, line, column, message);
    if (!hint.empty()) s += std::format(" ({})", hint);
    return s;
}

std::expected<std::vector<Sound>, std::vector<Problem>> read_sounds(std::string_view text, std::string_view file) {
    Reader r{std::string(file), {}};
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(std::vector<Problem>{{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), {}}});
    }
    std::vector<Sound> sounds;
    for (const auto& [k, v] : root) {
        if (k.str() == "doc") continue;
        if (k.str() != "sound") {
            const auto s = k.source();
            r.out.push_back({r.file, static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::format("a sound file has no key `{}`", k.str()),
                             "[sound.<name>] tables, and `doc`"});
            continue;
        }
        const toml::table* all = v.as_table();
        if (!all) {
            r.error(&v, "`sound` is [sound.<name>] tables");
            continue;
        }
        for (const auto& [name, body] : *all) {
            const toml::table* t = body.as_table();
            if (!t) {
                r.error(&body, std::format("sound `{}` is a table", name.str()));
                continue;
            }
            r.only(*t, sound_keys, std::format("sound `{}`", name.str()));
            Sound s;
            s.name = std::string(name.str());
            if (auto g = r.number(t->get("gain"), "a sound's `gain`", 0, 8)) s.gain = static_cast<float>(*g);
            if (const toml::node* b = t->get("bus")) {
                if (auto bs = b->value_exact<std::string>()) s.bus = *bs;
                else r.error(b, "a sound's `bus` is a name", "bus = \"sfx\"");
            }
            if (const toml::node* ln = t->get("layer")) {
                if (const toml::array* layers = ln->as_array()) {
                    for (const toml::node& l : *layers)
                        if (const toml::table* lt = l.as_table()) s.layers.push_back(r.layer(*lt, std::format("sound `{}`", s.name)));
                        else r.error(&l, "a layer is a table", "[[sound.<name>.layer]]");
                } else {
                    r.error(ln, "a sound's layers are [[sound.<name>.layer]] tables");
                }
            }
            if (s.layers.empty()) r.error(t, std::format("sound `{}` has no layer", s.name), "[[sound.<name>.layer]] wave = \"sine\", pitch = 220, length = 0.2");
            sounds.push_back(std::move(s));
        }
    }
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return sounds;
}

std::string write_sounds(std::span<const Sound> sounds) {
    std::string s;
    for (const Sound& sound : sounds) {
        s += std::format("[sound.{}]\n", sound.name);
        if (sound.gain != 1) s += std::format("gain = {}\n", num(sound.gain));
        if (sound.bus != "sfx") s += std::format("bus = \"{}\"\n", sound.bus);
        for (const Layer& l : sound.layers) {
            s += std::format("\n[[sound.{}.layer]]\nwave = \"{}\"\n", sound.name, layer_waves[static_cast<std::size_t>(l.wave)]);
            if (l.wave == Wave::recording) {
                s += std::format("recording = \"{}\"\n", l.recording);
                if (l.speed != std::array<float, 2>{1, 1}) s += std::format("speed = [{}, {}]\n", num(l.speed[0]), num(l.speed[1]));
            }
            if (l.at != 0) s += std::format("at = {}\n", num(l.at));
            if (l.wave != Wave::recording || l.length > 0) s += std::format("length = {}\n", num(l.length));
            if (l.wave != Wave::recording) s += std::format("pitch = [{}, {}]\n", num(l.pitch[0]), num(l.pitch[1]));
            const Envelope& e = l.envelope;
            s += std::format("envelope = {{ attack = {}, hold = {}, decay = {}, sustain = {}, release = {} }}\n", num(e.attack), num(e.hold), num(e.decay),
                             num(e.sustain), num(e.release));
            if (l.filter != FilterKind::none)
                s += std::format("filter = {{ kind = \"{}\", cutoff = [{}, {}], q = {} }}\n", filters[static_cast<std::size_t>(l.filter)], num(l.cutoff[0]),
                                 num(l.cutoff[1]), num(l.q));
            if (l.gain != 1) s += std::format("gain = {}\n", num(l.gain));
            if (l.pan != 0) s += std::format("pan = {}\n", num(l.pan));
            if (l.drive != 0) s += std::format("drive = {}\n", num(l.drive));
            if (l.seed != 1) s += std::format("seed = {}\n", l.seed);
        }
        s += "\n";
    }
    return s;
}

}  // namespace omui::audio
