// Reading and writing *.music.omui.toml, and the built-in patches (written in
// the same format). toml++ reports a malformed file by throwing; this unit —
// the library's adapter — turns that into a problem at its line and column,
// as it does every other: an unknown key with the nearest right one, a
// number out of its range, a name that is not there with the nearest that
// is, a pattern's wrong note or short bar at its very token in its text.
module openmui.audio;

import std;
import tomlplusplus;
import :detail;

namespace omui::audio {

namespace {

using detail::nearest;

constexpr std::array<std::string_view, 8> root_keys{"doc", "music", "patch", "instrument", "bus", "pattern", "section", "arrangement"};
constexpr std::array<std::string_view, 4> music_keys{"name", "tempo", "meter", "gain"};
constexpr std::array<std::string_view, 13> patch_keys{"base", "engine", "gain", "glide", "drive", "amp", "osc", "fm", "pluck", "drum", "filter", "vibrato", "doc"};
constexpr std::array<std::string_view, 4> adsr_keys{"attack", "decay", "sustain", "release"};
constexpr std::array<std::string_view, 9> osc_keys{"wave", "unison", "detune", "spread", "width", "pwm", "pwm_rate", "sub", "noise"};
constexpr std::array<std::string_view, 5> fm_keys{"ratio", "index", "sustain", "decay", "feedback"};
constexpr std::array<std::string_view, 3> pluck_keys{"brightness", "decay", "pick"};
constexpr std::array<std::string_view, 11> drum_keys{"tone", "pitch", "from", "sweep", "decay", "modes", "noise", "noise_decay", "metal", "click", "bursts"};
constexpr std::array<std::string_view, 9> filter_keys{"kind", "cutoff", "q", "envelope", "keytrack", "attack", "decay", "sustain", "release"};
constexpr std::array<std::string_view, 3> vibrato_keys{"depth", "rate", "delay"};
constexpr std::array<std::string_view, 12> instrument_keys{"patch", "bus", "gain", "pan", "legato", "humanize", "transpose", "voices", "sends", "gate", "choke", "doc"};
constexpr std::array<std::string_view, 5> bus_keys{"to", "gain", "effects", "duck", "doc"};
constexpr std::array<std::string_view, 5> duck_keys{"key", "depth", "threshold", "attack", "release"};
constexpr std::array<std::string_view, 3> filter_effect_keys{"kind", "cutoff", "q"};
constexpr std::array<std::string_view, 7> echo_keys{"kind", "time", "beats", "feedback", "wet", "dry", "pingpong"};
constexpr std::array<std::string_view, 7> reverb_keys{"kind", "size", "damping", "width", "predelay", "wet", "dry"};
constexpr std::array<std::string_view, 7> chorus_keys{"kind", "delay", "depth", "rate", "voices", "wet", "dry"};
constexpr std::array<std::string_view, 6> pattern_keys{"instrument", "notes", "lanes", "step", "swing", "doc"};
constexpr std::array<std::string_view, 3> section_keys{"bars", "layers", "doc"};
constexpr std::array<std::string_view, 7> layer_keys{"pattern", "instrument", "intensity", "at", "times", "transpose", "gain"};
constexpr std::array<std::string_view, 3> arrangement_keys{"intro", "loop", "outro"};

// The patches every piece may name: written as a piece's would be.
constexpr std::string_view builtin_text = R"(
# ── strings ──
[patch.koto]
engine = "pluck"
gain = 0.7
pluck = { brightness = 0.7, decay = 2.2, pick = 0.13 }
amp = { attack = 0.001, decay = 1, sustain = 1, release = 0.4 }
filter = { kind = "lowpass", cutoff = 5500, q = 0.8 }
glide = 0.1

[patch.guzheng]
engine = "pluck"
gain = 0.65
pluck = { brightness = 0.78, decay = 3.2, pick = 0.1 }
amp = { attack = 0.001, decay = 1, sustain = 1, release = 0.6 }
filter = { kind = "lowpass", cutoff = 7000, q = 0.7 }
vibrato = { depth = 14, rate = 5.5, delay = 0.35 }
glide = 0.12

[patch.harp]
engine = "pluck"
gain = 0.82
pluck = { brightness = 0.45, decay = 2.6, pick = 0.45 }
amp = { attack = 0.002, decay = 1, sustain = 1, release = 0.5 }

[patch.pluck_bass]
engine = "pluck"
gain = 1.0
pluck = { brightness = 0.35, decay = 1.2, pick = 0.2 }
amp = { attack = 0.001, decay = 1, sustain = 1, release = 0.12 }
filter = { kind = "lowpass", cutoff = 1400, q = 0.7 }

# ── keys and bells (FM) ──
[patch.epiano]
engine = "fm"
gain = 0.37
fm = { ratio = 1, index = 2.4, sustain = 0.25, decay = 0.5, feedback = 0.05 }
amp = { attack = 0.002, decay = 2.2, sustain = 0, release = 0.35 }

[patch.bell]
engine = "fm"
gain = 0.21
fm = { ratio = 3.5, index = 3, sustain = 0.4, decay = 1.2 }
amp = { attack = 0.001, decay = 3, sustain = 0, release = 1.2 }

[patch.fm_pluck]
engine = "fm"
gain = 0.4
fm = { ratio = 2, index = 3, sustain = 0.1, decay = 0.12, feedback = 0.1 }
amp = { attack = 0.001, decay = 0.6, sustain = 0, release = 0.2 }

[patch.fm_bass]
engine = "fm"
gain = 0.23
fm = { ratio = 1, index = 3.2, sustain = 0.7, decay = 0.14, feedback = 0.25 }
amp = { attack = 0.002, decay = 0.4, sustain = 0.75, release = 0.07 }

# ── analog ──
[patch.pad]
engine = "analog"
gain = 0.22
osc = { wave = "saw", unison = 5, detune = 22, spread = 0.8 }
amp = { attack = 0.6, decay = 1.5, sustain = 0.85, release = 1.2 }
filter = { kind = "lowpass", cutoff = 900, q = 0.6, envelope = 1.2, keytrack = 0.3, attack = 0.9, decay = 2, sustain = 0.5, release = 1.2 }

[patch.strings]
engine = "analog"
gain = 0.21
osc = { wave = "saw", unison = 3, detune = 12, spread = 0.6 }
amp = { attack = 0.25, decay = 1, sustain = 0.9, release = 0.6 }
filter = { kind = "lowpass", cutoff = 2200, q = 0.5, keytrack = 0.5 }
vibrato = { depth = 8, rate = 5, delay = 0.4 }

[patch.supersaw]
engine = "analog"
gain = 0.24
osc = { wave = "saw", unison = 7, detune = 30, spread = 0.9 }
amp = { attack = 0.005, decay = 0.4, sustain = 0.7, release = 0.25 }
filter = { kind = "lowpass", cutoff = 2500, q = 0.8, envelope = 1.5, keytrack = 0.4, attack = 0.002, decay = 0.3, sustain = 0.3, release = 0.3 }

[patch.saw_lead]
engine = "analog"
gain = 0.2
osc = { wave = "saw", unison = 2, detune = 8, spread = 0.3 }
amp = { attack = 0.005, decay = 0.2, sustain = 0.8, release = 0.2 }
filter = { kind = "lowpass", cutoff = 3500, q = 1.2, envelope = 1, keytrack = 0.5, attack = 0.002, decay = 0.25, sustain = 0.4, release = 0.2 }
vibrato = { depth = 15, rate = 5.5, delay = 0.25 }
glide = 0.07

[patch.pulse_lead]
engine = "analog"
gain = 0.15
osc = { wave = "square", width = 0.3, pwm = 0.15, pwm_rate = 3 }
amp = { attack = 0.004, decay = 0.2, sustain = 0.75, release = 0.15 }
filter = { kind = "lowpass", cutoff = 4000, q = 0.8, keytrack = 0.5 }
vibrato = { depth = 12, rate = 5.5, delay = 0.3 }
glide = 0.06

[patch.arp]
engine = "analog"
gain = 0.44
osc = { wave = "square", width = 0.5 }
amp = { attack = 0.002, decay = 0.2, sustain = 0, release = 0.08 }
filter = { kind = "lowpass", cutoff = 1200, q = 1.5, envelope = 2.5, keytrack = 0.5, attack = 0.001, decay = 0.15, sustain = 0, release = 0.1 }

[patch.pulse_bass]
engine = "analog"
gain = 0.18
osc = { wave = "square", width = 0.35, pwm = 0.15, pwm_rate = 0.4, sub = 0.6 }
amp = { attack = 0.003, decay = 0.25, sustain = 0.6, release = 0.08 }
filter = { kind = "lowpass", cutoff = 300, q = 1.1, envelope = 2.5, keytrack = 0.3, attack = 0.002, decay = 0.18, sustain = 0.15, release = 0.1 }

[patch.saw_bass]
engine = "analog"
gain = 0.19
osc = { wave = "saw", unison = 2, detune = 14, spread = 0, sub = 0.5 }
amp = { attack = 0.003, decay = 0.3, sustain = 0.8, release = 0.08 }
filter = { kind = "lowpass", cutoff = 380, q = 1, envelope = 2, keytrack = 0.3, attack = 0.002, decay = 0.25, sustain = 0.3, release = 0.1 }
drive = 0.3

[patch.sub_bass]
engine = "analog"
gain = 0.25
osc = { wave = "sine" }
amp = { attack = 0.004, decay = 0.3, sustain = 0.9, release = 0.08 }

[patch.dizi]
engine = "analog"
gain = 0.2
osc = { wave = "triangle", noise = 0.08 }
amp = { attack = 0.04, decay = 0.3, sustain = 0.85, release = 0.18 }
filter = { kind = "lowpass", cutoff = 3200, q = 0.7, keytrack = 0.6 }
vibrato = { depth = 22, rate = 5.2, delay = 0.3 }
glide = 0.08

# ── drums ──
[patch.kick]
engine = "drum"
gain = 0.45
drum = { pitch = 48, from = 4.5, sweep = 0.03, decay = 0.42, click = 0.3 }
drive = 0.25

[patch.taiko]
engine = "drum"
gain = 0.4
drum = { pitch = 62, from = 1.7, sweep = 0.05, decay = 0.85, modes = 0.5, noise = 0.35, noise_decay = 0.06, click = 0.25 }
filter = { kind = "lowpass", cutoff = 1200, q = 0.7 }
drive = 0.15

[patch.snare]
engine = "drum"
gain = 0.81
drum = { pitch = 185, from = 1.6, sweep = 0.02, decay = 0.12, modes = 0.3, noise = 0.9, noise_decay = 0.17, click = 0.2 }
filter = { kind = "highpass", cutoff = 1200, q = 0.7 }

[patch.clap]
engine = "drum"
gain = 2.16
drum = { tone = 0, noise = 1, noise_decay = 0.12, bursts = 3 }
filter = { kind = "bandpass", cutoff = 1300, q = 1.1 }

[patch.hat]
engine = "drum"
gain = 2.2
drum = { tone = 0, pitch = 410, noise = 0.6, metal = 0.6, noise_decay = 0.045 }
filter = { kind = "highpass", cutoff = 7000, q = 0.7 }

[patch.open_hat]
engine = "drum"
gain = 0.68
drum = { tone = 0, pitch = 410, noise = 0.6, metal = 0.6, noise_decay = 0.32 }
filter = { kind = "highpass", cutoff = 6500, q = 0.7 }

[patch.shaker]
engine = "drum"
gain = 2.48
drum = { tone = 0, noise = 1, noise_decay = 0.06 }
amp = { attack = 0.012 }
filter = { kind = "bandpass", cutoff = 6500, q = 1.4 }

[patch.tom]
engine = "drum"
gain = 0.56
drum = { pitch = 110, from = 1.5, sweep = 0.06, decay = 0.4, modes = 0.4, noise = 0.15, noise_decay = 0.05, click = 0.15 }
filter = { kind = "lowpass", cutoff = 3000, q = 0.7 }

[patch.woodblock]
engine = "drum"
gain = 1.18
drum = { pitch = 820, from = 1.05, sweep = 0.01, decay = 0.07, modes = 0.6, click = 0.2 }

[patch.crash]
engine = "drum"
gain = 0.26
drum = { tone = 0, pitch = 520, noise = 0.7, metal = 0.5, noise_decay = 1.6 }
filter = { kind = "highpass", cutoff = 4500, q = 0.7 }
)";

// Where in the file a string's content begins, and what each of its bytes
// is at: a pattern's problem is shown at its very token.
struct Text {
    std::string_view          all;
    std::vector<std::size_t>  lines;  // each line's first byte

