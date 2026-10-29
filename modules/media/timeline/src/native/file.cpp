// Reading and writing *.timeline.omui.toml. toml++ reports a malformed file
// by throwing; this unit — the library's adapter — turns that into a problem
// at its line and column, as it does every other.
module openmui.timeline;

import std;
import hanim;
import tomlplusplus;

namespace omui::timeline {

namespace {

constexpr std::array<std::string_view, 4> root_keys{"timeline", "marker", "track", "doc"};
constexpr std::array<std::string_view, 5> timeline_keys{"name", "rate", "sample_rate", "size", "doc"};
constexpr std::array<std::string_view, 2> marker_keys{"at", "name"};
constexpr std::array<std::string_view, 5> track_keys{"name", "kind", "muted", "clip", "doc"};
constexpr std::array<std::string_view, 10> clip_keys{"name", "source", "at", "length", "in", "speed", "level", "fade", "property", "doc"};
constexpr std::array<std::string_view, 3> fade_keys{"in", "out", "ease"};
constexpr std::array<std::string_view, 2> property_keys{"name", "keys"};
constexpr std::array<std::string_view, 4> kinds{"video", "audio", "animation", "events"};

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> d((a.size() + 1) * (b.size() + 1));
    const std::size_t w = b.size() + 1;
    for (std::size_t i = 0; i <= a.size(); ++i) d[i * w] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) d[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
        {
            std::size_t v = std::min({d[(i - 1) * w + j] + 1, d[i * w + j - 1] + 1, d[(i - 1) * w + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[(i - 2) * w + j - 2] + 1);  // two letters swapped
            d[i * w + j] = v;
        }
    return d[a.size() * w + b.size()];
}

template <std::size_t N>
std::string nearest(std::string_view name, const std::array<std::string_view, N>& options) {
    std::string_view best;
    std::size_t d = std::max<std::size_t>(1, name.size() / 3) + 1;
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) d = x, best = o;
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", o);
    return std::format("one of {}", all);
}

struct Reader {
    std::string          file;
    Rate                 rate;
    std::vector<Problem> out;

    void error(const toml::node* n, std::string message, std::string hint = {}) {
        int l = 0, c = 0;
        if (n) l = static_cast<int>(n->source().begin.line), c = static_cast<int>(n->source().begin.column);
        out.push_back({file, l, c, std::move(message), std::move(hint)});
    }
    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end()) {
                const auto s = k.source();
                out.push_back({file, static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::format("{} has no key `{}`", where, k.str()),
                               nearest(k.str(), keys)});
            }
    }
    std::optional<std::string> text(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n->value_exact<std::string>()) return *s;
        error(n, std::format("{} is text", what));
        return std::nullopt;
    }
    std::optional<double> number(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto v = n->value<double>()) return *v;
        error(n, std::format("{} is a number", what));
        return std::nullopt;
    }
    // A time: text ("90f", "1.5s", "250ms", "123fl") or a number of seconds.
    std::optional<Flicks> time(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n->value_exact<std::string>()) {
            if (auto t = parse_time(*s, rate)) return t;
            error(n, std::format("`{}` is not {}", *s, what), "90f (frames), 1.5s, 250ms or 123fl (flicks): a whole number of flicks");
            return std::nullopt;
        }
        if (auto v = n->value<double>()) return from_seconds(*v);
        error(n, std::format("{} is a time", what), "\"90f\", \"1.5s\", or a number of seconds");
        return std::nullopt;
    }
    template <class F>
    void tables(const toml::node* n, std::string_view name, F&& each) {
        if (!n) return;
        if (const toml::array* a = n->as_array()) {
            for (const toml::node& x : *a)
                if (const toml::table* t = x.as_table()) each(*t);
                else error(&x, std::format("a {} is a table", name), std::format("[[{}]]", name));
        } else {
            error(n, std::format("`{}` is [[{}]] tables", name, name));
        }
    }

    Property property(const toml::table& t) {
        only(t, property_keys, "a property");
        Property p;
        if (auto s = text(t.get("name"), "a property's `name`")) p.name = *s;
        else if (!t.get("name")) error(&t, "a property has a `name`", "name = \"opacity\"");
        const toml::array* keys = t.get("keys") ? t.get("keys")->as_array() : nullptr;
        if (!keys) {
            error(t.get("keys") ? t.get("keys") : &t, "a property's `keys` are [time, value] pairs", "keys = [[\"0f\", 0, \"out_cubic\"], [\"15f\", 1]]");
            return p;
        }
        for (const toml::node& k : *keys) {
            const toml::array* a = k.as_array();
            if (!a || a->size() < 2 || a->size() > 3) {
                error(&k, "a key is [time, value] or [time, value, ease]", "[\"15f\", 1, \"out_cubic\"]; \"hold\" in place of the ease holds it");
                continue;
            }
            const auto at = time(a->get(0), "a key's time");
            const auto v = number(a->get(1), "a key's value");
            if (!at || !v) continue;
            const hanim::seconds when(to_seconds(*at));
            if (a->size() == 3) {
                const auto e = text(a->get(2), "a key's ease");
                if (!e) continue;
                if (*e == "hold") {
                    p.keys.hold(when, *v);
                    continue;
                }
                if (auto ease = hanim::parse_ease(*e)) p.keys.key(when, *v, *ease);
                else error(a->get(2), std::format("`{}` is not an ease", *e), "linear, out_cubic, cubic-bezier(0.2, 0, 0, 1), steps(4, jump-end), hold");
            } else {
                p.keys.key(when, *v);
            }
        }
        return p;
    }

    Clip clip(const toml::table& t, std::string_view track) {
        only(t, clip_keys, std::format("a clip on `{}`", track));
        Clip c;
        if (auto s = text(t.get("name"), "a clip's `name`")) c.name = *s;
        if (auto s = text(t.get("source"), "a clip's `source`")) c.source = *s;
        if (auto v = time(t.get("at"), "a clip's `at`")) c.at = *v;
        if (auto v = time(t.get("length"), "a clip's `length`")) c.length = *v;
        else if (!t.get("length")) error(&t, std::format("clip `{}` has a `length`", c.name), "length = \"3s\"");
        if (auto v = time(t.get("in"), "a clip's `in`")) c.in = *v;
        if (auto v = number(t.get("speed"), "a clip's `speed`")) c.speed = *v;
        if (auto v = number(t.get("level"), "a clip's `level`")) c.level = static_cast<float>(*v);
        if (const toml::node* fn = t.get("fade")) {
            if (const toml::table* f = fn->as_table()) {
                only(*f, fade_keys, "a fade");
                if (auto v = time(f->get("in"), "a fade's `in`")) c.fade.in = *v;
                if (auto v = time(f->get("out"), "a fade's `out`")) c.fade.out = *v;
                if (auto e = text(f->get("ease"), "a fade's `ease`")) {
                    if (auto ease = hanim::parse_ease(*e)) c.fade.ease = *ease;
                    else error(f->get("ease"), std::format("`{}` is not an ease", *e), "linear, out_cubic, cubic-bezier(…)");
                }
            } else {
                error(fn, "a fade is a table", "fade = { in = \"15f\", out = \"0.25s\" }");
            }
        }
        tables(t.get("property"), "track.clip.property", [&](const toml::table& p) { c.properties.push_back(property(p)); });
        return c;
    }
};

