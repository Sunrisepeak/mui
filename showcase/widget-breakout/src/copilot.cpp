module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

constexpr double every = 1.0 / 30;  // it looks thirty times a second
constexpr double line = 30 + 8 + 8;  // the top paddle's face, a ball's radius below it
constexpr double half = 70;          // the top paddle's half width
constexpr double sqrt3 = 1.7320508075688772;

// The bricks still standing, as the semantic tree states them: what is in
// the field's room panel and not hidden (nor a plate: walls, shutters), their
// middles in the field's coordinates.
std::vector<std::pair<double, double>> standing(Runtime& rt, NodeId field) {
    std::vector<std::pair<double, double>> out;
    const spec::SemanticNode f = rt.semantic_node(field);
    if (!f.bounds || f.bounds->width <= 0 || f.bounds->height <= 0) return out;
    const double sx = field_w / f.bounds->width, sy = field_h / f.bounds->height;
    for (const std::string& panel : f.children)
        for (const std::string& brick : rt.semantic_node(rt.find(panel)).children) {
            const spec::SemanticNode b = rt.semantic_node(rt.find(brick));
            if (b.states.has(spec::State::hidden) || !b.bounds || b.role == spec::Role::generic) continue;  // broken, or a plate
            out.emplace_back((b.bounds->x - f.bounds->x + b.bounds->width / 2) * sx, (b.bounds->y - f.bounds->y + b.bounds->height / 2) * sy);
        }
    return out;
}

// What it says it will do, by the order.
std::string_view word(std::string_view order) {
    if (order == "left") return "Banking left!";
    if (order == "right") return "Banking right!";
    if (order == "feed") return "Coming your way!";
    return "I've got the top.";
}

}  // namespace

void Copilot::start(Runtime& runtime, NodeId field) {
    stop();
    runtime_ = &runtime;
    field_ = field;
    last_ = -1;
    timer_ = runtime.after(seconds(every), [self = shared_from_this()] { self->tick(); });
}

void Copilot::stop() {
    if (runtime_ && timer_) runtime_->cancel(timer_);
    runtime_ = nullptr;
    timer_ = 0;
}

std::optional<double> Copilot::meets(double x, double y, double vx, double vy, double at, double width, double radius) {
    if (vy >= 0 || y <= at) return std::nullopt;
    double px = x + vx * ((y - at) / -vy);
    // Folded back into [radius, width − radius]: each wall a mirror.
    const double lo = radius, span = width - 2 * radius;
    double u = std::fmod(px - lo, 2 * span);
    if (u < 0) u += 2 * span;
    px = lo + (u <= span ? u : 2 * span - u);
    return px;
}

void Copilot::tick() {
    Runtime* rt = runtime_;
    if (!rt) return;
    if (!rt->valid(field_)) {
        stop();
        return;
    }
    timer_ = rt->after(seconds(every), [self = shared_from_this()] { self->tick(); });
    const Value v = rt->semantic_node(field_).value;
    const Value* balls = v.get("balls");
    if (!balls || !balls->list() || (v.get("over") && v.get("over")->as_int() != 0)) return;
    if (const Value* c = v.get("copilot"); c && !c->as_bool(true)) return;
    // The rising ball that reaches the line first; none rising: under the highest.
    double best = std::numeric_limits<double>::infinity(), to = -1, highest = field_h;
    std::string seen;
    for (const Value& b : *balls->list()) {
        auto f = [&](std::string_view k) { return b.get(k) ? b.get(k)->as_double() : 0.0; };
        const double x = f("x") * field_w, y = f("y") * field_h, vx = f("vx"), vy = f("vy");
        const bool waiting = b.get("on_paddle") && b.get("on_paddle")->as_bool();
        if (auto px = meets(x, y, vx, vy, line, field_w, 8)) {
            const double steps = (y - line) / -vy;
            if (steps < best) {
                best = steps;
                to = *px;
                seen = std::format("ball ({:.0f}, {:.0f}) rising, meets {:.0f}", x, y, *px);
            }
        } else if (best == std::numeric_limits<double>::infinity() && y < highest) {
            highest = y;
            to = x;
            seen = std::format("ball ({:.0f}, {:.0f}) {}: above it", x, y, waiting ? "waiting below" : "falling");
        }
    }
    // Rising: met off-centre, so it goes where it was told.
    const std::string order = v.get("order") ? std::string(v.get("order")->as_string()) : std::string("hold");
    std::string_view says = word(order);
    double aim = to;
    if (best < std::numeric_limits<double>::infinity()) {
        double depth = field_h / 2;
        if (order == "left" || order == "right") {
            aim = order == "left" ? 0 : field_w;  // off the side wall, half way down
        } else if (order == "feed") {
            aim = (v.get("paddle") ? v.get("paddle")->as_double() : 0.5) * field_w;
            depth = field_h - 30;
        } else {
            double nearest = std::numeric_limits<double>::infinity();
            for (const auto& [bx, by] : standing(*rt, field_))
                if (std::abs(bx - to) < nearest) nearest = std::abs(bx - to), aim = bx, depth = by;
        }
        const double offset = std::clamp((aim - to) / ((depth - line) * sqrt3), -0.85, 0.85);  // the bounce: (offset·√3, 1)
        to -= offset * half;
        seen += std::format(", aims at {:.0f}", aim);
    }
    if (v.get("finisher") && v.get("finisher")->as_bool()) says = "Now — together!";
    if (to < 0 || (std::abs(to - last_) < 6 && says == said_)) return;  // where it is going already
    last_ = to;
    said_ = std::string(says);
    Invocation call;
    call.actor = {"agent", "copilot", {}};
    call.args = Value{}.set("x", to / field_w).set("seen", seen).set("says", std::string(says)).set("aim", aim / field_w);
    (void)rt->act(field_, "guard_to", call);
}

}  // namespace omui::breakout