    explicit Text(std::string_view t) : all(t) {
        lines.push_back(0);
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i] == '\n') lines.push_back(i + 1);
    }
    // A line and column (1-based, the column in characters) as a byte.
    [[nodiscard]] std::size_t byte(int line, int column) const {
        if (line < 1 || static_cast<std::size_t>(line) > lines.size()) return all.size();
        std::size_t b = lines[static_cast<std::size_t>(line - 1)];
        for (int c = 1; c < column && b < all.size() && all[b] != '\n'; ++c) {
            ++b;
            while (b < all.size() && (static_cast<unsigned char>(all[b]) & 0xC0) == 0x80) ++b;
        }
        return b;
    }
    [[nodiscard]] std::pair<int, int> place(std::size_t b) const {
        const auto it = std::ranges::upper_bound(lines, b);
        const auto line = static_cast<std::size_t>(it - lines.begin());
        int column = 1;
        for (std::size_t i = lines[line - 1]; i < b && i < all.size(); ++i)
            if ((static_cast<unsigned char>(all[i]) & 0xC0) != 0x80) ++column;
        return {static_cast<int>(line), column};
    }
    // The place of byte `offset` of the string value that begins (its quote) at `line`, `column`.
    [[nodiscard]] std::pair<int, int> inside(int line, int column, std::size_t offset) const {
        std::size_t b = byte(line, column);
        const std::string_view rest = all.substr(std::min(b, all.size()));
        const bool basic = rest.starts_with('"');
        if (rest.starts_with("\"\"\"") || rest.starts_with("'''")) {
            b += 3;
            if (all.substr(b).starts_with("\r\n")) b += 2;
            else if (all.substr(b).starts_with('\n')) b += 1;
        } else {
            b += 1;
        }
        for (std::size_t k = 0; k < offset && b < all.size(); ++k) b += basic && all[b] == '\\' ? 2 : 1;
        return place(b);
    }
};

