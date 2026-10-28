// Reading and writing *.look.gongfu.toml. toml++ reports a malformed file by
// throwing; this unit, the library's adapter, turns that into a diagnostic.
//
//     gongfu = "0.1"
//     name = "kata"
//     outline = { color = "#101014", width = 3 }
//
//     [[palette]]                 # the first is the left side's, the second the right's
//     name = "ink"
//     body = "#1f2430"            # a slot: parts name it
//     limb = "#3a4152"
//
//     [[bone]]
//     name = "hip"                # no parent: the root, above the feet
//     joint = [0, 92]
//     length = 10
//     rest = 90                   # degrees, counter-clockwise
//     limb = true                 # (on an upper arm, a thigh) its parts can reach out of the hurtbox
//     prop = true                 # (on a brush, a sword) held, not the body: never a hurtbox
//     [[bone]]
//     name = "torso"
//     parent = "hip"
//     joint = [10, 0]             # along the parent, then across
//     length = 56
//
//     [[part]]
//     bone = "torso"
//     shape = "capsule"           # capsule, ellipse or polygon
//     size = [18, 15]             # radii at the joint and the tip (an ellipse's half-axes)
//     color = "body"              # a palette slot, or "#rrggbb"
//     depth = 1
//
//     [clip.5LP]
//     frames = 12
//     ease = "out_cubic"          # each key's way to the next, unless the key says
//     angle.upper_front = [[0, 0], [3, 43], [5, 43], [11, 0]]   # [frame, degrees(, ease)]
//     rise = [[0, 0], [6, -4, "linear"], [12, 0]]               # [frame, pixels(, ease)]
//
//     [impact.heavy]
//     shake = [8, 12]             # pixels, frames
//     zoom = [0.05, 16]           # closer by, frames
//     flash = ["white", 4]        # none, white or invert; frames
//     spark = { style = "rays", size = 44, count = 10, frames = 18, color = "#ffd166", counter = "#ff4d5e" }
//     sound = ["heavy.attack", "heavy.body", "heavy.tail"]
//     rumble = [0.7, 12]          # 0–1, frames
module gongfu.present2d;

import std;
import gongfu;
import gongfu.data;
import hanim;
import openmui.geom;
import tomlplusplus;

namespace gongfu::present2d {

namespace {

using data::Diagnostic;

constexpr std::string_view format_version = "0.1";
constexpr std::array<std::string_view, 20> look_keys{"gongfu", "name", "outline", "accent", "trail", "glow", "footfall", "pane", "clone", "rewind", "palette", "bone", "part", "clip", "impact",
                                                     "glyph", "cinematic", "projectile", "doc", "light"};
constexpr std::array<std::string_view, 6> light_keys{"from", "shade", "darken", "shadow", "rim", "rim_color"};
constexpr std::array<std::string_view, 3> follow_keys{"lag", "amount", "max"};
constexpr std::array<std::string_view, 3> trail_keys{"color", "frames", "alpha"};
constexpr std::array<std::string_view, 2> glow_keys{"color", "width"};
constexpr std::array<std::string_view, 3> footfall_keys{"every", "dip", "frames"};
constexpr std::array<std::string_view, 4> pane_keys{"glass", "edge", "shine", "reflect"};
constexpr std::array<std::string_view, 1> clone_keys{"tint"};
constexpr std::array<std::string_view, 3> rewind_keys{"color", "ghosts", "frames"};
constexpr std::array<std::string_view, 3> stop_keys{"from", "to", "tint"};
constexpr std::array<std::string_view, 5> clock_keys{"from", "to", "at", "size", "color"};
constexpr std::array<std::string_view, 5> projectile_look_keys{"glyph", "size", "color", "halo", "spin"};
constexpr std::array<std::string_view, 2> stroke_keys{"points", "width"};
constexpr std::array<std::string_view, 18> cinematic_keys{"attacker", "defender", "backdrop", "camera", "glyph", "seal", "flash", "scanlines",
                                                          "attacker_path", "defender_path", "quake", "ring", "prism", "cracks", "fall", "stop", "clock", "doc"};
constexpr std::array<std::string_view, 5> prism_keys{"images", "spread", "from", "to", "tint"};
constexpr std::array<std::string_view, 1> cracks_keys{"color"};
constexpr std::array<std::string_view, 3> fall_keys{"frame", "count", "color"};
constexpr std::array<std::string_view, 4> backdrop_keys{"color", "in", "out", "clouds"};
constexpr std::array<std::string_view, 3> clouds_keys{"color", "count", "between"};
constexpr std::array<std::string_view, 6> written_keys{"name", "at", "size", "from", "ends", "color"};
constexpr std::array<std::string_view, 4> seal_keys{"frame", "at", "size", "color"};
constexpr std::array<std::string_view, 2> outline_keys{"color", "width"};
constexpr std::array<std::string_view, 8> bone_keys{"name", "parent", "joint", "length", "rest", "limb", "prop", "follow"};
constexpr std::array<std::string_view, 10> part_keys{"bone", "shape", "size", "points", "color", "depth", "doc", "name", "variants", "shade"};
constexpr std::array<std::string_view, 7> clip_keys{"frames", "loop", "ease", "angle", "rise", "doc", "swap"};
constexpr std::array<std::string_view, 8> impact_keys{"shake", "zoom", "flash", "spark", "sound", "rumble", "doc", "name"};
constexpr std::array<std::string_view, 6> spark_keys{"style", "size", "count", "frames", "color", "counter"};
constexpr std::array<std::string_view, 4> shape_names{"capsule", "ellipse", "polygon", "curve"};
constexpr std::array<std::string_view, 6> spark_names{"rays", "ring", "splash", "shards", "bolt", "cracks"};
constexpr std::array<std::string_view, 3> flash_names{"none", "white", "invert"};

struct Reader {
    std::string              file;
    std::vector<Diagnostic>& out;

    void error(const toml::node* n, std::string message, std::string hint = {}) {
        int line = 0, column = 0;
        if (n) line = static_cast<int>(n->source().begin.line), column = static_cast<int>(n->source().begin.column);
        out.push_back({file, line, column, std::move(message), std::move(hint)});
    }
    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end())
                out.push_back({file, static_cast<int>(k.source().begin.line), static_cast<int>(k.source().begin.column),
                               std::format("{} has no key `{}`", what, k.str()), data::nearest_hint(k.str(), keys)});
    }
    std::optional<std::string> text(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n->value<std::string>()) return *s;
        error(n, std::format("{} is a string", what));
        return std::nullopt;
    }
    std::optional<double> number(const toml::node* n, std::string_view what, double lo = -1e6, double hi = 1e6) {
        if (!n) return std::nullopt;
        double v = 0;
        if (auto i = n->as_integer()) v = static_cast<double>(i->get());
        else if (auto f = n->as_floating_point()) v = f->get();
        else {
            error(n, std::format("{} is a number", what));
            return std::nullopt;
        }
        if (!std::isfinite(v) || v < lo || v > hi) {
            error(n, std::format("{} is a number of {}–{}", what, lo, hi));
            return std::nullopt;
        }
        return v;
    }
    std::optional<std::int32_t> integer(const toml::node* n, std::string_view what, std::int64_t lo, std::int64_t hi) {
        if (!n) return std::nullopt;
        const auto v = n->as_integer();
        if (!v || v->get() < lo || v->get() > hi) {
            error(n, std::format("{} is a whole number of {}–{}", what, lo, hi));
            return std::nullopt;
        }
        return static_cast<std::int32_t>(v->get());
    }
    std::optional<bool> boolean(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto b = n->value<bool>()) return *b;
        error(n, std::format("{} is true or false", what));
        return std::nullopt;
    }
    std::optional<Color> colour(const toml::node* n, std::string_view what) {
        auto s = text(n, what);
        if (!s) return std::nullopt;
        if (auto c = Color::parse(*s); c && s->starts_with('#')) return c;
        error(n, std::format("{}: `{}` is not a colour", what, *s), "\"#rrggbb\" or \"#rrggbbaa\"");
        return std::nullopt;
    }
    // [a, b] of numbers.
    std::optional<std::array<double, 2>> pair(const toml::node* n, std::string_view what, std::string_view example) {
        if (!n) return std::nullopt;
        const toml::array* a = n->as_array();
        if (!a || a->size() != 2) {
            error(n, std::format("{} is two numbers", what), std::string(example));
            return std::nullopt;
        }
        auto x = number(a->get(0), what), y = number(a->get(1), what);
        if (!x || !y) return std::nullopt;
        return std::array<double, 2>{*x, *y};
    }
    template <std::size_t N>
    std::optional<std::size_t> choice(const toml::node* n, std::string_view what, const std::array<std::string_view, N>& names) {
        auto s = text(n, what);
        if (!s) return std::nullopt;
        if (auto it = std::ranges::find(names, *s); it != names.end()) return static_cast<std::size_t>(it - names.begin());
        error(n, std::format("{}: `{}` is not one", what, *s), data::nearest_hint(*s, names));
        return std::nullopt;
    }
    const toml::table* table_of(const toml::node* n, std::string_view what) {
        if (!n) return nullptr;
        if (const toml::table* t = n->as_table()) return t;
        error(n, std::format("{} is a table", what));
        return nullptr;
    }
    template <class F>
    void tables(const toml::node* n, std::string_view what, std::string_view example, F&& f) {
        if (!n) return;
        const toml::array* a = n->as_array();
        if (!a || !a->is_array_of_tables()) {
            error(n, std::format("`{}` is a list of tables", what), std::string(example));
            return;
        }
        for (const toml::node& e : *a) f(*e.as_table());
    }
    // [[frame, value(, ease)], …] with the clip's ease by default.
    std::optional<hanim::Track<double>> keys(const toml::node* n, std::string_view what, hanim::Ease ease, std::int32_t frames) {
        if (!n) return std::nullopt;
        const toml::array* a = n->as_array();
        if (!a) {
            error(n, std::format("{} is a list of keys", what), "[[0, 0], [6, 40], [12, 0]]: frame, value, and an ease if not the clip's");
            return std::nullopt;
        }
        hanim::Track<double> t;
        std::int32_t last = -1;
        for (const toml::node& k : *a) {
            const toml::array* key = k.as_array();
            if (!key || key->size() < 2 || key->size() > 3) {
                error(&k, std::format("a key of {} is [frame, value] or [frame, value, \"ease\"]", what));
                continue;
            }
            auto frame = integer(key->get(0), std::format("a key's frame of {}", what), 0, std::max(frames, 0));
            auto value = number(key->get(1), std::format("a key's value of {}", what));
            hanim::Ease e = ease;
            if (key->size() == 3) {
                if (auto name = text(key->get(2), "a key's ease")) {
                    if (auto parsed = hanim::parse_ease(*name)) e = *parsed;
                    else error(key->get(2), std::format("`{}` is not an ease", *name), "linear, out_cubic, in_out_sine, cubic-bezier(…), steps(…)");
                }
            }
            if (!frame || !value) continue;
            if (*frame <= last) error(key->get(0), std::format("{}: frame {} comes after frame {}", what, *frame, last), "keys go in order of their frames");
            last = *frame;
            t.key(hanim::seconds(*frame / 60.0), *value, e);
        }
        return t;
    }
};

