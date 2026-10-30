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
import openmui.math;
import gongfu;
import gongfu.data;
import hanim;
import openmui.geom;
import openmui.render.vector;
import tomlplusplus;

namespace gongfu::present2d {

namespace {

using data::Diagnostic;

constexpr std::string_view format_version = "0.1";
constexpr std::array<std::string_view, 27> look_keys{"gongfu", "name", "outline", "accent", "trail", "glow", "footfall", "pane", "clone", "rewind", "palette", "bone", "part", "clip", "impact",
                                                     "glyph", "cinematic", "projectile", "doc", "light", "picture", "art_scale", "grip", "fx", "cutin", "pose", "material"};
constexpr std::array<std::string_view, 7> grip_keys{"name", "upper", "lower", "target", "at", "across", "bend"};
constexpr std::array<std::string_view, 4> picture_keys{"name", "file", "size", "relief"};
constexpr std::array<std::string_view, 4> relief_keys{"bevel", "detail", "smooth", "strength"};
constexpr std::array<std::string_view, 9> material_keys{"bands", "softness", "shadow", "shadow_strength", "rim", "rim_strength", "rim_width", "light_z", "doc"};
constexpr std::array<std::string_view, 6> light_keys{"from", "shade", "darken", "shadow", "rim", "rim_color"};
constexpr std::array<std::string_view, 4> follow_keys{"lag", "amount", "max", "drag"};
constexpr std::array<std::string_view, 3> trail_keys{"color", "frames", "alpha"};
constexpr std::array<std::string_view, 2> glow_keys{"color", "width"};
constexpr std::array<std::string_view, 3> footfall_keys{"every", "dip", "frames"};
constexpr std::array<std::string_view, 7> pane_keys{"glass", "edge", "shine", "reflect", "picture", "whole", "cracked"};
constexpr std::array<std::string_view, 1> clone_keys{"tint"};
constexpr std::array<std::string_view, 4> rewind_keys{"color", "ghosts", "frames", "mark"};
constexpr std::array<std::string_view, 3> stop_keys{"from", "to", "tint"};
constexpr std::array<std::string_view, 5> clock_keys{"from", "to", "at", "size", "color"};
constexpr std::array<std::string_view, 6> projectile_look_keys{"glyph", "size", "color", "halo", "spin", "fx"};
constexpr std::array<std::string_view, 6> hold_keys{"from", "to", "bone", "along", "body", "clip"};
constexpr std::array<std::string_view, 9> pose_keys{"from", "aim", "rise", "grip", "grip_at", "grip_across", "swap", "show", "doc"};
constexpr std::array<std::string_view, 3> fx_keys{"sprite", "stroke", "spray"};
constexpr std::array<std::string_view, 12> fx_sprite_keys{"picture", "source", "at", "to", "lift", "from", "frames", "size", "turn", "alpha", "ease", "color"};
constexpr std::array<std::string_view, 9> fx_stroke_keys{"picture", "source", "points", "width", "from", "draw", "hold", "fade", "color"};
constexpr std::array<std::string_view, 12> fx_spray_keys{"picture", "sources", "at", "from", "frames", "count", "speed", "angle", "size", "gravity", "seed", "color"};
constexpr std::array<std::string_view, 11> panel_keys{"picture", "source", "from", "to", "in", "out", "y", "height", "slant", "pan", "band"};
constexpr std::array<std::string_view, 2> stroke_keys{"points", "width"};
constexpr std::array<std::string_view, 29> cinematic_keys{"attacker", "defender", "backdrop", "camera", "glyph", "seal", "flash", "scanlines", "attacker_path",
                                                          "defender_path", "quake", "ring", "prism", "cracks", "fall", "stop", "clock", "doc", "panel", "fx",
                                                          "hide", "reactions", "hold", "attacker_turn", "defender_turn", "hitstop", "afterimages",
                                                          "in_front", "behind"};
constexpr std::array<std::string_view, 5> prism_keys{"images", "spread", "from", "to", "tint"};
constexpr std::array<std::string_view, 1> cracks_keys{"color"};
constexpr std::array<std::string_view, 3> fall_keys{"frame", "count", "color"};
constexpr std::array<std::string_view, 4> backdrop_keys{"color", "in", "out", "clouds"};
constexpr std::array<std::string_view, 3> clouds_keys{"color", "count", "between"};
constexpr std::array<std::string_view, 6> written_keys{"name", "at", "size", "from", "ends", "color"};
constexpr std::array<std::string_view, 4> seal_keys{"frame", "at", "size", "color"};
constexpr std::array<std::string_view, 2> outline_keys{"color", "width"};
constexpr std::array<std::string_view, 9> bone_keys{"name", "parent", "joint", "length", "rest", "limb", "prop", "follow", "hang"};
constexpr std::array<std::string_view, 22> part_keys{"bone", "shape", "size", "points", "color", "depth", "doc", "name", "variants", "shade",
                                                    "picture", "source", "anchor", "tip", "mesh", "bend", "blend", "fit", "drawings", "mirror", "shown", "material"};
constexpr std::array<std::string_view, 14> clip_keys{"frames", "loop", "ease", "angle", "rise", "doc", "swap", "grip", "aim", "grip_at", "grip_across", "fx", "poses", "show"};
constexpr std::array<std::string_view, 11> impact_keys{"shake", "zoom", "flash", "spark", "sound", "rumble", "doc", "name", "fx", "fx_counter", "fx_punish"};
constexpr std::array<std::string_view, 6> spark_keys{"style", "size", "count", "frames", "color", "counter"};
constexpr std::array<std::string_view, 5> shape_names{"capsule", "ellipse", "polygon", "curve", "image"};
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
    // [x, y, width, height] of numbers.
    std::optional<omui::geom::Rect> rect(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        const toml::array* a = n->as_array();
        if (!a || a->size() != 4) {
            error(n, std::format("{} is [x, y, width, height]", what));
            return std::nullopt;
        }
        std::array<double, 4> v{};
        for (std::size_t i = 0; i < 4; ++i) {
            auto x = number(a->get(i), what);
            if (!x) return std::nullopt;
            v[i] = *x;
        }
        if (v[2] <= 0 || v[3] <= 0) {
            error(n, std::format("{} has a width and a height", what));
            return std::nullopt;
        }
        return omui::geom::Rect{static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3])};
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
    for (const auto& t : c.grips) count(t);
    for (const auto& t : c.aims) count(t);
    for (const auto& t : c.grip_at) count(t);
    for (const auto& t : c.grip_across) count(t);
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
    if (auto v = r.number(root->get("art_scale"), "`art_scale`", 0, 100)) look.art_scale = static_cast<float>(*v);
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
        if (auto h = r.boolean(t.get("hang"), "a bone's `hang`")) {
            b.hang = *h;
            if (b.hang && b.parent < 0) r.error(t.get("hang"), "the root cannot hang: it is the body");
        }
        if (const toml::table* f = r.table_of(t.get("follow"), "a bone's `follow`")) {
            r.only(*f, follow_keys, "a bone's `follow`");
            if (auto v = r.integer(f->get("lag"), "a follow's `lag`", 1, 30)) b.follow.lag = *v;
            if (auto v = r.number(f->get("amount"), "a follow's `amount`", -2, 2)) b.follow.amount = static_cast<float>(*v);
            if (auto v = r.number(f->get("max"), "a follow's `max`", 0, 180)) b.follow.max = static_cast<float>(*v);
            if (auto v = r.number(f->get("drag"), "a follow's `drag`", -10, 10)) b.follow.drag = static_cast<float>(*v);
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

    // Grips: a two-bone chain whose lower bone ends on a point of another bone.
    r.tables(root->get("grip"), "grip", "[[grip]] name = \"hands\" upper = \"upper_back\" lower = \"fore_back\" target = \"brush\" at = 12", [&](const toml::table& t) {
        r.only(t, grip_keys, "a grip");
        Grip g;
        if (auto n = r.text(t.get("name"), "a grip's `name`")) g.name = *n;
        else r.error(&t, "a grip has a `name`");
        if (std::ranges::any_of(look.grips, [&](const Grip& o) { return o.name == g.name; })) r.error(t.get("name"), std::format("two grips are called `{}`", g.name));
        if (auto b = bone_index(t.get("upper"), "a grip's `upper`")) g.upper = *b;
        if (auto b = bone_index(t.get("lower"), "a grip's `lower`")) g.lower = *b;
        if (const toml::node* tn = t.get("target"); tn && tn->value<std::string>() == "ground") g.target = -1;
        else if (auto b = bone_index(tn, "a grip's `target`")) g.target = *b;
        if (!t.get("upper") || !t.get("lower") || !t.get("target")) r.error(&t, "a grip has an `upper`, a `lower` and a `target` bone");
        if (auto v = r.number(t.get("at"), "a grip's `at`", -10000, 10000)) g.at = static_cast<float>(*v);
        if (auto v = r.number(t.get("across"), "a grip's `across`", -10000, 10000)) g.across = static_cast<float>(*v);
        if (auto v = r.number(t.get("bend"), "a grip's `bend`", -1, 1)) {
            if (*v != 1 && *v != -1) r.error(t.get("bend"), "a grip's `bend` is 1 or -1: the way its middle joint points");
            else g.bend = static_cast<float>(*v);
        }
        if (g.upper >= 0 && g.lower >= 0) {
            const Bone& up = look.bones[static_cast<std::size_t>(g.upper)];
            const Bone& low = look.bones[static_cast<std::size_t>(g.lower)];
            if (low.parent != g.upper) r.error(t.get("lower"), std::format("grip `{}`: `{}` is not a child of `{}`", g.name, low.name, up.name));
            if (up.hang || low.hang) r.error(&t, std::format("grip `{}`: a hanging bone cannot grip", g.name));
            for (std::int32_t b = g.target; b >= 0; b = look.bones[static_cast<std::size_t>(b)].parent)
                if (b == g.upper) {
                    r.error(t.get("target"), std::format("grip `{}` reaches for `{}`, which hangs from its own chain", g.name, look.bones[static_cast<std::size_t>(g.target)].name));
                    break;
                }
        }
        look.grips.push_back(std::move(g));
    });

    r.tables(root->get("picture"), "picture", "[[picture]] name = \"body\" file = \"mo/parts.webp\" size = [2048, 2048]", [&](const toml::table& t) {
        r.only(t, picture_keys, "a picture");
        Picture pic;
        if (auto n = r.text(t.get("name"), "a picture's `name`")) pic.name = *n;
        else r.error(&t, "a picture has a `name`");
        if (auto f = r.text(t.get("file"), "a picture's `file`")) pic.file = *f;
        else r.error(&t, "a picture has a `file`");
        if (auto sz = r.pair(t.get("size"), "a picture's `size`", "size = [2048, 2048]: its pixels")) {
            pic.width = static_cast<std::int32_t>((*sz)[0]);
            pic.height = static_cast<std::int32_t>((*sz)[1]);
        }
        if (const toml::table* rt = r.table_of(t.get("relief"), "a picture's `relief`")) {
            r.only(*rt, relief_keys, "a picture's `relief`");
            Picture::Relief rel;
            if (auto v = r.number(rt->get("bevel"), "a relief's `bevel`", 0, 256)) rel.bevel = static_cast<float>(*v);
            if (auto v = r.number(rt->get("detail"), "a relief's `detail`", 0, 256)) rel.detail = static_cast<float>(*v);
            if (auto v = r.integer(rt->get("smooth"), "a relief's `smooth`", 0, 32)) rel.smooth = *v;
            if (auto v = r.number(rt->get("strength"), "a relief's `strength`", 0, 8)) rel.strength = static_cast<float>(*v);
            pic.relief = rel;
        }
        if (std::ranges::any_of(look.pictures, [&](const Picture& o) { return o.name == pic.name; })) r.error(t.get("name"), std::format("two pictures are called `{}`", pic.name));
        look.pictures.push_back(std::move(pic));
    });

    auto picture_index = [&](const toml::node* n, std::string_view what) -> std::int32_t {
        auto pn = r.text(n, what);
        if (!pn) return -1;
        auto it = std::ranges::find(look.pictures, *pn, &Picture::name);
        if (it != look.pictures.end()) return static_cast<std::int32_t>(it - look.pictures.begin());
        std::vector<std::string_view> names;
        for (const Picture& o : look.pictures) names.push_back(o.name);
        r.error(n, std::format("no picture is called `{}`", *pn), names.empty() ? "[[picture]] name = … first" : data::nearest_hint(*pn, names));
        return -1;
    };
    auto inside = [&](const toml::node* n, std::int32_t picture, const omui::geom::Rect& src) {
        if (picture < 0) return;
        const Picture& pic = look.pictures[static_cast<std::size_t>(picture)];
        if (pic.width > 0 && (src.x < 0 || src.y < 0 || src.x + src.width > static_cast<float>(pic.width) || src.y + src.height > static_cast<float>(pic.height)))
            r.error(n, std::format("`source` is not inside picture `{}` ({} × {})", pic.name, pic.width, pic.height));
    };
    auto point = [&](const toml::node* n, std::string_view what, Point& out) {
        if (auto xy = r.pair(n, what, "[forward, up]")) out = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
    };
    auto range = [&](const toml::node* n, std::string_view what, std::array<float, 2>& out) {
        if (auto v = r.pair(n, what, "[from, to]")) out = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1])};
    };

    // Effects: sprites, strokes and sprays of the look's pictures.
    if (const toml::node* fn = root->get("fx")) {
        if (const toml::table* ft = fn->as_table()) {
            for (const auto& [name, node] : *ft) {
                const toml::table* t = node.as_table();
                const std::string what = std::format("effect `{}`", name.str());
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[[fx.splash.sprite]] picture = \"fx\" source = [0, 0, 64, 64]");
                    continue;
                }
                r.only(*t, fx_keys, what);
                Fx fx;
                r.tables(t->get("sprite"), "sprite", "[[fx.splash.sprite]] picture = \"fx\" source = [0, 0, 64, 64] size = [20, 90]", [&](const toml::table& e) {
                    r.only(e, fx_sprite_keys, std::format("a sprite of {}", what));
                    FxSprite sp;
                    sp.picture = picture_index(e.get("picture"), "a sprite's `picture`");
                    if (auto v = r.rect(e.get("source"), "a sprite's `source`")) sp.source = *v, inside(e.get("source"), sp.picture, *v);
                    point(e.get("at"), "a sprite's `at`", sp.at);
                    if (e.get("to")) {
                        Point to;
                        point(e.get("to"), "a sprite's `to`", to);
                        sp.to = to;
                    }
                    if (auto v = r.number(e.get("lift"), "a sprite's `lift`", -10000, 10000)) sp.lift = static_cast<float>(*v);
                    if (auto v = r.integer(e.get("from"), "a sprite's `from`", 0, 6000)) sp.from = *v;
                    if (auto v = r.integer(e.get("frames"), "a sprite's `frames`", 1, 6000)) sp.frames = *v;
                    range(e.get("size"), "a sprite's `size`", sp.size);
                    range(e.get("turn"), "a sprite's `turn`", sp.turn);
                    range(e.get("alpha"), "a sprite's `alpha`", sp.alpha);
                    if (auto v = r.text(e.get("ease"), "a sprite's `ease`")) {
                        if (auto parsed = hanim::parse_ease(*v)) sp.ease = *parsed;
                        else r.error(e.get("ease"), std::format("`{}` is not an ease", *v));
                    }
                    if (sp.picture < 0 && !e.get("picture")) r.error(&e, "a sprite names its `picture`");
                    if (e.get("color")) sp.color = r.colour(e.get("color"), "a sprite's `color`");
                    fx.sprites.push_back(sp);
                });
                r.tables(t->get("stroke"), "stroke", "[[fx.slash.stroke]] picture = \"fx\" source = [0, 0, 400, 60] points = [[0, 0], [60, 40], [120, 0]]", [&](const toml::table& e) {
                    r.only(e, fx_stroke_keys, std::format("a stroke of {}", what));
                    FxStroke st;
                    st.picture = picture_index(e.get("picture"), "a stroke's `picture`");
                    if (auto v = r.rect(e.get("source"), "a stroke's `source`")) st.source = *v, inside(e.get("source"), st.picture, *v);
                    if (const toml::array* a = e.get("points") ? e.get("points")->as_array() : nullptr) {
                        for (const toml::node& q : *a) {
                            Point pt;
                            point(&q, "a stroke's point", pt);
                            st.points.push_back(pt);
                        }
                    }
                    if (st.points.size() < 2) r.error(&e, "a stroke has two `points` or more", "points = [[0, 0], [60, 40], [120, 0]]");
                    if (auto v = r.number(e.get("width"), "a stroke's `width`", 0, 2000)) st.width = static_cast<float>(*v);
                    if (auto v = r.integer(e.get("from"), "a stroke's `from`", 0, 6000)) st.from = *v;
                    if (auto v = r.integer(e.get("draw"), "a stroke's `draw`", 0, 600)) st.draw = *v;
                    if (auto v = r.integer(e.get("hold"), "a stroke's `hold`", 0, 6000)) st.hold = *v;
                    if (auto v = r.integer(e.get("fade"), "a stroke's `fade`", 0, 600)) st.fade = *v;
                    if (e.get("color")) st.color = r.colour(e.get("color"), "a stroke's `color`");
                    fx.strokes.push_back(std::move(st));
                });
                r.tables(t->get("spray"), "spray", "[[fx.drops.spray]] picture = \"fx\" sources = [[0, 0, 16, 16]] count = 8", [&](const toml::table& e) {
                    r.only(e, fx_spray_keys, std::format("a spray of {}", what));
                    FxSpray sp;
                    sp.picture = picture_index(e.get("picture"), "a spray's `picture`");
                    if (const toml::array* a = e.get("sources") ? e.get("sources")->as_array() : nullptr) {
                        for (const toml::node& q : *a)
                            if (auto v = r.rect(&q, "a spray's source")) sp.sources.push_back(*v), inside(&q, sp.picture, *v);
                    }
                    if (sp.sources.empty()) r.error(&e, "a spray has `sources` = [[x, y, width, height], …]");
                    point(e.get("at"), "a spray's `at`", sp.at);
                    if (auto v = r.integer(e.get("from"), "a spray's `from`", 0, 6000)) sp.from = *v;
                    if (auto v = r.integer(e.get("frames"), "a spray's `frames`", 1, 6000)) sp.frames = *v;
                    if (auto v = r.integer(e.get("count"), "a spray's `count`", 1, 256)) sp.count = *v;
                    range(e.get("speed"), "a spray's `speed`", sp.speed);
                    range(e.get("angle"), "a spray's `angle`", sp.angle);
                    range(e.get("size"), "a spray's `size`", sp.size);
                    if (auto v = r.number(e.get("gravity"), "a spray's `gravity`", -100, 100)) sp.gravity = static_cast<float>(*v);
                    if (auto v = r.integer(e.get("seed"), "a spray's `seed`", 0, 0x7fffffff)) sp.seed = static_cast<std::uint32_t>(*v);
                    if (e.get("color")) sp.color = r.colour(e.get("color"), "a spray's `color`");
                    fx.sprays.push_back(std::move(sp));
                });
                look.fx.insert_or_assign(std::string(name.str()), std::move(fx));
            }
        } else {
            r.error(fn, "`fx` is a table of effects", "[[fx.splash.sprite]] …");
        }
    }
    auto fx_name = [&](const toml::node* n, std::string_view what) -> std::optional<std::string> {
        auto v = r.text(n, what);
        if (!v) return std::nullopt;
        if (look.fx.contains(*v)) return v;
        std::vector<std::string_view> names;
        for (const auto& [k, _] : look.fx) names.push_back(k);
        r.error(n, std::format("{} plays `{}`, which is no effect of the look", what, *v), names.empty() ? "[fx.<name>] … first" : data::nearest_hint(*v, names));
        return std::nullopt;
    };
    // [[frame, "name", forward, up(, scale)], …]
    auto fx_rows = [&](const toml::node* n, std::string_view what, std::vector<FxAt>& out) {
        if (!n) return;
        const toml::array* a = n->as_array();
        if (!a) {
            r.error(n, std::format("{}'s `fx` is a list", what), "fx = [[4, \"slash\", 40, 100]]: frame, effect, forward, up(, scale)");
            return;
        }
        for (const toml::node& e : *a) {
            const toml::array* row = e.as_array();
            if (!row || row->size() < 4 || row->size() > 5) {
                r.error(&e, std::format("an effect of {} is [frame, \"name\", forward, up(, scale)]", what));
                continue;
            }
            FxAt at;
            auto f = r.integer(row->get(0), "an effect's frame", 0, 6000);
            auto name = fx_name(row->get(1), std::format("{}", what));
            auto x = r.number(row->get(2), "an effect's place"), y = r.number(row->get(3), "an effect's place");
            if (row->size() == 5)
                if (auto k = r.number(row->get(4), "an effect's scale", 0.01, 100)) at.scale = static_cast<float>(*k);
            if (!f || !name || !x || !y) continue;
            at.frame = *f, at.name = *name, at.at = {static_cast<float>(*x), static_cast<float>(*y)};
            out.push_back(std::move(at));
        }
    };
    auto panel_of = [&](const toml::table& e, std::string_view what) {
        r.only(e, panel_keys, what);
        CinematicLook::Panel p;
        p.picture = picture_index(e.get("picture"), std::format("{}'s `picture`", what));
        if (!e.get("picture")) r.error(&e, std::format("{} names its `picture`", what));
        if (auto v = r.rect(e.get("source"), std::format("{}'s `source`", what))) p.source = *v, inside(e.get("source"), p.picture, *v);
        if (auto v = r.integer(e.get("from"), "a panel's `from`", 0, 6000)) p.from = *v;
        if (auto v = r.integer(e.get("to"), "a panel's `to`", 0, 6000)) p.to = *v;
        if (auto v = r.integer(e.get("in"), "a panel's `in`", 0, 600)) p.in = *v;
        if (auto v = r.integer(e.get("out"), "a panel's `out`", 0, 600)) p.out = *v;
        if (auto v = r.number(e.get("y"), "a panel's `y`", 0, 1)) p.y = static_cast<float>(*v);
        if (auto v = r.number(e.get("height"), "a panel's `height`", 0, 1)) p.height = static_cast<float>(*v);
        if (auto v = r.number(e.get("slant"), "a panel's `slant`", -1, 1)) p.slant = static_cast<float>(*v);
        if (auto v = r.pair(e.get("pan"), "a panel's `pan`", "pan = [0.5, 0]: source pixels a frame")) p.pan = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1])};
        if (auto c = r.colour(e.get("band"), "a panel's `band`")) p.band = *c;
        if (p.to < p.from) r.error(&e, std::format("{} ends (`to`) before it begins (`from`)", what));
        return p;
    };
    if (const toml::node* cn = root->get("cutin")) {
        if (const toml::table* ct = cn->as_table()) {
            for (const auto& [name, node] : *ct) {
                if (const toml::table* t = node.as_table()) look.cutins.insert_or_assign(std::string(name.str()), panel_of(*t, std::format("cut-in `{}`", name.str())));
                else r.error(&node, std::format("cut-in `{}` is a table", name.str()));
            }
        } else {
            r.error(cn, "`cutin` is a table of cut-ins, by a super's move");
        }
    }

    // Materials: how image parts are lit, by name.
    if (const toml::node* mn = root->get("material")) {
        if (const toml::table* mt = mn->as_table()) {
            for (const auto& [name, node] : *mt) {
                const std::string what = std::format("material `{}`", name.str());
                const toml::table* t = node.as_table();
                if (!t) {
                    r.error(&node, std::format("{} is a table", what), "[material.body] bands = 2 softness = 0.1 shadow_strength = 0.5");
                    continue;
                }
                r.only(*t, material_keys, what);
                omui::render::Material m;
                if (auto v = r.integer(t->get("bands"), std::format("{}'s `bands`", what), 0, 16)) m.bands = *v;
                if (auto v = r.number(t->get("softness"), std::format("{}'s `softness`", what), 0, 1)) m.softness = static_cast<float>(*v);
                if (auto c = r.colour(t->get("shadow"), std::format("{}'s `shadow`", what))) m.shadow = *c;
                if (auto v = r.number(t->get("shadow_strength"), std::format("{}'s `shadow_strength`", what), 0, 1)) m.shadow_strength = static_cast<float>(*v);
                if (auto c = r.colour(t->get("rim"), std::format("{}'s `rim`", what))) m.rim = *c;
                if (auto v = r.number(t->get("rim_strength"), std::format("{}'s `rim_strength`", what), 0, 4)) m.rim_strength = static_cast<float>(*v);
                if (auto v = r.number(t->get("rim_width"), std::format("{}'s `rim_width`", what), 0, 1)) m.rim_width = static_cast<float>(*v);
                if (auto v = r.number(t->get("light_z"), std::format("{}'s `light_z`", what), 0, 8)) m.light_z = static_cast<float>(*v);
                if (name.str() == "none") r.error(&node, "a material cannot be called `none`: a part's `material = \"none\"` is unlit");
                look.materials.insert_or_assign(std::string(name.str()), m);
            }
        } else {
            r.error(mn, "`material` is a table of materials", "[material.body] bands = 2 softness = 0.1");
        }
    }

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
        if (p.shape == Shape::image) {
            if (auto pn = r.text(t.get("picture"), "an image part's `picture`")) {
                auto it = std::ranges::find(look.pictures, *pn, &Picture::name);
                if (it == look.pictures.end()) {
                    std::vector<std::string_view> names;
                    for (const Picture& o : look.pictures) names.push_back(o.name);
                    r.error(t.get("picture"), std::format("no picture is called `{}`", *pn), names.empty() ? "[[picture]] name = … first" : data::nearest_hint(*pn, names));
                } else {
                    p.picture = static_cast<std::int32_t>(it - look.pictures.begin());
                }
            } else {
                r.error(&t, "an image part names its `picture`");
            }
            if (const toml::array* a = t.get("source") ? t.get("source")->as_array() : nullptr; a && a->size() == 4) {
                std::array<double, 4> v{};
                for (std::size_t i = 0; i < 4; ++i) v[i] = r.number(a->get(i), "a `source` value").value_or(0);
                p.source = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3])};
                if (p.picture >= 0) {
                    const Picture& pic = look.pictures[static_cast<std::size_t>(p.picture)];
                    if (v[2] <= 0 || v[3] <= 0 || (pic.width > 0 && (v[0] < 0 || v[1] < 0 || v[0] + v[2] > pic.width || v[1] + v[3] > pic.height)))
                        r.error(t.get("source"), std::format("`source` is not inside picture `{}` ({} × {})", pic.name, pic.width, pic.height));
                }
            } else {
                r.error(&t, "an image part has a `source` = [x, y, width, height] in its picture");
            }
            if (auto xy = r.pair(t.get("anchor"), "a part's `anchor`", "anchor = [40, 12]: where the bone's joint is, in the picture")) p.anchor = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
            if (auto xy = r.pair(t.get("tip"), "a part's `tip`", "tip = [40, 120]: where the bone ends, in the picture")) p.tip = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
            if (auto v = r.boolean(t.get("mirror"), "a part's `mirror`")) p.mirror = *v;
            if (const toml::node* dn = t.get("drawings")) {
                const toml::array* a = dn->as_array();
                if (!a || !a->is_array_of_tables()) r.error(dn, "an image part's `drawings` is a list of { picture, source, anchor, tip }");
                else
                    for (const toml::node& e : *a) {
                        const toml::table& dt = *e.as_table();
                        Part::Drawing d;
                        d.picture = p.picture;
                        if (dt.get("picture")) {
                            auto it = std::ranges::find(look.pictures, dt.get("picture")->value_or(std::string()), &Picture::name);
                            if (it == look.pictures.end()) r.error(dt.get("picture"), "a drawing's `picture` is no picture of the look");
                            else d.picture = static_cast<std::int32_t>(it - look.pictures.begin());
                        }
                        if (auto v = r.rect(dt.get("source"), "a drawing's `source`")) d.source = *v;
                        else if (!dt.get("source")) r.error(&dt, "a drawing has a `source`");
                        if (auto xy = r.pair(dt.get("anchor"), "a drawing's `anchor`", "anchor = [40, 12]")) d.anchor = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
                        if (auto xy = r.pair(dt.get("tip"), "a drawing's `tip`", "tip = [40, 120]")) d.tip = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
                        p.drawings.push_back(d);
                    }
            }
            if (auto m = r.pair(t.get("mesh"), "a part's `mesh`", "mesh = [1, 4]: columns, rows"))
                p.mesh = {std::clamp(static_cast<std::int32_t>((*m)[0]), 1, 32), std::clamp(static_cast<std::int32_t>((*m)[1]), 1, 32)};
            if (const toml::array* a = t.get("bend") ? t.get("bend")->as_array() : nullptr) {
                for (const toml::node& n : *a)
                    if (auto b = bone_index(&n, "a bone of `bend`")) p.bend.push_back(*b);
            } else if (t.get("bend")) {
                r.error(t.get("bend"), "`bend` is a list of bones", "bend = [\"fore_front\"]");
            }
            if (auto v = r.number(t.get("blend"), "a part's `blend`", 0, 1000)) p.blend = static_cast<float>(*v);
            if (auto v = r.boolean(t.get("fit"), "a part's `fit`")) p.fit = *v;
        }
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
        if (auto v = r.boolean(t.get("shown"), "a part's `shown`")) p.shown = *v;
        if (auto m = r.text(t.get("material"), "a part's `material`")) {
            p.material = *m;
            if (p.shape != Shape::image) r.error(t.get("material"), "a `material` lights an image part (its picture's relief)");
            if (*m != "none" && !look.materials.contains(*m)) {
                std::vector<std::string_view> names{"none"};
                for (const auto& [k, v] : look.materials) names.push_back(k);
                r.error(t.get("material"), std::format("no material is called `{}`", *m), data::nearest_hint(*m, names));
            }
        }
        look.parts.push_back(std::move(p));
    });

    // Key poses: read by name, then each made whole along its `from`s.
    auto pose_of = [&](const toml::table& t, std::string_view what) {
        KeyPose kp;
        r.only(t, pose_keys, what);
        if (auto f = r.text(t.get("from"), std::format("{}'s `from`", what))) kp.from = *f;
        if (auto v = r.number(t.get("rise"), std::format("{}'s `rise`", what))) kp.rise = *v;
        auto named = [&](std::string_view key, auto&& index, std::map<std::int32_t, double>& out) {
            const toml::node* n = t.get(key);
            if (!n) return;
            const toml::table* tt = n->as_table();
            if (!tt) {
                r.error(n, std::format("{}'s `{}` is a table", what, key), std::format("{} = {{ torso = 90 }}", key));
                return;
            }
            for (const auto& [k, v] : *tt) {
                const std::int32_t i = index(k.str());
                if (i < 0) {
                    r.out.push_back({r.file, static_cast<int>(k.source().begin.line), static_cast<int>(k.source().begin.column),
                                     std::format("{}'s `{}` names `{}`, which the look does not have", what, key, k.str())});
                    continue;
                }
                if (auto x = r.number(&v, std::format("{}'s `{}.{}`", what, key, k.str()))) out[i] = *x;
            }
        };
        auto bone_at = [&](std::string_view n) -> std::int32_t {
            auto it = std::ranges::find(look.bones, n, &Bone::name);
            return it == look.bones.end() ? -1 : static_cast<std::int32_t>(it - look.bones.begin());
        };
        auto grip_at = [&](std::string_view n) -> std::int32_t {
            auto it = std::ranges::find(look.grips, n, &Grip::name);
            return it == look.grips.end() ? -1 : static_cast<std::int32_t>(it - look.grips.begin());
        };
        auto part_at = [&](std::string_view n) -> std::int32_t {
            auto it = std::ranges::find(look.parts, n, &Part::name);
            return it == look.parts.end() ? -1 : static_cast<std::int32_t>(it - look.parts.begin());
        };
        named("aim", bone_at, kp.aim);
        named("grip", grip_at, kp.grip);
        named("grip_at", grip_at, kp.grip_at);
        named("grip_across", grip_at, kp.grip_across);
        named("swap", part_at, kp.swap);
        named("show", part_at, kp.show);
        return kp;
    };
    if (const toml::node* pn = root->get("pose")) {
        if (const toml::table* pt = pn->as_table()) {
            for (const auto& [name, node] : *pt) {
                if (const toml::table* t = node.as_table()) look.poses.insert_or_assign(std::string(name.str()), pose_of(*t, std::format("pose `{}`", name.str())));
                else r.error(&node, std::format("pose `{}` is a table", name.str()), "[pose.stance] aim = { torso = 90 }");
            }
        } else {
            r.error(pn, "`pose` is a table of key poses", "[pose.stance] aim = { torso = 90 }");
        }
    }
    // A pose made whole: its `from`s' values under its own (a loop of `from`s is a problem).
    auto whole = [&](std::string_view name, const toml::node* at) -> std::optional<KeyPose> {
        KeyPose out;
        std::vector<std::string_view> seen;
        for (std::string_view n = name; !n.empty();) {
            if (std::ranges::find(seen, n) != seen.end()) {
                r.error(at, std::format("pose `{}` is built on itself (through `from`)", n));
                return std::nullopt;
            }
            seen.push_back(n);
            const auto it = look.poses.find(n);
            if (it == look.poses.end()) {
                std::vector<std::string_view> names;
                for (const auto& [k, _] : look.poses) names.push_back(k);
                r.error(at, std::format("no pose is called `{}`", n), names.empty() ? "[pose.<name>] … first" : data::nearest_hint(n, names));
                return std::nullopt;
            }
            const KeyPose& p = it->second;
            for (const auto& [k, v] : p.aim) out.aim.try_emplace(k, v);
            for (const auto& [k, v] : p.grip) out.grip.try_emplace(k, v);
            for (const auto& [k, v] : p.grip_at) out.grip_at.try_emplace(k, v);
            for (const auto& [k, v] : p.grip_across) out.grip_across.try_emplace(k, v);
            for (const auto& [k, v] : p.swap) out.swap.try_emplace(k, v);
            for (const auto& [k, v] : p.show) out.show.try_emplace(k, v);
            if (!out.rise && p.rise) out.rise = p.rise;
            n = p.from;
        }
        return out;
    };

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
                    if (auto parsed = hanim::parse_ease(*e)) ease = *parsed, c.ease = *parsed;
                    else r.error(t->get("ease"), std::format("`{}` is not an ease", *e), "linear, out_cubic, in_out_sine, cubic-bezier(…), steps(…)");
                }
                if (auto rise = r.keys(t->get("rise"), std::format("{}'s `rise`", what), ease, c.frames)) c.rise = std::move(*rise);
                if (const toml::node* sh = t->get("show")) {
                    if (const toml::table* st = sh->as_table()) {
                        c.shows.resize(look.parts.size());
                        for (const auto& [part, keys] : *st) {
                            auto it = std::ranges::find(look.parts, part.str(), &Part::name);
                            if (it == look.parts.end()) {
                                r.out.push_back({r.file, static_cast<int>(part.source().begin.line), static_cast<int>(part.source().begin.column),
                                                 std::format("{} shows `{}`, which is no part's name", what, part.str())});
                                continue;
                            }
                            if (auto k = r.keys(&keys, std::format("{}'s `show.{}`", what, part.str()), hanim::Ease::stepped(1, hanim::StepPosition::jump_end), c.frames))
                                c.shows[static_cast<std::size_t>(it - look.parts.begin())] = std::move(*k);
                        }
                    } else {
                        r.error(sh, std::format("{}'s `show` is a table of parts", what), "show.brush = [[0, 0], [6, 1]]");
                    }
                }
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
                                    if (const std::size_t n = it->shape == Shape::image ? it->drawings.size() : it->variants.size();
                                        key.value < 0 || key.value > static_cast<double>(n) || key.value != std::floor(key.value))
                                        r.error(&keys, std::format("{}'s `swap.{}` chooses {}: `{}` has {} other drawing(s) (0: its own)", what, part.str(), key.value, part.str(), n));
                                c.swaps[static_cast<std::size_t>(it - look.parts.begin())] = std::move(*k);
                            }
                        }
                    } else {
                        r.error(sw, std::format("{}'s `swap` is a table of parts", what), "swap.fist = [[0, 0], [4, 1], [10, 0]]");
                    }
                }
                // Tracks by grip (`grip.<name>`: how much it holds; `grip_at.<name>`, `grip_across.<name>`: where).
                auto grip_tracks = [&](std::string_view key, std::vector<hanim::Track<double>>& tracks, bool weight) {
                    const toml::node* gn = t->get(key);
                    if (!gn) return;
                    const toml::table* gt = gn->as_table();
                    if (!gt) {
                        r.error(gn, std::format("{}'s `{}` is a table of grips", what, key), std::format("{}.hands = [[0, 1], [4, 0]]", key));
                        return;
                    }
                    tracks.resize(look.grips.size());
                    for (const auto& [grip, keys] : *gt) {
                        auto it = std::ranges::find(look.grips, grip.str(), &Grip::name);
                        if (it == look.grips.end()) {
                            std::vector<std::string_view> names;
                            for (const Grip& o : look.grips) names.push_back(o.name);
                            r.out.push_back({r.file, static_cast<int>(grip.source().begin.line), static_cast<int>(grip.source().begin.column),
                                             std::format("{} holds `{}`, which is no grip", what, grip.str()), data::nearest_hint(grip.str(), names)});
                            continue;
                        }
                        if (auto k = r.keys(&keys, std::format("{}'s `{}.{}`", what, key, grip.str()), ease, c.frames)) {
                            if (weight)
                                for (const auto& kk : k->keys())
                                    if (kk.value < 0 || kk.value > 1) r.error(&keys, std::format("{}'s `{}.{}` holds {}: 0 (let go) to 1 (held)", what, key, grip.str(), kk.value));
                            tracks[static_cast<std::size_t>(it - look.grips.begin())] = std::move(*k);
                        }
                    }
                };
                grip_tracks("grip", c.grips, true);
                grip_tracks("grip_at", c.grip_at, false);
                grip_tracks("grip_across", c.grip_across, false);
                // Tracks by bone (`angle.<bone>`: to its parent, from its rest; `aim.<bone>`: to the world).
                auto bone_tracks = [&](std::string_view key, std::vector<hanim::Track<double>>& tracks) {
                    const toml::node* an = t->get(key);
                    if (!an) return;
                    const toml::table* at = an->as_table();
                    if (!at) {
                        r.error(an, std::format("{}'s `{}` is a table of bones", what, key), std::format("{}.torso = [[0, 0], [6, 10]]", key));
                        return;
                    }
                    tracks.resize(look.bones.size());
                    for (const auto& [bone, keys] : *at) {
                        auto it = std::ranges::find(look.bones, bone.str(), &Bone::name);
                        if (it == look.bones.end()) {
                            std::vector<std::string_view> names;
                            for (const Bone& o : look.bones) names.push_back(o.name);
                            r.out.push_back({r.file, static_cast<int>(bone.source().begin.line), static_cast<int>(bone.source().begin.column),
                                             std::format("{} turns `{}`, which is no bone", what, bone.str()), data::nearest_hint(bone.str(), names)});
                            continue;
                        }
                        if (auto k = r.keys(&keys, std::format("{}'s `{}.{}`", what, key, bone.str()), ease, c.frames))
                            tracks[static_cast<std::size_t>(it - look.bones.begin())] = std::move(*k);
                    }
                };
                bone_tracks("angle", c.angles);
                bone_tracks("aim", c.aims);
                for (std::size_t b = 0; b < c.aims.size() && b < c.angles.size(); ++b)
                    if (!c.aims[b].empty() && !c.angles[b].empty())
                        r.error(t, std::format("{} both turns and aims `{}`: one or the other", what, look.bones[b].name));
                // Keyed by poses: each key the pose made whole, changed by the key's own table; the tracks they make.
                if (const toml::node* kn = t->get("poses")) {
                    const toml::array* a = kn->as_array();
                    if (!a) r.error(kn, std::format("{}'s `poses` is a list of keys", what), "poses = [[0, \"stance\"], [6, \"jab\", \"out_expo\"]]");
                    std::vector<std::pair<KeyPose, const toml::node*>> made;
                    static const toml::array none;
                    for (const toml::node& e : a ? *a : none) {
                        const toml::array* key = e.as_array();
                        if (!key || key->size() < 2 || key->size() > 4) {
                            r.error(&e, std::format("a key of {}'s `poses` is [frame, \"pose\"(, \"ease\")(, {{ changes }})]", what));
                            continue;
                        }
                        PoseKey pk;
                        auto f = r.integer(key->get(0), std::format("a key's frame of {}'s `poses`", what), 0, std::max(c.frames, 0));
                        auto n = r.text(key->get(1), "a key's pose");
                        if (!f || !n) continue;
                        if (!c.keys.empty() && *f <= c.keys.back().frame) r.error(key->get(0), std::format("{}: frame {} comes after frame {}", what, *f, c.keys.back().frame), "keys go in order of their frames");
                        pk.frame = *f, pk.pose = *n;
                        for (std::size_t i = 2; i < key->size(); ++i) {
                            if (const toml::table* ch = key->get(i)->as_table()) pk.change = pose_of(*ch, std::format("a key's changes in {}", what));
                            else if (auto e2 = r.text(key->get(i), "a key's ease")) {
                                if (auto parsed = hanim::parse_ease(*e2)) pk.ease = *parsed;
                                else r.error(key->get(i), std::format("`{}` is not an ease", *e2));
                            }
                        }
                        auto w = whole(pk.pose, key->get(1));
                        if (!w) continue;
                        if (!pk.change.from.empty()) r.error(key->get(1), "a key's changes have no `from` (the key names its pose)");
                        for (const auto& [k, v] : pk.change.aim) w->aim[k] = v;
                        for (const auto& [k, v] : pk.change.grip) w->grip[k] = v;
                        for (const auto& [k, v] : pk.change.grip_at) w->grip_at[k] = v;
                        for (const auto& [k, v] : pk.change.grip_across) w->grip_across[k] = v;
                        for (const auto& [k, v] : pk.change.swap) w->swap[k] = v;
                        for (const auto& [k, v] : pk.change.show) w->show[k] = v;
                        if (pk.change.rise) w->rise = pk.change.rise;
                        made.emplace_back(std::move(*w), &e);
                        c.keys.push_back(std::move(pk));
                    }
                    if (!made.empty()) {
                        // Every bone a key aims, every key aims (else the clip would not say where it is between).
                        std::set<std::int32_t> bones;
                        for (const auto& [kp, _] : made)
                            for (const auto& [b, v] : kp.aim) bones.insert(b);
                        auto track = [&](auto&& value, hanim::Ease step) {
                            hanim::Track<double> tr;
                            for (std::size_t i = 0; i < made.size(); ++i)
                                tr.key(hanim::seconds(c.keys[i].frame / 60.0), value(made[i].first), c.keys[i].ease.value_or(step));
                            return tr;
                        };
                        c.aims.resize(look.bones.size());
                        for (const std::int32_t b : bones) {
                            if (b < static_cast<std::int32_t>(c.angles.size()) && !c.angles[static_cast<std::size_t>(b)].empty())
                                r.error(kn, std::format("{} both turns `{}` and keys it by poses", what, look.bones[static_cast<std::size_t>(b)].name));
                            bool all = true;
                            for (const auto& [kp, node] : made)
                                if (!kp.aim.contains(b)) {
                                    r.error(node, std::format("{}: this key's pose does not aim `{}`, which another key aims", what, look.bones[static_cast<std::size_t>(b)].name),
                                            "give the pose (or its `from`) an angle for it");
                                    all = false;
                                }
                            if (all) c.aims[static_cast<std::size_t>(b)] = track([&](const KeyPose& kp) { return kp.aim.at(b); }, ease);
                        }
                        if (std::ranges::any_of(made, [](const auto& m) { return m.first.rise.has_value(); }))
                            c.rise = track([](const KeyPose& kp) { return kp.rise.value_or(0.0); }, ease);
                        auto by_grip = [&](std::map<std::int32_t, double> KeyPose::* field, std::vector<hanim::Track<double>>& out, auto&& fallback) {
                            std::set<std::int32_t> grips;
                            for (const auto& [kp, _] : made)
                                for (const auto& [g, v] : kp.*field) grips.insert(g);
                            if (grips.empty()) return;
                            out.resize(look.grips.size());
                            for (const std::int32_t g : grips)
                                out[static_cast<std::size_t>(g)] = track([&](const KeyPose& kp) {
                                    const auto it = (kp.*field).find(g);
                                    return it != (kp.*field).end() ? it->second : fallback(look.grips[static_cast<std::size_t>(g)]);
                                }, ease);
                        };
                        by_grip(&KeyPose::grip, c.grips, [](const Grip&) { return 1.0; });
                        by_grip(&KeyPose::grip_at, c.grip_at, [](const Grip& g) { return static_cast<double>(g.at); });
                        by_grip(&KeyPose::grip_across, c.grip_across, [](const Grip& g) { return static_cast<double>(g.across); });
                        std::set<std::int32_t> shown;
                        for (const auto& [kp, _] : made)
                            for (const auto& [k, v] : kp.show) shown.insert(k);
                        if (!shown.empty()) c.shows.resize(look.parts.size());
                        for (const std::int32_t k : shown) {
                            hanim::Track<double> tr;
                            for (std::size_t i = 0; i < made.size(); ++i) {
                                const auto it = made[i].first.show.find(k);
                                const double def = look.parts[static_cast<std::size_t>(k)].shown ? 1.0 : 0.0;
                                tr.key(hanim::seconds(c.keys[i].frame / 60.0), it != made[i].first.show.end() ? it->second : def, hanim::Ease::stepped(1, hanim::StepPosition::jump_end));
                            }
                            c.shows[static_cast<std::size_t>(k)] = std::move(tr);
                        }
                        std::set<std::int32_t> parts;
                        for (const auto& [kp, _] : made)
                            for (const auto& [k, v] : kp.swap) parts.insert(k);
                        if (!parts.empty()) c.swaps.resize(look.parts.size());
                        for (const std::int32_t k : parts) {
                            hanim::Track<double> tr;
                            for (std::size_t i = 0; i < made.size(); ++i) {
                                const auto it = made[i].first.swap.find(k);
                                tr.key(hanim::seconds(c.keys[i].frame / 60.0), it != made[i].first.swap.end() ? it->second : 0.0, hanim::Ease::stepped(1, hanim::StepPosition::jump_end));
                            }
                            c.swaps[static_cast<std::size_t>(k)] = std::move(tr);
                        }
                    }
                }
                fx_rows(t->get("fx"), what, c.fx);
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
                if (auto v = fx_name(t->get("fx"), what)) im.fx = *v;
                if (auto v = fx_name(t->get("fx_counter"), what)) im.fx_counter = *v;
                if (auto v = fx_name(t->get("fx_punish"), what)) im.fx_punish = *v;
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
        if (t->get("picture")) {
            look.pane.picture = picture_index(t->get("picture"), "a pane's `picture`");
            if (auto v = r.rect(t->get("whole"), "a pane's `whole`")) look.pane.whole = *v, inside(t->get("whole"), look.pane.picture, *v);
            else r.error(t, "a pane drawn from a picture has its `whole` region", "pane = { picture = \"fx\", whole = [x, y, w, h], cracked = [x, y, w, h] }");
            look.pane.cracked = look.pane.whole;
            if (auto v = r.rect(t->get("cracked"), "a pane's `cracked`")) look.pane.cracked = *v, inside(t->get("cracked"), look.pane.picture, *v);
        }
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
        if (const toml::node* m = t->get("mark"))
            if (auto v = fx_name(m, "a rewind's `mark`")) look.rewind.mark = *v;
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
                // Paths: [[frame, forward, up(, "ease")], …], each key's ease the way to it.
                auto path = [&](std::string_view key, std::vector<CinematicLook::PathKey>& out) {
                    const toml::node* n = t->get(key);
                    if (!n) return;
                    const toml::array* a = n->as_array();
                    if (!a) {
                        r.error(n, std::format("a cinematic's `{}` is a list", key), std::format("{} = [[0, 0, 0], [20, 60, 180, \"out_quad\"]]", key));
                        return;
                    }
                    for (const toml::node& e : *a) {
                        const toml::array* row = e.as_array();
                        if (!row || row->size() < 3 || row->size() > 4) {
                            r.error(&e, std::format("a `{}` key is [frame, forward, up] or [frame, forward, up, \"ease\"]", key));
                            continue;
                        }
                        auto f = r.integer(row->get(0), std::format("a `{}` key's frame", key), 0, 6000);
                        auto x = r.number(row->get(1), std::format("a `{}` key's forward", key));
                        auto y = r.number(row->get(2), std::format("a `{}` key's up", key));
                        if (!f || !x || !y) continue;
                        CinematicLook::PathKey k{static_cast<std::int32_t>(*f), {static_cast<float>(*x), static_cast<float>(*y)}, {}};
                        if (row->size() == 4)
                            if (auto name = r.text(row->get(3), "a path key's ease")) {
                                if (auto parsed = hanim::parse_ease(*name)) k.ease = *parsed;
                                else r.error(row->get(3), std::format("`{}` is not an ease", *name), "linear, out_quad, in_quad, in_out_sine, cubic-bezier(…)");
                            }
                        out.push_back(k);
                    }
                };
                path("attacker_path", c.attacker_path);
                path("defender_path", c.defender_path);
                // Spans of frames: [[from, to], …].
                auto spans = [&](std::string_view key, std::string_view hint, std::vector<std::array<std::int32_t, 2>>& out) {
                    rows(key, 2, [&](const std::vector<double>& v) {
                        if (key == "hitstop" ? v[1] < 0 : v[1] < v[0]) r.error(t->get(key), std::format("a `{}` ends after it begins", key), std::string(hint));
                        out.push_back({static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1])});
                    });
                };
                spans("attacker_turn", "attacker_turn = [[120, 170]]: frames the attacker faces the other way", c.attacker_turn);
                spans("defender_turn", "defender_turn = [[60, 90]]: frames the defender faces the other way", c.defender_turn);
                spans("hitstop", "hitstop = [[40, 8]]: from frame 40, 8 frames the struck stands", c.hitstops);
                spans("afterimages", "afterimages = [[60, 120]]: frames the attacker leaves afterimages", c.afterimages);
                spans("in_front", "in_front = [[28, 44]]: frames the attacker is drawn over the defender", c.in_front);
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
                r.tables(t->get("panel"), "panel", "[[cinematic.236236K.panel]] picture = \"cutins\" source = [0, 0, 1536, 512] from = 20 to = 60",
                         [&](const toml::table& e) { c.panels.push_back(panel_of(e, std::format("a panel of {}", what))); });
                fx_rows(t->get("fx"), what, c.fx);
                fx_rows(t->get("behind"), what, c.behind);
                if (const toml::node* rn = t->get("reactions")) {
                    if (const toml::array* a = rn->as_array()) {
                        for (const toml::node& e : *a)
                            if (auto v = r.text(&e, "a cinematic's reaction")) {
                                if (*v != "hitstun" && std::ranges::find(hit_reactions, *v) == hit_reactions.end() &&
                                    std::ranges::find(taken_reactions, *v) == taken_reactions.end()) {
                                    std::vector<std::string_view> known(hit_reactions.begin(), hit_reactions.end());
                                    known.insert(known.end(), taken_reactions.begin(), taken_reactions.end());
                                    r.error(&e, std::format("`{}` is not a hit reaction", *v), data::nearest_hint(*v, known));
                                }
                                c.reactions.push_back(*v);
                            }
                    } else {
                        r.error(rn, "a cinematic's `reactions` is a list of hit reactions", "reactions = [\"hitstun.body\", \"hitstun.head\"]");
                    }
                }
                r.tables(t->get("hold"), "hold", "hold = [{ from = 40, to = 80, bone = \"fore_front\", along = 30, body = [0, 120] }]", [&](const toml::table& e) {
                    r.only(e, hold_keys, std::format("a hold of {}", what));
                    CinematicLook::Hold h;
                    if (auto v = r.integer(e.get("from"), "a hold's `from`", 0, 6000)) h.from = *v;
                    if (auto v = r.integer(e.get("to"), "a hold's `to`", 0, 6000)) h.to = *v;
                    if (auto b = bone_index(e.get("bone"), "a hold's `bone`")) h.bone = *b;
                    else if (!e.get("bone")) r.error(&e, "a hold names the attacker's `bone` that holds");
                    if (auto v = r.number(e.get("along"), "a hold's `along`")) h.along = static_cast<float>(*v);
                    if (auto xy = r.pair(e.get("body"), "a hold's `body`", "body = [0, 120]: the defender's point held (forward, up)")) h.body = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
                    if (auto v = r.text(e.get("clip"), "a hold's `clip`")) h.clip = *v;
                    if (h.to <= h.from) r.error(&e, "a hold ends (`to`) after it begins (`from`)");
                    c.holds.push_back(std::move(h));
                });
                if (auto h = r.pair(t->get("hide"), "a cinematic's `hide`", "hide = [160, 196]: frames the attacker is not drawn"))
                    c.hide = {static_cast<std::int32_t>((*h)[0]), static_cast<std::int32_t>((*h)[1])};
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
                if (auto v = fx_name(t->get("fx"), what)) g.fx = *v;
                if (g.glyph.empty() && g.fx.empty()) r.error(t, std::format("{} is drawn as a `glyph` or an `fx`", what));
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

