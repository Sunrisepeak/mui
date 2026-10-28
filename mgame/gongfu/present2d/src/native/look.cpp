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
constexpr std::array<std::string_view, 9> look_keys{"gongfu", "name", "outline", "palette", "bone", "part", "clip", "impact", "doc"};
constexpr std::array<std::string_view, 2> outline_keys{"color", "width"};
constexpr std::array<std::string_view, 5> bone_keys{"name", "parent", "joint", "length", "rest"};
constexpr std::array<std::string_view, 7> part_keys{"bone", "shape", "size", "points", "color", "depth", "doc"};
constexpr std::array<std::string_view, 6> clip_keys{"frames", "loop", "ease", "angle", "rise", "doc"};
constexpr std::array<std::string_view, 8> impact_keys{"shake", "zoom", "flash", "spark", "sound", "rumble", "doc", "name"};
constexpr std::array<std::string_view, 6> spark_keys{"style", "size", "count", "frames", "color", "counter"};
constexpr std::array<std::string_view, 3> shape_names{"capsule", "ellipse", "polygon"};
constexpr std::array<std::string_view, 5> spark_names{"rays", "ring", "splash", "shards", "bolt"};
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
        if (p.shape == Shape::polygon && p.points.size() < 3) r.error(&t, "a polygon has three points or more", "points = [[0, -6], [30, -4], [30, 4]]");
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

    for (const Bone& b : look.bones)
        if (b.parent < 0 && &b != &look.bones.front()) r.error(&*root, std::format("bone `{}` has no parent; only the first bone is the root", b.name));
    if (!out.empty()) return std::unexpected(std::move(out));
    use_palette(look, 0);
    return look;
}

std::string write_look(const Look& look) {
    std::string s = std::format("gongfu = \"{}\"\nname = \"{}\"\noutline = {{ color = {}, width = {} }}\n", format_version, look.name,
                                colour_text(look.outline), look.outline_width);
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
    }
    constexpr std::array<std::string_view, 3> shapes{"capsule", "ellipse", "polygon"};
    for (const Part& p : look.parts) {
        s += std::format("\n[[part]]\nbone = \"{}\"\nshape = \"{}\"\n", look.bones.at(static_cast<std::size_t>(p.bone)).name, shapes[static_cast<std::size_t>(p.shape)]);
        if (p.shape == Shape::polygon) {
            s += "points = [";
            for (std::size_t i = 0; i < p.points.size(); ++i) s += std::format("{}[{}, {}]", i ? ", " : "", p.points[i].x, p.points[i].y);
            s += "]\n";
        } else {
            s += std::format("size = [{}, {}]\n", p.start, p.end);
        }
        s += std::format("color = {}\n", p.slot >= 0 ? std::format("\"{}\"", look.slots.at(static_cast<std::size_t>(p.slot))) : colour_text(p.color));
        if (p.depth != 0) s += std::format("depth = {}\n", p.depth);
    }
    for (const auto& [name, c] : look.clips) {
        const hanim::Ease ease = common_ease(c);
        s += std::format("\n[clip.{}]\nframes = {}\n", key_text(name), c.frames);
        if (c.loop) s += "loop = true\n";
        s += std::format("ease = \"{}\"\n", hanim::to_string(ease));
        if (!c.rise.empty()) s += std::format("rise = {}\n", keys_text(c.rise, ease));
        for (std::size_t b = 0; b < c.angles.size() && b < look.bones.size(); ++b)
            if (!c.angles[b].empty()) s += std::format("angle.{} = {}\n", key_text(look.bones[b].name), keys_text(c.angles[b], ease));
    }
    constexpr std::array<std::string_view, 5> sparks{"rays", "ring", "splash", "shards", "bolt"};
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
    return s;
}

}  // namespace gongfu::present2d
