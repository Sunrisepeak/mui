// Reading and writing timelines: the JSON a `*.timeline.omui.ts` evaluates
// to (D-139), every problem at its JSON pointer and its line and column.
module xengine.timeline;

import std;
import hanim.core;
import mlibs.json;

namespace xengine::timeline {

namespace {

using mlibs::json::Node;

constexpr std::array<std::string_view, 7> timeline_keys{"name", "rate", "sample_rate", "size", "markers", "tracks", "doc"};
constexpr std::array<std::string_view, 2> marker_keys{"at", "name"};
constexpr std::array<std::string_view, 5> track_keys{"name", "kind", "muted", "clips", "doc"};
constexpr std::array<std::string_view, 10> clip_keys{"name", "source", "at", "length", "in", "speed", "level", "fade", "properties", "doc"};
constexpr std::array<std::string_view, 3> fade_keys{"in", "out", "ease"};
constexpr std::array<std::string_view, 2> property_keys{"name", "keys"};
constexpr std::array<std::string_view, 4> kinds{"video", "audio", "animation", "events"};

struct Reader {
    const mlibs::json::Document& doc;
    Rate                         rate;
    std::vector<Problem>         out;

    void error(const Node& n, std::string message, std::string hint = {}) {
        const auto p = doc.problem(n, std::move(message), std::move(hint));
        out.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at});
    }
    template <std::size_t N>
    void only(const Node& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (const auto& [key, value] : t.members())
            if (std::ranges::find(keys, key) == keys.end()) error(value, std::format("{} has no key `{}`", where, key), mlibs::json::nearest(key, keys));
    }
    std::optional<std::string> text(const Node& n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n.text()) return *s;
        error(n, std::format("{} is text", what));
        return std::nullopt;
    }
    std::optional<double> number(const Node& n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto v = n.number()) return *v;
        error(n, std::format("{} is a number", what));
        return std::nullopt;
    }
    // A time: text ("90f", "1.5s", "250ms", "123fl") or a number of seconds.
    std::optional<Flicks> time(const Node& n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n.text()) {
            if (auto t = parse_time(*s, rate)) return t;
            error(n, std::format("`{}` is not {}", *s, what), "90f (frames), 1.5s, 250ms or 123fl (flicks): a whole number of flicks");
            return std::nullopt;
        }
        if (auto v = n.number()) return from_seconds(*v);
        error(n, std::format("{} is a time", what), "\"90f\", \"1.5s\", or a number of seconds");
        return std::nullopt;
    }
    template <class F>
    void objects(const Node& n, std::string_view list, std::string_view one, F&& each) {
        if (!n) return;
        if (!n.is_array()) return error(n, std::format("`{}` is a list of {}s", list, one));
        for (const Node& x : n.items())
            if (x.is_object()) each(x);
            else error(x, std::format("a {} is an object", one));
    }

    Property property(const Node& t) {
        only(t, property_keys, "a property");
        Property p;
        if (auto s = text(t["name"], "a property's `name`")) p.name = *s;
        else if (!t["name"]) error(t, "a property has a `name`", "name: \"opacity\"");
        const Node keys = t["keys"];
        if (!keys.is_array()) {
            error(keys ? keys : t, "a property's `keys` are [time, value] pairs", "keys: [[\"0f\", 0, \"out_cubic\"], [\"15f\", 1]]");
            return p;
        }
        for (const Node& k : keys.items()) {
            if (!k.is_array() || k.size() < 2 || k.size() > 3) {
                error(k, "a key is [time, value] or [time, value, ease]", "[\"15f\", 1, \"out_cubic\"]; \"hold\" in place of the ease holds it");
                continue;
            }
            const auto at = time(k[0], "a key's time");
            const auto v = number(k[1], "a key's value");
            if (!at || !v) continue;
            const hanim::seconds when(to_seconds(*at));
            if (k.size() == 3) {
                const auto e = text(k[2], "a key's ease");
                if (!e) continue;
                if (*e == "hold") {
                    p.keys.hold(when, *v);
                    continue;
                }
                if (auto ease = hanim::parse_ease(*e)) p.keys.key(when, *v, *ease);
                else error(k[2], std::format("`{}` is not an ease", *e), "linear, out_cubic, cubic-bezier(0.2, 0, 0, 1), steps(4, jump-end), hold");
            } else {
                p.keys.key(when, *v);
            }
        }
        return p;
    }

    Clip clip(const Node& t, std::string_view track) {
        only(t, clip_keys, std::format("a clip on `{}`", track));
        Clip c;
        if (auto s = text(t["name"], "a clip's `name`")) c.name = *s;
        if (auto s = text(t["source"], "a clip's `source`")) c.source = *s;
        if (auto v = time(t["at"], "a clip's `at`")) c.at = *v;
        if (auto v = time(t["length"], "a clip's `length`")) c.length = *v;
        else if (!t["length"]) error(t, std::format("clip `{}` has a `length`", c.name), "length: \"3s\"");
        if (auto v = time(t["in"], "a clip's `in`")) c.in = *v;
        if (auto v = number(t["speed"], "a clip's `speed`")) c.speed = *v;
        if (auto v = number(t["level"], "a clip's `level`")) c.level = static_cast<float>(*v);
        if (const Node f = t["fade"]) {
            if (f.is_object()) {
                only(f, fade_keys, "a fade");
                if (auto v = time(f["in"], "a fade's `in`")) c.fade.in = *v;
                if (auto v = time(f["out"], "a fade's `out`")) c.fade.out = *v;
                if (auto e = text(f["ease"], "a fade's `ease`")) {
                    if (auto ease = hanim::parse_ease(*e)) c.fade.ease = *ease;
                    else error(f["ease"], std::format("`{}` is not an ease", *e), "linear, out_cubic, cubic-bezier(…)");
                }
            } else {
                error(f, "a fade is an object", "fade: { in: \"15f\", out: \"0.25s\" }");
            }
        }
        objects(t["properties"], "properties", "property", [&](const Node& p) { c.properties.push_back(property(p)); });
        return c;
    }
};

