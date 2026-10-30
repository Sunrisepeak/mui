// The styles (0:28–0:36): the style workshop (S2) on the beat — glass,
// neon, retro, ink, liquid, and back to the blueprint. Each switch is the
// workshop's own (a style pack applied: tokens, and Slang effects on the
// window, on types and on roles; a cross-fade from the frame before). The
// corner hashes the preview's semantic tree each frame: the same under
// every pack, as S2 promises.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.theme;
import openmui.showcase.style_gallery;

namespace omui::promo {

namespace {

constexpr std::array<std::string_view, 6> packs{"blueprint", "glass", "neon", "retro", "ink", "liquid"};
constexpr std::array<std::string_view, 6> packs_zh{"蓝图", "玻璃", "霓虹", "复古", "水墨", "液态"};
constexpr Size studio_size{1440, 900};

struct State {
    double t = 0;
    Rect   studio;  // where it is drawn
};

class Styles final : public Shot {
public:
    explicit Styles(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {
        for (const auto& [t, v] : c.steps("style")) {
            const auto f = static_cast<std::int64_t>(std::llround(t * c.rate()));
            if (f > 0) switches_.push_back({f, std::clamp(static_cast<int>(std::lround(v)), 0, 5)});
            else first_ = std::clamp(static_cast<int>(std::lround(v)), 0, 5);
        }
        const Size screen = c.size;
        const bool portrait = c.portrait();
        const float top = portrait ? 300 : 150, bottom = portrait ? 420 : 210, side = portrait ? 54 : 96;
        const float k = std::min((screen.width - 2 * side) / studio_size.width, (screen.height - top - bottom) / studio_size.height);
        const float w = studio_size.width * k, h = studio_size.height * k;
        s_->studio = {std::round((screen.width - w) / 2), std::round(top + (screen.height - top - bottom - h) / 2), std::round(w), std::round(h)};
        scale_ = k;
    }

    View view() override {
        spec_ = styles::app(studio_, packs[static_cast<std::size_t>(first_)]);
        auto st = s_;
        const Rect r = s_->studio;
        Element back = painter({.id = "backdrop",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::none,
                                .paint = [st](PaintContext& p) {
                                    // The whole frame in the pack's own paper, its grid faint over it.
                                    p.list.fill_rect({0, 0, p.size.width, p.size.height}, p.theme.color("background"));
                                    const Color line = p.theme.color("accent").with_alpha(0.08f);
                                    const float step = 60, ox = std::fmod(static_cast<float>(st->t * 9), step), oy = std::fmod(static_cast<float>(st->t * 4), step);
                                    for (float x = ox; x < p.size.width; x += step) p.list.fill_rect({std::floor(x), 0, 1, p.size.height}, line);
                                    for (float y = oy; y < p.size.height; y += step) p.list.fill_rect({0, std::floor(y), p.size.width, 1}, line);
                                    const Rect& s = st->studio;
                                    p.list.box_shadow(s, CornerRadii::all(6), {0, 16}, 44, 0, hex(0x000000, 0.55f));
                                }});
        // The workshop at its own size, scaled into the frame (laid out at 1440 × 900, so it never reflows).
        Element studio = box({.id = "frame",
                              .width = Dim(studio_size.width),
                              .height = Dim(studio_size.height),
                              .translate_x = r.x - (studio_size.width - r.width) / 2,
                              .translate_y = r.y - (studio_size.height - r.height) / 2,
                              .scale = scale_,
                              .clip = true},
                             {spec_.view()});
        // Each pack's name as it comes in (the blueprint twice: first and last).
        std::vector<Element> names;
        for (std::size_t k = 0; k < segments().size(); ++k) {
            const auto [from, pack] = segments()[k];
            GlyphMotion in = GlyphMotion::typewriter(hanim::seconds(0.03));
            in.transition.delay = hanim::seconds(from);
            std::string up(packs[static_cast<std::size_t>(pack)]);
            for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            names.push_back(row({.id = std::format("name-{}", k), .gap = 16, .align = "end", .visible = false},
                                {animate_glyphs(text({.id = "en", .text = up, .style = "display", .color = Fill("text"), .wrap = false,
                                                      .size = 44.0f, .weight = 800.0f}),
                                                in),
                                 text({.id = "zh", .text = std::string(packs_zh[static_cast<std::size_t>(pack)]), .style = "title", .color = Fill("accent"),
                                       .wrap = false, .size = 26.0f, .weight = 600.0f})}));
        }
        const float side = c_.portrait() ? 54 : 96;
        Element type = column({.id = "type", .width = Dim(layout::pct(100)), .padding = Edges(Insets{c_.portrait() ? 160.0f : 80.0f, side, 0, side}), .gap = 8},
                              {row({.id = "head", .align = "end"},
                                   {stack({.id = "names", .align = "start", .grow = 1}, std::move(names)),
                                    text({.id = "tree", .text = "", .style = "code", .color = Fill("success"), .wrap = false, .size = 17.0f})})});
        return stack({.id = "styles", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {std::move(back), std::move(studio), std::move(type)});
    }

    void mounted(Runtime& rt) override {
        spec_.setup(rt);
        (void)studio_.show(packs[static_cast<std::size_t>(first_)], false);
        rt.walk([&](NodeId n, int) {
            if (!preview_ && rt.path(n).ends_with("/preview")) preview_ = n;
        });
        for (std::size_t k = 0; k < segments().size(); ++k) names_.push_back(rt.find(std::format("main/type/head/names/name-{}", k)));
        tree_ = rt.find("main/type/head/tree");
    }

    std::vector<std::int64_t> moments() const override {
        std::vector<std::int64_t> out;
        for (const auto& [f, i] : switches_) out.push_back(f);
        return out;
    }

    void moment(Runtime&, std::int64_t frame) override {
        for (const auto& [f, i] : switches_)
            if (f == frame) (void)studio_.show(packs[static_cast<std::size_t>(i)], true);
    }

    void at(Runtime& rt, double t) override {
        s_->t = t;
        std::size_t now = 0;
        for (std::size_t k = 0; k < segments().size(); ++k)
            if (segments()[k].first <= t + 1e-9) now = k;
        for (std::size_t k = 0; k < names_.size(); ++k) rt.set(names_[k], "visible", Value(k == now));
        // The preview's semantic tree, hashed: what an agent sees of the interface, under whatever pack.
        std::uint64_t h = 0xcbf29ce484222325ull;
        auto mixin = [&](std::string_view s) {
            for (unsigned char ch : s) h = (h ^ ch) * 0x100000001b3ull;
            h = (h ^ 0x1f) * 0x100000001b3ull;
        };
        std::size_t nodes = 0;
        std::function<void(NodeId)> walk = [&](NodeId n) {
            const spec::SemanticNode sn = rt.semantic_node(n);
            mixin(sn.id.substr(std::min(sn.id.size(), sn.id.find("/preview")))), mixin(spec::name(sn.role)), mixin(sn.name), mixin(sn.value.json());
            mixin(std::to_string(sn.states.bits));
            ++nodes;
            for (const NodeId c : rt.children(n)) walk(c);
        };
        if (preview_) walk(preview_);
        rt.set(tree_, "text", Value(std::format("semantic tree {:016x} · {} nodes · unchanged ✓", h, nodes)));
        rt.invalidate_paint(rt.find("main/backdrop"));
    }

    std::vector<Cue> cues(double seconds) override {
        std::vector<Cue> out;
        for (const auto& [f, i] : switches_) {
            const double at = static_cast<double>(f) / c_.rate();
            if (at < seconds) out.push_back({at, "snap", 1, 0, 1});
        }
        return out;
    }

private:
    // The packs in turn: from when (shot seconds), which.
    const std::vector<std::pair<double, int>>& segments() const {
        if (segments_.empty()) {
            segments_.push_back({0.0, first_});
            for (const auto& [f, i] : switches_) segments_.push_back({static_cast<double>(f) / c_.rate(), i});
        }
        return segments_;
    }

    ShotContext                               c_;
    std::shared_ptr<State>                    s_;
    styles::Studio                            studio_;
    AppSpec                                   spec_;
    std::vector<std::pair<std::int64_t, int>> switches_;
    int                                       first_ = 0;
    float                                     scale_ = 1;
    NodeId                                    preview_{}, tree_{};
    std::vector<NodeId>                       names_;
    mutable std::vector<std::pair<double, int>> segments_;
};

}  // namespace

std::unique_ptr<Shot> styles_shot(const ShotContext& c) { return std::make_unique<Styles>(c); }

}  // namespace omui::promo