std::optional<toml::table> parse(std::string_view source, std::string_view file, std::vector<Diagnostic>& out) {
    try {
        return toml::parse(source, std::string(file));
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        out.push_back({std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), ""});
        return std::nullopt;
    }
}

// ── writing ──

std::string colour_text(Color c) {
    auto q = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    if (q(c.a) == 255) return std::format("\"#{:02x}{:02x}{:02x}\"", q(c.r), q(c.g), q(c.b));
    return std::format("\"#{:02x}{:02x}{:02x}{:02x}\"", q(c.r), q(c.g), q(c.b), q(c.a));
}

// A key that TOML takes bare, else quoted ("j.HK").
std::string key_text(std::string_view k) {
    const bool bare = !k.empty() && std::ranges::all_of(k, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
    return bare ? std::string(k) : std::format("\"{}\"", k);
}

std::string keys_text(const hanim::Track<double>& t, const hanim::Ease& clip_ease) {
    std::string out = "[";
    bool first = true;
    for (const auto& k : t.keys()) {
        const auto frame = static_cast<long long>(std::llround(k.time.count() * 60.0));
        out += std::format("{}[{}, {}", first ? "" : ", ", frame, k.value);
        if (hanim::to_string(k.ease) != hanim::to_string(clip_ease)) out += std::format(", \"{}\"", hanim::to_string(k.ease));
        out += "]";
        first = false;
    }
    return out + "]";
}

// The ease most of a clip's keys use: what the clip says once.
hanim::Ease common_ease(const Clip& c) {
    std::map<std::string, std::pair<std::size_t, hanim::Ease>> seen;
    auto count = [&](const hanim::Track<double>& t) {
        for (std::size_t i = 0; i + 1 < t.keys().size(); ++i) {  // the last key's ease leads nowhere
            auto& [n, e] = seen[hanim::to_string(t.keys()[i].ease)];
            ++n;
            e = t.keys()[i].ease;
        }
    };
    for (const auto& t : c.angles) count(t);
    count(c.rise);
    hanim::Ease best = hanim::ease::linear;
    std::size_t most = 0;
    for (const auto& [name, v] : seen)
        if (v.first > most) most = v.first, best = v.second;
    return best;
}

}  // namespace

