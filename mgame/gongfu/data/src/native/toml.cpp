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

constexpr std::array<std::string_view, 10> fighter_keys{"gongfu", "name", "health", "posture", "walk", "jump", "push", "hurt", "move", "doc"};
constexpr std::array<std::string_view, 2> walk_keys{"forward", "back"};
constexpr std::array<std::string_view, 5> jump_keys{"prejump", "up", "forward", "gravity", "landing"};
constexpr std::array<std::string_view, 3> hurt_keys{"standing", "crouching", "air"};
constexpr std::array<std::string_view, 23> move_keys{"name", "input", "stance", "total", "priority", "hit", "cancel", "invulnerable",
                                                     "hurt", "step", "throw", "projectile", "meter", "freeze", "level", "weight", "posture", "clash",
                                                     "clash_cancel", "cinematic", "pass", "armor", "doc"};
constexpr std::array<std::string_view, 2> armor_keys{"frames", "hits"};
constexpr std::array<std::string_view, 6> cinematic_keys{"frames", "hits", "attacker", "defender", "down", "recovery"};
constexpr std::array<std::string_view, 15> hit_keys{"active", "boxes", "damage", "chip", "hitstun", "blockstun", "hitstop", "pushback",
                                                    "level", "knockdown", "launch", "impact", "down", "juggle", "juggle_cost"};
constexpr std::array<std::string_view, 2> hitstop_keys{"attacker", "defender"};
constexpr std::array<std::string_view, 2> invulnerable_keys{"frames", "to"};
constexpr std::array<std::string_view, 4> invulnerable_kinds{"strike", "air", "projectile", "throw"};
constexpr std::array<std::string_view, 2> move_hurt_keys{"frames", "boxes"};
constexpr std::array<std::string_view, 3> cancel_keys{"frames", "into", "on_contact"};
constexpr std::array<std::string_view, 2> step_keys{"frame", "velocity"};
constexpr std::array<std::string_view, 6> throw_keys{"active", "range", "damage", "tech", "swap", "air"};
constexpr std::array<std::string_view, 10> projectile_keys{"spawn", "offset", "speed", "velocity", "life", "hit", "hits", "interval", "strength", "limit"};
constexpr std::array<std::string_view, 2> meter_keys{"cost", "gain"};
constexpr std::array<std::string_view, 3> input_keys{"notation", "window", "buffer"};
constexpr std::array<std::string_view, 15> system_keys{"gongfu", "scaling", "counter", "round", "stage", "meter", "knockdown", "wakeup",
                                                      "proximity_guard", "socd", "posture", "clash", "deflect", "super_scaling", "doc"};