struct Reader {
    std::string          file;
    Text                 text;
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
    void error_key(const toml::key& k, std::string message, std::string hint = {}) {
        const auto s = k.source();
        out.push_back({file, static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::move(message), std::move(hint)});
    }
    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end()) error_key(k, std::format("{} has no key `{}`", what, k.str()), nearest(k.str(), keys));
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
    void get(const toml::table& t, std::string_view key, float& into, std::string_view what, double lo, double hi) {
        if (auto v = number(t.get(key), what, lo, hi)) into = static_cast<float>(*v);
    }
    void whole(const toml::table& t, std::string_view key, std::int32_t& into, std::string_view what, std::int64_t lo, std::int64_t hi) {
        const toml::node* n = t.get(key);
        if (!n) return;
        const auto v = n->value_exact<std::int64_t>();
        if (!v) return error(n, std::format("{} is a whole number", what));
        if (*v < lo || *v > hi) return error(n, std::format("{} is {}–{}, not {}", what, lo, hi, *v));
        into = static_cast<std::int32_t>(*v);
    }
    void text_of(const toml::table& t, std::string_view key, std::string& into, std::string_view what, std::string_view example) {
        const toml::node* n = t.get(key);
        if (!n) return;
        if (auto s = n->value_exact<std::string>()) into = *s;
        else error(n, std::format("{} is text", what), std::string(example));
    }
    void flag(const toml::table& t, std::string_view key, bool& into, std::string_view what) {
        const toml::node* n = t.get(key);
        if (!n) return;
        if (auto b = n->value_exact<bool>()) into = *b;
        else error(n, std::format("{} is true or false", what));
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
    const toml::table* table(const toml::table& t, std::string_view key, std::string_view what, std::string_view example) {
        const toml::node* n = t.get(key);
        if (!n) return nullptr;
        if (const toml::table* x = n->as_table()) return x;
        error(n, std::format("{} is a table", what), std::string(example));
        return nullptr;
    }
    std::vector<std::string> names(const toml::table& t, std::string_view key, std::string_view what) {
        std::vector<std::string> v;
        const toml::node* n = t.get(key);
        if (!n) return v;
        const toml::array* a = n->as_array();
        if (!a) {
            error(n, std::format("{} is a list of names", what), "[\"a\", \"b\"]");
            return v;
        }
        for (const toml::node& x : *a) {
            if (auto s = x.value_exact<std::string>()) v.push_back(*s);
            else error(&x, std::format("{} is a list of names", what));
        }
        return v;
    }
    // A pattern's problems, at their tokens.
    void text_problems(const toml::node* n, std::span<const detail::TextProblem> problems) {
        const auto [line, column] = at(n);
        for (const auto& p : problems) {
            const auto [l, c] = text.inside(line, column, p.offset);
            out.push_back({file, l, c, p.message, p.hint});
        }
    }

    void adsr(const toml::table& t, Adsr& a, std::string_view what) {
        get(t, "attack", a.attack, std::format("{}'s `attack` (seconds)", what), 0, 30);
        get(t, "decay", a.decay, std::format("{}'s `decay` (seconds)", what), 0, 60);
        get(t, "sustain", a.sustain, std::format("{}'s `sustain` (0–1)", what), 0, 1);
        get(t, "release", a.release, std::format("{}'s `release` (seconds)", what), 0, 60);
    }

    // An engine's own table, for that engine only.
    const toml::table* part(const toml::table& t, std::string_view key, Engine engine, Engine own, std::string_view name) {
        const toml::table* x = table(t, key, std::format("a patch's `{}`", key), "{ … }");
        if (x && engine != own)
            error(t.get(key), std::format("patch `{}` has `{}`, which is for engine = \"{}\"", name, key, detail::engines[static_cast<std::size_t>(own)]),
                  std::format("its engine is \"{}\"", detail::engines[static_cast<std::size_t>(engine)]));
        return engine == own ? x : nullptr;
    }

    Patch patch(const toml::table& t, std::string_view name, bool builtin) {
        only(t, patch_keys, std::format("patch `{}`", name));
        Patch p;
        if (const toml::node* b = t.get("base")) {
            const auto base = b->value_exact<std::string>();
            const auto all = builtin ? std::span<const Patch>{} : builtin_patches();
            const auto it = base ? std::ranges::find(all, *base, &Patch::name) : all.end();
            if (it == all.end()) {
                std::vector<std::string_view> known;
                for (const Patch& x : all) known.push_back(x.name);
                error(b, std::format("patch `{}`'s base is not a built-in patch", name), base ? nearest(*base, known) : "base = \"koto\"");
            } else {
                p = *it;
                p.base = *base;
            }
        }
        p.name = std::string(name);
        if (auto e = choice(t.get("engine"), "an engine (analog, fm, pluck, drum)", detail::engines)) p.engine = static_cast<Engine>(*e);
        else if (p.base.empty() && !t.get("engine")) error(&t, std::format("patch `{}` has no engine", name), "engine = \"analog\", \"fm\", \"pluck\" or \"drum\"");
        get(t, "gain", p.gain, "a patch's `gain`", 0, 8);
        get(t, "glide", p.glide, "a patch's `glide` (seconds)", 0, 5);
        get(t, "drive", p.drive, "a patch's `drive` (0–1)", 0, 1);
        if (const toml::table* a = table(t, "amp", "a patch's `amp`", "amp = { attack = 0.01, decay = 0.3, sustain = 0.7, release = 0.2 }")) {
            only(*a, adsr_keys, "an `amp` envelope");
            adsr(*a, p.amp, "`amp`");
        }
        if (const toml::table* o = part(t, "osc", p.engine, Engine::analog, name)) {
            only(*o, osc_keys, "an `osc`");
            if (auto w = choice(o->get("wave"), "a wave (sine, triangle, square, saw, noise)", detail::waves)) p.osc.wave = static_cast<Wave>(*w);
            whole(*o, "unison", p.osc.unison, "an `osc`'s `unison`", 1, 7);
            get(*o, "detune", p.osc.detune, "an `osc`'s `detune` (cents)", 0, 200);
            get(*o, "spread", p.osc.spread, "an `osc`'s `spread` (0–1)", 0, 1);
            get(*o, "width", p.osc.width, "an `osc`'s `width`", 0.05, 0.95);
            get(*o, "pwm", p.osc.pwm, "an `osc`'s `pwm`", 0, 0.45);
            get(*o, "pwm_rate", p.osc.pwm_rate, "an `osc`'s `pwm_rate` (Hz)", 0, 40);
            get(*o, "sub", p.osc.sub, "an `osc`'s `sub`", 0, 2);
            get(*o, "noise", p.osc.noise, "an `osc`'s `noise`", 0, 2);
        }
        if (const toml::table* f = part(t, "fm", p.engine, Engine::fm, name)) {
            only(*f, fm_keys, "an `fm`");
            get(*f, "ratio", p.fm.ratio, "an `fm`'s `ratio`", 0.01, 32);
            get(*f, "index", p.fm.index, "an `fm`'s `index` (radians)", 0, 30);
            get(*f, "sustain", p.fm.sustain, "an `fm`'s `sustain` (radians)", 0, 30);
            get(*f, "decay", p.fm.decay, "an `fm`'s `decay` (seconds)", 0, 60);
            get(*f, "feedback", p.fm.feedback, "an `fm`'s `feedback` (0–1)", 0, 1);
        }
        if (const toml::table* k = part(t, "pluck", p.engine, Engine::pluck, name)) {
            only(*k, pluck_keys, "a `pluck`");
            get(*k, "brightness", p.pluck.brightness, "a `pluck`'s `brightness` (0–1)", 0, 1);
            get(*k, "decay", p.pluck.decay, "a `pluck`'s `decay` (seconds)", 0.05, 60);
            get(*k, "pick", p.pluck.pick, "a `pluck`'s `pick` (0–0.5)", 0, 0.5);
        }
        if (const toml::table* d = part(t, "drum", p.engine, Engine::drum, name)) {
            only(*d, drum_keys, "a `drum`");
            get(*d, "tone", p.drum.tone, "a `drum`'s `tone`", 0, 4);
            get(*d, "pitch", p.drum.pitch, "a `drum`'s `pitch` (Hz)", 10, 12000);
            get(*d, "from", p.drum.from, "a `drum`'s `from` (× its pitch)", 1, 16);
            get(*d, "sweep", p.drum.sweep, "a `drum`'s `sweep` (seconds)", 0.0001, 5);
            get(*d, "decay", p.drum.decay, "a `drum`'s `decay` (seconds)", 0.005, 30);
            get(*d, "modes", p.drum.modes, "a `drum`'s `modes`", 0, 2);
            get(*d, "noise", p.drum.noise, "a `drum`'s `noise`", 0, 4);
            get(*d, "noise_decay", p.drum.noise_decay, "a `drum`'s `noise_decay` (seconds)", 0.005, 30);
            get(*d, "metal", p.drum.metal, "a `drum`'s `metal`", 0, 4);
            get(*d, "click", p.drum.click, "a `drum`'s `click`", 0, 4);
            whole(*d, "bursts", p.drum.bursts, "a `drum`'s `bursts`", 1, 8);
        }
        if (const toml::table* f = table(t, "filter", "a patch's `filter`", "filter = { kind = \"lowpass\", cutoff = 1200, q = 0.7 }")) {
            only(*f, filter_keys, "a patch's `filter`");
            if (auto k = choice(f->get("kind"), "a filter (none, lowpass, highpass, bandpass)", detail::filters)) p.filter.kind = static_cast<FilterKind>(*k);
            get(*f, "cutoff", p.filter.cutoff, "a filter's `cutoff` (Hz)", 10, 24000);
            get(*f, "q", p.filter.q, "a filter's `q`", 0.05, 40);
            get(*f, "envelope", p.filter.envelope, "a filter's `envelope` (octaves)", -10, 10);
            get(*f, "keytrack", p.filter.keytrack, "a filter's `keytrack` (0–1)", 0, 1);
            adsr(*f, p.filter.adsr, "a filter");
        }
        if (const toml::table* v = table(t, "vibrato", "a patch's `vibrato`", "vibrato = { depth = 12, rate = 5.5, delay = 0.3 }")) {
            only(*v, vibrato_keys, "a `vibrato`");
            get(*v, "depth", p.vibrato.depth, "a vibrato's `depth` (cents)", 0, 1200);
            get(*v, "rate", p.vibrato.rate, "a vibrato's `rate` (Hz)", 0, 40);
            get(*v, "delay", p.vibrato.delay, "a vibrato's `delay` (seconds)", 0, 30);
        }
        return p;
    }

    EffectSpec effect(const toml::table& t) {
        EffectSpec e;
        const auto kind = choice(t.get("kind"), "an effect (lowpass, highpass, bandpass, echo, reverb, chorus)", detail::effects);
        if (!kind) {
            if (!t.get("kind")) error(&t, "an effect has no `kind`", "{ kind = \"reverb\", size = 0.8, wet = 1, dry = 0 }");
            return e;
        }
        e.kind = static_cast<EffectKind>(*kind);
        switch (e.kind) {
            case EffectKind::lowpass:
            case EffectKind::highpass:
            case EffectKind::bandpass:
                only(t, filter_effect_keys, std::format("a {}", detail::effects[*kind]));
                get(t, "cutoff", e.cutoff, "a filter's `cutoff` (Hz)", 10, 24000);
                get(t, "q", e.q, "a filter's `q`", 0.05, 40);
                break;
            case EffectKind::echo:
                only(t, echo_keys, "an echo");
                get(t, "time", e.time, "an echo's `time` (seconds)", 0.001, 10);
                get(t, "beats", e.beats, "an echo's `beats`", 0, 32);
                get(t, "feedback", e.feedback, "an echo's `feedback`", 0, 0.98);
                flag(t, "pingpong", e.pingpong, "an echo's `pingpong`");
                break;
            case EffectKind::reverb:
                only(t, reverb_keys, "a reverb");
                get(t, "size", e.size, "a reverb's `size` (0–1)", 0, 1);
                get(t, "damping", e.damping, "a reverb's `damping` (0–1)", 0, 1);
                get(t, "width", e.width, "a reverb's `width` (0–1)", 0, 1);
                get(t, "predelay", e.predelay, "a reverb's `predelay` (seconds)", 0, 1);
                break;
            case EffectKind::chorus:
                only(t, chorus_keys, "a chorus");
                get(t, "delay", e.delay, "a chorus's `delay` (seconds)", 0.001, 0.1);
                get(t, "depth", e.depth, "a chorus's `depth` (seconds)", 0, 0.05);
                get(t, "rate", e.rate, "a chorus's `rate` (Hz)", 0, 20);
                whole(t, "voices", e.voices, "a chorus's `voices`", 1, 4);
                break;
        }
        get(t, "wet", e.wet, "an effect's `wet`", 0, 4);
        get(t, "dry", e.dry, "an effect's `dry`", 0, 4);
        return e;
    }

    // A name that must be one of `known`.
    void known(const toml::node* n, std::string_view name, std::span<const std::string> names, std::string_view what, std::string_view where) {
        if (std::ranges::find(names, name) != names.end()) return;
        std::vector<std::string_view> options(names.begin(), names.end());
        error(n, std::format("{} `{}` is not {}", where, name, what), options.empty() ? std::string{} : nearest(name, options));
    }
    void known_key(const toml::key& k, std::span<const std::string> names, std::string_view what, std::string_view where) {
        if (std::ranges::find(names, k.str()) != names.end()) return;
        std::vector<std::string_view> options(names.begin(), names.end());
        error_key(k, std::format("{} `{}` is not {}", where, k.str(), what), options.empty() ? std::string{} : nearest(k.str(), options));
    }
};

