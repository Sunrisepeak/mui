module openmui.host.headless;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.agent;
import openmui.raster;
import openmui.render;
import openmui.text;
import openmui.image;
import openmui.trace;

namespace omui {

namespace {

Stages stages_of(HeadlessLevel level) {
    switch (level) {
        case HeadlessLevel::semantics: return Stages::semantics;
        case HeadlessLevel::layout: return Stages::layout;
        case HeadlessLevel::pixels:
        case HeadlessLevel::session: return Stages::paint;
    }
    return Stages::paint;
}

}  // namespace

struct Headless::Impl {
    HeadlessOptions                             options;
    std::unique_ptr<typeset::FontCollection>    own_fonts;
    std::unique_ptr<Runtime>                    runtime;
    std::unique_ptr<semantic::Tree>             tree;
    std::unique_ptr<typeset::GlyphCache>        glyphs;
    render::CpuRenderer                         renderer;
    raster::Surface                             surface;
    std::uint64_t                               rendered = 0;   // the list hash the surface shows
    bool                                        have_pixels = false;
    std::vector<std::uint32_t>                  ids;            // object ids of the last render
    std::unique_ptr<agent::Endpoint>            endpoint;
    std::int64_t                                frame = -1;

    void render_now() {
        const render::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        if (have_pixels && h == rendered) return;
        const int w = static_cast<int>(std::lround(options.size.width * options.scale));
        const int hgt = static_cast<int>(std::lround(options.size.height * options.scale));
        if (surface.width() != w || surface.height() != hgt) surface = raster::Surface(w, hgt);
        render::CpuOptions o;
        o.scale = options.scale;
        o.background = options.background;
        o.object_ids = true;
        trace::Span span("render", "host");
        (void)renderer.render(list, surface, {&runtime->images(), glyphs.get()}, o);
        ids.assign(renderer.object_ids().begin(), renderer.object_ids().end());
        rendered = h;
        have_pixels = true;
    }
};

Headless::Headless(HeadlessOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    typeset::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        d.own_fonts = std::make_unique<typeset::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        fonts = d.own_fonts.get();
    }
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = fonts;
    ro.clock = anim::Clock::virtual_time();
    ro.size = d.options.size;
    ro.scale = d.options.scale;
    ro.reduce_motion = d.options.reduce_motion;
    ro.stages = stages_of(d.options.level);
    d.runtime = std::make_unique<Runtime>(std::move(ro));
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
    if (d.options.level == HeadlessLevel::session) {
        agent::EndpointOptions eo;
        eo.name = d.options.app;
        eo.directory = d.options.endpoint_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.capture = capture();
        d.endpoint = std::make_unique<agent::Endpoint>(*d.runtime, std::move(eo));
        if (auto r = d.endpoint->start(); !r) {
            warn("the agent endpoint did not start: {}", r.error());
            d.endpoint.reset();
        }
    }
}

Headless::~Headless() {
    if (impl_->endpoint) impl_->endpoint->stop();
}

Runtime& Headless::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Headless::tree() noexcept { return *impl_->tree; }
const HeadlessOptions& Headless::options() const noexcept { return impl_->options; }
agent::Endpoint* Headless::endpoint() noexcept { return impl_->endpoint.get(); }

NodeId Headless::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }

seconds Headless::now() const noexcept { return impl_->options.fps.time_of(std::max<std::int64_t>(0, impl_->frame)); }
std::int64_t Headless::frame_index() const noexcept { return impl_->frame; }

FrameOutput Headless::step(std::vector<InputEvent> events) {
    Impl& d = *impl_;
    if (d.endpoint) d.endpoint->pump();
    ++d.frame;
    FrameInput in;
    in.time = d.options.fps.time_of(d.frame);
    in.events = std::move(events);
    return d.runtime->frame(in);
}

FrameOutput Headless::advance(seconds duration) {
    const seconds end = now() + duration;
    FrameOutput out = step();
    while (now() < end) out = step();
    return out;
}

std::size_t Headless::settle(seconds limit) {
    const seconds end = now() + limit;
    std::size_t frames = 0;
    while (true) {
        FrameOutput out = step();
        ++frames;
        const bool timer_soon = out.next_timer && *out.next_timer <= end;
        if ((!out.animating && !timer_soon) || now() >= end) break;
    }
    return frames;
}

