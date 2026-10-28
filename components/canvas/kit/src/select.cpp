// select and delete: what is chosen, and removing it.
module openmui.ui.canvas.kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.canvas;
import :parts;

namespace omui::kit {

namespace {

// A click chooses a node (⇧ adds or takes it out), a click on the background
// clears; a drag on the background with ⇧ (or always, with
// "select:marquee") draws a marquee that chooses what it touches.
class Select final : public Behavior {
public:
    explicit Select(std::string_view args) : marquee_always_(args == "marquee") {
        if (!args.empty() && args != "marquee") warn("canvas-kit: select takes `marquee` or nothing, not `{}`", args);
    }
    std::string_view name() const override { return "select"; }
    int priority() const override { return 30; }

    Claim pointer(Host& h, const PointerInput& in) override {
        switch (in.kind) {
            case PointerKind::down:
                if (in.hit || in.button != MouseButton::left || in.clicks >= 2) return Claim::pass;
                if (!marquee_always_ && !in.mods.has(Modifier::shift)) return Claim::pass;
                active_ = true;
                from_ = to_ = in.world;
                base_ = in.mods.has(Modifier::shift) ? h.selection() : std::vector<std::string>{};
                h.select(base_);
                return Claim::take;
            case PointerKind::move:
                if (!active_) return Claim::pass;
                to_ = in.world;
                choose(h);
                h.redraw();
                return Claim::take;
            default:
                active_ = false;
                h.redraw();
                return Claim::take;
        }
    }

    bool tap(Host& h, const PointerInput& in) override {
        if (in.button != MouseButton::left && in.button != MouseButton::none) return false;
        const bool shift = in.mods.has(Modifier::shift);
        if (!in.hit) {
            if (!shift) h.select({});
            return true;
        }
        const std::string key(h.scene().key(in.hit->node));
        std::vector<std::string> keys = shift ? h.selection() : std::vector<std::string>{};
        if (auto it = std::ranges::find(keys, key); it != keys.end()) keys.erase(it);
        else keys.push_back(key);
        h.select(std::move(keys));
        return true;
    }

    bool key(Host& h, const KeyEvent& e) override {
        if (e.key == Key::escape && !h.selection().empty()) {
            h.select({});
            return true;
        }
        if (e.key == Key::a && (e.mods.has(Modifier::ctrl) || e.mods.has(Modifier::meta))) {
            // What is in view: a scene of a million nodes is not chosen whole by accident.
            std::vector<NodeRef> seen;
            h.scene().query(h.camera().visible(h.viewport()), seen);
            std::vector<std::string> keys;
            keys.reserve(seen.size());
            for (NodeRef n : seen) keys.emplace_back(h.scene().key(n));
            h.select(std::move(keys));
            return true;
        }
        return false;
    }

    void paint_over(Host& h, render::DisplayList& list) override {
        const Color c = accent(h);
        Scene& s = h.scene();
        const WorldRect view = h.camera().visible(h.viewport());
        const double zoom = h.camera().zoom;
        for (const std::string& key : h.selection()) {
            const auto n = s.find(key);
            if (!n || !s.rect(*n).overlaps(view)) continue;
            const Rect r = grow(screen_of(h, s.rect(*n)), 2);
            const float radius = static_cast<float>(s.radius(*n) * zoom) + 2;
            list.stroke_round_rect(r, CornerRadii::all(radius), 2, render::Paint::solid(c));
        }
        if (active_) {
            const Rect m = marquee(h);
            list.fill_rect(m, c.with_alpha(0.12f));
            list.stroke_round_rect(m, {}, 1, render::Paint::solid(c.with_alpha(0.8f)));
        }
    }

    void describe(Host& h, NodeRef n, spec::SemanticNode& node) override {
        // A canvas chooses many: `select` adds to the choice, `deselect` takes out.
        if (selected(h, h.scene().key(n))) node.actions.push_back({"deselect", spec::Effect::reversible, {}, {}, {}});
        else node.actions.push_back({"select", spec::Effect::reversible, {}, {}, {}});
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value&) override {
        const std::string key(h.scene().key(n));
        std::vector<std::string> keys = h.selection();
        if (action == "select") {
            if (std::ranges::find(keys, key) == keys.end()) keys.push_back(key);
        } else if (action == "deselect") {
            std::erase(keys, key);
        } else {
            return std::nullopt;
        }
        h.select(std::move(keys));
        return ActResult::done();
    }

private:
    Rect marquee(Host& h) const {
        const Point a = h.camera().to_screen(from_[0], from_[1], h.viewport()), b = h.camera().to_screen(to_[0], to_[1], h.viewport());
        return Rect::ltrb(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
    }

    void choose(Host& h) {
        const WorldRect r{std::min(from_[0], to_[0]), std::min(from_[1], to_[1]), std::abs(to_[0] - from_[0]), std::abs(to_[1] - from_[1])};
        std::vector<std::string> keys = base_;
        if (!r.empty()) {
            hits_.clear();
            h.scene().query(r, hits_);
            for (NodeRef n : hits_)
                if (std::string_view k = h.scene().key(n); std::ranges::find(keys, k) == keys.end()) keys.emplace_back(k);
        }
        h.select(std::move(keys));
    }

    bool                     marquee_always_ = false;
    bool                     active_ = false;
    std::array<double, 2>    from_{}, to_{};
    std::vector<std::string> base_;  // chosen before the marquee (⇧ adds to it)
    std::vector<NodeRef>     hits_;
};

// Delete or Backspace removes what is chosen; a node's `delete` removes it
// (irreversible: it states its answer, "yes").
class Delete final : public Behavior {
public:
    explicit Delete(std::string_view args) {
        if (!args.empty()) warn("canvas-kit: delete takes no arguments, not `{}`", args);
    }
    std::string_view name() const override { return "delete"; }

    bool key(Host& h, const KeyEvent& e) override {
        if ((e.key != Key::delete_ && e.key != Key::backspace) || h.selection().empty()) return false;
        const std::vector<std::string> keys = h.selection();
        remove(h, keys);
        return true;
    }

    void describe(Host&, NodeRef, spec::SemanticNode& node) override {
        node.actions.push_back({"delete", spec::Effect::irreversible, {}, "yes", {}});
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value&) override {
        if (action != "delete") return std::nullopt;
        const std::array<std::string, 1> keys{std::string(h.scene().key(n))};
        remove(h, keys);
        return ActResult::done();
    }

private:
    static void remove(Host& h, std::span<const std::string> keys) {
        Scene& s = h.scene();
        std::vector<std::string> gone;
        for (const std::string& k : keys)
            if (auto n = s.find(k); n && s.remove(*n)) gone.push_back(k);
        if (gone.empty()) return;
        std::vector<std::string> left = h.selection();
        std::erase_if(left, [&](const std::string& k) { return std::ranges::find(gone, k) != gone.end(); });
        h.select(std::move(left));
        h.committed("deleted", Value{}.set("keys", keys_value(gone)));
    }
};

}  // namespace

std::unique_ptr<Behavior> make_select(std::string_view args) { return std::make_unique<Select>(args); }
std::unique_ptr<Behavior> make_delete(std::string_view args) { return std::make_unique<Delete>(args); }

}  // namespace omui::kit