template <class T>
std::vector<std::string> names_of(const std::vector<T>& v) {
    std::vector<std::string> n;
    for (const T& x : v) n.push_back(x.name);
    return n;
}

// Reads every [patch.<name>] of `root` into `out`.
void read_patches(Reader& r, const toml::table& root, std::vector<Patch>& out, bool builtin) {
    const toml::node* n = root.get("patch");
    if (!n) return;
    const toml::table* all = n->as_table();
    if (!all) return r.error(n, "`patch` is [patch.<name>] tables");
    for (const auto& [name, body] : *all) {
        if (const toml::table* t = body.as_table()) out.push_back(r.patch(*t, name.str(), builtin));
        else r.error(&body, std::format("patch `{}` is a table", name.str()));
    }
}

std::vector<Patch> make_builtins() {
    Reader r{"builtin.music.omui.toml", Text(builtin_text), {}};
    toml::table root = toml::parse(builtin_text, std::string_view{"builtin.music.omui.toml"});
    std::vector<Patch> v;
    read_patches(r, root, v, true);
    if (!r.out.empty()) {
        for (const auto& p : r.out) std::println(std::cerr, "{}", p.text());
        std::abort();  // the library's own patches: a test catches this first
    }
    return v;
}

// ── writing ──

std::string num(float v) { return std::format("{}", v); }

std::string toml_string(std::string_view s) {
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

// A name as a key: bare when it may be, else quoted.
std::string key(std::string_view name) {
    const bool bare = !name.empty() && std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    return bare ? std::string(name) : toml_string(name);
}

// Text over lines as a multi-line string (so it reads back as itself), else quoted.
std::string text_value(std::string_view s) {
    if (s.find('\n') == std::string_view::npos) return toml_string(s);
    std::string out = "\"\"\"\n";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\r') {
            out += "\\r";
            continue;
        }
        out += c;
    }
    return out + "\"\"\"";
}