std::string number_text(double v) { return std::format("{}", v); }

}  // namespace

std::expected<Timeline, std::vector<Problem>> read_timeline(std::string_view text, std::string_view file) {
    Reader r{std::string(file), {}, {}};
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(std::vector<Problem>{{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), {}}});
    }
    r.only(root, root_keys, "a timeline file");
    Timeline tl;
    if (const toml::node* tn = root.get("timeline")) {
        if (const toml::table* t = tn->as_table()) {
            r.only(*t, timeline_keys, "[timeline]");
            if (auto s = r.text(t->get("name"), "the timeline's `name`")) tl.name = *s;
            if (const toml::node* rn = t->get("rate")) {
                if (auto n = rn->value_exact<std::int64_t>()) tl.rate = {static_cast<std::int32_t>(*n), 1};
                else if (auto s = rn->value_exact<std::string>(); s && s->find('/') != std::string::npos) {
                    std::int32_t num = 0, den = 0;
                    const auto slash = s->find('/');
                    const auto [p1, e1] = std::from_chars(s->data(), s->data() + slash, num);
                    const auto [p2, e2] = std::from_chars(s->data() + slash + 1, s->data() + s->size(), den);
                    if (e1 == std::errc{} && e2 == std::errc{} && p2 == s->data() + s->size()) tl.rate = {num, den};
                    else r.error(rn, std::format("`{}` is not a rate", *s), "60, or \"30000/1001\"");
                } else {
                    r.error(rn, "a rate is frames a second", "rate = 60, or rate = \"30000/1001\"");
                }
                if (!exact(tl.rate)) r.error(rn, std::format("a frame at {}/{} is not a whole number of flicks", tl.rate.num, tl.rate.den), "24, 25, 30, 48, 50, 60, 120, \"30000/1001\" …");
            }
            if (auto v = r.number(t->get("sample_rate"), "the `sample_rate`")) {
                tl.sample_rate = static_cast<std::int32_t>(*v);
                if (!exact_samples(tl.sample_rate)) r.error(t->get("sample_rate"), std::format("a sample at {} Hz is not a whole number of flicks", tl.sample_rate), "44100, 48000, 96000");
            }
            if (const toml::node* sn = t->get("size")) {
                const toml::array* a = sn->as_array();
                if (a && a->size() == 2 && a->get(0)->value<std::int64_t>() && a->get(1)->value<std::int64_t>()) {
                    tl.width = static_cast<std::int32_t>(*a->get(0)->value<std::int64_t>());
                    tl.height = static_cast<std::int32_t>(*a->get(1)->value<std::int64_t>());
                } else {
                    r.error(sn, "`size` is [width, height]", "size = [1920, 1080]");
                }
            }
        } else {
            r.error(tn, "[timeline] is a table");
        }
    }
    r.rate = tl.rate;
    r.tables(root.get("marker"), "marker", [&](const toml::table& t) {
        r.only(t, marker_keys, "a marker");
        Marker m;
        if (auto v = r.time(t.get("at"), "a marker's `at`")) m.at = *v;
        if (auto s = r.text(t.get("name"), "a marker's `name`")) m.name = *s;
        tl.markers.push_back(std::move(m));
    });
    r.tables(root.get("track"), "track", [&](const toml::table& t) {
        r.only(t, track_keys, "a track");
        Track track;
        if (auto s = r.text(t.get("name"), "a track's `name`")) track.name = *s;
        if (auto s = r.text(t.get("kind"), "a track's `kind`")) {
            if (const auto k = std::ranges::find(kinds, *s); k != kinds.end()) track.kind = static_cast<Kind>(k - kinds.begin());
            else r.error(t.get("kind"), std::format("`{}` is not a kind of track", *s), nearest(*s, kinds));
        }
        if (const toml::node* mn = t.get("muted")) {
            if (auto b = mn->value_exact<bool>()) track.muted = *b;
            else r.error(mn, "`muted` is true or false");
        }
        r.tables(t.get("clip"), "track.clip", [&](const toml::table& c) { track.clips.push_back(r.clip(c, track.name)); });
        tl.tracks.push_back(std::move(track));
    });
    // The rules a timeline keeps (order, overlap, fades), said with where the track is.
    if (r.out.empty())
        for (const std::string& why : check(tl)) r.out.push_back({r.file, 0, 0, why, {}});
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return tl;
}

