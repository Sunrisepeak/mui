module openmui.view;

import std;

namespace omui {

Value ValueOf<Color>::to(const Color& c) {
    auto byte = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Value(std::format("#{:02x}{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b), byte(c.a)));
}

Color ValueOf<Color>::from(const Value& v) {
    if (v.type() == Value::Type::text)
        if (auto c = Color::parse(v.as_string())) return *c;
    if (const Value::List* l = v.list(); l && (l->size() == 3 || l->size() == 4)) {
        auto at = [&](std::size_t i, double d) { return static_cast<float>(i < l->size() ? (*l)[i].as_double(d) : d); };
        return {at(0, 0), at(1, 0), at(2, 0), at(3, 1)};
    }
    if (v.map()) {
        auto at = [&](std::string_view k, double d) {
            const Value* x = v.get(k);
            return static_cast<float>(x ? x->as_double(d) : d);
        };
        return {at("r", 0), at("g", 0), at("b", 0), at("a", 1)};
    }
    return Color::transparent();
}

std::optional<Transition> Transition::token(std::string_view name) {
    std::string full = name.starts_with("motion.") ? std::string(name) : std::format("motion.{}", name);
    const spec::MotionToken* m = spec::find_motion_token(full);
    if (!m) return std::nullopt;
    if (m->stiffness > 0) return spring_with({m->stiffness, m->damping, m->mass});
    anim::Ease e = anim::parse_ease(m->easing).value_or(anim::ease::standard);
    return tween(anim::seconds(m->duration_ms / 1000.0), e);
}

Element& Element::prop(std::string name, Source s) {
    for (auto& [k, v] : props)
        if (k == name) {
            v = std::move(s);
            return *this;
        }
    props.emplace_back(std::move(name), std::move(s));
    return *this;
}

Element& Element::on(std::string event, Handler h) {
    handlers.emplace_back(std::move(event), std::move(h));
    return *this;
}

Element& Element::animate(std::string property, Transition t) {
    for (auto& [k, v] : transitions)
        if (k == property) {
            v = t;
            return *this;
        }
    transitions.emplace_back(std::move(property), t);
    return *this;
}

Element& Element::child(Element e) {
    children.push_back(std::move(e));
    return *this;
}

Element dynamic(std::function<std::vector<Element>()> build, std::source_location where) {
    Element e;
    e.region = std::make_shared<Region>(Region{std::move(build)});
    e.where = where;
    return e;
}

Element when(std::function<bool()> cond, std::function<View()> then, std::function<View()> otherwise,
             std::source_location where) {
    // The condition is a computed value: the region rebuilds when it flips,
    // not whenever what it reads changes.
    Computed<bool> c(std::move(cond));
    return dynamic(
        [c, then = std::move(then), otherwise = std::move(otherwise)] {
            std::vector<Element> out;
            bool yes = c.get();
            untracked([&] {
                if (yes) out.push_back(then());
                else if (otherwise) out.push_back(otherwise());
            });
            return out;
        },
        where);
}

}  // namespace omui
