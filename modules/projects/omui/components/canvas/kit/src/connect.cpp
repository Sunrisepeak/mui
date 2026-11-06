// connect: edges between nodes, drawn by ⌥-dragging from one to another.
module openmui.ui.canvas.kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.canvas;
import :parts;

namespace omui::kit {

namespace {

class Connect final : public Behavior {
public:
    explicit Connect(std::string_view args) {
        if (!args.empty()) warn("canvas-kit: connect takes no arguments, not `{}`", args);
    }
    std::string_view name() const override { return "connect"; }
    int priority() const override { return 50; }

    Claim pointer(Host& h, const PointerInput& in) override {
        Scene& s = h.scene();
        switch (in.kind) {
            case PointerKind::down:
                if (!in.hit || in.button != MouseButton::left || !in.mods.has(Modifier::alt)) return Claim::pass;
                from_ = std::string(s.key(in.hit->node));
                to_.clear();
                at_ = in.screen;
                h.redraw();
                return Claim::take;
            case PointerKind::move:
                if (from_.empty()) return Claim::take;
                at_ = in.screen;
                to_ = in.hit && s.key(in.hit->node) != from_ ? std::string(s.key(in.hit->node)) : std::string{};
                h.redraw();
                return Claim::take;
            default:
                if (in.kind == PointerKind::up && !from_.empty() && !to_.empty() && s.find(from_) && s.find(to_)) connect(h, from_, to_);
                from_.clear();
                to_.clear();
                h.redraw();
                return Claim::take;
        }
    }

    bool key(Host& h, const KeyEvent& e) override {
        if (e.key != Key::escape || from_.empty()) return false;
        from_.clear();
        to_.clear();
        h.redraw();
        return true;
    }

    void paint_over(Host& h, render::DisplayList& list) override {
        if (from_.empty()) return;
        Scene& s = h.scene();
        const auto a = s.find(from_);
        if (!a) return;
        const Color c = accent(h);
        const Rect ra = screen_of(h, s.rect(*a));
        Point end = at_;
        if (const auto b = to_.empty() ? std::nullopt : s.find(to_)) {
            const Rect rb = screen_of(h, s.rect(*b));
            list.stroke_round_rect(grow(rb, 3), CornerRadii::all(static_cast<float>(s.radius(*b) * h.camera().zoom) + 3), 2, render::Paint::solid(c));
            end = rb.center();
        }
        line(list, ra.center(), end, 2, c);
        render::Path dot;
        dot.add_circle(end, 4);
        list.fill_path(dot, render::FillRule::nonzero, render::Paint::solid(c));
    }

    void describe(Host& h, NodeRef n, spec::SemanticNode& node) override {
        node.actions.push_back({"connect", spec::Effect::reversible, {}, {}, {{"to", "string"}}});
        if (!h.scene().edges_of(h.scene().key(n)).empty())
            node.actions.push_back({"disconnect", spec::Effect::reversible, {}, {}, {{"to", "string"}}});
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value& args) override {
        if (action != "connect" && action != "disconnect") return std::nullopt;
        Scene& s = h.scene();
        const std::string self(s.key(n));
        const Value* to = args.get("to");
        const auto other = to ? node_of(s, *to) : std::nullopt;
        if (!other) return ActResult::fail(std::format("{} takes `to`, the key of a node in the scene", action));
        const std::string target(s.key(*other));
        if (target == self) return ActResult::fail(std::format("a node does not {} to itself", action));
        if (action == "connect") return ActResult::done(Value(connect(h, self, target)));
        // Every edge between the two, either way.
        std::vector<std::string> gone;
        for (const world::Edge* e : s.edges_of(self))
            if ((e->from == self && e->to == target) || (e->from == target && e->to == self)) gone.push_back(e->key);
        if (gone.empty()) return ActResult::fail(std::format("`{}` and `{}` are not connected", self, target));
        for (const std::string& k : gone) {
            s.disconnect(k);
            h.committed("disconnected", Value{}.set("edge", Value(k)).set("from", Value(self)).set("to", Value(target)));
        }
        return ActResult::done();
    }

private:
    static std::string connect(Host& h, const std::string& from, const std::string& to) {
        const std::string key = h.scene().connect({.from = from, .to = to});
        h.committed("connected", Value{}.set("edge", Value(key)).set("from", Value(from)).set("to", Value(to)));
        return key;
    }

    std::string from_, to_;
    Point       at_{};
};

}  // namespace

std::unique_ptr<Behavior> make_connect(std::string_view args) { return std::make_unique<Connect>(args); }

}  // namespace omui::kit
