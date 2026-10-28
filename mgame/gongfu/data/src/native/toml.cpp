// Reading *.fighter.gongfu.toml and *.system.gongfu.toml. toml++ reports a
// malformed file by throwing; this unit — the library's adapter — turns that
// into a diagnostic, and keeps where each field was so that gongfu::compile's
// problems carry their line and column too.
module gongfu.data;

import std;
import gongfu;
import tomlplusplus;

namespace gongfu::data {

namespace {

constexpr std::array<std::string_view, 9> fighter_keys{"gongfu", "name", "health", "walk", "jump", "push", "hurt", "move", "doc"};
constexpr std::array<std::string_view, 2> walk_keys{"forward", "back"};
constexpr std::array<std::string_view, 5> jump_keys{"prejump", "up", "forward", "gravity", "landing"};
constexpr std::array<std::string_view, 3> hurt_keys{"standing", "crouching", "air"};
constexpr std::array<std::string_view, 14> move_keys{"name", "input", "stance", "total", "priority", "hit", "cancel", "invulnerable",
                                                     "step", "throw", "projectile", "meter", "freeze", "doc"};
constexpr std::array<std::string_view, 12> hit_keys{"active", "boxes", "damage", "chip", "hitstun", "blockstun",
                                                    "hitstop", "pushback", "level", "knockdown", "launch", "impact"};
constexpr std::array<std::string_view, 3> cancel_keys{"frames", "into", "on_contact"};
constexpr std::array<std::string_view, 2> step_keys{"frame", "velocity"};
constexpr std::array<std::string_view, 4> throw_keys{"active", "range", "damage", "tech"};
constexpr std::array<std::string_view, 5> projectile_keys{"spawn", "offset", "speed", "life", "hit"};
constexpr std::array<std::string_view, 2> meter_keys{"cost", "gain"};
constexpr std::array<std::string_view, 3> input_keys{"notation", "window", "buffer"};
constexpr std::array<std::string_view, 11> system_keys{"gongfu", "scaling", "counter", "round", "stage", "meter", "knockdown", "wakeup",
                                                      "proximity_guard", "socd", "doc"};
constexpr std::array<std::string_view, 2> counter_keys{"damage", "hitstun"};
constexpr std::array<std::string_view, 2> stage_keys{"width", "start"};
constexpr std::string_view format_version = "0.1";

// Edits from a to b, swaps of two neighbours included (optimal string alignment).
std::size_t distance(std::string_view a, std::string_view b) {
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

std::string nearest(std::string_view name, std::span<const std::string_view> options) {
    std::string_view best;
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) d = x, best = o;
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", o);
    return std::format("the keys are {}", all);
}

// A decimal from the file as the simulation's number: the nearest step.
Fixed fixed_of(double v) { return Fixed::raw(static_cast<std::int32_t>(std::llround(v * Fixed::one_bits))); }

struct Reader {
    std::string                                         file;
    std::vector<Diagnostic>&                            out;
    std::map<std::string, std::pair<int, int>, std::less<>> where;  // a compile() path: its line and column
    std::vector<std::string>                            impacts{};  // the impact profiles hits name, in the order first named