std::expected<Look, std::vector<Diagnostic>> read_look(std::string_view source, std::string_view file) {
    std::vector<Diagnostic> out;
    auto root = parse(source, file, out);
    if (!root) return std::unexpected(std::move(out));
    Reader r{std::string(file), out};
    r.only(*root, look_keys, "a look file");
    if (auto v = r.text(root->get("gongfu"), "`gongfu`")) {
        if (*v != format_version) r.error(root->get("gongfu"), std::format("this reads gongfu {} files; the file says {}", format_version, *v));
    } else if (!root->get("gongfu")) {
        r.error(&*root, "the file does not say its format", std::format("gongfu = \"{}\" first", format_version));
    }
    Look look;
    if (auto n = r.text(root->get("name"), "`name`")) look.name = *n;
    if (const toml::node* o = root->get("outline")) {
        if (const toml::table* t = o->as_table()) {
            r.only(*t, outline_keys, "`outline`");
            if (auto c = r.colour(t->get("color"), "`outline.color`")) look.outline = *c;
            if (auto w = r.number(t->get("width"), "`outline.width`", 0, 64)) look.outline_width = static_cast<float>(*w);
        } else {
            r.error(o, "`outline` is a table", "outline = { color = \"#101014\", width = 3 }");
        }
    }

    // Palettes: the first names the slots; each other has the same.
    r.tables(root->get("palette"), "palette", "[[palette]] name = \"ink\" body = \"#1f2430\"", [&](const toml::table& t) {
        std::vector<Color> colours(look.slots.size());
        std::vector<bool>  given(look.slots.size());
        const bool first = look.palettes.empty();
        std::string name;
        for (const auto& [k, v] : t) {
            if (k.str() == "name") {
                if (auto n = r.text(&v, "a palette's `name`")) name = *n;
                continue;
            }
            auto c = r.colour(&v, std::format("slot `{}`", k.str()));
            if (first) {
                look.slots.emplace_back(k.str());
                colours.push_back(c.value_or(Color{}));
                given.push_back(true);
                continue;
            }
            auto it = std::ranges::find(look.slots, k.str());
            if (it == look.slots.end()) {
                std::vector<std::string_view> names(look.slots.begin(), look.slots.end());
                r.out.push_back({r.file, static_cast<int>(k.source().begin.line), static_cast<int>(k.source().begin.column),
                                 std::format("the first palette has no slot `{}`", k.str()), data::nearest_hint(k.str(), names)});
                continue;
            }
            const auto i = static_cast<std::size_t>(it - look.slots.begin());
            colours[i] = c.value_or(Color{});
            given[i] = true;
        }
        for (std::size_t i = 0; i < given.size(); ++i)
            if (!given[i]) r.error(&t, std::format("palette `{}` has no colour for slot `{}`", name, look.slots[i]), "every palette colours every slot");
        look.palettes.push_back(std::move(colours));
        look.palette_names.push_back(std::move(name));
    });

    // Bones: a parent comes before its children.
    r.tables(root->get("bone"), "bone", "[[bone]] name = \"hip\" joint = [0, 92] length = 10 rest = 90", [&](const toml::table& t) {
        r.only(t, bone_keys, "a bone");
        Bone b;
        if (auto n = r.text(t.get("name"), "a bone's `name`")) b.name = *n;
        else r.error(&t, "a bone has no `name`");
        if (std::ranges::any_of(look.bones, [&](const Bone& o) { return o.name == b.name; }))
            r.error(t.get("name"), std::format("two bones are called `{}`", b.name));
        if (auto p = r.text(t.get("parent"), "a bone's `parent`")) {
            auto it = std::ranges::find(look.bones, *p, &Bone::name);
            if (it == look.bones.end()) {
                std::vector<std::string_view> names;
                for (const Bone& o : look.bones) names.push_back(o.name);
                r.error(t.get("parent"), std::format("no bone before `{}` is called `{}`", b.name, *p),
                        names.empty() ? "a parent comes before its children" : data::nearest_hint(*p, names));
            } else {
                b.parent = static_cast<std::int32_t>(it - look.bones.begin());
            }
        }
        if (auto j = r.pair(t.get("joint"), "a bone's `joint`", "joint = [10, 0]: along the parent, then across")) b.joint = {static_cast<float>((*j)[0]), static_cast<float>((*j)[1])};
        if (auto l = r.number(t.get("length"), "a bone's `length`", 0, 10000)) b.length = static_cast<float>(*l);
        if (auto a = r.number(t.get("rest"), "a bone's `rest`", -720, 720)) b.rest = static_cast<float>(*a);
        if (auto l = r.boolean(t.get("limb"), "a bone's `limb`")) b.limb = *l;
        if (auto l = r.boolean(t.get("prop"), "a bone's `prop`")) b.prop = *l;
        if (const toml::table* f = r.table_of(t.get("follow"), "a bone's `follow`")) {
            r.only(*f, follow_keys, "a bone's `follow`");
            if (auto v = r.integer(f->get("lag"), "a follow's `lag`", 1, 30)) b.follow.lag = *v;
            if (auto v = r.number(f->get("amount"), "a follow's `amount`", -2, 2)) b.follow.amount = static_cast<float>(*v);
            if (auto v = r.number(f->get("max"), "a follow's `max`", 0, 180)) b.follow.max = static_cast<float>(*v);
            if (b.follow.lag <= 0) r.error(f, "a follow has a `lag` (frames)", "follow = { lag = 4, amount = 0.6 }");
        }
        look.bones.push_back(std::move(b));
    });
    auto bone_index = [&](const toml::node* n, std::string_view what) -> std::optional<std::int32_t> {
        auto name = r.text(n, what);
        if (!name) return std::nullopt;
        auto it = std::ranges::find(look.bones, *name, &Bone::name);
        if (it != look.bones.end()) return static_cast<std::int32_t>(it - look.bones.begin());
        std::vector<std::string_view> names;
        for (const Bone& o : look.bones) names.push_back(o.name);
        r.error(n, std::format("no bone is called `{}`", *name), data::nearest_hint(*name, names));
        return std::nullopt;
    };

    r.tables(root->get("part"), "part", "[[part]] bone = \"torso\" shape = \"capsule\" size = [18, 15] color = \"body\"", [&](const toml::table& t) {
        r.only(t, part_keys, "a part");
        Part p;
        if (auto n = r.text(t.get("name"), "a part's `name`")) {
            p.name = *n;
            if (std::ranges::any_of(look.parts, [&](const Part& o) { return o.name == p.name; }))
                r.error(t.get("name"), std::format("two parts are called `{}`", p.name));
        }
        if (auto b = bone_index(t.get("bone"), "a part's `bone`")) p.bone = *b;
        else if (!t.get("bone")) r.error(&t, "a part has no `bone`");
        if (auto s = r.choice(t.get("shape"), "a part's `shape`", shape_names)) p.shape = static_cast<Shape>(*s);
        if (auto sz = r.pair(t.get("size"), "a part's `size`", "size = [18, 15]: radii at the joint and the tip")) {
            p.start = static_cast<float>((*sz)[0]);
            p.end = static_cast<float>((*sz)[1]);
        }
        if (const toml::node* pts = t.get("points")) {
            if (const toml::array* a = pts->as_array()) {
                for (const toml::node& q : *a)
                    if (auto xy = r.pair(&q, "a point of `points`", "[[0, -6], [30, -4], [30, 4]]")) p.points.push_back({static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])});
            } else {
                r.error(pts, "`points` is a list of [x, y]");
            }
        }
        if ((p.shape == Shape::polygon || p.shape == Shape::curve) && p.points.size() < 3)
            r.error(&t, std::format("a {} has three points or more", p.shape == Shape::curve ? "curve" : "polygon"), "points = [[0, -6], [30, -4], [30, 4]]");
        if (const toml::node* vs = t.get("variants")) {
            const toml::array* a = vs->as_array();
            if (!a || (p.shape != Shape::polygon && p.shape != Shape::curve)) {
                r.error(vs, "`variants` are other drawings of a polygon or a curve: a list of point lists", "variants = [[[0, -6], [34, -2], [30, 6]]]");
            } else {
                for (const toml::node& v : *a) {
                    std::vector<Point> pts;
                    if (const toml::array* va = v.as_array())
                        for (const toml::node& q : *va)
                            if (auto xy = r.pair(&q, "a point of a variant", "[[0, -6], [30, -4], [30, 4]]")) pts.push_back({static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])});
                    if (pts.size() < 3) r.error(&v, "a variant has three points or more");
                    p.variants.push_back(std::move(pts));
                }
            }
        }
        if (auto sh = r.boolean(t.get("shade"), "a part's `shade`")) p.shade = *sh;
        if (auto c = r.text(t.get("color"), "a part's `color`")) {
            if (c->starts_with('#')) {
                if (auto col = Color::parse(*c)) p.color = *col;
                else r.error(t.get("color"), std::format("`{}` is not a colour", *c), "\"#rrggbb\", or a palette slot");
            } else if (auto it = std::ranges::find(look.slots, *c); it != look.slots.end()) {
                p.slot = static_cast<std::int32_t>(it - look.slots.begin());
            } else {
                std::vector<std::string_view> names(look.slots.begin(), look.slots.end());
                r.error(t.get("color"), std::format("no palette slot is called `{}`", *c), names.empty() ? "\"#rrggbb\", or a [[palette]]'s slot" : data::nearest_hint(*c, names));
            }
        }
        if (auto d = r.integer(t.get("depth"), "a part's `depth`", -1000, 1000)) p.depth = *d;
        look.parts.push_back(std::move(p));
    });

    if (const toml::node* clips = root->get("clip")) {
        if (const toml::table* ct = clips->as_table()) {
            for (const auto& [name, node] : *ct) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("clip `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[clip.idle] frames = 60 loop = true");
                    continue;
                }
                r.only(*t, clip_keys, what);
                Clip c;
                if (auto f = r.integer(t->get("frames"), std::format("{}'s `frames`", what), 1, 6000)) c.frames = *f;
                else if (!t->get("frames")) r.error(t, std::format("{} has no `frames`", what));
                if (auto l = r.boolean(t->get("loop"), std::format("{}'s `loop`", what))) c.loop = *l;
                hanim::Ease ease = hanim::ease::linear;
                if (auto e = r.text(t->get("ease"), std::format("{}'s `ease`", what))) {
                    if (auto parsed = hanim::parse_ease(*e)) ease = *parsed;
                    else r.error(t->get("ease"), std::format("`{}` is not an ease", *e), "linear, out_cubic, in_out_sine, cubic-bezier(…), steps(…)");
                }
                if (auto rise = r.keys(t->get("rise"), std::format("{}'s `rise`", what), ease, c.frames)) c.rise = std::move(*rise);
                if (const toml::node* sw = t->get("swap")) {
                    if (const toml::table* st = sw->as_table()) {
                        c.swaps.resize(look.parts.size());
                        for (const auto& [part, keys] : *st) {
                            auto it = std::ranges::find(look.parts, part.str(), &Part::name);
                            if (it == look.parts.end()) {
                                std::vector<std::string_view> names;
                                for (const Part& o : look.parts)
                                    if (!o.name.empty()) names.push_back(o.name);
                                r.out.push_back({r.file, static_cast<int>(part.source().begin.line), static_cast<int>(part.source().begin.column),
                                                 std::format("{} swaps `{}`, which is no part's name", what, part.str()), data::nearest_hint(part.str(), names)});
                                continue;
                            }
                            if (auto k = r.keys(&keys, std::format("{}'s `swap.{}`", what, part.str()), hanim::Ease::stepped(1, hanim::StepPosition::jump_end), c.frames)) {
                                for (const auto& key : k->keys())
                                    if (key.value < 0 || key.value > static_cast<double>(it->variants.size()) || key.value != std::floor(key.value))
                                        r.error(&keys, std::format("{}'s `swap.{}` chooses {}: `{}` has {} variant(s) (0: its points)", what, part.str(), key.value, part.str(), it->variants.size()));
                                c.swaps[static_cast<std::size_t>(it - look.parts.begin())] = std::move(*k);
                            }
                        }
                    } else {
                        r.error(sw, std::format("{}'s `swap` is a table of parts", what), "swap.fist = [[0, 0], [4, 1], [10, 0]]");
                    }
                }
                if (const toml::node* an = t->get("angle")) {
                    if (const toml::table* at = an->as_table()) {
                        c.angles.resize(look.bones.size());
                        for (const auto& [bone, keys] : *at) {
                            auto it = std::ranges::find(look.bones, bone.str(), &Bone::name);
                            if (it == look.bones.end()) {
                                std::vector<std::string_view> names;
                                for (const Bone& o : look.bones) names.push_back(o.name);
                                r.out.push_back({r.file, static_cast<int>(bone.source().begin.line), static_cast<int>(bone.source().begin.column),
                                                 std::format("{} turns `{}`, which is no bone", what, bone.str()), data::nearest_hint(bone.str(), names)});
                                continue;
                            }
                            if (auto k = r.keys(&keys, std::format("{}'s `angle.{}`", what, bone.str()), ease, c.frames))
                                c.angles[static_cast<std::size_t>(it - look.bones.begin())] = std::move(*k);
                        }
                    } else {
                        r.error(an, std::format("{}'s `angle` is a table of bones", what), "angle.torso = [[0, 0], [6, 10]]");
                    }
                }
                look.clips.insert_or_assign(std::string(name.str()), std::move(c));
            }
        } else {
            r.error(clips, "`clip` is a table of clips", "[clip.idle] frames = 60");
        }
    }

    if (const toml::node* impacts = root->get("impact")) {
        if (const toml::table* it = impacts->as_table()) {
            for (const auto& [name, node] : *it) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("impact `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[impact.light] shake = [2, 6]");
                    continue;
                }
                r.only(*t, impact_keys, what);
                Impact im{.name = std::string(name.str())};
                auto amount_frames = [&](std::string_view key, double hi, float& amount, std::int32_t& frames) {
                    if (auto p = r.pair(t->get(key), std::format("{}'s `{}`", what, key), std::format("{} = [amount, frames]", key))) {
                        if ((*p)[0] < 0 || (*p)[0] > hi || (*p)[1] < 0 || (*p)[1] > 600 || (*p)[1] != std::floor((*p)[1]))
                            r.error(t->get(key), std::format("{}'s `{}` is [0–{}, whole frames 0–600]", what, key, hi));
                        else amount = static_cast<float>((*p)[0]), frames = static_cast<std::int32_t>((*p)[1]);
                    }
                };
                amount_frames("shake", 200, im.shake, im.shake_frames);
                amount_frames("zoom", 1, im.zoom, im.zoom_frames);
                amount_frames("rumble", 1, im.rumble, im.rumble_frames);
                if (const toml::node* f = t->get("flash")) {
                    const toml::array* a = f->as_array();
                    if (a && a->size() == 2) {
                        if (auto k = r.choice(a->get(0), std::format("{}'s flash", what), flash_names)) im.flash = static_cast<Flash>(*k);
                        if (auto n = r.integer(a->get(1), std::format("{}'s flash frames", what), 0, 600)) im.flash_frames = *n;
                    } else {
                        r.error(f, std::format("{}'s `flash` is [kind, frames]", what), "flash = [\"white\", 4]: none, white or invert");
                    }
                }
                if (const toml::node* sn = t->get("spark")) {
                    if (const toml::table* st = sn->as_table()) {
                        r.only(*st, spark_keys, std::format("{}'s spark", what));
                        if (auto k = r.choice(st->get("style"), "a spark's `style`", spark_names)) im.spark = static_cast<Spark>(*k);
                        if (auto v = r.number(st->get("size"), "a spark's `size`", 0, 2000)) im.spark_size = static_cast<float>(*v);
                        if (auto v = r.integer(st->get("count"), "a spark's `count`", 1, 64)) im.spark_count = *v;
                        if (auto v = r.integer(st->get("frames"), "a spark's `frames`", 0, 600)) im.spark_frames = *v;
                        if (auto c = r.colour(st->get("color"), "a spark's `color`")) im.spark_color = *c;
                        if (auto c = r.colour(st->get("counter"), "a spark's `counter`")) im.counter_color = *c;
                    } else {
                        r.error(sn, std::format("{}'s `spark` is a table", what), "spark = { style = \"rays\", size = 30 }");
                    }
                }
                if (const toml::node* sd = t->get("sound")) {
                    const toml::array* a = sd->as_array();
                    if (a && a->size() <= 3) {
                        for (std::size_t i = 0; i < a->size(); ++i)
                            if (auto s = r.text(a->get(i), "a sound")) im.sound[i] = *s;
                    } else {
                        r.error(sd, std::format("{}'s `sound` is up to three names", what), "sound = [\"attack\", \"body\", \"tail\"]");
                    }
                }
                look.impacts.push_back(std::move(im));
            }
        } else {
            r.error(impacts, "`impact` is a table of profiles", "[impact.light] shake = [2, 6]");
        }
    }

    if (auto c = r.colour(root->get("accent"), "`accent`")) look.accent = *c;
    if (const toml::table* t = r.table_of(root->get("trail"), "`trail`")) {
        r.only(*t, trail_keys, "`trail`");
        if (auto c = r.colour(t->get("color"), "a trail's `color`")) look.trail.color = *c;
        if (auto v = r.number(t->get("alpha"), "a trail's `alpha`", 0, 1)) look.trail.alpha = static_cast<float>(*v);
        if (const toml::array* a = t->get("frames") ? t->get("frames")->as_array() : nullptr)
            for (const toml::node& n : *a)
                if (auto v = r.integer(&n, "a trail's frame", 1, 8)) look.trail.frames.push_back(*v);
    }
    if (const toml::table* t = r.table_of(root->get("light"), "`light`")) {
        r.only(*t, light_keys, "`light`");
        if (auto f = r.pair(t->get("from"), "a light's `from`", "from = [-0.6, -0.8]: where it comes from, on the picture (y down)")) {
            const double len = std::sqrt((*f)[0] * (*f)[0] + (*f)[1] * (*f)[1]);
            if (len > 0) look.light.from = {static_cast<float>((*f)[0] / len), static_cast<float>((*f)[1] / len)};
            else r.error(t->get("from"), "a light comes from somewhere: not [0, 0]");
        }
        if (auto v = r.number(t->get("shade"), "a light's `shade`", 0, 64)) look.light.shade = static_cast<float>(*v);
        if (auto v = r.number(t->get("darken"), "a light's `darken`", 0, 1)) look.light.darken = static_cast<float>(*v);
        if (auto c = r.colour(t->get("shadow"), "a light's `shadow`")) look.light.shadow = *c;
        if (auto v = r.number(t->get("rim"), "a light's `rim`", 0, 32)) look.light.rim = static_cast<float>(*v);
        if (auto c = r.colour(t->get("rim_color"), "a light's `rim_color`")) look.light.rim_color = *c;
    }
    if (const toml::table* t = r.table_of(root->get("glow"), "`glow`")) {
        r.only(*t, glow_keys, "`glow`");
        if (auto c = r.colour(t->get("color"), "a glow's `color`")) look.glow.color = *c;
        if (auto v = r.number(t->get("width"), "a glow's `width`", 0, 64)) look.glow.width = static_cast<float>(*v);
    }
    if (const toml::table* t = r.table_of(root->get("pane"), "`pane`")) {
        r.only(*t, pane_keys, "`pane`");
        if (auto c = r.colour(t->get("glass"), "a pane's `glass`")) look.pane.glass = *c;
        if (auto c = r.colour(t->get("edge"), "a pane's `edge`")) look.pane.edge = *c;
        if (auto c = r.colour(t->get("shine"), "a pane's `shine`")) look.pane.shine = *c;
        if (auto c = r.colour(t->get("reflect"), "a pane's `reflect`")) look.pane.reflect = *c;
    }
    if (const toml::table* t = r.table_of(root->get("clone"), "`clone`")) {
        r.only(*t, clone_keys, "`clone`");
        if (auto c = r.colour(t->get("tint"), "a clone's `tint`")) look.clone.tint = *c;
    }
    if (const toml::table* t = r.table_of(root->get("rewind"), "`rewind`")) {
        r.only(*t, rewind_keys, "`rewind`");
        if (auto c = r.colour(t->get("color"), "a rewind's `color`")) look.rewind.color = *c;
        if (auto v = r.integer(t->get("ghosts"), "a rewind's `ghosts`", 0, 12)) look.rewind.ghosts = static_cast<std::int32_t>(*v);
        if (auto v = r.integer(t->get("frames"), "a rewind's `frames`", 1, 120)) look.rewind.frames = static_cast<std::int32_t>(*v);
    }
    if (const toml::table* t = r.table_of(root->get("footfall"), "`footfall`")) {
        r.only(*t, footfall_keys, "`footfall`");
        if (auto v = r.integer(t->get("every"), "a footfall's `every`", 1, 600)) look.footfall.every = static_cast<std::int32_t>(*v);
        if (auto v = r.number(t->get("dip"), "a footfall's `dip`", 0, 64)) look.footfall.dip = static_cast<float>(*v);
        if (auto v = r.integer(t->get("frames"), "a footfall's `frames`", 1, 600)) look.footfall.frames = static_cast<std::int32_t>(*v);
    }

    // Glyphs: strokes in a 100 × 100 box, y up, in the order they are written.
    if (const toml::node* gn = root->get("glyph")) {
        if (const toml::table* gt = gn->as_table()) {
            for (const auto& [name, node] : *gt) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("glyph `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[glyph.\"永\"] strokes = [{ points = [[48, 92], [54, 84]], width = [10, 14, 8] }]");
                    continue;
                }
                std::vector<Stroke> strokes;
                r.tables(t->get("strokes"), "strokes", "strokes = [{ points = [[48, 92], [54, 84]], width = [10, 14, 8] }]", [&](const toml::table& st) {
                    r.only(st, stroke_keys, "a stroke");
                    Stroke stroke;
                    if (const toml::array* pts = st.get("points") ? st.get("points")->as_array() : nullptr) {
                        for (const toml::node& q : *pts)
                            if (auto xy = r.pair(&q, "a stroke's point", "[48, 92]")) stroke.points.push_back({static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])});
                    }
                    if (stroke.points.size() < 2) r.error(&st, std::format("a stroke of {} has two points or more", what));
                    if (const toml::array* w = st.get("width") ? st.get("width")->as_array() : nullptr; w && w->size() == 3) {
                        for (std::size_t k = 0; k < 3; ++k)
                            if (auto v = r.number(w->get(k), "a stroke's width", 0, 200)) stroke.width[k] = static_cast<float>(*v);
                    } else if (st.get("width")) {
                        r.error(st.get("width"), "a stroke's `width` is [start, middle, end]");
                    }
                    strokes.push_back(std::move(stroke));
                });
                look.glyphs.insert_or_assign(std::string(name.str()), std::move(strokes));
            }
        } else {
            r.error(gn, "`glyph` is a table of glyphs");
        }
    }

    // Cinematics: a level 3's, by its move's name.
    if (const toml::node* cn = root->get("cinematic")) {
        if (const toml::table* ct = cn->as_table()) {
            for (const auto& [name, node] : *ct) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("cinematic `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what));
                    continue;
                }
                r.only(*t, cinematic_keys, what);
                CinematicLook c;
                if (auto v = r.text(t->get("attacker"), "a cinematic's `attacker` clip")) c.attacker_clip = *v;
                if (auto v = r.text(t->get("defender"), "a cinematic's `defender` clip")) c.defender_clip = *v;
                for (const auto* clip : {&c.attacker_clip, &c.defender_clip})
                    if (!clip->empty() && !look.clips.contains(*clip)) r.error(t, std::format("{} plays `{}`, which is no clip", what, *clip));
                if (const toml::table* b = r.table_of(t->get("backdrop"), "a cinematic's `backdrop`")) {
                    r.only(*b, backdrop_keys, "a backdrop");
                    if (auto v = r.colour(b->get("color"), "a backdrop's `color`")) c.backdrop = *v;
                    if (auto v = r.pair(b->get("in"), "a backdrop's `in`", "in = [0, 20]: the frames it fades in over")) c.backdrop_in = {static_cast<std::int32_t>((*v)[0]), static_cast<std::int32_t>((*v)[1])};
                    if (auto v = r.pair(b->get("out"), "a backdrop's `out`", "out = [184, 200]")) c.backdrop_out = {static_cast<std::int32_t>((*v)[0]), static_cast<std::int32_t>((*v)[1])};
                    if (const toml::table* cl = r.table_of(b->get("clouds"), "a backdrop's `clouds`")) {
                        r.only(*cl, clouds_keys, "a backdrop's clouds");
                        if (auto v = r.colour(cl->get("color"), "the clouds' `color`")) c.clouds.color = *v;
                        if (auto v = r.integer(cl->get("count"), "the clouds' `count`", 0, 64)) c.clouds.count = static_cast<std::int32_t>(*v);
                        if (auto v = r.pair(cl->get("between"), "the clouds' `between`", "between = [250, 900]: how high, from where the lock began"))
                            c.clouds.low = static_cast<float>((*v)[0]), c.clouds.high = static_cast<float>((*v)[1]);
                    }
                }
                if (const toml::table* pr = r.table_of(t->get("prism"), "a cinematic's `prism`")) {
                    r.only(*pr, prism_keys, "a prism");
                    if (auto v = r.integer(pr->get("images"), "a prism's `images`", 0, 4)) c.prism.images = static_cast<std::int32_t>(*v);
                    if (auto v = r.number(pr->get("spread"), "a prism's `spread`", 0, 400)) c.prism.spread = static_cast<float>(*v);
                    if (auto v = r.integer(pr->get("from"), "a prism's `from`", 0, 1200)) c.prism.from = static_cast<std::int32_t>(*v);
                    if (auto v = r.integer(pr->get("to"), "a prism's `to`", 0, 1200)) c.prism.to = static_cast<std::int32_t>(*v);
                    if (auto v = r.colour(pr->get("tint"), "a prism's `tint`")) c.prism.tint = *v;
                }
                if (const toml::table* cr = r.table_of(t->get("cracks"), "a cinematic's `cracks`")) {
                    r.only(*cr, cracks_keys, "cracks");
                    c.cracks.on = true;
                    if (auto v = r.colour(cr->get("color"), "the cracks' `color`")) c.cracks.color = *v;
                }
                if (const toml::table* st = r.table_of(t->get("stop"), "a cinematic's `stop`")) {
                    r.only(*st, stop_keys, "a stop");
                    if (auto v = r.integer(st->get("from"), "a stop's `from`", 0, 1200)) c.stop.from = static_cast<std::int32_t>(*v);
                    if (auto v = r.integer(st->get("to"), "a stop's `to`", 0, 1200)) c.stop.to = static_cast<std::int32_t>(*v);
                    if (auto v = r.colour(st->get("tint"), "a stop's `tint`")) c.stop.tint = *v;
                }
                if (const toml::table* ck = r.table_of(t->get("clock"), "a cinematic's `clock`")) {
                    r.only(*ck, clock_keys, "a clock");
                    if (auto v = r.integer(ck->get("from"), "a clock's `from`", 0, 1200)) c.clock.from = static_cast<std::int32_t>(*v);
                    if (auto v = r.integer(ck->get("to"), "a clock's `to`", 0, 1200)) c.clock.to = static_cast<std::int32_t>(*v);
                    if (auto v = r.pair(ck->get("at"), "a clock's `at`", "at = [120, 300]: forward and up")) c.clock.at = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1])};
                    if (auto v = r.number(ck->get("size"), "a clock's `size`", 10, 2000)) c.clock.size = static_cast<float>(*v);
                    if (auto v = r.colour(ck->get("color"), "a clock's `color`")) c.clock.color = *v;
                }
                if (const toml::table* fa = r.table_of(t->get("fall"), "a cinematic's `fall`")) {
                    r.only(*fa, fall_keys, "a fall");
                    if (auto v = r.integer(fa->get("frame"), "a fall's `frame`", 0, 1200)) c.fall.frame = static_cast<std::int32_t>(*v);
                    if (auto v = r.integer(fa->get("count"), "a fall's `count`", 1, 200)) c.fall.count = static_cast<std::int32_t>(*v);
                    if (auto v = r.colour(fa->get("color"), "a fall's `color`")) c.fall.color = *v;
                }
                if (const toml::node* cam = t->get("camera")) {
                    if (const toml::array* a = cam->as_array()) {
                        for (const toml::node& k : *a) {
                            const toml::array* key = k.as_array();
                            if (!key || key->size() != 4) {
                                r.error(&k, "a camera key is [frame, zoom, forward, up]");
                                continue;
                            }
                            auto f = r.integer(key->get(0), "a camera key's frame", 0, 1200);
                            auto z = r.number(key->get(1), "a camera key's zoom", 0.1, 10);
                            auto x = r.number(key->get(2), "a camera key's forward");
                            auto y = r.number(key->get(3), "a camera key's up");
                            if (f && z && x && y) c.camera.push_back({*f, static_cast<float>(*z), {static_cast<float>(*x), static_cast<float>(*y)}});
                        }
                    } else {
                        r.error(cam, "a cinematic's `camera` is a list of keys", "camera = [[0, 1, 85, 110], [12, 2.6, 0, 160]]");
                    }
                }
                if (const toml::table* g = r.table_of(t->get("glyph"), "a cinematic's `glyph`")) {
                    r.only(*g, written_keys, "a written glyph");
                    if (auto v = r.text(g->get("name"), "the glyph's `name`")) {
                        c.glyph = *v;
                        if (!look.glyphs.contains(*v)) r.error(g->get("name"), std::format("{} writes `{}`, which is no glyph of the look", what, *v));
                    }
                    if (auto v = r.pair(g->get("at"), "the glyph's `at`", "at = [170, 110]")) c.glyph_at = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1])};
                    if (auto v = r.number(g->get("size"), "the glyph's `size`", 1, 4000)) c.glyph_size = static_cast<float>(*v);
                    if (auto v = r.integer(g->get("from"), "the glyph's `from`", 0, 1200)) c.glyph_from = *v;
                    if (const toml::array* e = g->get("ends") ? g->get("ends")->as_array() : nullptr)
                        for (const toml::node& n : *e)
                            if (auto v = r.integer(&n, "a stroke's end frame", 0, 1200)) c.glyph_ends.push_back(*v);
                    if (auto v = r.colour(g->get("color"), "the glyph's `color`")) c.glyph_color = *v;
                }
                if (const toml::table* sl = r.table_of(t->get("seal"), "a cinematic's `seal`")) {
                    r.only(*sl, seal_keys, "a seal");
                    if (auto v = r.integer(sl->get("frame"), "the seal's `frame`", 0, 1200)) c.seal_frame = *v;
                    if (auto v = r.pair(sl->get("at"), "the seal's `at`", "at = [250, 40]")) c.seal_at = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1])};
                    if (auto v = r.number(sl->get("size"), "the seal's `size`", 1, 1000)) c.seal_size = static_cast<float>(*v);
                    if (auto v = r.colour(sl->get("color"), "the seal's `color`")) c.seal_color = *v;
                }
                if (auto v = r.boolean(t->get("scanlines"), "a cinematic's `scanlines`")) c.scanlines = *v;
                // Paths: [[frame, forward, up], …]; quakes: [[frame, strength, frames], …]; rings: [[frame, forward, up, size, frames], …].
                auto rows = [&](std::string_view key, std::size_t width, auto&& add) {
                    const toml::node* n = t->get(key);
                    if (!n) return;
                    const toml::array* a = n->as_array();
                    if (!a) {
                        r.error(n, std::format("a cinematic's `{}` is a list", key));
                        return;
                    }
                    for (const toml::node& e : *a) {
                        const toml::array* row = e.as_array();
                        if (!row || row->size() != width) {
                            r.error(&e, std::format("a `{}` entry has {} numbers", key, width));
                            continue;
                        }
                        std::vector<double> v;
                        for (std::size_t k = 0; k < width; ++k)
                            if (auto x = r.number(row->get(k), std::format("a `{}` number", key))) v.push_back(*x);
                        if (v.size() == width) add(v);
                    }
                };
                rows("attacker_path", 3, [&](const std::vector<double>& v) { c.attacker_path.push_back({static_cast<std::int32_t>(v[0]), {static_cast<float>(v[1]), static_cast<float>(v[2])}}); });
                rows("defender_path", 3, [&](const std::vector<double>& v) { c.defender_path.push_back({static_cast<std::int32_t>(v[0]), {static_cast<float>(v[1]), static_cast<float>(v[2])}}); });
                rows("quake", 3, [&](const std::vector<double>& v) { c.quakes.push_back({static_cast<std::int32_t>(v[0]), static_cast<float>(v[1]), static_cast<std::int32_t>(v[2])}); });
                rows("ring", 5, [&](const std::vector<double>& v) {
                    c.rings.push_back({static_cast<std::int32_t>(v[0]), {static_cast<float>(v[1]), static_cast<float>(v[2])}, static_cast<float>(v[3]), static_cast<std::int32_t>(v[4])});
                });
                if (const toml::node* fl = t->get("flash")) {
                    if (const toml::array* a = fl->as_array()) {
                        for (const toml::node& k : *a) {
                            const toml::array* key = k.as_array();
                            if (!key || key->size() != 3) {
                                r.error(&k, "a flash is [frame, kind, frames]");
                                continue;
                            }
                            auto f = r.integer(key->get(0), "a flash's frame", 0, 1200);
                            auto kind = r.choice(key->get(1), "a flash's kind", flash_names);
                            auto n = r.integer(key->get(2), "a flash's frames", 1, 600);
                            if (f && kind && n) c.flashes.push_back({*f, static_cast<Flash>(*kind), *n});
                        }
                    } else {
                        r.error(fl, "a cinematic's `flash` is a list", "flash = [[180, \"white\", 6]]");
                    }
                }
                look.cinematics.insert_or_assign(std::string(name.str()), std::move(c));
            }
        } else {
            r.error(cn, "`cinematic` is a table of cinematics");
        }
    }

    // Projectiles drawn as glyphs: [projectile.236P] glyph = "一", size = 60.
    if (const toml::node* pn = root->get("projectile")) {
        if (const toml::table* pt = pn->as_table()) {
            for (const auto& [name, node] : *pt) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("projectile `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[projectile.236P] glyph = \"一\" size = 60");
                    continue;
                }
                r.only(*t, projectile_look_keys, what);
                Look::GlyphProjectile g;
                if (auto v = r.text(t->get("glyph"), "a projectile's `glyph`")) {
                    g.glyph = *v;
                    if (!look.glyphs.contains(*v)) r.error(t->get("glyph"), std::format("{} is drawn as `{}`, which is no glyph of the look", what, *v));
                }
                if (auto v = r.number(t->get("size"), "a projectile's `size`", 1, 2000)) g.size = static_cast<float>(*v);
                if (auto v = r.colour(t->get("color"), "a projectile's `color`")) g.color = *v;
                if (auto v = r.colour(t->get("halo"), "a projectile's `halo`")) g.halo = *v;
                if (auto v = r.number(t->get("spin"), "a projectile's `spin` (degrees a frame)", -90, 90)) g.spin = static_cast<float>(*v);
                look.projectiles.insert_or_assign(std::string(name.str()), std::move(g));
            }
        } else {
            r.error(pn, "`projectile` is a table of projectiles");
        }
    }

    for (const Bone& b : look.bones)
        if (b.parent < 0 && &b != &look.bones.front()) r.error(&*root, std::format("bone `{}` has no parent; only the first bone is the root", b.name));
    if (!out.empty()) return std::unexpected(std::move(out));
    use_palette(look, 0);
    return look;
}