constexpr std::array<std::string_view, 5> posture_rule_keys{"delay", "rate", "break_stagger", "break_protect", "break_scaling"};
constexpr std::array<std::string_view, 4> clash_rule_keys{"window", "stop", "recovery", "lost"};
constexpr std::array<std::string_view, 4> deflect_rule_keys{"window", "fresh", "again", "gain"};
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
        // The same for both, or each its own: hitstop = { attacker = 4, defender = 10 }.
        if (const toml::node* hs = t.get("hitstop")) {
            if (const toml::table* ht = hs->as_table()) {
                only(*ht, hitstop_keys, "a hitstop");
                if (auto v = integer(ht->get("defender"), "the defender's `hitstop`", 0, 120)) h.hitstop = *v;
                if (auto v = integer(ht->get("attacker"), "the attacker's `hitstop`", 0, 120)) h.hitstop_attacker = *v;
            } else if (auto v = integer(hs, "`hitstop`", 0, 120)) {
                h.hitstop = *v;
            }
        }
        mark(path + ".hitstop", t.get("hitstop"));
        if (auto v = integer(t.get("down"), "`down` frames", 0, 600)) h.down = *v;
        if (auto v = integer(t.get("juggle"), "`juggle` points", 0, 10)) h.juggle = *v;
        if (auto v = integer(t.get("juggle_cost"), "`juggle_cost`", 0, 10)) h.juggle_cost = *v;
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
        // Frame ranges invulnerable to everything, or tables saying to what.
        if (const toml::node* inv = t.get("invulnerable")) {
            if (const toml::array* a = inv->as_array()) {
                for (std::size_t i = 0; i < a->size(); ++i) {
                    const toml::node* e = &(*a)[i];
                    mark(std::format("{}.invulnerable[{}]", at, i), e);
                    if (const toml::table* it = e->as_table()) {
                        only(*it, invulnerable_keys, "an invulnerability");
                        Invulnerable v{.to = 0};
                        if (auto f = frames(it->get("frames"), "an invulnerability's `frames`")) v.frames = *f;
                        if (const toml::node* to = it->get("to")) {
                            if (const toml::array* kinds = to->as_array()) {
                                for (const toml::node& k : *kinds)
                                    if (auto name = text(&k, "what an invulnerability is to")) {
                                        auto found = std::ranges::find(invulnerable_kinds, *name);
                                        if (found == invulnerable_kinds.end()) error(&k, std::format("`{}` is not what an invulnerability is to", *name), nearest(*name, invulnerable_kinds));
                                        else v.to |= static_cast<std::uint8_t>(1u << (found - invulnerable_kinds.begin()));
                                    }
                            } else {
                                error(to, "`to` is a list", "to = [\"air\"] (strike, air, projectile, throw)");
                            }
                        } else {
                            v.to = to_all;
                        }
                        m.invulnerable.push_back(v);
                    } else if (auto f = frames(e, "an invulnerable range")) {
                        m.invulnerable.push_back({*f, to_all});
                    }
                }
            } else {
                error(inv, "`invulnerable` is a list of frame ranges", "invulnerable = [[1, 6]], or [{ frames = [1, 6], to = [\"air\"] }]");
            }
        }
        each_table(t.get("hurt"), "hurt", "[[move.hurt]] frames = [6, 12], boxes = [[-35, 0, 70, 180], [35, 90, 70, 40]]",
                   [&](const toml::table& h, std::size_t i) {
                       only(h, move_hurt_keys, "a move's hurtboxes");
                       mark(std::format("{}.hurt[{}]", at, i), &h);
                       HurtDef hd;
                       if (auto f = frames(h.get("frames"), "a move's hurtboxes' `frames`")) hd.frames = *f;
                       mark(std::format("{}.hurt[{}].frames", at, i), h.get("frames"));
                       if (const toml::node* b = h.get("boxes")) {
                           if (const toml::array* a = b->as_array()) {
                               for (std::size_t k = 0; k < a->size(); ++k)
                                   if (auto bx = box(&(*a)[k], "a hurtbox")) hd.boxes.push_back(*bx);
                           } else {
                               error(b, "`boxes` is a list of boxes", "boxes = [[-35, 0, 70, 180]]");
                           }
                       }
                       mark(std::format("{}.hurt[{}].boxes", at, i), h.get("boxes"));
                       m.hurt.push_back(std::move(hd));
                   });
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
            if (auto v = boolean(th->get("swap"), "a throw's `swap`")) td.swap = *v;
            if (auto v = boolean(th->get("air"), "a throw's `air` (it takes only a fighter in the air)")) td.air = *v;
            m.throw_ = td;
        }
        if (const toml::table* pt = table(t.get("projectile"), "`projectile`")) {
            only(*pt, projectile_keys, "a projectile");
            mark(at + ".projectile", pt);
            ProjectileDef pd;
            if (auto v = integer(pt->get("spawn"), "a projectile's `spawn` frame", 1, 600)) pd.spawn = *v;
            mark(at + ".projectile.spawn", pt->get("spawn"));
            if (auto v = vec(pt->get("offset"), "a projectile's `offset`")) pd.offset = *v;
            // Forward alone (speed = 8), or forward and up (velocity = [5, -5]).
            if (pt->get("speed") && pt->get("velocity")) error(pt->get("velocity"), "a projectile has a `speed` or a `velocity`, not both");
            if (auto v = number(pt->get("speed"), "a projectile's `speed`")) pd.velocity = {*v, Fixed{}};
            if (auto v = vec(pt->get("velocity"), "a projectile's `velocity`")) pd.velocity = *v;
            if (auto v = integer(pt->get("life"), "a projectile's `life`", 1, 3600)) pd.life = *v;
            if (auto v = integer(pt->get("hits"), "a projectile's `hits`", 1, 60)) pd.hits = *v;
            if (auto v = integer(pt->get("interval"), "a projectile's `interval`", 0, 600)) pd.interval = *v;
            if (auto v = integer(pt->get("strength"), "a projectile's `strength`", 1, 100)) pd.strength = *v;
            if (auto v = integer(pt->get("limit"), "a projectile's `limit`", 0, 8)) pd.limit = *v;
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
        // System 0.3: a super's level; clash weight; the posture a block of the whole move costs; clashing.
        if (auto v = integer(t.get("level"), "`level` (a super's: 1, 2 or 3)", 0, 3)) m.level = *v;
        if (auto v = integer(t.get("weight"), "`weight`", 0, 10)) m.weight = *v;
        if (auto v = integer(t.get("posture"), "`posture`", 0, 10000)) m.posture = *v;
        if (auto v = boolean(t.get("clash"), "`clash`")) m.clash = *v;
        if (auto v = boolean(t.get("clash_cancel"), "`clash_cancel`")) m.clash_cancel = *v;
        if (auto f = frames(t.get("pass"), "`pass` (frames passing through the other fighter)")) m.pass = *f;
        if (const toml::table* ar = table(t.get("armor"), "`armor`")) {
            only(*ar, armor_keys, "an armour");
            ArmorDef a;
            if (auto f = frames(ar->get("frames"), "an armour's `frames`")) a.frames = *f;
            if (auto v = integer(ar->get("hits"), "an armour's `hits`", 1, 10)) a.hits = *v;
            m.armor = a;
            mark(at + ".armor", ar);
        }
        // A level 3's cinematic: [move.cinematic] frames, hits = [[frame, damage, "impact"], …], where each ends, down, recovery.
        if (const toml::table* ct = table(t.get("cinematic"), "`cinematic`")) {
            only(*ct, cinematic_keys, "a cinematic");
            mark(at + ".cinematic", ct);
            CinematicDef c;
            if (auto v = integer(ct->get("frames"), "a cinematic's `frames`", 1, 1200)) c.frames = *v;
            mark(at + ".cinematic.frames", ct->get("frames"));
            if (auto v = vec(ct->get("attacker"), "where the attacker ends")) c.attacker_at = *v;
            if (auto v = vec(ct->get("defender"), "where the defender ends")) c.defender_at = *v;
            if (auto v = integer(ct->get("down"), "a cinematic's `down`", 0, 600)) c.down = *v;
            if (auto v = integer(ct->get("recovery"), "a cinematic's `recovery`", 0, 600)) c.recovery = *v;
            if (const toml::node* hs = ct->get("hits")) {
                mark(at + ".cinematic.hits", hs);
                if (const toml::array* a = hs->as_array()) {
                    for (const toml::node& e : *a) {
                        const toml::array* hit = e.as_array();
                        if (!hit || hit->size() < 2 || hit->size() > 3) {
                            error(&e, "a cinematic's hit is [frame, damage] or [frame, damage, \"impact\"]");
                            continue;
                        }
                        CinematicHit h;
                        if (auto v = integer(hit->get(0), "a cinematic hit's frame", 1, 1200)) h.frame = *v;
                        if (auto v = integer(hit->get(1), "a cinematic hit's damage", 0, 100000)) h.damage = *v;
                        if (hit->size() == 3)
                            if (auto name = text(hit->get(2), "a cinematic hit's impact")) {
                                auto it = std::ranges::find(impacts, *name);
                                if (it == impacts.end()) it = impacts.insert(impacts.end(), *name);
                                h.impact = static_cast<std::uint16_t>(it - impacts.begin());
                            }
                        c.hits.push_back(h);
                    }
                } else {
                    error(hs, "a cinematic's `hits` is a list", "hits = [[40, 450, \"stroke\"], [55, 450, \"stroke\"]]");
                }
            }
            m.cinematic = std::move(c);
        }
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
    return read_fighter(source, file, {}, {});
}