    std::pair<int, int> at(const toml::node* n) const {
        if (!n) return {0, 0};
        const auto s = n->source();
        return {static_cast<int>(s.begin.line), static_cast<int>(s.begin.column)};
    }
    void error(const toml::node* n, std::string message, std::string hint = {}) {
        const auto [l, c] = at(n);
        out.push_back({file, l, c, std::move(message), std::move(hint)});
    }
    void mark(std::string path, const toml::node* n) {
        if (n) where.emplace(std::move(path), at(n));
    }
    // A key no table of this kind has: at the key, with the nearest that is.
    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view what) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end()) {
                const auto src = k.source();
                out.push_back({file, static_cast<int>(src.begin.line), static_cast<int>(src.begin.column),
                               std::format("{} has no key `{}`", what, k.str()), nearest(k.str(), keys)});
            }
    }
    const toml::table* table(const toml::node* n, std::string_view what) {
        if (!n) return nullptr;
        if (const toml::table* t = n->as_table()) return t;
        error(n, std::format("{} is a table", what));
        return nullptr;
    }
    std::optional<std::string> text(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n->value<std::string>()) return *s;
        error(n, std::format("{} is a string", what));
        return std::nullopt;
    }
    std::optional<std::int32_t> integer(const toml::node* n, std::string_view what, std::int64_t lo = 0, std::int64_t hi = 1'000'000'000) {
        if (!n) return std::nullopt;
        const auto v = n->as_integer();
        if (!v) {
            error(n, std::format("{} is a whole number", what));
            return std::nullopt;
        }
        if (v->get() < lo || v->get() > hi) {
            error(n, std::format("{} is {}–{}", what, lo, hi));
            return std::nullopt;
        }
        return static_cast<std::int32_t>(v->get());
    }
    std::optional<Fixed> number(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        double v = 0;
        if (auto i = n->as_integer()) v = static_cast<double>(i->get());
        else if (auto f = n->as_floating_point()) v = f->get();
        else {
            error(n, std::format("{} is a number", what));
            return std::nullopt;
        }
        if (!std::isfinite(v) || v < -32767 || v > 32767) {
            error(n, std::format("{} is a number of −32767–32767", what));
            return std::nullopt;
        }
        return fixed_of(v);
    }
    std::optional<bool> boolean(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto b = n->value<bool>()) return *b;
        error(n, std::format("{} is true or false", what));
        return std::nullopt;
    }
    // [a, b, …] of numbers, exactly `count` of them.
    std::optional<std::vector<Fixed>> numbers(const toml::node* n, std::size_t count, std::string_view what, std::string_view example) {
        if (!n) return std::nullopt;
        const toml::array* a = n->as_array();
        if (!a || a->size() != count) {
            error(n, std::format("{} is {} numbers", what, count), std::string(example));
            return std::nullopt;
        }
        std::vector<Fixed> out;
        for (const toml::node& e : *a) {
            auto v = number(&e, what);
            if (!v) return std::nullopt;
            out.push_back(*v);
        }
        return out;
    }
    std::optional<Box> box(const toml::node* n, std::string_view what) {
        auto v = numbers(n, 4, what, "[x, y, width, height]: x forward from the fighter, y up from its feet");
        if (!v) return std::nullopt;
        return Box{(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
    }
    std::optional<Vec> vec(const toml::node* n, std::string_view what) {
        auto v = numbers(n, 2, what, "[forward, up]");
        if (!v) return std::nullopt;
        return Vec{(*v)[0], (*v)[1]};
    }
    std::optional<Frames> frames(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        const toml::array* a = n->as_array();
        if (!a || a->size() != 2 || !(*a)[0].as_integer() || !(*a)[1].as_integer()) {
            error(n, std::format("{} is [first, last] frames", what), "[4, 5]: frames 4 and 5, the move's first frame 1");
            return std::nullopt;
        }
        return Frames{static_cast<std::int32_t>((*a)[0].as_integer()->get()), static_cast<std::int32_t>((*a)[1].as_integer()->get())};
    }

    HitDef hit(const toml::table& t, const std::string& path) {
        only(t, hit_keys, "a hit");
        HitDef h;
        mark(path, &t);
        if (auto f = frames(t.get("active"), "a hit's `active`")) h.active = *f;
        mark(path + ".active", t.get("active"));
        if (!t.get("active")) error(&t, "a hit has no `active` frames", "active = [4, 5]");
        if (const toml::node* b = t.get("boxes")) {
            mark(path + ".boxes", b);
            if (const toml::array* a = b->as_array()) {
                for (std::size_t i = 0; i < a->size(); ++i) {
                    mark(std::format("{}.boxes[{}]", path, i), &(*a)[i]);
                    if (auto bx = box(&(*a)[i], "a hit box")) h.boxes.push_back(*bx);
                }
            } else {
                error(b, "`boxes` is a list of boxes", "boxes = [[20, 110, 70, 30]]");
            }
        }
        if (auto v = integer(t.get("damage"), "`damage`")) h.damage = *v;
        mark(path + ".damage", t.get("damage"));
        if (auto v = integer(t.get("chip"), "`chip`")) h.chip = *v;
        mark(path + ".chip", t.get("chip"));
        if (auto v = integer(t.get("hitstun"), "`hitstun`", 0, 600)) h.hitstun = *v;
        mark(path + ".hitstun", t.get("hitstun"));
        if (auto v = integer(t.get("blockstun"), "`blockstun`", 0, 600)) h.blockstun = *v;
        if (auto v = integer(t.get("hitstop"), "`hitstop`", 0, 120)) h.hitstop = *v;
        mark(path + ".hitstop", t.get("hitstop"));
        if (auto v = number(t.get("pushback"), "`pushback`")) h.pushback = *v;
        if (auto l = text(t.get("level"), "`level`")) {
            if (*l == "mid") h.level = Level::mid;
            else if (*l == "low") h.level = Level::low;
            else if (*l == "overhead") h.level = Level::overhead;
            else error(t.get("level"), std::format("`{}` is not a level", *l), "mid, low or overhead");
        }
        if (auto v = boolean(t.get("knockdown"), "`knockdown`")) h.knockdown = *v;
        if (auto v = vec(t.get("launch"), "`launch`")) h.launch = *v;
        if (const toml::node* n = t.get("impact")) {
            if (auto name = n->value<std::string>(); name && !name->empty()) {
                auto it = std::ranges::find(impacts, *name);
                if (it == impacts.end()) it = impacts.insert(impacts.end(), *name);
                h.impact = static_cast<std::uint16_t>(it - impacts.begin());
            } else {
                error(n, "`impact` names a profile of the fighter's look", "impact = \"light\" (the look's [impact.light] says how it is drawn)");
            }
        }
        return h;
    }

    template <class F>
    void each_table(const toml::node* n, std::string_view what, std::string_view example, F&& f) {
        if (!n) return;
        const toml::array* a = n->as_array();
        if (!a || !a->is_array_of_tables()) {
            error(n, std::format("`{}` is a list of tables", what), std::string(example));
            return;
        }
        for (std::size_t i = 0; i < a->size(); ++i) f(*(*a)[i].as_table(), i);
    }

    std::optional<MoveDef> move(const toml::table& t, const std::string& fighter) {
        only(t, move_keys, "a move");
        MoveDef m;
        const auto name = text(t.get("name"), "a move's `name`");
        if (!name) {
            if (!t.get("name")) error(&t, "a move has no `name`", "name = \"5LP\": what cancels and what draws call it");
            return std::nullopt;
        }
        m.name = *name;
        const std::string at = fighter + "/" + m.name;
        mark(at, &t);
        // The input: notation, or a table with its window and buffer.
        if (const toml::node* in = t.get("input")) {
            mark(at + ".command", in);
            std::optional<std::string> note;
            const toml::table* it = in->as_table();
            if (it) {
                only(*it, input_keys, "an input");
                note = text(it->get("notation"), "an input's `notation`");
            } else {
                note = text(in, "`input`");
            }
            if (note) {
                if (auto c = parse_command(*note)) {
                    m.command = *c;
                    if (it) {
                        if (auto w = integer(it->get("window"), "`window`", 1, 60)) m.command.motion.window = static_cast<std::uint8_t>(*w);
                        if (auto b = integer(it->get("buffer"), "`buffer`", 0, 15)) m.command.buffer = static_cast<std::uint8_t>(*b);
                    }
                } else {
                    error(in, c.error(), "236P, [4]6P, 2LK, 6HP, LP+LK");
                }
            }
        } else {
            error(&t, std::format("`{}` has no `input`", m.name), "input = \"236P\"");
        }
        if (auto s = text(t.get("stance"), "`stance`")) {
            if (*s == "standing") m.stance = Stance::standing;
            else if (*s == "crouching") m.stance = Stance::crouching;
            else if (*s == "air") m.stance = Stance::air;
            else error(t.get("stance"), std::format("`{}` is not a stance", *s), "standing, crouching or air");
        }
        if (auto v = integer(t.get("total"), "`total`", 1, 600)) m.total = *v;
        else if (!t.get("total")) error(&t, std::format("`{}` has no `total` frames", m.name), "total = 12: startup, active and recovery");
        mark(at + ".total", t.get("total"));
        if (auto v = integer(t.get("priority"), "`priority`", -1000, 1000)) m.priority = *v;
        each_table(t.get("hit"), "hit", "[[move.hit]] active = [4, 5] …", [&](const toml::table& h, std::size_t i) {
            m.hits.push_back(hit(h, std::format("{}.hits[{}]", at, i)));
        });
        each_table(t.get("cancel"), "cancel", "[[move.cancel]] frames = [4, 8], into = [\"236P\"]", [&](const toml::table& c, std::size_t i) {
            only(c, cancel_keys, "a cancel");
            const std::string p = std::format("{}.cancels[{}]", at, i);
            mark(p, &c);
            CancelDef cd;
            if (auto f = frames(c.get("frames"), "a cancel's `frames`")) cd.frames = *f;
            mark(p + ".frames", c.get("frames"));
            if (const toml::node* into = c.get("into")) {
                if (const toml::array* a = into->as_array()) {
                    for (const toml::node& e : *a)
                        if (auto s = text(&e, "a move's name in `into`")) cd.into.push_back(*s);
                } else {
                    error(into, "`into` is a list of moves' names", "into = [\"236P\", \"623P\"]");
                }
            }
            if (auto v = boolean(c.get("on_contact"), "`on_contact`")) cd.on_contact = *v;
            m.cancels.push_back(std::move(cd));
        });
        if (const toml::node* inv = t.get("invulnerable")) {
            if (const toml::array* a = inv->as_array()) {
                for (std::size_t i = 0; i < a->size(); ++i) {
                    mark(std::format("{}.invulnerable[{}]", at, i), &(*a)[i]);
                    if (auto f = frames(&(*a)[i], "an invulnerable range")) m.invulnerable.push_back(*f);
                }
            } else {
                error(inv, "`invulnerable` is a list of frame ranges", "invulnerable = [[1, 6]]");
            }
        }
        each_table(t.get("step"), "step", "[[move.step]] frame = 4, velocity = [2, 16]", [&](const toml::table& s, std::size_t i) {
            only(s, step_keys, "a step");
            mark(std::format("{}.steps[{}]", at, i), &s);
            StepDef st;
            if (auto f = integer(s.get("frame"), "a step's `frame`", 1, 600)) st.frame = *f;
            if (auto v = vec(s.get("velocity"), "a step's `velocity`")) st.velocity = *v;
            m.steps.push_back(st);
        });
        if (const toml::table* th = table(t.get("throw"), "`throw`")) {
            only(*th, throw_keys, "a throw");
            mark(at + ".throw", th);
            ThrowDef td;
            if (auto f = frames(th->get("active"), "a throw's `active`")) td.active = *f;
            mark(at + ".throw.active", th->get("active"));
            if (auto b = box(th->get("range"), "a throw's `range`")) td.range = *b;
            mark(at + ".throw.range", th->get("range"));
            if (auto v = integer(th->get("damage"), "a throw's `damage`")) td.damage = *v;
            if (auto v = integer(th->get("tech"), "a throw's `tech` window", 0, 30)) td.tech_window = *v;
            m.throw_ = td;
        }
        if (const toml::table* pt = table(t.get("projectile"), "`projectile`")) {
            only(*pt, projectile_keys, "a projectile");
            mark(at + ".projectile", pt);
            ProjectileDef pd;
            if (auto v = integer(pt->get("spawn"), "a projectile's `spawn` frame", 1, 600)) pd.spawn = *v;
            mark(at + ".projectile.spawn", pt->get("spawn"));
            if (auto v = vec(pt->get("offset"), "a projectile's `offset`")) pd.offset = *v;
            if (auto v = number(pt->get("speed"), "a projectile's `speed`")) pd.speed = *v;
            if (auto v = integer(pt->get("life"), "a projectile's `life`", 1, 3600)) pd.life = *v;
            if (const toml::table* h = table(pt->get("hit"), "a projectile's `hit`")) pd.hit = hit(*h, at + ".projectile.hit");
            else if (!pt->get("hit")) error(pt, "a projectile has no `hit`", "[move.projectile.hit] active = [1, 120] …");
            m.projectile = std::move(pd);
        }
        if (const toml::table* mt = table(t.get("meter"), "`meter`")) {
            only(*mt, meter_keys, "a meter");
            mark(at + ".meter", mt);
            if (auto v = integer(mt->get("cost"), "a meter's `cost`")) m.meter_cost = *v;
            if (auto v = integer(mt->get("gain"), "a meter's `gain`")) m.meter_gain = *v;
        }
        if (auto v = integer(t.get("freeze"), "`freeze`", 0, 600)) m.freeze = *v;
        return m;
    }

    // Where a compile() problem points: its path, or the nearest enclosing one.
    std::pair<int, int> locate(std::string path) const {
        while (!path.empty()) {
            if (auto it = where.find(path); it != where.end()) return it->second;
            const std::size_t cut = path.find_last_of(".[/");
            if (cut == std::string::npos) break;
            path.resize(cut);
        }
        return {1, 1};
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

void version(Reader& r, const toml::table& root) {
    if (auto v = r.text(root.get("gongfu"), "`gongfu`")) {
        if (*v != format_version) r.error(root.get("gongfu"), std::format("this reads gongfu {} files; the file says {}", format_version, *v));
    } else if (!root.get("gongfu")) {
        r.error(&root, "the file does not say its format", std::format("gongfu = \"{}\" first", format_version));
    }
}

}  // namespace

std::string nearest_hint(std::string_view name, std::span<const std::string_view> keys) { return nearest(name, keys); }

std::expected<Fighter, std::vector<Diagnostic>> read_fighter(std::string_view source, std::string_view file) {
    std::vector<Diagnostic> out;
    auto root = parse(source, file, out);
    if (!root) return std::unexpected(std::move(out));
    Reader r{std::string(file), out, {}};
    r.only(*root, fighter_keys, "a fighter file");
    version(r, *root);
    FighterDef f;
    if (auto name = r.text(root->get("name"), "`name`")) f.name = *name;
    else if (!root->get("name")) r.error(&*root, "the fighter has no `name`", "name = \"Kata\"");
    r.mark(f.name, &*root);
    if (auto v = r.integer(root->get("health"), "`health`", 1)) f.health = *v;
    r.mark(f.name + ".health", root->get("health"));
    if (const toml::table* w = r.table(root->get("walk"), "`walk`")) {
        r.only(*w, walk_keys, "`walk`");
        if (auto v = r.number(w->get("forward"), "`walk.forward`")) f.walk_forward = *v;
        if (auto v = r.number(w->get("back"), "`walk.back`")) f.walk_back = *v;
    }
    if (const toml::table* j = r.table(root->get("jump"), "`jump`")) {
        r.only(*j, jump_keys, "`jump`");
        if (auto v = r.integer(j->get("prejump"), "`jump.prejump`", 0, 60)) f.prejump = *v;
        r.mark(f.name + ".prejump", j->get("prejump"));
        if (auto v = r.number(j->get("up"), "`jump.up`")) f.jump_up = *v;
        r.mark(f.name + ".jump_up", j->get("up"));
        if (auto v = r.number(j->get("forward"), "`jump.forward`")) f.jump_forward = *v;
        if (auto v = r.number(j->get("gravity"), "`jump.gravity`")) f.gravity = *v;
        r.mark(f.name + ".gravity", j->get("gravity"));
        if (auto v = r.integer(j->get("landing"), "`jump.landing`", 0, 60)) f.landing = *v;
    }
    if (auto b = r.box(root->get("push"), "`push`")) f.push = *b;
    r.mark(f.name + ".push", root->get("push"));
    if (const toml::table* h = r.table(root->get("hurt"), "`hurt`")) {
        r.only(*h, hurt_keys, "`hurt`");
        if (auto b = r.box(h->get("standing"), "`hurt.standing`")) f.hurt_standing = *b;
        if (auto b = r.box(h->get("crouching"), "`hurt.crouching`")) f.hurt_crouching = *b;
        if (auto b = r.box(h->get("air"), "`hurt.air`")) f.hurt_air = *b;
        r.mark(f.name + ".hurt_standing", h->get("standing"));
        r.mark(f.name + ".hurt_crouching", h->get("crouching"));
        r.mark(f.name + ".hurt_air", h->get("air"));
    }
    r.each_table(root->get("move"), "move", "[[move]] name = \"5LP\" …", [&](const toml::table& t, std::size_t) {
        if (auto m = r.move(t, f.name)) f.moves.push_back(std::move(*m));
    });
    f.impacts = std::move(r.impacts);
    if (!out.empty()) return std::unexpected(std::move(out));
    auto compiled = compile(std::move(f));
    if (!compiled) {
        for (const Problem& p : compiled.error()) {
            const auto [l, c] = r.locate(p.where);
            out.push_back({std::string(file), l, c, std::format("{}: {}", p.where, p.text), ""});
        }
        return std::unexpected(std::move(out));
    }
    return std::move(*compiled);
}

std::expected<SystemDef, std::vector<Diagnostic>> read_system(std::string_view source, std::string_view file) {
    std::vector<Diagnostic> out;
    auto root = parse(source, file, out);
    if (!root) return std::unexpected(std::move(out));
    Reader r{std::string(file), out, {}};
    r.only(*root, system_keys, "a system file");
    version(r, *root);
    SystemDef s;
    if (const toml::node* n = root->get("scaling")) {
        r.mark("system.scaling", n);
        if (const toml::array* a = n->as_array()) {
            s.scaling.clear();
            for (std::size_t i = 0; i < a->size(); ++i) {
                r.mark(std::format("system.scaling[{}]", i), &(*a)[i]);
                if (auto v = r.integer(&(*a)[i], "a scaling step", 0, 100)) s.scaling.push_back(*v);
            }
        } else {
            r.error(n, "`scaling` is a list of percentages", "scaling = [100, 100, 80, 70, 60, 50]");
        }
    }
    if (const toml::table* c = r.table(root->get("counter"), "`counter`")) {
        r.only(*c, counter_keys, "`counter`");
        if (auto v = r.integer(c->get("damage"), "`counter.damage`", 0, 1000)) s.counter_damage = *v;
        r.mark("system.counter_damage", c->get("damage"));
        if (auto v = r.integer(c->get("hitstun"), "`counter.hitstun`", 0, 60)) s.counter_hitstun = *v;
    }
    if (auto v = r.integer(root->get("round"), "`round` (seconds)", 1, 3600)) s.round_frames = *v * 60;
    r.mark("system.round_frames", root->get("round"));
    if (const toml::table* st = r.table(root->get("stage"), "`stage`")) {
        r.only(*st, stage_keys, "`stage`");
        if (auto v = r.number(st->get("width"), "`stage.width`")) s.stage_width = *v;
        if (auto v = r.number(st->get("start"), "`stage.start`")) s.start_distance = *v;
        r.mark("system.start_distance", st->get("start"));
    }
    if (auto v = r.integer(root->get("meter"), "`meter`", 0, 100000)) s.meter_max = *v;
    if (auto v = r.integer(root->get("knockdown"), "`knockdown`", 0, 600)) s.knockdown = *v;
    r.mark("system.knockdown", root->get("knockdown"));
    if (auto v = r.integer(root->get("wakeup"), "`wakeup`", 0, 600)) s.wakeup = *v;
    if (auto v = r.number(root->get("proximity_guard"), "`proximity_guard`")) s.proximity_guard = *v;
    if (auto v = r.text(root->get("socd"), "`socd`")) {
        if (*v == "neutral") s.socd = Socd::neutral;
        else if (*v == "last_wins") s.socd = Socd::last_wins;
        else if (*v == "up_wins") s.socd = Socd::up_wins;
        else r.error(root->get("socd"), std::format("`{}` is not an SOCD rule", *v), "neutral, last_wins or up_wins");
    }
    if (!out.empty()) return std::unexpected(std::move(out));
    for (const Problem& p : check(s)) {
        const auto [l, c] = r.locate(p.where);
        out.push_back({std::string(file), l, c, std::format("{}: {}", p.where, p.text), ""});
    }
    if (!out.empty()) return std::unexpected(std::move(out));
    return s;
}

}  // namespace gongfu::data