std::string write_look(const Look& look) {
    std::string s = std::format("gongfu = \"{}\"\nname = \"{}\"\noutline = {{ color = {}, width = {} }}\naccent = {}\n", format_version, look.name,
                                colour_text(look.outline), look.outline_width, colour_text(look.accent));
    if (!look.trail.frames.empty()) {
        s += std::format("trail = {{ color = {}, frames = [", colour_text(look.trail.color));
        for (std::size_t i = 0; i < look.trail.frames.size(); ++i) s += std::format("{}{}", i ? ", " : "", look.trail.frames[i]);
        s += std::format("], alpha = {} }}\n", look.trail.alpha);
    }
    if (look.glow.width > 0) s += std::format("glow = {{ color = {}, width = {} }}\n", colour_text(look.glow.color), look.glow.width);
    if (look.light.shade > 0)
        s += std::format("light = {{ from = [{}, {}], shade = {}, darken = {}, shadow = {}, rim = {}, rim_color = {} }}\n", look.light.from.x, look.light.from.y,
                         look.light.shade, look.light.darken, colour_text(look.light.shadow), look.light.rim, colour_text(look.light.rim_color));
    if (look.pane.glass != Look::Pane{}.glass || look.pane.edge != Look::Pane{}.edge || look.pane.shine != Look::Pane{}.shine || look.pane.reflect != Look::Pane{}.reflect)
        s += std::format("pane = {{ glass = {}, edge = {}, shine = {}, reflect = {} }}\n", colour_text(look.pane.glass), colour_text(look.pane.edge),
                         colour_text(look.pane.shine), colour_text(look.pane.reflect));
    if (look.clone.tint != Look::CloneLook{}.tint) s += std::format("clone = {{ tint = {} }}\n", colour_text(look.clone.tint));
    if (look.rewind.ghosts > 0)
        s += std::format("rewind = {{ color = {}, ghosts = {}, frames = {} }}\n", colour_text(look.rewind.color), look.rewind.ghosts, look.rewind.frames);
    if (look.footfall.dip > 0) s += std::format("footfall = {{ every = {}, dip = {}, frames = {} }}\n", look.footfall.every, look.footfall.dip, look.footfall.frames);
    for (std::size_t p = 0; p < look.palettes.size(); ++p) {
        s += std::format("\n[[palette]]\nname = \"{}\"\n", p < look.palette_names.size() ? look.palette_names[p] : std::string());
        for (std::size_t k = 0; k < look.slots.size() && k < look.palettes[p].size(); ++k)
            s += std::format("{} = {}\n", key_text(look.slots[k]), colour_text(look.palettes[p][k]));
    }
    for (const Bone& b : look.bones) {
        s += std::format("\n[[bone]]\nname = \"{}\"\n", b.name);
        if (b.parent >= 0) s += std::format("parent = \"{}\"\n", look.bones[static_cast<std::size_t>(b.parent)].name);
        s += std::format("joint = [{}, {}]\nlength = {}\n", b.joint.x, b.joint.y, b.length);
        if (b.rest != 0) s += std::format("rest = {}\n", b.rest);
        if (b.limb) s += "limb = true\n";
        if (b.prop) s += "prop = true\n";
        if (b.follow.lag > 0) s += std::format("follow = {{ lag = {}, amount = {}, max = {} }}\n", b.follow.lag, b.follow.amount, b.follow.max);
    }
    constexpr std::array<std::string_view, 4> shapes{"capsule", "ellipse", "polygon", "curve"};
    auto points_text = [](const std::vector<Point>& pts) {
        std::string t = "[";
        for (std::size_t i = 0; i < pts.size(); ++i) t += std::format("{}[{}, {}]", i ? ", " : "", pts[i].x, pts[i].y);
        return t + "]";
    };
    for (const Part& p : look.parts) {
        s += "\n[[part]]\n";
        if (!p.name.empty()) s += std::format("name = \"{}\"\n", p.name);
        s += std::format("bone = \"{}\"\nshape = \"{}\"\n", look.bones.at(static_cast<std::size_t>(p.bone)).name, shapes[static_cast<std::size_t>(p.shape)]);
        if (p.shape == Shape::polygon || p.shape == Shape::curve) {
            s += std::format("points = {}\n", points_text(p.points));
            if (!p.variants.empty()) {
                s += "variants = [";
                for (std::size_t v = 0; v < p.variants.size(); ++v) s += std::format("{}{}", v ? ", " : "", points_text(p.variants[v]));
                s += "]\n";
            }
        } else {
            s += std::format("size = [{}, {}]\n", p.start, p.end);
        }
        s += std::format("color = {}\n", p.slot >= 0 ? std::format("\"{}\"", look.slots.at(static_cast<std::size_t>(p.slot))) : colour_text(p.color));
        if (p.depth != 0) s += std::format("depth = {}\n", p.depth);
        if (!p.shade) s += "shade = false\n";
    }
    for (const auto& [name, c] : look.clips) {
        const hanim::Ease ease = common_ease(c);
        s += std::format("\n[clip.{}]\nframes = {}\n", key_text(name), c.frames);
        if (c.loop) s += "loop = true\n";
        s += std::format("ease = \"{}\"\n", hanim::to_string(ease));
        if (!c.rise.empty()) s += std::format("rise = {}\n", keys_text(c.rise, ease));
        for (std::size_t b = 0; b < c.angles.size() && b < look.bones.size(); ++b)
            if (!c.angles[b].empty()) s += std::format("angle.{} = {}\n", key_text(look.bones[b].name), keys_text(c.angles[b], ease));
        for (std::size_t k = 0; k < c.swaps.size() && k < look.parts.size(); ++k)
            if (!c.swaps[k].empty()) s += std::format("swap.{} = {}\n", key_text(look.parts[k].name), keys_text(c.swaps[k], hanim::Ease::stepped(1, hanim::StepPosition::jump_end)));
    }
    constexpr std::array<std::string_view, 6> sparks{"rays", "ring", "splash", "shards", "bolt", "cracks"};
    constexpr std::array<std::string_view, 3> flashes{"none", "white", "invert"};
    for (const Impact& im : look.impacts) {
        s += std::format("\n[impact.{}]\n", key_text(im.name));
        if (im.shake_frames) s += std::format("shake = [{}, {}]\n", im.shake, im.shake_frames);
        if (im.zoom_frames) s += std::format("zoom = [{}, {}]\n", im.zoom, im.zoom_frames);
        if (im.flash != Flash::none) s += std::format("flash = [\"{}\", {}]\n", flashes[static_cast<std::size_t>(im.flash)], im.flash_frames);
        s += std::format("spark = {{ style = \"{}\", size = {}, count = {}, frames = {}, color = {}, counter = {} }}\n", sparks[static_cast<std::size_t>(im.spark)],
                         im.spark_size, im.spark_count, im.spark_frames, colour_text(im.spark_color), colour_text(im.counter_color));
        if (std::ranges::any_of(im.sound, [](const std::string& x) { return !x.empty(); }))
            s += std::format("sound = [\"{}\", \"{}\", \"{}\"]\n", im.sound[0], im.sound[1], im.sound[2]);
        if (im.rumble_frames) s += std::format("rumble = [{}, {}]\n", im.rumble, im.rumble_frames);
    }
    for (const auto& [name, strokes] : look.glyphs) {
        s += std::format("\n[glyph.\"{}\"]\nstrokes = [\n", name);
        for (const Stroke& st : strokes) {
            s += "  { points = [";
            for (std::size_t i = 0; i < st.points.size(); ++i) s += std::format("{}[{}, {}]", i ? ", " : "", st.points[i].x, st.points[i].y);
            s += std::format("], width = [{}, {}, {}] }},\n", st.width[0], st.width[1], st.width[2]);
        }
        s += "]\n";
    }
    for (const auto& [name, g] : look.projectiles)
    {
        s += std::format("\n[projectile.{}]\nglyph = \"{}\"\nsize = {}\ncolor = {}\n", key_text(name), g.glyph, g.size, colour_text(g.color));
        if (g.halo.a > 0) s += std::format("halo = {}\n", colour_text(g.halo));
        if (g.spin != 0) s += std::format("spin = {}\n", g.spin);
    }
    constexpr std::array<std::string_view, 3> flash_text{"none", "white", "invert"};
    for (const auto& [name, c] : look.cinematics) {
        s += std::format("\n[cinematic.{}]\nattacker = \"{}\"\ndefender = \"{}\"\n", key_text(name), c.attacker_clip, c.defender_clip);
        s += std::format("backdrop = {{ color = {}, in = [{}, {}], out = [{}, {}]", colour_text(c.backdrop), c.backdrop_in[0], c.backdrop_in[1], c.backdrop_out[0],
                         c.backdrop_out[1]);
        if (c.clouds.count > 0)
            s += std::format(", clouds = {{ color = {}, count = {}, between = [{}, {}] }}", colour_text(c.clouds.color), c.clouds.count, c.clouds.low, c.clouds.high);
        s += " }\n";
        if (!c.camera.empty()) {
            s += "camera = [";
            for (std::size_t i = 0; i < c.camera.size(); ++i)
                s += std::format("{}[{}, {}, {}, {}]", i ? ", " : "", c.camera[i].frame, c.camera[i].zoom, c.camera[i].at.x, c.camera[i].at.y);
            s += "]\n";
        }
        if (!c.glyph.empty()) {
            s += std::format("glyph = {{ name = \"{}\", at = [{}, {}], size = {}, from = {}, ends = [", c.glyph, c.glyph_at.x, c.glyph_at.y, c.glyph_size, c.glyph_from);
            for (std::size_t i = 0; i < c.glyph_ends.size(); ++i) s += std::format("{}{}", i ? ", " : "", c.glyph_ends[i]);
            s += std::format("], color = {} }}\n", colour_text(c.glyph_color));
        }
        if (c.scanlines) s += "scanlines = true\n";
        auto list = [&](std::string_view key, const auto& items, auto&& row) {
            if (items.empty()) return;
            s += std::format("{} = [", key);
            for (std::size_t i = 0; i < items.size(); ++i) s += (i ? ", " : "") + row(items[i]);
            s += "]\n";
        };
        list("attacker_path", c.attacker_path, [](const auto& k) { return std::format("[{}, {}, {}]", k.frame, k.at.x, k.at.y); });
        list("defender_path", c.defender_path, [](const auto& k) { return std::format("[{}, {}, {}]", k.frame, k.at.x, k.at.y); });
        list("quake", c.quakes, [](const auto& q) { return std::format("[{}, {}, {}]", q.frame, q.strength, q.frames); });
        list("ring", c.rings, [](const auto& g) { return std::format("[{}, {}, {}, {}, {}]", g.frame, g.at.x, g.at.y, g.size, g.frames); });
        if (c.prism.images > 0)
            s += std::format("prism = {{ images = {}, spread = {}, from = {}, to = {}, tint = {} }}\n", c.prism.images, c.prism.spread, c.prism.from, c.prism.to,
                             colour_text(c.prism.tint));
        if (c.cracks.on) s += std::format("cracks = {{ color = {} }}\n", colour_text(c.cracks.color));
        if (c.stop.from >= 0) s += std::format("stop = {{ from = {}, to = {}, tint = {} }}\n", c.stop.from, c.stop.to, colour_text(c.stop.tint));
        if (c.clock.from >= 0)
            s += std::format("clock = {{ from = {}, to = {}, at = [{}, {}], size = {}, color = {} }}\n", c.clock.from, c.clock.to, c.clock.at.x, c.clock.at.y,
                             c.clock.size, colour_text(c.clock.color));
        if (c.fall.frame >= 0) s += std::format("fall = {{ frame = {}, count = {}, color = {} }}\n", c.fall.frame, c.fall.count, colour_text(c.fall.color));
        if (c.seal_frame >= 0)
            s += std::format("seal = {{ frame = {}, at = [{}, {}], size = {}, color = {} }}\n", c.seal_frame, c.seal_at.x, c.seal_at.y, c.seal_size, colour_text(c.seal_color));
        if (!c.flashes.empty()) {
            s += "flash = [";
            for (std::size_t i = 0; i < c.flashes.size(); ++i)
                s += std::format("{}[{}, \"{}\", {}]", i ? ", " : "", c.flashes[i].frame, flash_text[static_cast<std::size_t>(c.flashes[i].kind)], c.flashes[i].frames);
            s += "]\n";
        }
    }
    return s;
}