std::expected<Fighter, std::vector<Diagnostic>> read_fighter(std::string_view source, std::string_view file, std::string_view boxes,
                                                           std::string_view boxes_file) {
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
    if (auto v = r.integer(root->get("posture"), "`posture`", 1, 100000)) f.posture_max = *v;
    r.mark(f.name + ".posture_max", root->get("posture"));
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
    if (!boxes.empty()) {
        auto baked = read_boxes(boxes, boxes_file);
        if (!baked) {
            out.insert(out.end(), baked.error().begin(), baked.error().end());
        } else {
            if (baked->fighter != f.name)
                out.push_back({std::string(boxes_file), 1, 1, std::format("baked for `{}`, not `{}`", baked->fighter, f.name), "bake it again"});
            for (auto& [name, hurt] : baked->moves) {
                auto it = std::ranges::find(f.moves, name, &MoveDef::name);
                if (it == f.moves.end()) {
                    out.push_back({std::string(boxes_file), 1, 1, std::format("`{}` has no move `{}`", f.name, name), "bake it again"});
                } else if (!it->hurt.empty()) {
                    out.push_back({std::string(boxes_file), 1, 1, std::format("`{}` has hurtboxes written by hand and baked; keep one", name), ""});
                } else {
                    it->hurt = std::move(hurt);
                }
            }
        }
    }
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
    // System 0.3's numbers: posture, clashes, deflects, supers' scaling floors.
    if (const toml::table* t = r.table(root->get("posture"), "`posture`")) {
        r.only(*t, posture_rule_keys, "`posture`");
        if (auto v = r.integer(t->get("delay"), "`posture.delay`", 0, 600)) s.posture_delay = *v;
        if (auto v = r.integer(t->get("rate"), "`posture.rate`", 0, 1000)) s.posture_rate = *v;
        if (auto v = r.integer(t->get("break_stagger"), "`posture.break_stagger`", 1, 600)) s.break_stagger = *v;
        if (auto v = r.integer(t->get("break_protect"), "`posture.break_protect`", 0, 6000)) s.break_protect = *v;
        if (auto v = r.integer(t->get("break_scaling"), "`posture.break_scaling`", 1, 100)) s.break_scaling = *v;
    }
    if (const toml::table* t = r.table(root->get("clash"), "`clash`")) {
        r.only(*t, clash_rule_keys, "`clash`");
        if (auto v = r.integer(t->get("window"), "`clash.window`", 0, 30)) s.clash_window = *v;
        if (auto v = r.integer(t->get("stop"), "`clash.stop`", 0, 120)) s.clash_stop = *v;
        if (auto v = r.integer(t->get("recovery"), "`clash.recovery`", 1, 120)) s.clash_recovery = *v;
        if (auto v = r.integer(t->get("lost"), "`clash.lost`", 1, 120)) s.clash_lost = *v;
    }
    if (const toml::table* t = r.table(root->get("deflect"), "`deflect`")) {
        r.only(*t, deflect_rule_keys, "`deflect`");
        if (auto v = r.integer(t->get("window"), "`deflect.window` (−1: none)", -1, 30)) s.deflect_window = *v;
        if (auto v = r.integer(t->get("fresh"), "`deflect.fresh`", 0, 60)) s.deflect_fresh = *v;
        if (auto v = r.integer(t->get("again"), "`deflect.again`", 0, 60)) s.deflect_again = *v;
        if (auto v = r.integer(t->get("gain"), "`deflect.gain`", 0, 30)) s.deflect_gain = *v;
    }
    if (const toml::node* n = root->get("super_scaling")) {
        const toml::array* a = n->as_array();
        if (a && a->size() == 3) {
            for (std::size_t i = 0; i < 3; ++i)
                if (auto v = r.integer(&(*a)[i], "a super's scaling floor", 0, 100)) s.super_scaling[i + 1] = *v;
        } else {
            r.error(n, "`super_scaling` is three percentages", "super_scaling = [30, 40, 50]: levels 1, 2 and 3");
        }
    }
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

namespace {

constexpr std::array<std::string_view, 5> boxes_keys{"gongfu", "fighter", "from", "move", "doc"};
constexpr std::array<std::string_view, 2> baked_move_keys{"name", "hurt"};

std::string number_text(Fixed f) { return std::format("{}", f.to_double()); }

}  // namespace

std::expected<Baked, std::vector<Diagnostic>> read_boxes(std::string_view source, std::string_view file) {
    std::vector<Diagnostic> out;
    auto root = parse(source, file, out);
    if (!root) return std::unexpected(std::move(out));
    Reader r{std::string(file), out, {}};
    r.only(*root, boxes_keys, "a boxes file");
    version(r, *root);
    Baked b;
    if (auto v = r.text(root->get("fighter"), "`fighter`")) b.fighter = *v;
    if (auto v = r.text(root->get("from"), "`from`")) b.from = *v;
    r.each_table(root->get("move"), "move", "[[move]] name = \"5LP\" hurt = [{ frames = [3, 5], boxes = [[…]] }]", [&](const toml::table& t, std::size_t) {
        r.only(t, baked_move_keys, "a baked move");
        auto name = r.text(t.get("name"), "a move's `name`");
        std::vector<HurtDef> hurt;
        if (const toml::node* h = t.get("hurt")) {
            if (const toml::array* a = h->as_array()) {
                for (const toml::node& e : *a) {
                    const toml::table* et = e.as_table();
                    if (!et) {
                        r.error(&e, "a baked hurt is { frames, boxes }");
                        continue;
                    }
                    HurtDef d;
                    if (auto f = r.frames(et->get("frames"), "a baked hurt's `frames`")) d.frames = *f;
                    if (const toml::array* bx = et->get("boxes") ? et->get("boxes")->as_array() : nullptr)
                        for (const toml::node& one : *bx)
                            if (auto x = r.box(&one, "a baked box")) d.boxes.push_back(*x);
                    hurt.push_back(std::move(d));
                }
            }
        }
        if (name) b.moves.emplace_back(*name, std::move(hurt));
    });
    if (!out.empty()) return std::unexpected(std::move(out));
    return b;
}

std::string write_boxes(const Baked& b) {
    std::string s = std::format("# {}'s hurtboxes, baked by gongfu.rules from its look and its moves: what its limbs\n"
                                "# reach out of its stance's box, frame by frame. Not edited by hand: `gongfu-studio bake`\n"
                                "# writes it again, and the build checks it is what the files make now.\n"
                                "gongfu = \"{}\"\nfighter = \"{}\"\nfrom = \"{}\"\n",
                                b.fighter, format_version, b.fighter, b.from);
    for (const auto& [name, hurt] : b.moves) {
        s += std::format("\n[[move]]\nname = \"{}\"\nhurt = [\n", name);
        for (const HurtDef& h : hurt) {
            s += std::format("  {{ frames = [{}, {}], boxes = [", h.frames.from, h.frames.to);
            for (std::size_t k = 0; k < h.boxes.size(); ++k) {
                const Box& x = h.boxes[k];
                s += std::format("{}[{}, {}, {}, {}]", k ? ", " : "", number_text(x.x), number_text(x.y), number_text(x.w), number_text(x.h));
            }
            s += "] },\n";
        }
        s += "]\n";
    }
    return s;
}

}  // namespace gongfu::data
