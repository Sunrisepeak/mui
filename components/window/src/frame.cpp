module openmui.ui.window;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.ui.base;

namespace omui {

using parts::keep_frames;
using parts::mix;

namespace {

constexpr float standard_width = 46;  // a standard button: as wide as Windows' own
constexpr float traffic_size = 12;     // a circle's diameter
constexpr float traffic_gap = 8;
constexpr float edge_inset = 12;       // the buttons' distance from the title bar's ends (traffic)

// The texts the frame shows or names, as keys with the English text in the
// source catalog and Chinese beside it.
constexpr std::string_view messages_en = R"([window]
minimize = "Minimize"
maximize = "Maximize"
restore = "Restore"
close = "Close"
)";
constexpr std::string_view messages_zh_hans = R"([window]
minimize = "最小化"
maximize = "最大化"
restore = "还原"
close = "关闭"
)";
constexpr std::string_view messages_zh_hant = R"([window]
minimize = "最小化"
maximize = "最大化"
restore = "還原"
close = "關閉"
)";

std::string_view kind_name(CaptionButtonKind k) {
    switch (k) {
        case CaptionButtonKind::minimize: return "minimize";
        case CaptionButtonKind::maximize: return "maximize";
        case CaptionButtonKind::restore: return "restore";
        case CaptionButtonKind::close: break;
    }
    return "close";
}

// The traffic lights' colours (macOS's), and the grey of an inactive window's.
Color traffic_color(CaptionButtonKind k, bool active, const Theme& theme) {
    if (!active) return theme.color(theme.dark ? "#4b5563" : "#d1d5db");
    switch (k) {
        case CaptionButtonKind::close: return theme.color("#ff5f57");
        case CaptionButtonKind::minimize: return theme.color("#febc2e");
        case CaptionButtonKind::maximize:
        case CaptionButtonKind::restore: break;
    }
    return theme.color("#28c840");
}

render::Paint solid(Color c) { return render::Paint::solid(c); }

}  // namespace

// ── window_frame ─────────────────────────────────────────────────────────

void WindowFrame::mounted() {
    active_.attach(services(), window().active() ? 1 : 0);
    title_.set_text(props.title);
    title_.set_style("label");
    // The window's state decides the frame's shape and its buttons: what the
    // watch reads, it follows (the first run only subscribes).
    bool first = true;
    watch_ = Watch([this, first]() mutable {
        (void)window().state_name.get();
        (void)window().is_framework.get();
        (void)window().is_active.get();
        (void)window().is_transparent.get();
        if (std::exchange(first, false)) return;
        untracked([this] {
            services().restyle(node());
            services().recompose(node());
            services().invalidate_paint(node());
        });
    });
}

void WindowFrame::prop_changed(WindowFrameProp p) {
    if (p == WindowFrameProp::title) title_.set_text(props.title);
    if (!attached()) return;
    switch (p) {
        case WindowFrameProp::title: services().invalidate_paint(node()); break;
        case WindowFrameProp::radius: services().invalidate_paint(node()); break;
        default:
            services().restyle(node());
            services().recompose(node());
            services().invalidate_paint(node());
    }
}

void WindowFrame::restyle() { title_.set_style("label"); }

bool WindowFrame::drawn() const { return window().framework_drawn(); }

bool WindowFrame::fills_screen() const {
    const WindowState s = window().state();
    return s == WindowState::maximized || s == WindowState::fullscreen;
}

float WindowFrame::inset() const { return content_insets(props).left; }

Insets WindowFrame::content_insets(const WindowFrameProps& p) {
    if (!window().framework_drawn()) return {};
    const WindowState s = window().state();
    const bool fills = s == WindowState::maximized || s == WindowState::fullscreen;
    const float i = !fills && window().transparent() ? static_cast<float>(std::max(0.0, p.shadow)) : 0;
    const float bar = s == WindowState::fullscreen ? 0 : static_cast<float>(std::max(0.0, p.title_bar));
    return {i + bar, i, i, i};
}

Rect WindowFrame::title_bar() const {
    if (!drawn() || window().state() == WindowState::fullscreen || !attached()) return {};
    const Rect b = services().bounds(node());
    const float i = inset();
    return {i, i, std::max(0.0f, b.width - 2 * i), static_cast<float>(props.title_bar)};
}

void WindowFrame::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    const float i = inset();
    const float bar = drawn() && window().state() != WindowState::fullscreen ? static_cast<float>(props.title_bar) : 0;
    s.padding = {i + bar, i, i, i};
}

