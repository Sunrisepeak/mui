// What the system draws inside the interface (openmui.ui.base:external): a
// web page in the system's own web view, placed by the host over the node.
module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

bool web_address(std::string_view url) { return url.starts_with("https://") || url.starts_with("http://"); }

}  // namespace

WebView::~WebView() {
    if (attached() && number_ != 0) services().close_web(number_);
}

void WebView::mounted() {
    title_.set_style("label");
    title_.set_max_lines(1);
    address_.set_style("caption");
    address_.set_max_lines(1);
    page_.url = props.url;
    // The system's own view where the host has one; a card that opens the browser where not.
    if (web_address(props.url)) number_ = services().open_web(props.url, [this](const WebPage& now) { heard(now); });
}

void WebView::prop_changed(WebViewProp p) {
    if (!attached()) return;
    if (p == WebViewProp::url && props.url != page_.url) {
        page_ = {.url = props.url};
        if (number_ != 0) (void)services().command_web(number_, "navigate", props.url);
        else if (web_address(props.url)) number_ = services().open_web(props.url, [this](const WebPage& now) { heard(now); });
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

// The page changed in the system's view: its address follows into the binding, its title is its name.
void WebView::heard(const WebPage& now) {
    const bool moved = now.url != page_.url;
    const bool finished = page_.loading && !now.loading;
    page_ = now;
    if (moved) {
        write_url(now.url);
        emit_navigated(now.url);
    }
    if (finished) emit_loaded(now.title);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size WebView::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw,
                      mlibs::layout::Available ah) {
    // As large as it is let be (a page fills what it is given), else a page's usual window.
    return {width.value_or(aw.is_definite() ? aw.value : 480.0f), height.value_or(ah.is_definite() ? std::min(ah.value, 360.0f) : 360.0f)};
}

void WebView::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect whole{0, 0, ctx.size.width, ctx.size.height};
    if (number_ != 0) {
        // The system draws the page; the host is told where (and what shows until it does: the surface).
        const Rect at = services().bounds(node());
        if (at.x != placed_.x || at.y != placed_.y || at.width != placed_.width || at.height != placed_.height) {
            placed_ = at;
            services().place_web(number_, at, true);
        }
        ctx.list.fill_rect(whole, xrender::Paint::solid(th.color("surface")));
        return;
    }
    // No web view here: a card saying what the page is, which opens it in the system's browser.
    ctx.part({.role = "card", .allocation = whole, .kind = xrender::PartKind::box}, [&] {
        ctx.list.fill_round_rect(whole, th.radii(th.corner("medium", 8)), xrender::Paint::solid(th.color("surface_variant")));
        title_.set_text(!page_.title.empty() ? page_.title : !props.label.empty() ? props.label : std::string("Web page"));
        const Size t = title_.measure(ctx.services, th.color("text"), std::max(0.0f, ctx.size.width - 32));
        const float top = std::max(16.0f, ctx.size.height / 2 - t.height - 4);
        title_.paint(ctx, {16, top}, th.color("text"));
        address_.set_text(props.url.empty() ? std::string("no address") : std::format("{} — opens in the browser", props.url));
        (void)address_.measure(ctx.services, th.color("text_muted"), std::max(0.0f, ctx.size.width - 32));
        ctx.part({.role = "address", .kind = xrender::PartKind::content}, [&] { address_.paint(ctx, {16, top + t.height + 6}, th.color("accent")); });
    });
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, whole, th.radii(th.corner("medium", 8)));
}

bool WebView::on_event(EventContext&, const InputEvent& e) {
    // The card pressed: the person opens the page (their press is their answer).
    const auto* pointer = std::get_if<PointerEvent>(&e);
    if (number_ != 0 || !pointer || pointer->kind != PointerKind::up || (pointer->type == PointerType::mouse && pointer->button != MouseButton::left))
        return false;
    (void)services().perform(node(), "open_external", Value{});
    return true;
}

void WebView::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = !page_.title.empty() ? page_.title : props.label;
    node.value = Value(page_.url);
    node.description = number_ != 0 ? std::string() : std::string("opens in the browser");
    node.states.set(State::busy, page_.loading);
}

ActResult WebView::navigate(ActContext&, const Value& url) {
    const std::string_view to = url.as_string();
    if (!web_address(to)) return ActResult::fail(std::format("`{}` is not a web address (http, https)", to));
    if (number_ != 0) {
        if (!services().command_web(number_, "navigate", to)) return ActResult::fail("the web view did not take the address");
        return ActResult::done(Value(std::string(to)));
    }
    // No web view here: the address kept, for the browser to open.
    page_ = {.url = std::string(to)};
    write_url(page_.url);
    emit_navigated(page_.url);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value(page_.url));
}

ActResult WebView::back(ActContext&) {
    if (number_ == 0) return ActResult::fail("no web view here: the page opens in the browser (open_external)");
    if (!page_.can_go_back) return ActResult::fail("there is no page before this one");
    return services().command_web(number_, "back", {}) ? ActResult::done() : ActResult::fail("the web view did not go back");
}

ActResult WebView::forward(ActContext&) {
    if (number_ == 0) return ActResult::fail("no web view here: the page opens in the browser (open_external)");
    if (!page_.can_go_forward) return ActResult::fail("there is no page after this one");
    return services().command_web(number_, "forward", {}) ? ActResult::done() : ActResult::fail("the web view did not go forward");
}

ActResult WebView::reload(ActContext&) {
    if (number_ == 0) return ActResult::fail("no web view here: the page opens in the browser (open_external)");
    return services().command_web(number_, "reload", {}) ? ActResult::done() : ActResult::fail("the web view did not reload");
}

ActResult WebView::open_external(ActContext&) {
    if (!web_address(page_.url)) return ActResult::fail("no web address to open");
    if (!services().open_url(page_.url)) return ActResult::fail(std::format("nothing here opens {}", page_.url));
    return ActResult::done(Value(page_.url));
}

}  // namespace omui