// `key = value` pairs, those that differ from a base, as an inline table.
struct Fields {
    std::vector<std::string> items;
    void add(std::string_view key, float v, float base) {
        if (v != base) items.push_back(std::format("{} = {}", key, num(v)));
    }
    void add(std::string_view key, std::int32_t v, std::int32_t base) {
        if (v != base) items.push_back(std::format("{} = {}", key, v));
    }
    void add(std::string_view key, std::string_view v, std::string_view base) {
        if (v != base) items.push_back(std::format("{} = {}", key, toml_string(v)));
    }
    void add(std::string_view key, bool v, bool base) {
        if (v != base) items.push_back(std::format("{} = {}", key, v));
    }
    [[nodiscard]] std::string inline_table() const {
        std::string s = "{ ";
        for (std::size_t i = 0; i < items.size(); ++i) s += (i ? ", " : "") + items[i];
        return s + " }";
    }
};

void write_adsr(Fields& f, const Adsr& a, const Adsr& b) {
    f.add("attack", a.attack, b.attack);
    f.add("decay", a.decay, b.decay);
    f.add("sustain", a.sustain, b.sustain);
    f.add("release", a.release, b.release);
}

std::string write_patch(const Patch& p) {
    Patch b;  // what it differs from: its base, or the defaults
    if (!p.base.empty()) {
        const auto all = builtin_patches();
        if (const auto it = std::ranges::find(all, p.base, &Patch::name); it != all.end()) b = *it;
    }
    std::string s = std::format("[patch.{}]\n", key(p.name));
    if (!p.base.empty()) s += std::format("base = {}\n", toml_string(p.base));
    if (p.base.empty() || p.engine != b.engine) s += std::format("engine = \"{}\"\n", detail::engines[static_cast<std::size_t>(p.engine)]);
    if (p.engine != b.engine) {
        // Another engine than its base's: its own parts in full against the defaults.
        const Patch d;
        b.osc = d.osc, b.fm = d.fm, b.pluck = d.pluck, b.drum = d.drum;
    }
    auto line = [&](std::string_view key, const Fields& f) {
        if (!f.items.empty()) s += std::format("{} = {}\n", key, f.inline_table());
    };
    {
        Fields f;
        write_adsr(f, p.amp, b.amp);
        line("amp", f);
    }
    if (p.engine == Engine::analog) {
        Fields f;
        f.add("wave", detail::waves[static_cast<std::size_t>(p.osc.wave)], detail::waves[static_cast<std::size_t>(b.osc.wave)]);
        f.add("unison", p.osc.unison, b.osc.unison);
        f.add("detune", p.osc.detune, b.osc.detune);
        f.add("spread", p.osc.spread, b.osc.spread);
        f.add("width", p.osc.width, b.osc.width);
        f.add("pwm", p.osc.pwm, b.osc.pwm);
        f.add("pwm_rate", p.osc.pwm_rate, b.osc.pwm_rate);
        f.add("sub", p.osc.sub, b.osc.sub);
        f.add("noise", p.osc.noise, b.osc.noise);
        line("osc", f);
    }
    if (p.engine == Engine::fm) {
        Fields f;
        f.add("ratio", p.fm.ratio, b.fm.ratio);
        f.add("index", p.fm.index, b.fm.index);
        f.add("sustain", p.fm.sustain, b.fm.sustain);
        f.add("decay", p.fm.decay, b.fm.decay);
        f.add("feedback", p.fm.feedback, b.fm.feedback);
        line("fm", f);
    }
    if (p.engine == Engine::pluck) {
        Fields f;
        f.add("brightness", p.pluck.brightness, b.pluck.brightness);
        f.add("decay", p.pluck.decay, b.pluck.decay);
        f.add("pick", p.pluck.pick, b.pluck.pick);
        line("pluck", f);
    }
    if (p.engine == Engine::drum) {
        Fields f;
        f.add("tone", p.drum.tone, b.drum.tone);
        f.add("pitch", p.drum.pitch, b.drum.pitch);
        f.add("from", p.drum.from, b.drum.from);
        f.add("sweep", p.drum.sweep, b.drum.sweep);
        f.add("decay", p.drum.decay, b.drum.decay);
        f.add("modes", p.drum.modes, b.drum.modes);
        f.add("noise", p.drum.noise, b.drum.noise);
        f.add("noise_decay", p.drum.noise_decay, b.drum.noise_decay);
        f.add("metal", p.drum.metal, b.drum.metal);
        f.add("click", p.drum.click, b.drum.click);
        f.add("bursts", p.drum.bursts, b.drum.bursts);
        line("drum", f);
    }
    {
        Fields f;
        f.add("kind", detail::filters[static_cast<std::size_t>(p.filter.kind)], detail::filters[static_cast<std::size_t>(b.filter.kind)]);
        f.add("cutoff", p.filter.cutoff, b.filter.cutoff);
        f.add("q", p.filter.q, b.filter.q);
        f.add("envelope", p.filter.envelope, b.filter.envelope);
        f.add("keytrack", p.filter.keytrack, b.filter.keytrack);
        write_adsr(f, p.filter.adsr, b.filter.adsr);
        line("filter", f);
    }
    {
        Fields f;
        f.add("depth", p.vibrato.depth, b.vibrato.depth);
        f.add("rate", p.vibrato.rate, b.vibrato.rate);
        f.add("delay", p.vibrato.delay, b.vibrato.delay);
        line("vibrato", f);
    }
    if (p.glide != b.glide) s += std::format("glide = {}\n", num(p.glide));
    if (p.drive != b.drive) s += std::format("drive = {}\n", num(p.drive));
    if (p.gain != b.gain) s += std::format("gain = {}\n", num(p.gain));
    return s + "\n";
}

std::string write_effect(const EffectSpec& e) {
    const EffectSpec d{.kind = e.kind};
    Fields f;
    f.items.push_back(std::format("kind = \"{}\"", detail::effects[static_cast<std::size_t>(e.kind)]));
    switch (e.kind) {
        case EffectKind::lowpass:
        case EffectKind::highpass:
        case EffectKind::bandpass:
            f.add("cutoff", e.cutoff, d.cutoff);
            f.add("q", e.q, d.q);
            return f.inline_table();
        case EffectKind::echo:
            f.add("time", e.time, d.time);
            f.add("beats", e.beats, d.beats);
            f.add("feedback", e.feedback, d.feedback);
            f.add("pingpong", e.pingpong, d.pingpong);
            break;
        case EffectKind::reverb:
            f.add("size", e.size, d.size);
            f.add("damping", e.damping, d.damping);
            f.add("width", e.width, d.width);
            f.add("predelay", e.predelay, d.predelay);
            break;
        case EffectKind::chorus:
            f.add("delay", e.delay, d.delay);
            f.add("depth", e.depth, d.depth);
            f.add("rate", e.rate, d.rate);
            f.add("voices", e.voices, d.voices);
            break;
    }
    f.add("wet", e.wet, d.wet);
    f.add("dry", e.dry, d.dry);
    return f.inline_table();
}

std::string names_list(std::span<const std::string> v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + toml_string(v[i]);
    return s + "]";
}

// Every name a piece's references point at, by kind.
struct Names {
    std::vector<std::string> patches, instruments, buses, patterns, sections;
};

}  // namespace