void WindowFrame::paint(PaintContext& ctx) const {
    if (!drawn()) return;
    const Theme& t = ctx.theme;
    active_.to(window().active() ? 1.0 : 0.0, t.transition("motion.fast"));
    const float active = static_cast<float>(active_.value());
    const float i = inset();
    const Rect edge{i, i, std::max(0.0f, ctx.size.width - 2 * i), std::max(0.0f, ctx.size.height - 2 * i)};
    const float r = fills_screen() ? 0 : static_cast<float>(std::max(0.0, props.radius));
    const CornerRadii radii = CornerRadii::all(r).fitted({edge.width, edge.height});
    if (i > 0) {
        // Deeper for the active window, as the systems draw it.
        const Color shadow = t.color("#0f172a / 0.30");
        ctx.list.box_shadow(edge, radii, {0, i * 0.25f}, i * (0.6f + 0.4f * active), 0, Color{shadow.r, shadow.g, shadow.b, shadow.a * (0.55f + 0.45f * active)});
    }
    ctx.list.fill_round_rect(edge, radii, solid(t.color("background", Color::white())));
    const Rect bar = title_bar();
    if (bar.height > 0) {
        CornerRadii top = radii;
        top.bottom_left = top.bottom_right = 0;
        ctx.list.fill_round_rect(bar, top, solid(mix(t.color("surface_variant"), t.color("surface"), 1 - active)));
        ctx.list.fill_rect({bar.x, bar.y + bar.height - 1, bar.width, 1}, t.color("border"));
        const Color text = mix(t.color("text_muted"), t.color("text"), active);
        const Size s = title_.measure(ctx.services, text);
        // Centred on the window, as macOS and GNOME do; never under the buttons.
        float x = bar.x + (bar.width - s.width) / 2;
        const float reserve = props.controls == WindowFrameControls::none ? 0 : props.look == WindowFrameLook::traffic ? edge_inset + 3 * traffic_size + 2 * traffic_gap + 12 : 3 * standard_width;
        if (props.controls == WindowFrameControls::end) x = std::min(x, bar.x + bar.width - reserve - s.width - 8);
        if (props.controls == WindowFrameControls::start) x = std::max(x, bar.x + reserve + 8);
        if (!props.title.empty()) title_.paint(ctx, {std::max(bar.x + 8, x), bar.y + (bar.height - s.height) / 2}, text);
    }
    ctx.list.stroke_round_rect({edge.x + 0.5f, edge.y + 0.5f, edge.width - 1, edge.height - 1}, radii, 1,
                               solid(mix(t.color("border"), t.color("border_strong"), active * 0.5f)));
    keep_frames(ctx, {&active_});
}

void WindowFrame::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title;  // the window's name (S-6)
}

std::vector<Element> WindowFrame::compose() {
    if (!drawn() || props.controls == WindowFrameControls::none || window().state() == WindowState::fullscreen) return {};
    const bool traffic = props.look == WindowFrameLook::traffic;
    const bool start = props.controls == WindowFrameControls::start;
    const std::string look = traffic ? "traffic" : "standard";
    const bool active = window().active();
    auto button = [&](std::string id, std::string kind, std::string verb) {
        Element b;
        b.type = &component_type<CaptionButton>();
        b.id = std::move(id);
        b.prop("look", Source{Value(look), {}, {}});
        b.prop("active", Source{Value(active), {}, {}});
        b.prop("verb", Source{Value(std::move(verb)), {}, {}});
        // Its name in the person's language, following a change of language.
        b.prop("label", tr(std::format("window.{}", kind)));
        b.prop("kind", Source{Value(std::move(kind)), {}, {}});
        return b;
    };
    std::vector<Element> buttons;
    const std::string maximize = window().state() == WindowState::maximized ? "restore" : "maximize";
    if (props.minimizable) buttons.push_back(button("minimize", "minimize", "window.minimize"));
    if (props.maximizable) buttons.push_back(button("maximize", maximize, "window.toggle_maximize"));
    buttons.push_back(button("close", "close", "window.close"));
    // macOS's order at the start: close, minimize, maximize.
    if (start) std::ranges::rotate(buttons, buttons.end() - 1);
    const float i = inset();
    const float h = static_cast<float>(props.title_bar);
    Element bar = row({.id = "controls", .height = Dim(h), .gap = traffic ? traffic_gap : 0.0f, .align = "center"}, std::move(buttons));
    bar.prop("position", Source{Value("absolute"), {}, {}});
    bar.prop("top", Source{Value(static_cast<double>(i)), {}, {}});
    bar.prop(start ? "left" : "right", Source{Value(static_cast<double>(i + (traffic ? edge_inset : 0))), {}, {}});
    return {std::move(bar)};
}