// ── stages (*.stage.gongfu.toml) ────────────────────────────────────────────
//
//     gongfu = "0.1"
//     name = "study"
//     sky = ["#f3eadb", "#e4d5bb"]          # top, bottom
//     floor = "#6d5240"
//     line = "#2e2119"
//     marks = false
//     light = { from = [-0.7, -0.7], shadow = "#2a1d14", rim_color = "#fff2d0aa" }
//
//     [[layer]]
//     name = "window"
//     parallax = 0.3                        # 0 still, 1 with the ground, more: in front
//     repeat = 900                          # drawn again every 900 units across
//     front = false                         # over the fighters
//     [[layer.shape]]
//     shape = "polygon"                     # or "curve"
//     points = [[-300, 120], [300, 120], [300, 520], [-300, 520]]   # x along, y up from the ground
//     color = "#f7efe0"
//     color2 = "#efe2c8"                    # a gradient to it, top to bottom
//     sway = { degrees = 3, period = 5, phase = 0.2, pivot = [0, 520] }
//     flicker = 0.15
//     [[layer.bamboo]]                      # generators: bamboo, lattice, planks
//     seed = 7
//     count = 9
//     span = [-600, 600]
//     height = [380, 520]
//     width = 9
//     color = "#6f7d5c"
//     leaves = "#7f8f68"
//     sway = 2.5