std::string write_timeline(const Timeline& tl) {
    const Rate rate = tl.rate;
    auto when = [&](Flicks t) { return std::format("\"{}\"", time_text(t, rate)); };
    std::string s = std::format("[timeline]\nname = \"{}\"\n", tl.name);
    s += rate.den == 1 ? std::format("rate = {}\n", rate.num) : std::format("rate = \"{}/{}\"\n", rate.num, rate.den);
    s += std::format("sample_rate = {}\nsize = [{}, {}]\n", tl.sample_rate, tl.width, tl.height);
    for (const Marker& m : tl.markers) s += std::format("\n[[marker]]\nat = {}\nname = \"{}\"\n", when(m.at), m.name);
    for (const Track& track : tl.tracks) {
        s += std::format("\n[[track]]\nname = \"{}\"\nkind = \"{}\"\n", track.name, kinds[static_cast<std::size_t>(track.kind)]);
        if (track.muted) s += "muted = true\n";
        for (const Clip& c : track.clips) {
            s += std::format("\n[[track.clip]]\nname = \"{}\"\n", c.name);
            if (!c.source.empty()) s += std::format("source = \"{}\"\n", c.source);
            s += std::format("at = {}\nlength = {}\n", when(c.at), when(c.length));
            if (c.in != 0) s += std::format("in = {}\n", when(c.in));
            if (c.speed != 1) s += std::format("speed = {}\n", number_text(c.speed));
            if (c.level != 1) s += std::format("level = {}\n", number_text(c.level));
            if (c.fade.in != 0 || c.fade.out != 0) {
                s += std::format("fade = {{ in = {}, out = {}", when(c.fade.in), when(c.fade.out));
                if (!(c.fade.ease == hanim::ease::linear)) s += std::format(", ease = \"{}\"", hanim::to_string(c.fade.ease));
                s += " }\n";
            }
            for (const Property& p : c.properties) {
                s += std::format("\n[[track.clip.property]]\nname = \"{}\"\nkeys = [", p.name);
                bool first = true;
                for (const auto& k : p.keys.keys()) {
                    s += std::format("{}[{}, {}", first ? "" : ", ", when(from_seconds(k.time.count())), number_text(k.value));
                    if (k.hold) s += ", \"hold\"";
                    else if (!(k.ease == hanim::ease::linear)) s += std::format(", \"{}\"", hanim::to_string(k.ease));
                    s += "]";
                    first = false;
                }
                s += "]\n";
            }
        }
    }
    return s;
}

}  // namespace omui::timeline
