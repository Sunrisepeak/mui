module omui.semantic;

import std;
import mlibs.error;
import omui.core;

namespace omui::semantic {

namespace {

std::size_t char_length(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xe) return 3;
    if ((lead >> 3) == 0x1e) return 4;
    return 1;
}

bool is_interactive(const spec::SemanticNode& n) {
    if (n.states.has(State::focusable)) return true;
    return std::ranges::any_of(n.actions, [](const spec::ActionDesc& a) { return a.effect != Effect::read; });
}

// A wanted value against a node's: a number is its value however it was written (JSON's 80 is a slider's
// 80.0), and is a range's ({value, min, max, step}) value now; anything else, as it is.
bool same_value(const Value& has, const Value& wanted) {
    if (!wanted.is_number()) return has == wanted;
    const Value* now = has.is_number() ? &has : has.map() ? has.get("value") : nullptr;
    return now && now->is_number() && now->as_double() == wanted.as_double();
}

std::expected<std::vector<State>, QueryFailure> states_of(const Value& v, std::string_view field) {
    std::vector<State> out;
    const Value::List* l = v.list();
    if (!l) return std::unexpected(QueryFailure{QueryError::not_a_list, std::format("`{}` is a list of state names", field), std::string(field)});
    for (const Value& s : *l) {
        auto st = spec::parse_state(s.as_string());
        if (!st) return std::unexpected(QueryFailure{QueryError::unknown_state, std::format("`{}`: no state `{}`", field, s.as_string()), std::string(field)});
        out.push_back(*st);
    }
    return out;
}

}  // namespace

bool glob(std::string_view p, std::string_view t) noexcept {
    // Iterative matching with one backtrack point (the last star).
    std::size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (pi < p.size() && p[pi] == '?') {
            ++pi;
            ti += char_length(static_cast<unsigned char>(t[ti]));
        } else if (pi < p.size() && p[pi] == t[ti]) {
            ++pi;
            ++ti;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            mark += char_length(static_cast<unsigned char>(t[mark]));
            ti = mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

bool Query::matches(const spec::SemanticNode& n) const {
    if (!id.empty() && n.id != id) return false;
    if (!under.empty()) {
        // The root ("app") is above every path; any other node is above the paths it prefixes.
        const bool inside = under == "app" ? n.id != "app"
                                           : n.id.size() > under.size() && n.id.starts_with(under) && n.id[under.size()] == '/';
        if (!inside) return false;
    }
    if (role && n.role != *role) return false;
    if (!name.empty() && !glob(name, n.name)) return false;
    for (State s : states)
        if (!n.states.has(s)) return false;
    for (State s : without)
        if (n.states.has(s)) return false;
    if (value && !same_value(n.value, *value)) return false;
    if (within && (!n.bounds || !from_spec(*n.bounds).intersects(*within))) return false;
    if (at && (!n.bounds || !from_spec(*n.bounds).contains(*at))) return false;
    if (interactive && !is_interactive(n)) return false;
    return true;
}

std::expected<Query, QueryFailure> Query::parse(const Value& v) {
    if (!v.map()) return std::unexpected(QueryFailure{QueryError::not_an_object, "a query is an object"});
    Query q;
    for (const auto& [k, x] : *v.map()) {
        if (k == "id") q.id = std::string(x.as_string());
        else if (k == "under") q.under = std::string(x.as_string());
        else if (k == "role") {
            auto r = spec::parse_role(x.as_string());
            if (!r) return std::unexpected(QueryFailure{QueryError::unknown_role, std::format("no role `{}`", x.as_string()), "role"});
            q.role = *r;
        } else if (k == "name") q.name = std::string(x.as_string());
        else if (k == "state") {
            auto s = states_of(x, k);
            if (!s) return std::unexpected(s.error());
            q.states = std::move(*s);
        } else if (k == "not") {
            auto s = states_of(x, k);
            if (!s) return std::unexpected(s.error());
            q.without = std::move(*s);
        } else if (k == "value") q.value = x;
        else if (k == "within") {
            const Value::List* l = x.list();
            if (!l || l->size() != 4) return std::unexpected(QueryFailure{QueryError::bad_rect, "`within` is [x, y, width, height]", "within"});
            q.within = Rect{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()),
                            static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
        } else if (k == "at") {
            const Value::List* l = x.list();
            if (!l || l->size() != 2) return std::unexpected(QueryFailure{QueryError::bad_point, "`at` is [x, y]", "at"});
            q.at = Point{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double())};
        } else if (k == "interactive") q.interactive = x.as_bool();
        else if (k == "limit") q.limit = static_cast<std::size_t>(std::max<std::int64_t>(0, x.as_int()));
        else return std::unexpected(QueryFailure{QueryError::unknown_field, std::format("a query has no field `{}` (id, under, role, name, state, not, value, within, at, interactive, limit)", k), k});
    }
    return q;
}

Value Query::to_value() const {
    Value v = Value(Value::Map{});
    if (!id.empty()) v.set("id", id);
    if (!under.empty()) v.set("under", under);
    if (role) v.set("role", spec::name(*role));
    if (!name.empty()) v.set("name", name);
    auto list = [](const std::vector<State>& s) {
        Value::List l;
        for (State x : s) l.emplace_back(spec::name(x));
        return Value(std::move(l));
    };
    if (!states.empty()) v.set("state", list(states));
    if (!without.empty()) v.set("not", list(without));
    if (value) v.set("value", *value);
    if (within)
        v.set("within", Value(Value::List{Value(within->x), Value(within->y), Value(within->width), Value(within->height)}));
    if (at) v.set("at", Value(Value::List{Value(at->x), Value(at->y)}));
    if (interactive) v.set("interactive", true);
    if (limit) v.set("limit", static_cast<std::int64_t>(limit));
    return v;
}

std::expected<Condition, QueryFailure> Condition::parse(const Value& v) {
    if (!v.map()) return std::unexpected(QueryFailure{QueryError::not_an_object, "a condition is an object"});
    Condition c;
    for (const auto& [k, x] : *v.map()) {
        if (k == "q") {
            auto q = Query::parse(x);
            if (!q) { auto failure = std::move(q.error()); failure.where = "q/" + failure.where; return std::unexpected(std::move(failure)); }
            c.query = std::move(*q);
        } else if (k == "absent") c.absent = x.as_bool();
        else if (k == "settled") c.settled = x.as_bool();
        else if (k != "timeout") return std::unexpected(QueryFailure{QueryError::unknown_field, std::format("a condition has no field `{}` (q, absent, settled, timeout)", k), k});
    }
    if (!c.query && !c.settled) return std::unexpected(QueryFailure{QueryError::nothing_to_wait_for, "a condition needs `q` or `settled`"});
    return c;
}

}  // namespace omui::semantic