std::span<const Patch> builtin_patches() {
    static const std::vector<Patch> all = make_builtins();
    return all;
}

std::expected<Music, std::vector<Problem>> read_music(std::string_view source, std::string_view file) {
    Reader r{std::string(file), Text(source), {}};
    toml::table root;
    try {
        root = toml::parse(source, file);
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(std::vector<Problem>{{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), {}}});
    }
    Music m;
    r.only(root, root_keys, "a music file");
    r.text_of(root, "doc", m.doc, "`doc`", "doc = \"what the piece is\"");

    // The piece itself.
    if (const toml::table* t = r.table(root, "music", "`music`", "[music] tempo = 128, meter = [4, 4]")) {
        r.only(*t, music_keys, "[music]");
        r.text_of(*t, "name", m.name, "a piece's `name`", "name = \"study\"");
        r.get(*t, "tempo", m.tempo, "a piece's `tempo` (beats a minute)", 20, 400);
        r.get(*t, "gain", m.gain, "a piece's `gain`", 0, 4);
        if (const toml::node* n = t->get("meter")) {
            const toml::array* a = n->as_array();
            const auto beats = a && a->size() == 2 ? a->get(0)->value_exact<std::int64_t>() : std::nullopt;
            const auto note = a && a->size() == 2 ? a->get(1)->value_exact<std::int64_t>() : std::nullopt;
            if (!beats || !note || *beats < 1 || *beats > 32 || (*note != 1 && *note != 2 && *note != 4 && *note != 8 && *note != 16))
                r.error(n, "a `meter` is [beats in a bar, the beat's note]: 1–32 beats of a 1, 2, 4, 8 or 16th note", "meter = [4, 4]");
            else
                m.meter = {static_cast<std::int32_t>(*beats), static_cast<std::int32_t>(*note)};
        }
    } else if (!root.get("music")) {
        r.error(&root, "a music file has no [music] table", "[music] name = \"…\", tempo = 128, meter = [4, 4]");
    }

    read_patches(r, root, m.patches, false);

    // Each kind of [<kind>.<name>] table, read by `each`.
    auto tables = [&](std::string_view kind, auto&& each) {
        const toml::node* n = root.get(kind);
        if (!n) return;
        const toml::table* all = n->as_table();
        if (!all) return r.error(n, std::format("`{}` is [{}.<name>] tables", kind, kind));
        for (const auto& [name, body] : *all) {
            if (const toml::table* t = body.as_table()) each(name, *t);
            else r.error(&body, std::format("{} `{}` is a table", kind, name.str()));
        }
    };

    // Where each reference was, to check once every name is known.
    struct Ref {
        const toml::node* node = nullptr;
        const toml::key*  key = nullptr;
        std::string       name, what, where;
        int               kind = 0;  // 0 patch, 1 instrument, 2 bus, 3 pattern, 4 section
    };
    std::vector<Ref> refs;

    tables("instrument", [&](const toml::key& name, const toml::table& t) {
        r.only(t, instrument_keys, std::format("instrument `{}`", name.str()));
        Instrument in;
        in.name = std::string(name.str());
        r.text_of(t, "patch", in.patch, "an instrument's `patch`", "patch = \"koto\"");
        if (!t.get("patch")) r.error(&t, std::format("instrument `{}` has no patch", in.name), "patch = \"koto\" (one of the piece's, or a built-in)");
        else if (!in.patch.empty()) refs.push_back({t.get("patch"), nullptr, in.patch, "a patch of the piece's or a built-in", std::format("instrument `{}`'s patch", in.name), 0});
        r.text_of(t, "bus", in.bus, "an instrument's `bus`", "bus = \"drums\"");
        if (!in.bus.empty()) refs.push_back({t.get("bus"), nullptr, in.bus, "one of the piece's buses", std::format("instrument `{}`'s bus", in.name), 2});
        r.get(t, "gain", in.gain, "an instrument's `gain`", 0, 8);
        r.get(t, "pan", in.pan, "an instrument's `pan` (−1–1)", -1, 1);
        r.get(t, "legato", in.legato, "an instrument's `legato` (0–1)", 0.01, 1);
        r.get(t, "humanize", in.humanize, "an instrument's `humanize` (0–1)", 0, 1);
        r.whole(t, "transpose", in.transpose, "an instrument's `transpose` (semitones)", -48, 48);
        r.whole(t, "voices", in.voices, "an instrument's `voices`", 1, 64);
        if (const toml::table* s = r.table(t, "sends", "an instrument's `sends`", "sends = { reverb = 0.3 }")) {
            for (const auto& [bus, level] : *s) {
                Send send{std::string(bus.str()), 0};
                if (auto v = r.number(&level, "a send's level", 0, 4)) send.level = static_cast<float>(*v);
                refs.push_back({nullptr, &bus, send.bus, "one of the piece's buses", std::format("instrument `{}` sends to", in.name), 2});
                in.sends.push_back(std::move(send));
            }
        }
        r.text_of(t, "gate", in.gate, "an instrument's `gate`", "gate = \"x.xx.x.x\"");
        if (const toml::node* g = t.get("gate")) {
            std::vector<detail::TextProblem> problems;
            std::size_t steps = 0;
            for (std::size_t i = 0; i < in.gate.size(); ++i) {
                const char c = in.gate[i];
                if (c == 'x' || c == '.') ++steps;
                else if (c != '|' && c != ' ')
                    problems.push_back({i, std::format("`{}` is not a gate's step", std::string_view(&in.gate[i], 1)), "`x` open, `.` shut, a sixteenth each"});
            }
            if (steps == 0 && problems.empty()) r.error(g, "a gate has no steps", "gate = \"x.x.xx.x\"");
            r.text_problems(g, problems);
        }
        r.text_of(t, "choke", in.choke, "an instrument's `choke`", "choke = \"hats\"");
        m.instruments.push_back(std::move(in));
    });

    tables("bus", [&](const toml::key& name, const toml::table& t) {
        r.only(t, bus_keys, std::format("bus `{}`", name.str()));
        MusicBus b;
        b.name = std::string(name.str());
        r.text_of(t, "to", b.to, "a bus's `to`", "to = \"drums\"");
        if (!b.to.empty()) refs.push_back({t.get("to"), nullptr, b.to, "one of the piece's buses", std::format("bus `{}` goes to", b.name), 2});
        r.get(t, "gain", b.gain, "a bus's `gain`", 0, 8);
        if (const toml::node* n = t.get("effects")) {
            if (const toml::array* a = n->as_array()) {
                for (const toml::node& e : *a)
                    if (const toml::table* et = e.as_table()) b.effects.push_back(r.effect(*et));
                    else r.error(&e, "an effect is a table", "{ kind = \"reverb\", size = 0.8 }");
            } else {
                r.error(n, "a bus's `effects` is a list", "effects = [{ kind = \"reverb\", size = 0.8, wet = 1, dry = 0 }]");
            }
        }
        if (const toml::table* d = r.table(t, "duck", "a bus's `duck`", "duck = { key = \"kick\", depth = 0.6, release = 0.18 }")) {
            r.only(*d, duck_keys, "a `duck`");
            Duck duck;
            r.text_of(*d, "key", duck.key, "a duck's `key`", "key = \"kick\"");
            if (duck.key.empty()) r.error(t.get("duck"), "a duck has no `key`", "key = \"kick\" (the bus it ducks under)");
            else refs.push_back({d->get("key"), nullptr, duck.key, "one of the piece's buses", std::format("bus `{}` ducks under", b.name), 2});
            r.get(*d, "depth", duck.depth, "a duck's `depth` (0–1)", 0, 1);
            r.get(*d, "threshold", duck.threshold, "a duck's `threshold`", 0.001, 4);
            r.get(*d, "attack", duck.attack, "a duck's `attack` (seconds)", 0, 5);
            r.get(*d, "release", duck.release, "a duck's `release` (seconds)", 0, 10);
            b.duck = duck;
        }
        m.buses.push_back(std::move(b));
    });

    tables("pattern", [&](const toml::key& name, const toml::table& t) {
        r.only(t, pattern_keys, std::format("pattern `{}`", name.str()));
        Pattern p;
        p.name = std::string(name.str());
        r.text_of(t, "instrument", p.instrument, "a pattern's `instrument`", "instrument = \"lead\"");
        r.text_of(t, "notes", p.notes, "a pattern's `notes`", "notes = \"D5 . F5 G5 | A5:2 . C6\"");
        if (const toml::node* s = t.get("step")) {
            const auto v = s->value_exact<std::string>();
            std::int32_t d = 0;
            if (v && v->starts_with("1/")) {
                const auto [end, error] = std::from_chars(v->data() + 2, v->data() + v->size(), d);
                if (error != std::errc{} || end != v->data() + v->size()) d = 0;
            }
            if (d < 1 || d > 128 || whole_ticks % d != 0) r.error(s, "a pattern's `step` is a note: \"1/4\", \"1/8\", \"1/16\", \"1/12\" (triplets) …", "step = \"1/16\"");
            else p.step = d;
        }
        r.get(t, "swing", p.swing, "a pattern's `swing` (0–0.5)", 0, 0.5);
        std::vector<const toml::node*> lane_nodes;
        if (const toml::table* lanes = r.table(t, "lanes", "a pattern's `lanes`", "[pattern.<name>.lanes] kick = \"x...x...\"")) {
            for (const auto& [inst, steps] : *lanes) {
                Lane lane{std::string(inst.str()), {}};
                if (auto s = steps.value_exact<std::string>()) lane.steps = *s;
                else r.error(&steps, "a lane is text", "kick = \"x...x...x..x.x..\"");
                refs.push_back({nullptr, &inst, lane.instrument, "one of the piece's instruments", std::format("pattern `{}` has a lane for", p.name), 1});
                p.lanes.push_back(std::move(lane));
                lane_nodes.push_back(&steps);
            }
        }
        if (p.notes.empty() == p.lanes.empty())
            r.error(&t, std::format("pattern `{}` has {}", p.name, p.notes.empty() ? "neither notes nor lanes" : "both notes and lanes"),
                    "notes = \"…\" with instrument = \"…\", or [pattern.<name>.lanes]");
        if (!p.notes.empty()) {
            if (!t.get("instrument")) r.error(t.get("notes"), std::format("pattern `{}`'s notes have no instrument", p.name), "instrument = \"lead\"");
            else refs.push_back({t.get("instrument"), nullptr, p.instrument, "one of the piece's instruments", std::format("pattern `{}`'s instrument", p.name), 1});
        }
        // The text, read (with the piece's metre, once [music] is read: it is, toml++ has the whole file).
        const std::int32_t step = detail::step_ticks(p), bar = m.bar_ticks();
        if (bar % step != 0) {
            r.error(t.get("step") ? t.get("step") : &t, std::format("steps of 1/{} do not fill a bar of {}/{}", whole_ticks / step, m.meter[0], m.meter[1]));
        } else {
            std::vector<Hit> hits;
            std::vector<detail::TextProblem> problems;
            if (!p.notes.empty()) {
                detail::read_notes(p.notes, step, bar, p.swing, hits, problems);
                r.text_problems(t.get("notes"), problems);
            }
            for (std::size_t l = 0; l < p.lanes.size(); ++l) {
                problems.clear();
                detail::read_lane(p.lanes[l].steps, step, bar, p.swing, static_cast<std::int32_t>(l), hits, problems);
                r.text_problems(lane_nodes[l], problems);
            }
        }
        m.patterns.push_back(std::move(p));
    });

    tables("section", [&](const toml::key& name, const toml::table& t) {
        r.only(t, section_keys, std::format("section `{}`", name.str()));
        Section s;
        s.name = std::string(name.str());
        if (!t.get("bars")) r.error(&t, std::format("section `{}` has no `bars`", s.name), "bars = 8");
        r.whole(t, "bars", s.bars, "a section's `bars`", 1, 1024);
        if (const toml::node* n = t.get("layers")) {
            if (const toml::array* a = n->as_array()) {
                for (const toml::node& x : *a) {
                    const toml::table* lt = x.as_table();
                    if (!lt) {
                        r.error(&x, "a layer is a table", "{ pattern = \"hook\", intensity = [0.5, 1] }");
                        continue;
                    }
                    r.only(*lt, layer_keys, "a layer");
                    MusicLayer l;
                    r.text_of(*lt, "pattern", l.pattern, "a layer's `pattern`", "pattern = \"hook\"");
                    r.text_of(*lt, "instrument", l.instrument, "a layer's `instrument`", "instrument = \"flute\"");
                    if (!l.instrument.empty())
                        refs.push_back({lt->get("instrument"), nullptr, l.instrument, "one of the piece's instruments", std::format("section `{}` plays with", s.name), 1});
                    if (l.pattern.empty()) r.error(&x, "a layer has no pattern", "{ pattern = \"hook\" }");
                    else refs.push_back({lt->get("pattern"), nullptr, l.pattern, "one of the piece's patterns", std::format("section `{}` plays", s.name), 3});
                    if (const toml::node* i = lt->get("intensity")) {
                        const toml::array* ia = i->as_array();
                        std::optional<double> lo, hi;
                        if (ia && ia->size() == 2) lo = r.number(ia->get(0), "an intensity", 0, 1), hi = r.number(ia->get(1), "an intensity", 0, 1);
                        else r.error(i, "a layer's `intensity` is [from, to]", "intensity = [0.5, 1]");
                        if (lo && hi) {
                            if (*lo >= *hi) r.error(i, std::format("an intensity's range [{}, {}] holds nothing", *lo, *hi), "from below to: [0.5, 1]");
                            else l.intensity = {static_cast<float>(*lo), static_cast<float>(*hi)};
                        }
                    }
                    r.whole(*lt, "at", l.at, "a layer's `at` (bars)", 0, 1023);
                    r.whole(*lt, "times", l.times, "a layer's `times`", 0, 1024);
                    r.whole(*lt, "transpose", l.transpose, "a layer's `transpose` (semitones)", -48, 48);
                    r.get(*lt, "gain", l.gain, "a layer's `gain`", 0, 4);
                    if (l.at >= s.bars && lt->get("at")) r.error(lt->get("at"), std::format("bar {} is past section `{}`'s {} bars", l.at, s.name, s.bars), "bars count from 0");
                    s.layers.push_back(std::move(l));
                }
                if (s.layers.size() > 64) r.error(n, std::format("section `{}` has {} layers; 64 at most", s.name, s.layers.size()));
            } else {
                r.error(n, "a section's `layers` is a list", "layers = [{ pattern = \"hook\" }, { pattern = \"beat\", intensity = [0.5, 1] }]");
            }
        }
        m.sections.push_back(std::move(s));
    });

    if (const toml::table* t = r.table(root, "arrangement", "`arrangement`", "[arrangement] intro = [\"intro\"], loop = [\"a\", \"b\"]")) {
        r.only(*t, arrangement_keys, "[arrangement]");
        m.arrangement.intro = r.names(*t, "intro", "an arrangement's `intro`");
        m.arrangement.loop = r.names(*t, "loop", "an arrangement's `loop`");
        m.arrangement.outro = r.names(*t, "outro", "an arrangement's `outro`");
        if (m.arrangement.loop.empty()) r.error(t, "an arrangement has no loop", "loop = [\"a\", \"b\"]");
        for (const char* part : {"intro", "loop", "outro"})
            if (const toml::array* a = t->get(part) ? t->get(part)->as_array() : nullptr)
                for (const toml::node& x : *a)
                    if (auto s = x.value_exact<std::string>()) refs.push_back({&x, nullptr, *s, "one of the piece's sections", std::format("the {} plays", part), 4});
    } else if (!root.get("arrangement")) {
        r.error(&root, "a music file has no [arrangement]", "[arrangement] intro = [\"intro\"], loop = [\"a\", \"b\"]");
    }

    // Every reference, now that every name is known.
    Names known{names_of(m.patches), names_of(m.instruments), names_of(m.buses), names_of(m.patterns), names_of(m.sections)};
    for (const Patch& p : builtin_patches())
        if (std::ranges::find(known.patches, p.name) == known.patches.end()) known.patches.push_back(p.name);
    const std::array<const std::vector<std::string>*, 5> lists{&known.patches, &known.instruments, &known.buses, &known.patterns, &known.sections};
    for (const Ref& ref : refs) {
        if (ref.key) r.known_key(*ref.key, *lists[static_cast<std::size_t>(ref.kind)], ref.what, ref.where);
        else r.known(ref.node, ref.name, *lists[static_cast<std::size_t>(ref.kind)], ref.what, ref.where);
    }
    // Buses going round.
    for (const MusicBus& b : m.buses) {
        std::string at = b.to;
        for (std::size_t hops = 0; !at.empty() && hops <= m.buses.size(); ++hops) {
            if (at == b.name) {
                const toml::node* n = root.at_path(std::format("bus.{}.to", b.name)).node();
                r.error(n, std::format("bus `{}` comes back to itself through `to`", b.name), "a bus goes to one nearer the piece's own");
                break;
            }
            const auto it = std::ranges::find(m.buses, at, &MusicBus::name);
            at = it == m.buses.end() ? std::string{} : it->to;
        }
    }
    if (!r.out.empty()) {
        std::ranges::stable_sort(r.out, [](const Problem& a, const Problem& b) { return std::pair(a.line, a.column) < std::pair(b.line, b.column); });
        return std::unexpected(std::move(r.out));
    }
    return m;
}