namespace {

constexpr std::array<std::string_view, 8> stage_keys{"gongfu", "name", "sky", "floor", "line", "marks", "light", "layer"};
constexpr std::array<std::string_view, 3> stage_light_keys{"from", "shadow", "rim_color"};
constexpr std::array<std::string_view, 10> layer_keys{"name", "parallax", "repeat", "opacity", "front", "clip", "shape", "bamboo", "lattice", "planks"};
constexpr std::array<std::string_view, 6> stage_shape_keys{"shape", "points", "color", "color2", "sway", "flicker"};
constexpr std::array<std::string_view, 4> sway_keys{"degrees", "period", "phase", "pivot"};
constexpr std::array<std::string_view, 8> bamboo_keys{"seed", "count", "span", "height", "width", "color", "leaves", "sway"};
constexpr std::array<std::string_view, 6> lattice_keys{"at", "size", "cells", "bar", "color", "paper"};
constexpr std::array<std::string_view, 6> planks_keys{"span", "depth", "rows", "length", "color", "seed"};

// splitmix64: a generator's randomness from its seed, the same everywhere.
struct Draws {
    std::uint64_t s;
    double next() {
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return static_cast<double>((z ^ (z >> 31)) >> 11) / 9007199254740992.0;  // [0, 1)
    }
    double between(double lo, double hi) { return lo + (hi - lo) * next(); }
};

StageShape quad(float x0, float y0, float x1, float y1, Color c) {
    StageShape q;
    q.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    q.color = c;
    return q;
}

// Stalks of bamboo: segments with darker nodes, a lean, leaves in sprays at some nodes; swaying from the ground.
void bamboo(StageLayer& layer, std::uint64_t seed, int count, std::array<double, 2> span, std::array<double, 2> height, float width, Color stalk, Color leaf,
            float sway) {
    Draws d{seed};
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(span[0] + (span[1] - span[0]) * (i + d.between(0.15, 0.85)) / count);
        const float h = static_cast<float>(d.between(height[0], height[1]));
        const float lean = static_cast<float>(d.between(-0.06, 0.06));  // across per unit up
        const float w = width * static_cast<float>(d.between(0.8, 1.2));
        const float period = static_cast<float>(d.between(4.5, 6.5)), phase = static_cast<float>(d.next());
        const float deg = sway * static_cast<float>(d.between(0.7, 1.3));
        const int segments = 5 + static_cast<int>(d.next() * 3);
        for (int k = 0; k < segments; ++k) {
            const float y0 = h * static_cast<float>(k) / segments, y1 = h * static_cast<float>(k + 1) / segments - 3;
            StageShape seg;
            seg.points = {{x + lean * y0 - w / 2, y0}, {x + lean * y1 - w / 2 * 0.9f, y1}, {x + lean * y1 + w / 2 * 0.9f, y1}, {x + lean * y0 + w / 2, y0}};
            seg.color = stalk;
            seg.sway = deg * y1 / h, seg.period = period, seg.phase = phase, seg.pivot = {x, 0};
            layer.shapes.push_back(std::move(seg));
            // A spray of three leaves at some nodes, pointing out and down.
            if (k >= segments / 2 && d.next() < 0.6) {
                const float nx = x + lean * y1, side = d.next() < 0.5 ? -1.0f : 1.0f;
                for (int l = 0; l < 3; ++l) {
                    const float len = w * static_cast<float>(d.between(4.5, 6.5)), droop = static_cast<float>(l) * 0.35f + 0.15f;
                    const Point tip{nx + side * len, y1 - len * droop};
                    StageShape lf;
                    lf.shape = Shape::curve;
                    lf.points = {{nx, y1}, {nx + side * len * 0.45f, y1 - len * droop * 0.45f + w * 0.55f}, tip, {nx + side * len * 0.45f, y1 - len * droop * 0.45f - w * 0.35f}};
                    lf.color = leaf;
                    lf.sway = deg * 1.6f, lf.period = period, lf.phase = phase, lf.pivot = {x, 0};
                    layer.shapes.push_back(std::move(lf));
                }
            }
        }
    }
}