std::string fx_rows_text(const std::vector<FxAt>& rows, std::string_view key = "fx") {
    std::string s = std::format("{} = [", key);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const FxAt& a = rows[i];
        s += std::format("{}[{}, \"{}\", {}, {}", i ? ", " : "", a.frame, a.name, a.at.x, a.at.y);
        if (a.scale != 1) s += std::format(", {}", a.scale);
        s += "]";
    }
    return s + "]\n";
}

std::string rect_text(const omui::geom::Rect& r) { return std::format("[{}, {}, {}, {}]", r.x, r.y, r.width, r.height); }

std::string panel_text(const Look& look, const CinematicLook::Panel& p, std::string_view sep) {
    return std::format("picture = \"{}\"{}source = {}{}from = {}{}to = {}{}in = {}{}out = {}{}y = {}{}height = {}{}slant = {}{}pan = [{}, {}]{}band = {}",
                       p.picture >= 0 ? look.pictures.at(static_cast<std::size_t>(p.picture)).name : std::string(), sep, rect_text(p.source), sep, p.from, sep, p.to, sep,
                       p.in, sep, p.out, sep, p.y, sep, p.height, sep, p.slant, sep, p.pan.x, p.pan.y, sep, colour_text(p.band));
}

// A key pose's changes as TOML tables (`aim = { torso = 90 }`), `sep` between them.
std::string pose_text(const Look& look, const KeyPose& p, std::string_view sep) {
    std::vector<std::string> out;
    auto table = [&](std::string_view key, const std::map<std::int32_t, double>& m, auto&& name) {
        if (m.empty()) return;
        std::string t = std::format("{} = {{ ", key);
        bool first = true;
        for (const auto& [k, v] : m) t += std::format("{}{} = {}", first ? "" : ", ", key_text(name(k)), v), first = false;
        out.push_back(t + " }");
    };
    if (!p.from.empty()) out.push_back(std::format("from = \"{}\"", p.from));
    auto bone = [&](std::int32_t i) { return look.bones.at(static_cast<std::size_t>(i)).name; };
    auto grip = [&](std::int32_t i) { return look.grips.at(static_cast<std::size_t>(i)).name; };
    auto part = [&](std::int32_t i) { return look.parts.at(static_cast<std::size_t>(i)).name; };
    table("aim", p.aim, bone);
    if (p.rise) out.push_back(std::format("rise = {}", *p.rise));
    table("grip", p.grip, grip);
    table("grip_at", p.grip_at, grip);
    table("grip_across", p.grip_across, grip);
    table("swap", p.swap, part);
    table("show", p.show, part);
    std::string s;
    for (std::size_t i = 0; i < out.size(); ++i) s += (i ? std::string(sep) : std::string()) + out[i];
    return s;
}