std::string write_music(const Music& m) {
    std::string s;
    if (!m.doc.empty()) s += std::format("doc = {}\n\n", text_value(m.doc));
    s += std::format("[music]\nname = {}\ntempo = {}\nmeter = [{}, {}]\n", toml_string(m.name), num(m.tempo), m.meter[0], m.meter[1]);
    if (m.gain != 1) s += std::format("gain = {}\n", num(m.gain));
    s += "\n";
    for (const Patch& p : m.patches) s += write_patch(p);
    for (const Instrument& in : m.instruments) {
        s += std::format("[instrument.{}]\npatch = {}\n", key(in.name), toml_string(in.patch));
        const Instrument d;
        if (!in.bus.empty()) s += std::format("bus = {}\n", toml_string(in.bus));
        if (in.gain != d.gain) s += std::format("gain = {}\n", num(in.gain));
        if (in.pan != d.pan) s += std::format("pan = {}\n", num(in.pan));
        if (in.legato != d.legato) s += std::format("legato = {}\n", num(in.legato));
        if (in.humanize != d.humanize) s += std::format("humanize = {}\n", num(in.humanize));
        if (in.transpose != d.transpose) s += std::format("transpose = {}\n", in.transpose);
        if (in.voices != d.voices) s += std::format("voices = {}\n", in.voices);
        if (!in.sends.empty()) {
            Fields f;
            for (const Send& x : in.sends) f.items.push_back(std::format("{} = {}", key(x.bus), num(x.level)));
            s += std::format("sends = {}\n", f.inline_table());
        }
        if (!in.gate.empty()) s += std::format("gate = {}\n", toml_string(in.gate));
        if (!in.choke.empty()) s += std::format("choke = {}\n", toml_string(in.choke));
        s += "\n";
    }
    for (const MusicBus& b : m.buses) {
        s += std::format("[bus.{}]\n", key(b.name));
        if (!b.to.empty()) s += std::format("to = {}\n", toml_string(b.to));
        if (b.gain != 1) s += std::format("gain = {}\n", num(b.gain));
        if (!b.effects.empty()) {
            s += "effects = [\n";
            for (const EffectSpec& e : b.effects) s += std::format("    {},\n", write_effect(e));
            s += "]\n";
        }
        if (b.duck) {
            const Duck d;
            Fields f;
            f.items.push_back(std::format("key = {}", toml_string(b.duck->key)));
            f.add("depth", b.duck->depth, d.depth);
            f.add("threshold", b.duck->threshold, d.threshold);
            f.add("attack", b.duck->attack, d.attack);
            f.add("release", b.duck->release, d.release);
            s += std::format("duck = {}\n", f.inline_table());
        }
        s += "\n";
    }
    for (const Pattern& p : m.patterns) {
        s += std::format("[pattern.{}]\n", key(p.name));
        if (!p.notes.empty() || !p.instrument.empty()) s += std::format("instrument = {}\n", toml_string(p.instrument));
        if (p.step != 0) s += std::format("step = \"1/{}\"\n", p.step);
        if (p.swing != 0) s += std::format("swing = {}\n", num(p.swing));
        if (!p.notes.empty()) s += std::format("notes = {}\n", text_value(p.notes));
        if (!p.lanes.empty()) {
            std::size_t width = 0;
            for (const Lane& l : p.lanes) width = std::max(width, key(l.instrument).size());
            s += std::format("\n[pattern.{}.lanes]\n", key(p.name));
            for (const Lane& l : p.lanes) s += std::format("{:<{}} = {}\n", key(l.instrument), width, text_value(l.steps));
        }
        s += "\n";
    }
    for (const Section& sec : m.sections) {
        s += std::format("[section.{}]\nbars = {}\n", key(sec.name), sec.bars);
        if (!sec.layers.empty()) {
            s += "layers = [\n";
            for (const MusicLayer& l : sec.layers) {
                const MusicLayer d;
                Fields f;
                f.items.push_back(std::format("pattern = {}", toml_string(l.pattern)));
                if (!l.instrument.empty()) f.items.push_back(std::format("instrument = {}", toml_string(l.instrument)));
                if (l.intensity != d.intensity) f.items.push_back(std::format("intensity = [{}, {}]", num(l.intensity[0]), num(l.intensity[1])));
                f.add("at", l.at, d.at);
                f.add("times", l.times, d.times);
                f.add("transpose", l.transpose, d.transpose);
                f.add("gain", l.gain, d.gain);
                s += std::format("    {},\n", f.inline_table());
            }
            s += "]\n";
        }
        s += "\n";
    }
    s += "[arrangement]\n";
    if (!m.arrangement.intro.empty()) s += std::format("intro = {}\n", names_list(m.arrangement.intro));
    s += std::format("loop = {}\n", names_list(m.arrangement.loop));
    if (!m.arrangement.outro.empty()) s += std::format("outro = {}\n", names_list(m.arrangement.outro));
    return s;
}

}  // namespace omui::audio