// A paper window: its frame and the bars of a lattice over paper.
void lattice(StageLayer& layer, Point at, Point size, int cols, int rows, float bar, Color wood, Color paper) {
    if (paper.a > 0) layer.shapes.push_back(quad(at.x, at.y, at.x + size.x, at.y + size.y, paper));  // transparent: the bars alone
    for (int c = 0; c <= cols; ++c) {
        const float x = at.x + size.x * static_cast<float>(c) / cols;
        layer.shapes.push_back(quad(x - bar / 2, at.y, x + bar / 2, at.y + size.y, wood));
    }
    for (int r = 0; r <= rows; ++r) {
        const float y = at.y + size.y * static_cast<float>(r) / rows;
        layer.shapes.push_back(quad(at.x, y - bar / 2, at.x + size.x, y + bar / 2, wood));
    }
}

// Floor planks below the ground line: rows of boards, their joints staggered.
void planks(StageLayer& layer, std::uint64_t seed, std::array<double, 2> span, float depth, int rows, float length, Color seam) {
    Draws d{seed};
    for (int r = 1; r <= rows; ++r) {
        const float y = -depth * static_cast<float>(r) / rows;
        layer.shapes.push_back(quad(static_cast<float>(span[0]), y - 1, static_cast<float>(span[1]), y + 1, seam));
        const float offset = static_cast<float>(d.between(0, length));
        for (float x = static_cast<float>(span[0]) + offset; x < span[1]; x += length * static_cast<float>(d.between(0.8, 1.2)))
            layer.shapes.push_back(quad(x - 1, y, x + 1, y + depth / rows, seam));
    }
}

}  // namespace

