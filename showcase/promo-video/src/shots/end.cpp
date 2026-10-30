// The end (0:55–1:00): back into the pixel the film began with — the panel
// shrinks to its button and the camera dives into that one pixel — which
// becomes the cursor of a command line, and the command that starts an
// openmui program is typed. Under it, the film itself as data: this cut's
// timeline in openmui.ui.timeline's editor, its playhead on this frame.
// Then the closing line.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.ui.timeline;

namespace omui::promo {

namespace {

constexpr std::string_view command = "$ mcpp new hello --template openmui";

struct State {
    double t = 0, zoom = 1, typed = 0, close = 0, caret = 0;  // caret: 0 the pixel, 1 the cursor
    float  caret_away = 0;  // the cursor going with the command
    Point  look, at;
    Rect   cursor;     // the cursor's place at the end of the typed text
    float  advance = 20;  // a character of the command's font
    Point  line;       // the command's top left
    float  line_height = 40;
    std::pair<render::ImageRef, render::ImageRef> refs;
};

class End final : public Shot {
public:
    explicit End(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {
        const auto typed = c.steps("typed");
        typed_from_ = typed.empty() ? 1.6 : typed.front().first;
    }

    View view() override {
        const bool portrait = c_.portrait();
        const Size screen = c_.size;
        auto st = s_;
        const float size = portrait ? 30.0f : 36.0f;
        Element back = painter({.id = "backdrop",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::figure,
                                .label = "The pixel the film began with",
                                .paint = [st](PaintContext& p) { paint(p, *st); }});
        GlyphMotion type = GlyphMotion::typewriter(hanim::seconds(0.025));
        type.transition.delay = hanim::seconds(typed_from_);
        const auto typed = c_.steps("typed");
        if (typed.size() >= 2) type.stagger = hanim::seconds((typed.back().first - typed.front().first) / static_cast<double>(command.size()));
        stagger_ = type.stagger.count();
        const auto closing = c_.steps("close");
        const double close_from = closing.empty() ? 3.2 : closing.front().first;
        GlyphMotion rise = GlyphMotion::rise(hanim::seconds(0.05));
        rise.transition.delay = hanim::seconds(close_from);
        GlyphMotion rise2 = rise;
        rise2.transition.delay = hanim::seconds(close_from + 0.15);
        GlyphMotion gather = GlyphMotion::gather(5);
        gather.transition.delay = hanim::seconds(close_from - 0.1);
        const std::int32_t frame0 = static_cast<std::int32_t>(tl::frame_at(c_.cut->timeline.rate, c_.clip->at));
        Element editor = timeline_editor({.id = "film",
                                          .document = c_.cut->text,
                                          .frame = frame,
                                          .zoom = (portrait ? 960.0 - 112 : 1500.0 - 112) / std::max(1.0, tl::to_seconds(c_.cut->timeline.duration())),
                                          .width = Dim(portrait ? 960.0f : 1500.0f),
                                          .height = 210,
                                          .label = "This cut's timeline"});
        (void)frame0;
        // First the command and the film as data; then they give way to a clean card.
        Element first = column(
            {.id = "first", .width = Dim(layout::pct(100)), .padding = Edges(Insets{portrait ? 420.0f : 260.0f, 0, 0, 0}), .gap = 30, .align = "center"},
            {animate_glyphs(text({.id = "command", .text = std::string(command), .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = size,
                                  .weight = 500.0f}),
                            type),
             column({.id = "data", .gap = 8, .align = "start", .opacity = 0.0f},
                    {text({.id = "caption", .text = "this film is data — promo.timeline.omui.toml, and scenes in C++", .style = "code",
                           .color = Fill(hex(ink::muted)), .wrap = false, .size = 15.0f}),
                     std::move(editor)})});
        Element card = column(
            {.id = "card", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .gap = 14, .justify = "center", .align = "center"},
            {animate_glyphs(text({.id = "wordmark", .text = "OPENMUI", .style = "display", .color = Fill(hex(ink::cyan)), .wrap = false,
                                  .size = portrait ? 110.0f : 150.0f, .weight = 800.0f}),
                            gather),
             animate_glyphs(text({.id = "zh", .text = "这部片子由 agent 用 openmui 制作 · 源码见仓库", .style = "title", .color = Fill(hex(ink::ice)), .wrap = false,
                                  .size = portrait ? 30.0f : 36.0f, .weight = 600.0f}),
                            rise),
             animate_glyphs(text({.id = "en", .text = "Made by an agent with openmui · the source: showcase/promo-video", .style = "body",
                                  .color = Fill(hex(ink::muted)), .wrap = portrait, .size = portrait ? 20.0f : 24.0f, .max_width = Dim(screen.width * 0.86f)}),
                            rise2)});
        Element type_layer = stack({.id = "type", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                                   {std::move(first), std::move(card)});
        return stack({.id = "end", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"}, {std::move(back), std::move(type_layer)});
    }

    void mounted(Runtime& rt) override {
        const PanelPicture& panel = panel_picture();
        s_->refs = {rt.images().add(panel.x1), rt.images().add(panel.x2)};
        rt.frame({hanim::seconds(0), {}});
        const Rect cmd = rt.drawn_bounds(rt.find("main/type/first/command"));
        s_->line = {cmd.x, cmd.y};
        s_->line_height = cmd.height;
        s_->advance = cmd.width / static_cast<float>(text_length(command));
        back_ = rt.find("main/backdrop");
        data_ = rt.find("main/type/first/data");
        first_ = rt.find("main/type/first");
    }

    void at(Runtime& rt, double t) override {
        State& s = *s_;
        const PanelPicture& panel = panel_picture();
        const Size screen = c_.size;
        s.t = t;
        const double dive = c_.value("dive", t, 9.6);
        s.zoom = hanim::dm::exp(dive * hanim::dm::ln2);
        const Point pixel{panel.pixel.x + 0.5f, panel.pixel.y + 0.5f}, middle{panel.size.width / 2, panel.size.height / 2};
        const double u = smooth(dive, 0.2, 5.5);
        s.look = {static_cast<float>(mix(middle.x, pixel.x, u)), static_cast<float>(mix(middle.y, pixel.y, u))};
        s.at = {screen.width / 2, screen.height / 2};
        s.caret = smooth(t, typed_from_ - 0.3, typed_from_);
        // The cursor after what is typed.
        const double chars = std::clamp((t - typed_from_) / std::max(1e-6, stagger_), 0.0, static_cast<double>(text_length(command)));
        s.cursor = {s.line.x + static_cast<float>(std::ceil(chars)) * s.advance + 4, s.line.y + s.line_height * 0.12f, s.advance * 0.62f, s.line_height * 0.8f};
        const auto closing = c_.steps("close");
        const double close_from = closing.empty() ? 3.2 : closing.front().first;
        rt.set(data_, "opacity", Value(smooth(t, typed_from_ + 0.5, typed_from_ + 0.9)));
        rt.set(data_, "translate_y", Value((1 - smooth(t, typed_from_ + 0.5, typed_from_ + 0.9)) * 18));
        frame.set(static_cast<std::int32_t>(tl::frame_at(c_.cut->timeline.rate, c_.clip->at) + std::llround((t - tl::to_seconds(c_.clip->in)) * c_.rate())));
        // The command and the editor give way as the card comes.
        const double away = smooth(t, close_from - 0.2, close_from + 0.15);
        rt.set(first_, "opacity", Value(1 - away));
        s.caret_away = static_cast<float>(away);
        rt.invalidate_paint(back_);
    }

    std::vector<Cue> cues(double seconds) override {
        std::vector<Cue> out;
        for (std::size_t k = 0; k < text_length(command); k += 2) {
            const double at = typed_from_ + static_cast<double>(k) * stagger_;
            if (at < seconds && command[k] != ' ') out.push_back({at, "tick", 0.8f, 0, 1.0f + static_cast<float>(k % 5) * 0.03f});
        }
        return out;
    }

private:
    static std::size_t text_length(std::string_view s) {
        std::size_t n = 0;
        for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
        return n;
    }
    static void paint(PaintContext& p, State& s);

    ShotContext                  c_;
    std::shared_ptr<State>       s_;
    double                       typed_from_ = 1.6, stagger_ = 0.025;
    Signal<std::int32_t>         frame = signal(std::int32_t{0});
    NodeId                       back_{}, data_{}, first_{};
};

void End::paint(PaintContext& p, State& s) {
    auto& list = p.list;
    const Size screen = p.size;
    const PanelPicture& panel = panel_picture();
    paint_paper(list, screen, {}, 1);
    // The dive: the panel, the button, the pixel filling the screen.
    const Point p0 = panel_to_screen(panel.pixel, s.at, s.look, s.zoom);
    const Point p1 = panel_to_screen({panel.pixel.x + 1, panel.pixel.y + 1}, s.at, s.look, s.zoom);
    const Rect pix{p0.x, p0.y, p1.x - p0.x, p1.y - p0.y};
    if (s.caret < 1) {
        list.push_layer(static_cast<float>(1 - s.caret));
        paint_panel(list, p.services.images(), s.refs, panel, screen, s.at, s.look, s.zoom);
        list.pop_layer();
    }
    // The pixel becomes the cursor: from where it is to after the command's last letter.
    if (s.caret > 0) {
        const double u = hanim::ease::in_out_cubic(s.caret);
        const Rect r{static_cast<float>(mix(pix.x, s.cursor.x, u)), static_cast<float>(mix(pix.y, s.cursor.y, u)), static_cast<float>(mix(pix.width, s.cursor.width, u)),
                     static_cast<float>(mix(pix.height, s.cursor.height, u))};
        // A cursor blinks twice a second once it has landed.
        const bool on = s.caret < 1 || std::fmod(s.t * 2, 1.0) < 0.62;
        if (on && s.caret_away < 1) list.fill_rect(r, hex(ink::cyan, 1 - s.caret_away));
    }
}

}  // namespace

std::unique_ptr<Shot> end_shot(const ShotContext& c) { return std::make_unique<End>(c); }

}  // namespace omui::promo