std::string number_text(double v) { return std::format("{}", v); }

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

std::expected<Timeline, std::vector<Problem>> read_timeline(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) {
        const auto& p = doc.error();
        return std::unexpected(std::vector<Problem>{{p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at}});
    }
    Reader r{*doc, {}, {}};
    constexpr std::string_view shape = "{ \"timeline\": { name, rate, size, markers: […], tracks: […] } }, as defineTimeline({ … }) evaluates";
    for (const auto& [key, value] : doc->root().members())
        if (key != "timeline") r.error(value, std::format("a timeline file has no key `{}`", key), std::string(shape));
    const Node t = doc->root()["timeline"];
    Timeline tl;
    if (!t.is_object()) {
        r.error(t, "a timeline file is a timeline: an object", std::string(shape));
        return std::unexpected(std::move(r.out));
    }
    r.only(t, timeline_keys, "a timeline");
    if (auto s = r.text(t["name"], "the timeline's `name`")) tl.name = *s;
    if (const Node rn = t["rate"]) {
        if (auto n = rn.integer()) tl.rate = {static_cast<std::int32_t>(*n), 1};
        else if (auto s = rn.text(); s && s->find('/') != std::string::npos) {
            std::int32_t num = 0, den = 0;
            const auto slash = s->find('/');
            const auto [p1, e1] = std::from_chars(s->data(), s->data() + slash, num);
            const auto [p2, e2] = std::from_chars(s->data() + slash + 1, s->data() + s->size(), den);
            if (e1 == std::errc{} && e2 == std::errc{} && p2 == s->data() + s->size()) tl.rate = {num, den};
            else r.error(rn, std::format("`{}` is not a rate", *s), "60, or \"30000/1001\"");
        } else {
            r.error(rn, "a rate is frames a second", "rate: 60, or rate: \"30000/1001\"");
        }
        if (!tl.rate.exact()) r.error(rn, std::format("a frame at {}/{} is not a whole number of flicks", tl.rate.num, tl.rate.den), "24, 25, 30, 48, 50, 60, 120, \"30000/1001\" …");
    }
    if (auto v = r.number(t["sample_rate"], "the `sample_rate`")) {
        tl.sample_rate = static_cast<std::int32_t>(*v);
        if (!exact_samples(tl.sample_rate)) r.error(t["sample_rate"], std::format("a sample at {} Hz is not a whole number of flicks", tl.sample_rate), "44100, 48000, 96000");
    }
    if (const Node sn = t["size"]) {
        if (sn.is_array() && sn.size() == 2 && sn[0].integer() && sn[1].integer()) {
            tl.width = static_cast<std::int32_t>(*sn[0].integer());
            tl.height = static_cast<std::int32_t>(*sn[1].integer());
        } else {
            r.error(sn, "`size` is [width, height]", "size: [1920, 1080]");
        }
    }
    r.rate = tl.rate;
    r.objects(t["markers"], "markers", "marker", [&](const Node& m) {
        r.only(m, marker_keys, "a marker");
        Marker marker;
        if (auto v = r.time(m["at"], "a marker's `at`")) marker.at = *v;
        if (auto s = r.text(m["name"], "a marker's `name`")) marker.name = *s;
        tl.markers.push_back(std::move(marker));
    });
    r.objects(t["tracks"], "tracks", "track", [&](const Node& n) {
        r.only(n, track_keys, "a track");
        Track track;
        if (auto s = r.text(n["name"], "a track's `name`")) track.name = *s;
        if (auto s = r.text(n["kind"], "a track's `kind`")) {
            if (const auto k = std::ranges::find(kinds, *s); k != kinds.end()) track.kind = static_cast<Kind>(k - kinds.begin());
            else r.error(n["kind"], std::format("`{}` is not a kind of track", *s), mlibs::json::nearest(*s, kinds));
        }
        if (const Node mn = n["muted"]) {
            if (auto b = mn.boolean()) track.muted = *b;
            else r.error(mn, "`muted` is true or false");
        }
        r.objects(n["clips"], "clips", "clip", [&](const Node& c) { track.clips.push_back(r.clip(c, track.name)); });
        tl.tracks.push_back(std::move(track));
    });
    // The rules a timeline keeps (order, overlap, fades), said with where the track is.
    if (r.out.empty())
        for (const std::string& why : check(tl)) r.out.push_back({std::string(file), 0, 0, why, {}, {}});
    if (!r.out.empty()) return std::unexpected(std::move(r.out));
    return tl;
}