std::expected<Stage, std::vector<Diagnostic>> read_stage(std::string_view source, std::string_view file) {
    std::vector<Diagnostic> out;
    auto root = parse(source, file, out);
    if (!root) return std::unexpected(std::move(out));
    Reader r{std::string(file), out};
    r.only(*root, stage_keys, "a stage file");
    if (auto v = r.text(root->get("gongfu"), "`gongfu`")) {
        if (*v != format_version) r.error(root->get("gongfu"), std::format("this reads gongfu {} files; the file says {}", format_version, *v));
    } else if (!root->get("gongfu")) {
        r.error(&*root, "the file does not say its format", std::format("gongfu = \"{}\" first", format_version));
    }
    Stage st;
    if (auto n = r.text(root->get("name"), "`name`")) st.name = *n;
    if (const toml::node* sky = root->get("sky")) {
        const toml::array* a = sky->as_array();
        if (a && a->size() == 2) {
            if (auto c = r.colour(a->get(0), "the sky's top")) st.sky_top = *c;
            if (auto c = r.colour(a->get(1), "the sky's bottom")) st.sky_bottom = *c;
        } else {
            r.error(sky, "`sky` is [top, bottom]", "sky = [\"#f3eadb\", \"#e4d5bb\"]");
        }
    }
    if (auto c = r.colour(root->get("floor"), "`floor`")) st.floor = *c;
    if (auto c = r.colour(root->get("line"), "`line`")) st.line = *c;
    if (auto m = r.boolean(root->get("marks"), "`marks`")) st.marks = *m;
    if (const toml::table* t = r.table_of(root->get("light"), "`light`")) {
        r.only(*t, stage_light_keys, "a stage's `light`");
        if (auto f = r.pair(t->get("from"), "a light's `from`", "from = [-0.7, -0.7]")) {
            const double len = std::sqrt((*f)[0] * (*f)[0] + (*f)[1] * (*f)[1]);
            if (len > 0) st.light_from = Point{static_cast<float>((*f)[0] / len), static_cast<float>((*f)[1] / len)};
            else r.error(t->get("from"), "a light comes from somewhere: not [0, 0]");
        }
        if (auto c = r.colour(t->get("shadow"), "a light's `shadow`")) st.shadow = *c;
        if (auto c = r.colour(t->get("rim_color"), "a light's `rim_color`")) st.rim_color = *c;
    }
    auto points_of = [&](const toml::node* n, std::string_view what) {
        std::vector<Point> pts;
        if (const toml::array* a = n ? n->as_array() : nullptr) {
            for (const toml::node& q : *a)
                if (auto xy = r.pair(&q, what, "[[0, 0], [100, 0], [100, 80]]")) pts.push_back({static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])});
        } else if (n) {
            r.error(n, std::format("{} are a list of [x, y]", what));
        }
        return pts;
    };
    r.tables(root->get("layer"), "layer", "[[layer]] name = \"window\" parallax = 0.3", [&](const toml::table& t) {
        r.only(t, layer_keys, "a layer");
        StageLayer layer;
        if (auto n = r.text(t.get("name"), "a layer's `name`")) layer.name = *n;
        if (auto v = r.number(t.get("parallax"), "a layer's `parallax`", 0, 4)) layer.parallax = static_cast<float>(*v);
        if (auto v = r.number(t.get("repeat"), "a layer's `repeat`", 0, 100000)) layer.repeat = static_cast<float>(*v);
        if (auto v = r.number(t.get("opacity"), "a layer's `opacity`", 0, 1)) layer.opacity = static_cast<float>(*v);
        if (auto v = r.boolean(t.get("front"), "a layer's `front`")) layer.front = *v;
        if (const toml::node* c = t.get("clip")) {
            const toml::array* a = c->as_array();
            std::array<float, 4> box{};
            bool ok = a && a->size() == 4;
            for (std::size_t i = 0; ok && i < 4; ++i) {
                auto v = r.number(a->get(i), "a layer's `clip`");
                ok = v.has_value();
                if (v) box[i] = static_cast<float>(*v);
            }
            if (ok && box[2] > box[0] && box[3] > box[1]) layer.clip = box;
            else r.error(c, "a layer's `clip` is [x0, y0, x1, y1], x1 > x0 and y1 > y0", "clip = [660, 150, 1260, 440]");
        }
        r.tables(t.get("shape"), "layer.shape", "[[layer.shape]] points = [[0, 0], [100, 0], [100, 80]] color = \"#000000\"", [&](const toml::table& u) {
            r.only(u, stage_shape_keys, "a stage's shape");
            StageShape sh;
            if (auto k = r.choice(u.get("shape"), "a stage shape's `shape`", shape_names)) {
                sh.shape = static_cast<Shape>(*k);
                if (sh.shape != Shape::polygon && sh.shape != Shape::curve) r.error(u.get("shape"), "a stage's shape is a polygon or a curve");
            }
            sh.points = points_of(u.get("points"), "a shape's points");
            if (sh.points.size() < 3) r.error(&u, "a shape has three points or more", "points = [[0, 0], [100, 0], [100, 80]]");
            if (auto c = r.colour(u.get("color"), "a shape's `color`")) sh.color = *c;
            else if (!u.get("color")) r.error(&u, "a shape has a `color`");
            if (auto c = r.colour(u.get("color2"), "a shape's `color2`")) sh.color2 = *c;
            if (const toml::table* w = r.table_of(u.get("sway"), "a shape's `sway`")) {
                r.only(*w, sway_keys, "a `sway`");
                if (auto v = r.number(w->get("degrees"), "a sway's `degrees`", 0, 90)) sh.sway = static_cast<float>(*v);
                if (auto v = r.number(w->get("period"), "a sway's `period`", 0.1, 600)) sh.period = static_cast<float>(*v);
                if (auto v = r.number(w->get("phase"), "a sway's `phase`", 0, 1)) sh.phase = static_cast<float>(*v);
                if (auto xy = r.pair(w->get("pivot"), "a sway's `pivot`", "pivot = [0, 0]")) sh.pivot = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
            }
            if (auto v = r.number(u.get("flicker"), "a shape's `flicker`", 0, 1)) sh.flicker = static_cast<float>(*v);
            layer.shapes.push_back(std::move(sh));
        });
        r.tables(t.get("bamboo"), "layer.bamboo", "[[layer.bamboo]] seed = 7 count = 9 span = [-600, 600] height = [380, 520]", [&](const toml::table& u) {
            r.only(u, bamboo_keys, "a bamboo");
            const auto seed = r.integer(u.get("seed"), "a bamboo's `seed`", 0, std::numeric_limits<std::int32_t>::max()).value_or(1);
            const auto count = r.integer(u.get("count"), "a bamboo's `count`", 1, 200).value_or(6);
            const auto span = r.pair(u.get("span"), "a bamboo's `span`", "span = [-600, 600]").value_or(std::array<double, 2>{-400, 400});
            const auto height = r.pair(u.get("height"), "a bamboo's `height`", "height = [380, 520]").value_or(std::array<double, 2>{300, 450});
            const float width = static_cast<float>(r.number(u.get("width"), "a bamboo's `width`", 1, 200).value_or(10));
            const Color stalk = r.colour(u.get("color"), "a bamboo's `color`").value_or(Color::hex(0x6f7d5c));
            const Color leaf = r.colour(u.get("leaves"), "a bamboo's `leaves`").value_or(stalk);
            const float sway = static_cast<float>(r.number(u.get("sway"), "a bamboo's `sway`", 0, 30).value_or(0));
            bamboo(layer, static_cast<std::uint64_t>(seed), count, span, height, width, stalk, leaf, sway);
        });
        r.tables(t.get("lattice"), "layer.lattice", "[[layer.lattice]] at = [-300, 120] size = [600, 400] cells = [6, 4]", [&](const toml::table& u) {
            r.only(u, lattice_keys, "a lattice");
            const auto at = r.pair(u.get("at"), "a lattice's `at`", "at = [-300, 120]").value_or(std::array<double, 2>{0, 0});
            const auto size = r.pair(u.get("size"), "a lattice's `size`", "size = [600, 400]").value_or(std::array<double, 2>{100, 100});
            const auto cells = r.pair(u.get("cells"), "a lattice's `cells`", "cells = [6, 4]").value_or(std::array<double, 2>{4, 4});
            const float bar = static_cast<float>(r.number(u.get("bar"), "a lattice's `bar`", 0.5, 100).value_or(6));
            const Color wood = r.colour(u.get("color"), "a lattice's `color`").value_or(Color::hex(0x5b3a24));
            const Color paper = r.colour(u.get("paper"), "a lattice's `paper`").value_or(Color::hex(0xf7efe0));
            lattice(layer, {static_cast<float>(at[0]), static_cast<float>(at[1])}, {static_cast<float>(size[0]), static_cast<float>(size[1])},
                    std::max(1, static_cast<int>(cells[0])), std::max(1, static_cast<int>(cells[1])), bar, wood, paper);
        });
        r.tables(t.get("planks"), "layer.planks", "[[layer.planks]] span = [-1200, 1200] depth = 70 rows = 4 length = 240", [&](const toml::table& u) {
            r.only(u, planks_keys, "planks");
            const auto span = r.pair(u.get("span"), "planks' `span`", "span = [-1200, 1200]").value_or(std::array<double, 2>{-1000, 1000});
            const float depth = static_cast<float>(r.number(u.get("depth"), "planks' `depth`", 1, 2000).value_or(70));
            const auto rows = r.integer(u.get("rows"), "planks' `rows`", 1, 64).value_or(4);
            const float length = static_cast<float>(r.number(u.get("length"), "planks' `length`", 10, 10000).value_or(240));
            const Color seam = r.colour(u.get("color"), "planks' `color`").value_or(Color::hex(0x3b2a1e));
            const auto seed = r.integer(u.get("seed"), "planks' `seed`", 0, std::numeric_limits<std::int32_t>::max()).value_or(1);
            planks(layer, static_cast<std::uint64_t>(seed), span, depth, rows, length, seam);
        });
        st.layers.push_back(std::move(layer));
    });
    if (!out.empty()) return std::unexpected(std::move(out));
    return st;
}

std::string write_stage(const Stage& st) {
    std::string s = std::format("gongfu = \"{}\"\nname = \"{}\"\nsky = [{}, {}]\nfloor = {}\nline = {}\n", format_version, st.name, colour_text(st.sky_top),
                                colour_text(st.sky_bottom), colour_text(st.floor), colour_text(st.line));
    if (!st.marks) s += "marks = false\n";
    if (st.light_from || st.shadow || st.rim_color) {
        std::string parts;
        if (st.light_from) parts += std::format("from = [{}, {}]", st.light_from->x, st.light_from->y);
        if (st.shadow) parts += std::format("{}shadow = {}", parts.empty() ? "" : ", ", colour_text(*st.shadow));
        if (st.rim_color) parts += std::format("{}rim_color = {}", parts.empty() ? "" : ", ", colour_text(*st.rim_color));
        s += std::format("light = {{ {} }}\n", parts);
    }
    constexpr std::array<std::string_view, 4> shapes{"capsule", "ellipse", "polygon", "curve"};
    for (const StageLayer& l : st.layers) {
        s += std::format("\n[[layer]]\nname = \"{}\"\nparallax = {}\n", l.name, l.parallax);
        if (l.repeat > 0) s += std::format("repeat = {}\n", l.repeat);
        if (l.opacity != 1) s += std::format("opacity = {}\n", l.opacity);
        if (l.front) s += "front = true\n";
        if (l.clip) s += std::format("clip = [{}, {}, {}, {}]\n", (*l.clip)[0], (*l.clip)[1], (*l.clip)[2], (*l.clip)[3]);
        for (const StageShape& sh : l.shapes) {
            s += std::format("[[layer.shape]]\nshape = \"{}\"\npoints = [", shapes[static_cast<std::size_t>(sh.shape)]);
            for (std::size_t i = 0; i < sh.points.size(); ++i) s += std::format("{}[{}, {}]", i ? ", " : "", sh.points[i].x, sh.points[i].y);
            s += std::format("]\ncolor = {}\n", colour_text(sh.color));
            if (sh.color2.a > 0) s += std::format("color2 = {}\n", colour_text(sh.color2));
            if (sh.sway > 0)
                s += std::format("sway = {{ degrees = {}, period = {}, phase = {}, pivot = [{}, {}] }}\n", sh.sway, sh.period, sh.phase, sh.pivot.x, sh.pivot.y);
            if (sh.flicker > 0) s += std::format("flicker = {}\n", sh.flicker);
        }
    }
    return s;
}

}  // namespace gongfu::present2d