bool WindowFrame::on_event(EventContext& ctx, const InputEvent& e) {
    // A double click on the title bar maximizes, or restores (where the
    // system does not do it itself).
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || p->kind != PointerKind::down || p->clicks != 2 || !props.maximizable) return false;
    if (!title_bar().contains(ctx.local)) return false;
    (void)run("window.toggle_maximize");
    return true;
}

std::optional<WindowArea> WindowFrame::window_area(Point local, Size size) const {
    if (!drawn()) return std::nullopt;
    if (window().state() == WindowState::normal) {
        // The shadow's reach and `resize` in from the edge resize the window.
        const float reach = inset() + static_cast<float>(std::max(0.0, props.resize));
        const bool left = local.x < reach, right = local.x >= size.width - reach;
        const bool top = local.y < reach, bottom = local.y >= size.height - reach;
        // Corners reach further along the edges, as the systems' do.
        const float corner = reach + static_cast<float>(props.radius);
        const bool near_left = local.x < corner, near_right = local.x >= size.width - corner;
        const bool near_top = local.y < corner, near_bottom = local.y >= size.height - corner;
        if ((top && near_left) || (left && near_top)) return WindowArea::resize_top_left;
        if ((top && near_right) || (right && near_top)) return WindowArea::resize_top_right;
        if ((bottom && near_left) || (left && near_bottom)) return WindowArea::resize_bottom_left;
        if ((bottom && near_right) || (right && near_bottom)) return WindowArea::resize_bottom_right;
        if (top) return WindowArea::resize_top;
        if (bottom) return WindowArea::resize_bottom;
        if (left) return WindowArea::resize_left;
        if (right) return WindowArea::resize_right;
    }
    if (title_bar().contains(local)) return WindowArea::drag;
    return std::nullopt;
}

ActResult WindowFrame::run(std::string_view verb) {
    Envelope e = services().verbs().invoke(verb, Invocation{});
    if (e.exit_code().value_or(1) == 0) return ActResult::done();
    for (const auto& r : e.records())
        if (r.kind == spec::RecordKind::error)
            if (const Value* text = r.body.get("text")) return ActResult::fail(std::string(text->as_string()));
    return ActResult::fail(std::format("{} did not run", verb));
}

ActResult WindowFrame::minimize(ActContext&) { return run("window.minimize"); }
ActResult WindowFrame::toggle_maximize(ActContext&) { return run("window.toggle_maximize"); }
ActResult WindowFrame::close(ActContext&) { return run("window.close"); }

// ── caption_button ───────────────────────────────────────────────────────

void CaptionButton::mounted() { hover_.attach(services(), 0); }

std::string_view CaptionButton::verb() const noexcept {
    switch (props.kind) {
        case CaptionButtonKind::minimize: return "window.minimize";
        case CaptionButtonKind::maximize:
        case CaptionButtonKind::restore: return "window.toggle_maximize";
        case CaptionButtonKind::close: break;
    }
    return "window.close";
}

void CaptionButton::style(const Theme& theme, layout::Style& s) const {
    if (props.look == CaptionButtonLook::traffic) {
        s.width = s.height = layout::pt(theme.scaled(traffic_size));
    } else {
        s.width = layout::pt(theme.scaled(standard_width));
        s.height = layout::Length::percent(100);
    }
}