std::string write_timeline(const Timeline& tl) {
    const Rate rate = tl.rate;
    auto when = [&](Flicks t) { return json_text(time_text(t, rate)); };
    std::string s = std::format("{{\"timeline\": {{\n  \"name\": {}, \"rate\": {}, \"sample_rate\": {}, \"size\": [{}, {}]", json_text(tl.name),
                                rate.den == 1 ? std::format("{}", rate.num) : std::format("\"{}/{}\"", rate.num, rate.den), tl.sample_rate, tl.width, tl.height);
    if (!tl.markers.empty()) {
        s += ",\n  \"markers\": [";
        for (std::size_t i = 0; i < tl.markers.size(); ++i)
            s += std::format("{}\n    {{\"at\": {}, \"name\": {}}}", i ? "," : "", when(tl.markers[i].at), json_text(tl.markers[i].name));
        s += "\n  ]";
    }
    s += ",\n  \"tracks\": [";
    for (std::size_t t = 0; t < tl.tracks.size(); ++t) {
        const Track& track = tl.tracks[t];
        s += std::format("{}\n    {{\"name\": {}, \"kind\": \"{}\"", t ? "," : "", json_text(track.name), kinds[static_cast<std::size_t>(track.kind)]);
        if (track.muted) s += ", \"muted\": true";
        s += ", \"clips\": [";
        for (std::size_t k = 0; k < track.clips.size(); ++k) {
            const Clip& c = track.clips[k];
            s += std::format("{}\n      {{\"name\": {}", k ? "," : "", json_text(c.name));
            if (!c.source.empty()) s += std::format(", \"source\": {}", json_text(c.source));
            s += std::format(", \"at\": {}, \"length\": {}", when(c.at), when(c.length));
            if (c.in != 0) s += std::format(", \"in\": {}", when(c.in));
            if (c.speed != 1) s += std::format(", \"speed\": {}", number_text(c.speed));
            if (c.level != 1) s += std::format(", \"level\": {}", number_text(c.level));
            if (c.fade.in != 0 || c.fade.out != 0) {
                s += std::format(", \"fade\": {{\"in\": {}, \"out\": {}", when(c.fade.in), when(c.fade.out));
                if (!(c.fade.ease == hanim::ease::linear)) s += std::format(", \"ease\": {}", json_text(hanim::to_string(c.fade.ease)));
                s += "}";
            }
            if (!c.properties.empty()) {
                s += ", \"properties\": [";
                for (std::size_t p = 0; p < c.properties.size(); ++p) {
                    const Property& property = c.properties[p];
                    s += std::format("{}{{\"name\": {}, \"keys\": [", p ? ", " : "", json_text(property.name));
                    bool first = true;
                    for (const auto& key : property.keys.keys()) {
                        s += std::format("{}[{}, {}", first ? "" : ", ", when(from_seconds(key.time.count())), number_text(key.value));
                        if (key.hold) s += ", \"hold\"";
                        else if (!(key.ease == hanim::ease::linear)) s += std::format(", {}", json_text(hanim::to_string(key.ease)));
                        s += "]";
                        first = false;
                    }
                    s += "]}";
                }
                s += "]";
            }
            s += "}";
        }
        s += track.clips.empty() ? "]}" : "\n    ]}";
    }
    s += "\n  ]\n}}\n";
    return s;
}

}  // namespace xengine::timeline