void Headless::move(Point p) {
    PointerEvent e;
    e.kind = PointerKind::move;
    e.position = p;
    step({e});
}

void Headless::click(Point p, MouseButton button) {
    PointerEvent down;
    down.kind = PointerKind::down;
    down.position = p;
    down.button = button;
    down.buttons = 1;
    PointerEvent up = down;
    up.kind = PointerKind::up;
    up.buttons = 0;
    PointerEvent move = down;
    move.kind = PointerKind::move;
    move.button = MouseButton::none;
    move.buttons = 0;
    step({move, down, up});
}

bool Headless::click(std::string_view id) {
    Runtime& rt = *impl_->runtime;
    if (impl_->options.level == HeadlessLevel::semantics) {
        // No layout, no points: a click is the node's activation, as a person's would be.
        NodeId n = rt.find(id);
        if (!rt.valid(n)) return false;
        Invocation call;
        call.actor = {"person", "user", {"input:pointer"}};
        (void)rt.act(n, "activate", call);
        step();
        return true;
    }
    NodeId n = rt.find(id);
    if (!rt.valid(n)) return false;
    const Rect b = rt.bounds(n);
    if (b.empty()) return false;
    click(b.center());
    return true;
}

void Headless::key(Key k, Modifiers mods) {
    KeyEvent down;
    down.key = k;
    down.mods = mods;
    KeyEvent up = down;
    up.kind = KeyKind::up;
    step({down, up});
}

void Headless::type(std::string_view text) { step({TextEvent{std::string(text)}}); }

void Headless::scroll(Point at, Point delta, bool precise) {
    WheelEvent w;
    w.position = at;
    w.delta = delta;
    w.precise = precise;
    step({w});
}

void Headless::focus_next(bool backward) { key(Key::tab, backward ? Modifiers(Modifier::shift) : Modifiers{}); }

const raster::Surface& Headless::pixels() {
    impl_->render_now();
    return impl_->surface;
}

namespace {

// The pixels of a region (logical coordinates), as an image view of the surface.
std::expected<imageio::View, std::string> view_of(const raster::Surface& s, float scale, std::optional<Rect> region) {
    int x0 = 0, y0 = 0, x1 = s.width(), y1 = s.height();
    if (region) {
        const PixelRect r = geom::snap(*region, scale);
        x0 = std::clamp(r.x, 0, s.width());
        y0 = std::clamp(r.y, 0, s.height());
        x1 = std::clamp(r.x + r.width, x0, s.width());
        y1 = std::clamp(r.y + r.height, y0, s.height());
    }
    if (x1 <= x0 || y1 <= y0) return std::unexpected("the region is empty");
    return imageio::View{reinterpret_cast<const std::uint8_t*>(s.row(y0) + x0), x1 - x0, y1 - y0, s.width() * 4,
                         imageio::Alpha::premultiplied};
}

}  // namespace

std::expected<std::vector<std::uint8_t>, std::string> Headless::png(std::optional<Rect> region) {
    Impl& d = *impl_;
    if (d.options.level < HeadlessLevel::pixels) return std::unexpected("no pixels below H2 (level pixels)");
    d.render_now();
    auto v = view_of(d.surface, d.options.scale, region);
    if (!v) return std::unexpected(v.error());
    return imageio::encode_png(*v);
}

std::expected<void, std::string> Headless::save_png(const std::filesystem::path& file, std::optional<Rect> region) {
    Impl& d = *impl_;
    if (d.options.level < HeadlessLevel::pixels) return std::unexpected("no pixels below H2 (level pixels)");
    d.render_now();
    auto v = view_of(d.surface, d.options.scale, region);
    if (!v) return std::unexpected(v.error());
    return imageio::save_png(file, *v);
}

NodeId Headless::node_at_pixel(Point p) {
    Impl& d = *impl_;
    d.render_now();
    const int x = static_cast<int>(p.x * d.options.scale), y = static_cast<int>(p.y * d.options.scale);
    if (x < 0 || y < 0 || x >= d.surface.width() || y >= d.surface.height()) return {};
    return d.runtime->node_at_index(d.ids[static_cast<std::size_t>(y) * static_cast<std::size_t>(d.surface.width()) + static_cast<std::size_t>(x)]);
}

agent::CaptureFn Headless::capture() {
    return [this](Rect region) { return png(region); };
}

}  // namespace omui