void CaptionButton::paint(PaintContext& ctx) const {
    const Theme& t = ctx.theme;
    hover_.to(ctx.state.hovered || ctx.state.pressed ? 1.0 : 0.0, t.transition("motion.fast"));
    const float h = static_cast<float>(hover_.value());
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const Point c = box.center();
    const bool close = props.kind == CaptionButtonKind::close;
    if (props.look == CaptionButtonLook::traffic) {
        const float r = std::min(box.width, box.height) / 2;
        Color fill = traffic_color(props.kind, props.active || h > 0, t);
        if (ctx.state.pressed) fill = mix(fill, Color::black(), 0.2f);
        ctx.list.fill_path(render::Path{}.add_circle(c, r), render::FillRule::nonzero, solid(fill));
        ctx.list.stroke_path(render::Path{}.add_circle(c, r - 0.25f), {.width = 0.5f}, solid(mix(fill, Color::black(), 0.18f)));
        if (h > 0) {
            // The glyph shows while the pointer is over it.
            const Color g = Color{0, 0, 0, 0.55f * h};
            const float k = r * 0.45f;
            render::Path p;
            switch (props.kind) {
                case CaptionButtonKind::close:
                    p.move_to({c.x - k, c.y - k}).line_to({c.x + k, c.y + k}).move_to({c.x + k, c.y - k}).line_to({c.x - k, c.y + k});
                    break;
                case CaptionButtonKind::minimize: p.move_to({c.x - k * 1.2f, c.y}).line_to({c.x + k * 1.2f, c.y}); break;
                case CaptionButtonKind::maximize:
                case CaptionButtonKind::restore:
                    p.move_to({c.x - k, c.y}).line_to({c.x + k, c.y}).move_to({c.x, c.y - k}).line_to({c.x, c.y + k});
                    break;
            }
            ctx.list.stroke_path(p, {.width = 1.2f, .cap = render::LineCap::round}, solid(g));
        }
    } else {
        const Color over = close ? t.color("danger") : t.color("surface_pressed");
        if (h > 0) ctx.list.fill_rect(box, Color{over.r, over.g, over.b, over.a * h * (ctx.state.pressed ? 1.0f : 0.85f)});
        const Color base = mix(t.color("text_muted"), t.color("text"), props.active ? 1.0f : 0.0f);
        const Color g = close ? mix(base, t.color("on_danger"), h) : base;
        const float k = t.scaled(5);
        const float x0 = std::round(c.x) + 0.5f, y0 = std::round(c.y) + 0.5f;
        render::Path p;
        switch (props.kind) {
            case CaptionButtonKind::minimize: p.move_to({x0 - k, y0}).line_to({x0 + k, y0}); break;
            case CaptionButtonKind::maximize: p.add_rect({x0 - k, y0 - k, 2 * k, 2 * k}); break;
            case CaptionButtonKind::restore:
                p.add_rect({x0 - k, y0 - k + 2, 2 * k - 2, 2 * k - 2});
                p.move_to({x0 - k + 2, y0 - k}).line_to({x0 + k, y0 - k}).line_to({x0 + k, y0 + k - 2});
                break;
            case CaptionButtonKind::close:
                p.move_to({x0 - k, y0 - k}).line_to({x0 + k, y0 + k}).move_to({x0 + k, y0 - k}).line_to({x0 - k, y0 + k});
                break;
        }
        ctx.list.stroke_path(p, {.width = 1}, solid(g));
    }
    keep_frames(ctx, {&hover_});
}

void CaptionButton::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible) {
        const Rect box{0, 0, ctx.size.width, ctx.size.height};
        const float r = props.look == CaptionButtonLook::traffic ? std::min(box.width, box.height) / 2 : 0;
        parts::focus_ring(ctx, box, CornerRadii::all(r));
    }
}

void CaptionButton::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (!node.name.empty()) return;
    node.name = props.label.empty() ? translate(std::format("window.{}", kind_name(props.kind)), Value{}) : props.label;
}

ActResult CaptionButton::activate(ActContext&) {
    emit_activated();
    return ActResult::done();
}

FramedWindow::FramedWindow(Runtime& runtime, WindowFrameOptions options, std::string slot) : runtime_(&runtime), slot_(std::move(slot)) {
    // The properties the insets come from, as the frame will have them.
    const auto number = [](const Prop<double>& p, double fallback) { return p.is_set() ? untracked([&] { return p.source().current(); }).as_double(fallback) : fallback; };
    props_.shadow = number(options.shadow, props_.shadow);
    props_.title_bar = number(options.title_bar, props_.title_bar);
    runtime.mount_beneath("window", window_frame(std::move(options)));
    watch_ = Watch([this] {
        (void)window().state_name.get();
        (void)window().is_framework.get();
        (void)window().is_transparent.get();
        const Insets insets = WindowFrame::content_insets(props_);
        untracked([&] { runtime_->set_slot_insets(slot_, insets); });
    });
}

FramedWindow::~FramedWindow() {
    watch_.dispose();
    runtime_->set_slot_insets(slot_, {});
    runtime_->unmount("window");
}

void register_window_components() {
    register_type(component_type<WindowFrame>());
    register_type(component_type<CaptionButton>());
    static const bool texts = [] {
        Languages& l = languages();
        for (auto [tag, toml] : {std::pair{"en", messages_en}, {"zh-Hans", messages_zh_hans}, {"zh-Hant", messages_zh_hant}})
            if (auto loc = i18n::Locale::parse(tag))
                if (auto r = l.add_messages(*loc, toml, std::format("openmui.ui.window.{}.messages.omui.toml", tag)); !r)
                    for (const auto& d : r.error()) warn("{}", d.text());
        return true;
    }();
    (void)texts;
}

}  // namespace omui