// What a pose (made whole along its `from`s) sets: which tracks a clip keyed by it makes.
struct Sets {
    std::set<std::int32_t> aim, grip, grip_at, grip_across, swap, show;
    bool                   rise = false;
};
void sets_of(const Look& look, std::string_view name, const KeyPose& change, Sets& out) {
    auto add = [&](const KeyPose& p) {
        for (const auto& [k, v] : p.aim) out.aim.insert(k);
        for (const auto& [k, v] : p.grip) out.grip.insert(k);
        for (const auto& [k, v] : p.grip_at) out.grip_at.insert(k);
        for (const auto& [k, v] : p.grip_across) out.grip_across.insert(k);
        for (const auto& [k, v] : p.swap) out.swap.insert(k);
        for (const auto& [k, v] : p.show) out.show.insert(k);
        out.rise = out.rise || p.rise.has_value();
    };
    add(change);
    for (int guard = 0; !name.empty() && guard < 64; ++guard) {
        const auto it = look.poses.find(name);
        if (it == look.poses.end()) break;
        add(it->second);
        name = it->second.from;
    }
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
    if (look.art_scale > 0) s += std::format("art_scale = {}\n", look.art_scale);
    if (look.light.shade > 0)
        s += std::format("light = {{ from = [{}, {}], shade = {}, darken = {}, shadow = {}, rim = {}, rim_color = {} }}\n", look.light.from.x, look.light.from.y,
                         look.light.shade, look.light.darken, colour_text(look.light.shadow), look.light.rim, colour_text(look.light.rim_color));
    if (look.pane.glass != Look::Pane{}.glass || look.pane.edge != Look::Pane{}.edge || look.pane.shine != Look::Pane{}.shine || look.pane.reflect != Look::Pane{}.reflect ||
        look.pane.picture >= 0)
        s += std::format("pane = {{ glass = {}, edge = {}, shine = {}, reflect = {}{} }}\n", colour_text(look.pane.glass), colour_text(look.pane.edge),
                         colour_text(look.pane.shine), colour_text(look.pane.reflect),
                         look.pane.picture >= 0 ? std::format(", picture = \"{}\", whole = {}, cracked = {}", look.pictures.at(static_cast<std::size_t>(look.pane.picture)).name,
                                                              rect_text(look.pane.whole), rect_text(look.pane.cracked))
                                                : std::string());
    if (look.clone.tint != Look::CloneLook{}.tint) s += std::format("clone = {{ tint = {} }}\n", colour_text(look.clone.tint));
    if (look.rewind.ghosts > 0 || !look.rewind.mark.empty())
        s += std::format("rewind = {{ color = {}, ghosts = {}, frames = {}{} }}\n", colour_text(look.rewind.color), look.rewind.ghosts, look.rewind.frames,
                         look.rewind.mark.empty() ? std::string() : std::format(", mark = \"{}\"", look.rewind.mark));
    if (look.footfall.dip > 0) s += std::format("footfall = {{ every = {}, dip = {}, frames = {} }}\n", look.footfall.every, look.footfall.dip, look.footfall.frames);
    for (std::size_t p = 0; p < look.palettes.size(); ++p) {
        s += std::format("\n[[palette]]\nname = \"{}\"\n", p < look.palette_names.size() ? look.palette_names[p] : std::string());
        for (std::size_t k = 0; k < look.slots.size() && k < look.palettes[p].size(); ++k)
            s += std::format("{} = {}\n", key_text(look.slots[k]), colour_text(look.palettes[p][k]));
    }
    for (const Picture& pic : look.pictures) {
        s += std::format("\n[[picture]]\nname = \"{}\"\nfile = \"{}\"\nsize = [{}, {}]\n", pic.name, pic.file, pic.width, pic.height);
        if (pic.relief)
            s += std::format("relief = {{ bevel = {}, detail = {}, smooth = {}, strength = {} }}\n", pic.relief->bevel, pic.relief->detail, pic.relief->smooth,
                             pic.relief->strength);
    }
    for (const auto& [name, m] : look.materials)
        s += std::format("\n[material.{}]\nbands = {}\nsoftness = {}\nshadow = {}\nshadow_strength = {}\nrim = {}\nrim_strength = {}\nrim_width = {}\nlight_z = {}\n",
                         key_text(name), m.bands, m.softness, colour_text(m.shadow), m.shadow_strength, colour_text(m.rim), m.rim_strength, m.rim_width, m.light_z);
    for (const Bone& b : look.bones) {
        s += std::format("\n[[bone]]\nname = \"{}\"\n", b.name);
        if (b.parent >= 0) s += std::format("parent = \"{}\"\n", look.bones[static_cast<std::size_t>(b.parent)].name);
        s += std::format("joint = [{}, {}]\nlength = {}\n", b.joint.x, b.joint.y, b.length);
        if (b.rest != 0) s += std::format("rest = {}\n", b.rest);
        if (b.limb) s += "limb = true\n";
        if (b.prop) s += "prop = true\n";
        if (b.hang) s += "hang = true\n";
        if (b.follow.lag > 0) s += std::format("follow = {{ lag = {}, amount = {}, max = {}, drag = {} }}\n", b.follow.lag, b.follow.amount, b.follow.max, b.follow.drag);
    }
    constexpr std::array<std::string_view, 5> shapes{"capsule", "ellipse", "polygon", "curve", "image"};
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
        } else if (p.shape == Shape::image) {
            s += std::format("picture = \"{}\"\nsource = [{}, {}, {}, {}]\nanchor = [{}, {}]\ntip = [{}, {}]\nmesh = [{}, {}]\n",
                             p.picture >= 0 ? look.pictures.at(static_cast<std::size_t>(p.picture)).name : std::string(), p.source.x, p.source.y, p.source.width,
                             p.source.height, p.anchor.x, p.anchor.y, p.tip.x, p.tip.y, p.mesh[0], p.mesh[1]);
            if (!p.bend.empty()) {
                s += "bend = [";
                for (std::size_t i = 0; i < p.bend.size(); ++i) s += std::format("{}\"{}\"", i ? ", " : "", look.bones.at(static_cast<std::size_t>(p.bend[i])).name);
                s += std::format("]\nblend = {}\n", p.blend);
            }
            if (p.fit) s += "fit = true\n";
            if (p.mirror) s += "mirror = true\n";
            if (!p.drawings.empty()) {
                s += "drawings = [";
                for (std::size_t i = 0; i < p.drawings.size(); ++i) {
                    const Part::Drawing& d = p.drawings[i];
                    s += std::format("{}{{ picture = \"{}\", source = {}, anchor = [{}, {}], tip = [{}, {}] }}", i ? ", " : "", look.pictures.at(static_cast<std::size_t>(d.picture)).name,
                                     rect_text(d.source), d.anchor.x, d.anchor.y, d.tip.x, d.tip.y);
                }
                s += "]\n";
            }
        } else {
            s += std::format("size = [{}, {}]\n", p.start, p.end);
        }
        s += std::format("color = {}\n", p.slot >= 0 ? std::format("\"{}\"", look.slots.at(static_cast<std::size_t>(p.slot))) : colour_text(p.color));
        if (p.depth != 0) s += std::format("depth = {}\n", p.depth);
        if (!p.shown) s += "shown = false\n";
        if (!p.material.empty()) s += std::format("material = \"{}\"\n", p.material);
        if (!p.shade) s += "shade = false\n";
    }
    for (const Grip& g : look.grips) {
        s += std::format("\n[[grip]]\nname = \"{}\"\nupper = \"{}\"\nlower = \"{}\"\ntarget = \"{}\"\nat = {}\n", g.name, look.bones.at(static_cast<std::size_t>(g.upper)).name,
                         look.bones.at(static_cast<std::size_t>(g.lower)).name, g.target < 0 ? std::string("ground") : look.bones.at(static_cast<std::size_t>(g.target)).name, g.at);
        if (g.across != 0) s += std::format("across = {}\n", g.across);
        if (g.bend != 1) s += std::format("bend = {}\n", g.bend);
    }
    for (const auto& [name, p] : look.poses) s += std::format("\n[pose.{}]\n{}\n", key_text(name), pose_text(look, p, "\n"));
    for (const auto& [name, c] : look.clips) {
        Sets made;
        for (const PoseKey& k : c.keys) sets_of(look, k.pose, k.change, made);
        const hanim::Ease ease = c.ease.value_or(common_ease(c));
        s += std::format("\n[clip.{}]\nframes = {}\n", key_text(name), c.frames);
        if (c.loop) s += "loop = true\n";
        s += std::format("ease = \"{}\"\n", hanim::to_string(ease));
        if (!c.keys.empty()) {
            s += "poses = [";
            for (std::size_t i = 0; i < c.keys.size(); ++i) {
                const PoseKey& k = c.keys[i];
                s += std::format("{}[{}, \"{}\"", i ? ", " : "", k.frame, k.pose);
                if (k.ease) s += std::format(", \"{}\"", hanim::to_string(*k.ease));
                if (const std::string ch = pose_text(look, k.change, ", "); !ch.empty()) s += std::format(", {{ {} }}", ch);
                s += "]";
            }
            s += "]\n";
        }
        if (!c.rise.empty() && !made.rise) s += std::format("rise = {}\n", keys_text(c.rise, ease));
        for (std::size_t b = 0; b < c.angles.size() && b < look.bones.size(); ++b)
            if (!c.angles[b].empty()) s += std::format("angle.{} = {}\n", key_text(look.bones[b].name), keys_text(c.angles[b], ease));
        for (std::size_t k = 0; k < c.swaps.size() && k < look.parts.size(); ++k)
            if (!c.swaps[k].empty() && !made.swap.contains(static_cast<std::int32_t>(k)))
                s += std::format("swap.{} = {}\n", key_text(look.parts[k].name), keys_text(c.swaps[k], hanim::Ease::stepped(1, hanim::StepPosition::jump_end)));
        for (std::size_t k = 0; k < c.shows.size() && k < look.parts.size(); ++k)
            if (!c.shows[k].empty() && !made.show.contains(static_cast<std::int32_t>(k)))
                s += std::format("show.{} = {}\n", key_text(look.parts[k].name), keys_text(c.shows[k], hanim::Ease::stepped(1, hanim::StepPosition::jump_end)));
        for (std::size_t b = 0; b < c.aims.size() && b < look.bones.size(); ++b)
            if (!c.aims[b].empty() && !made.aim.contains(static_cast<std::int32_t>(b))) s += std::format("aim.{} = {}\n", key_text(look.bones[b].name), keys_text(c.aims[b], ease));
        for (const auto& [key, tracks, derived] : {std::tuple<std::string_view, const std::vector<hanim::Track<double>>*, const std::set<std::int32_t>*>{"grip", &c.grips, &made.grip},
                                                   {"grip_at", &c.grip_at, &made.grip_at}, {"grip_across", &c.grip_across, &made.grip_across}})
            for (std::size_t g = 0; g < tracks->size() && g < look.grips.size(); ++g)
                if (!(*tracks)[g].empty() && !derived->contains(static_cast<std::int32_t>(g)))
                    s += std::format("{}.{} = {}\n", key, key_text(look.grips[g].name), keys_text((*tracks)[g], ease));
        if (!c.fx.empty()) s += fx_rows_text(c.fx);
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
        if (!im.fx.empty()) s += std::format("fx = \"{}\"\n", im.fx);
        if (!im.fx_counter.empty()) s += std::format("fx_counter = \"{}\"\n", im.fx_counter);
        if (!im.fx_punish.empty()) s += std::format("fx_punish = \"{}\"\n", im.fx_punish);
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
        s += std::format("\n[projectile.{}]\n", key_text(name));
        if (!g.glyph.empty()) s += std::format("glyph = \"{}\"\n", g.glyph);
        if (!g.fx.empty()) s += std::format("fx = \"{}\"\n", g.fx);
        s += std::format("size = {}\ncolor = {}\n", g.size, colour_text(g.color));
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
        auto path_key = [](const auto& k) {
            return k.ease ? std::format("[{}, {}, {}, \"{}\"]", k.frame, k.at.x, k.at.y, hanim::to_string(*k.ease)) : std::format("[{}, {}, {}]", k.frame, k.at.x, k.at.y);
        };
        list("attacker_path", c.attacker_path, path_key);
        list("defender_path", c.defender_path, path_key);
        auto span = [](const auto& v) { return std::format("[{}, {}]", v[0], v[1]); };
        list("attacker_turn", c.attacker_turn, span);
        list("defender_turn", c.defender_turn, span);
        list("hitstop", c.hitstops, span);
        list("afterimages", c.afterimages, span);
        list("in_front", c.in_front, span);
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
        if (!c.fx.empty()) s += fx_rows_text(c.fx);
        if (!c.behind.empty()) s += fx_rows_text(c.behind, "behind");
        if (c.hide[0] >= 0) s += std::format("hide = [{}, {}]\n", c.hide[0], c.hide[1]);
        if (!c.holds.empty()) {
            s += "hold = [";
            for (std::size_t i = 0; i < c.holds.size(); ++i) {
                const auto& h = c.holds[i];
                s += std::format("{}{{ from = {}, to = {}, bone = \"{}\", along = {}, body = [{}, {}], clip = \"{}\" }}", i ? ", " : "", h.from, h.to,
                                 look.bones.at(static_cast<std::size_t>(h.bone)).name, h.along, h.body.x, h.body.y, h.clip);
            }
            s += "]\n";
        }
        if (!c.reactions.empty()) {
            s += "reactions = [";
            for (std::size_t i = 0; i < c.reactions.size(); ++i) s += std::format("{}\"{}\"", i ? ", " : "", c.reactions[i]);
            s += "]\n";
        }
        for (const auto& p : c.panels) s += std::format("\n[[cinematic.{}.panel]]\n{}\n", key_text(name), panel_text(look, p, "\n"));
    }
    for (const auto& [name, p] : look.cutins) s += std::format("\n[cutin.{}]\n{}\n", key_text(name), panel_text(look, p, "\n"));
    auto pic = [&](std::int32_t i) { return i >= 0 ? look.pictures.at(static_cast<std::size_t>(i)).name : std::string(); };
    for (const auto& [name, fx] : look.fx) {
        auto colour_line = [&](const std::optional<Color>& c) {
            if (c) s += std::format("color = {}\n", colour_text(*c));
        };
        for (const FxSprite& e : fx.sprites) {
            s += std::format("\n[[fx.{}.sprite]]\npicture = \"{}\"\nsource = {}\nat = [{}, {}]\nfrom = {}\nframes = {}\nsize = [{}, {}]\nturn = [{}, {}]\nalpha = [{}, {}]\nease = \"{}\"\n",
                             key_text(name), pic(e.picture), rect_text(e.source), e.at.x, e.at.y, e.from, e.frames, e.size[0], e.size[1], e.turn[0], e.turn[1], e.alpha[0],
                             e.alpha[1], hanim::to_string(e.ease));
            if (e.to) s += std::format("to = [{}, {}]\n", e.to->x, e.to->y);
            if (e.lift != 0) s += std::format("lift = {}\n", e.lift);
            colour_line(e.color);
        }
        for (const FxStroke& e : fx.strokes) {
            s += std::format("\n[[fx.{}.stroke]]\npicture = \"{}\"\nsource = {}\npoints = [", key_text(name), pic(e.picture), rect_text(e.source));
            for (std::size_t i = 0; i < e.points.size(); ++i) s += std::format("{}[{}, {}]", i ? ", " : "", e.points[i].x, e.points[i].y);
            s += std::format("]\nwidth = {}\nfrom = {}\ndraw = {}\nhold = {}\nfade = {}\n", e.width, e.from, e.draw, e.hold, e.fade);
            colour_line(e.color);
        }
        for (const FxSpray& e : fx.sprays) {
            s += std::format("\n[[fx.{}.spray]]\npicture = \"{}\"\nsources = [", key_text(name), pic(e.picture));
            for (std::size_t i = 0; i < e.sources.size(); ++i) s += (i ? ", " : "") + rect_text(e.sources[i]);
            s += std::format("]\nat = [{}, {}]\nfrom = {}\nframes = {}\ncount = {}\nspeed = [{}, {}]\nangle = [{}, {}]\nsize = [{}, {}]\ngravity = {}\nseed = {}\n", e.at.x,
                             e.at.y, e.from, e.frames, e.count, e.speed[0], e.speed[1], e.angle[0], e.angle[1], e.size[0], e.size[1], e.gravity, e.seed);
            colour_line(e.color);
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
//     light = { from = [-0.7, -0.7], shadow = "#2a1d14", rim_color = "#fff2d0aa", color = "#ffe8c8" }   # color: the light's own, on the fighters' lit side
//     contact = "#00000080"                 # the fighters' contact shadows (transparent: none)
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

constexpr std::array<std::string_view, 10> stage_keys{"gongfu", "name", "sky", "floor", "line", "marks", "light", "contact", "layer", "picture"};
constexpr std::array<std::string_view, 7> stage_image_keys{"picture", "source", "at", "width", "height", "flip", "flicker"};
constexpr std::array<std::string_view, 4> stage_light_keys{"from", "shadow", "rim_color", "color"};
constexpr std::array<std::string_view, 12> layer_keys{"name", "parallax", "repeat", "opacity", "front", "clip", "shape", "bamboo", "lattice", "planks", "rain", "image"};
constexpr std::array<std::string_view, 9> stage_shape_keys{"shape", "points", "color", "color2", "sway", "flicker", "drift", "wrap", "glow"};
constexpr std::array<std::string_view, 8> rain_keys{"seed", "count", "span", "height", "length", "speed", "slant", "color"};
constexpr std::array<std::string_view, 4> sway_keys{"degrees", "period", "phase", "pivot"};
constexpr std::array<std::string_view, 8> bamboo_keys{"seed", "count", "span", "height", "width", "color", "leaves", "sway"};
constexpr std::array<std::string_view, 6> lattice_keys{"at", "size", "cells", "bar", "color", "paper"};
constexpr std::array<std::string_view, 6> planks_keys{"span", "depth", "rows", "length", "color", "seed"};

// splitmix64: a generator's randomness from its seed, the same everywhere.
struct Draws {
    std::uint64_t s;
    double next() {
        s += omui::math::golden;
        return omui::math::unit53(omui::math::finalize(s));  // [0, 1)
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

// Rain: thin streaks spread over the span and the heights, each falling (and
// slanting) at about `speed` units a second, wrapping round the height's range.
void rain(StageLayer& layer, std::uint64_t seed, int count, std::array<double, 2> span, std::array<double, 2> height, float length, float speed, float slant,
          Color colour) {
    Draws d{seed};
    const float range = static_cast<float>(height[1] - height[0]);
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(d.between(span[0], span[1])), y = static_cast<float>(d.between(height[0], height[1]));
        const float v = speed * static_cast<float>(d.between(0.85, 1.15)), l = length * static_cast<float>(d.between(0.7, 1.3));
        StageShape st;
        // Drawn above the top of its range so that, falling `range` and wrapping, it covers the range whatever the frame.
        const float top = y + range;
        st.points = {{x, top}, {x + 1.2f, top}, {x + 1.2f - slant * l, top - l}, {x - slant * l, top - l}};
        st.color = colour;
        st.drift = {-slant * v, -v};
        st.wrap = range;
        layer.shapes.push_back(std::move(st));
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
    if (auto c = r.colour(root->get("contact"), "`contact` (the fighters' shadows)")) st.contact = *c;
    if (const toml::table* t = r.table_of(root->get("light"), "`light`")) {
        r.only(*t, stage_light_keys, "a stage's `light`");
        if (auto f = r.pair(t->get("from"), "a light's `from`", "from = [-0.7, -0.7]")) {
            const double len = std::sqrt((*f)[0] * (*f)[0] + (*f)[1] * (*f)[1]);
            if (len > 0) st.light_from = Point{static_cast<float>((*f)[0] / len), static_cast<float>((*f)[1] / len)};
            else r.error(t->get("from"), "a light comes from somewhere: not [0, 0]");
        }
        if (auto c = r.colour(t->get("shadow"), "a light's `shadow`")) st.shadow = *c;
        if (auto c = r.colour(t->get("rim_color"), "a light's `rim_color`")) st.rim_color = *c;
        if (auto c = r.colour(t->get("color"), "a light's `color`")) st.light_color = *c;
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
    r.tables(root->get("picture"), "picture", "[[picture]] name = \"far\" file = \"art/study-far.webp\" size = [1536, 1024]", [&](const toml::table& t) {
        r.only(t, picture_keys, "a picture");
        Picture pic;
        if (auto n = r.text(t.get("name"), "a picture's `name`")) pic.name = *n;
        else r.error(&t, "a picture has a `name`");
        if (auto f = r.text(t.get("file"), "a picture's `file`")) pic.file = *f;
        else r.error(&t, "a picture has a `file`");
        if (auto sz = r.pair(t.get("size"), "a picture's `size`", "size = [1536, 1024]: its pixels")) {
            pic.width = static_cast<std::int32_t>((*sz)[0]);
            pic.height = static_cast<std::int32_t>((*sz)[1]);
        }
        if (std::ranges::any_of(st.pictures, [&](const Picture& o) { return o.name == pic.name; })) r.error(t.get("name"), std::format("two pictures are called `{}`", pic.name));
        st.pictures.push_back(std::move(pic));
    });
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
        r.tables(t.get("image"), "layer.image", "[[layer.image]] picture = \"far\" source = [0, 0, 1536, 1024] at = [-200, -40] width = 2400", [&](const toml::table& u) {
            r.only(u, stage_image_keys, "a layer's image");
            StageImage im;
            if (auto pn = r.text(u.get("picture"), "an image's `picture`")) {
                const auto it = std::ranges::find(st.pictures, *pn, &Picture::name);
                if (it == st.pictures.end()) {
                    std::vector<std::string_view> names;
                    for (const Picture& o : st.pictures) names.push_back(o.name);
                    r.error(u.get("picture"), std::format("no picture is called `{}`", *pn), names.empty() ? "[[picture]] name = … first" : data::nearest_hint(*pn, names));
                } else {
                    im.picture = static_cast<std::int32_t>(it - st.pictures.begin());
                }
            } else {
                r.error(&u, "an image names its `picture`");
            }
            if (auto v = r.rect(u.get("source"), "an image's `source`")) {
                im.source = *v;
                if (im.picture >= 0) {
                    const Picture& pic = st.pictures[static_cast<std::size_t>(im.picture)];
                    if (pic.width > 0 && (v->x < 0 || v->y < 0 || v->x + v->width > static_cast<float>(pic.width) || v->y + v->height > static_cast<float>(pic.height)))
                        r.error(u.get("source"), std::format("`source` is not inside picture `{}` ({} × {})", pic.name, pic.width, pic.height));
                }
            } else {
                r.error(&u, "an image has a `source` = [x, y, width, height] in its picture");
            }
            if (auto xy = r.pair(u.get("at"), "an image's `at`", "at = [-200, -40]: its bottom left, along and up")) im.at = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
            if (auto v = r.number(u.get("width"), "an image's `width`", 1, 100000)) im.width = static_cast<float>(*v);
            if (auto v = r.number(u.get("height"), "an image's `height`", 0, 100000)) im.height = static_cast<float>(*v);
            if (auto v = r.boolean(u.get("flip"), "an image's `flip`")) im.flip = *v;
            if (auto v = r.number(u.get("flicker"), "an image's `flicker`", 0, 1)) im.flicker = static_cast<float>(*v);
            layer.images.push_back(im);
        });
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
            if (auto xy = r.pair(u.get("drift"), "a shape's `drift`", "drift = [0, -900]: units a second")) sh.drift = {static_cast<float>((*xy)[0]), static_cast<float>((*xy)[1])};
            if (auto v = r.number(u.get("wrap"), "a shape's `wrap`", 0, 100000)) sh.wrap = static_cast<float>(*v);
            if (auto v = r.number(u.get("glow"), "a shape's `glow`", 0, 200)) sh.glow = static_cast<float>(*v);
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
        r.tables(t.get("rain"), "layer.rain", "[[layer.rain]] seed = 3 count = 120 span = [-300, 2220] height = [0, 700] speed = 900", [&](const toml::table& u) {
            r.only(u, rain_keys, "rain");
            const auto seed = r.integer(u.get("seed"), "rain's `seed`", 0, std::numeric_limits<std::int32_t>::max()).value_or(1);
            const auto count = r.integer(u.get("count"), "rain's `count`", 1, 2000).value_or(100);
            const auto span = r.pair(u.get("span"), "rain's `span`", "span = [-300, 2220]").value_or(std::array<double, 2>{0, 1920});
            const auto height = r.pair(u.get("height"), "rain's `height`", "height = [0, 700]").value_or(std::array<double, 2>{0, 700});
            const float length = static_cast<float>(r.number(u.get("length"), "rain's `length`", 1, 500).value_or(26));
            const float speed = static_cast<float>(r.number(u.get("speed"), "rain's `speed`", 1, 10000).value_or(900));
            const float slant = static_cast<float>(r.number(u.get("slant"), "rain's `slant`", -2, 2).value_or(0.12));
            const Color colour = r.colour(u.get("color"), "rain's `color`").value_or(Color::hex(0xbfd8ff, 0.35f));
            rain(layer, static_cast<std::uint64_t>(seed), count, span, height, length, speed, slant, colour);
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
    if (const Color d = Stage{}.contact; st.contact.r != d.r || st.contact.g != d.g || st.contact.b != d.b || st.contact.a != d.a)
        s += std::format("contact = {}\n", colour_text(st.contact));
    if (st.light_from || st.shadow || st.rim_color || st.light_color) {
        std::string parts;
        if (st.light_from) parts += std::format("from = [{}, {}]", st.light_from->x, st.light_from->y);
        if (st.shadow) parts += std::format("{}shadow = {}", parts.empty() ? "" : ", ", colour_text(*st.shadow));
        if (st.rim_color) parts += std::format("{}rim_color = {}", parts.empty() ? "" : ", ", colour_text(*st.rim_color));
        if (st.light_color) parts += std::format("{}color = {}", parts.empty() ? "" : ", ", colour_text(*st.light_color));
        s += std::format("light = {{ {} }}\n", parts);
    }
    for (const Picture& pic : st.pictures) s += std::format("\n[[picture]]\nname = \"{}\"\nfile = \"{}\"\nsize = [{}, {}]\n", pic.name, pic.file, pic.width, pic.height);
    constexpr std::array<std::string_view, 4> shapes{"capsule", "ellipse", "polygon", "curve"};
    for (const StageLayer& l : st.layers) {
        s += std::format("\n[[layer]]\nname = \"{}\"\nparallax = {}\n", l.name, l.parallax);
        if (l.repeat > 0) s += std::format("repeat = {}\n", l.repeat);
        if (l.opacity != 1) s += std::format("opacity = {}\n", l.opacity);
        if (l.front) s += "front = true\n";
        if (l.clip) s += std::format("clip = [{}, {}, {}, {}]\n", (*l.clip)[0], (*l.clip)[1], (*l.clip)[2], (*l.clip)[3]);
        for (const StageImage& im : l.images) {
            s += std::format("[[layer.image]]\npicture = \"{}\"\nsource = {}\nat = [{}, {}]\nwidth = {}\n",
                             im.picture >= 0 ? st.pictures.at(static_cast<std::size_t>(im.picture)).name : std::string(), rect_text(im.source), im.at.x, im.at.y, im.width);
            if (im.height > 0) s += std::format("height = {}\n", im.height);
            if (im.flip) s += "flip = true\n";
            if (im.flicker > 0) s += std::format("flicker = {}\n", im.flicker);
        }
        for (const StageShape& sh : l.shapes) {
            s += std::format("[[layer.shape]]\nshape = \"{}\"\npoints = [", shapes[static_cast<std::size_t>(sh.shape)]);
            for (std::size_t i = 0; i < sh.points.size(); ++i) s += std::format("{}[{}, {}]", i ? ", " : "", sh.points[i].x, sh.points[i].y);
            s += std::format("]\ncolor = {}\n", colour_text(sh.color));
            if (sh.color2.a > 0) s += std::format("color2 = {}\n", colour_text(sh.color2));
            if (sh.sway > 0)
                s += std::format("sway = {{ degrees = {}, period = {}, phase = {}, pivot = [{}, {}] }}\n", sh.sway, sh.period, sh.phase, sh.pivot.x, sh.pivot.y);
            if (sh.flicker > 0) s += std::format("flicker = {}\n", sh.flicker);
            if (sh.drift.x != 0 || sh.drift.y != 0) s += std::format("drift = [{}, {}]\n", sh.drift.x, sh.drift.y);
            if (sh.wrap > 0) s += std::format("wrap = {}\n", sh.wrap);
            if (sh.glow > 0) s += std::format("glow = {}\n", sh.glow);
        }
    }
    return s;
}

}  // namespace gongfu::present2d
