module omui.host.headless;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.agent;
import xrender.raster;
import xrender.vector;
import mlibs.text;
import mlibs.image;
import mlibs.trace;
import xrender.text;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::warn;
}

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
    std::vector<mlibs::image::Image>                 recording;  // record()'s frames, premultiplied
    std::optional<Rect>                         recording_region;
    std::unique_ptr<mlibs::text::FontCollection>    own_fonts;
    std::unique_ptr<Runtime>                    runtime;
    std::unique_ptr<semantic::Tree>             tree;
    std::unique_ptr<xrender::text::GlyphCache>        glyphs;
    xrender::CpuRenderer                         renderer;
    xrender::raster::Surface                             surface;
    std::uint64_t                               rendered = 0;   // the list hash the surface shows
    std::uint64_t                               shown_list = 0; // the list hash of the last frame shown (frame_shown)
    bool                                        have_pixels = false;
    xrender::DamageTracker                       damage;         // what the next list changes
    std::unique_ptr<agent::Endpoint>            endpoint;
    std::int64_t                                frame = -1;

    void render_now() {
        const xrender::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        if (have_pixels && h == rendered) return;
        const int w = static_cast<int>(std::lround(options.size.width * options.scale));
        const int hgt = static_cast<int>(std::lround(options.size.height * options.scale));
        if (surface.width() != w || surface.height() != hgt) surface = xrender::raster::Surface(w, hgt);
        if (!have_pixels) damage.invalidate();
        mlibs::trace::Span span("render", "host");
        const xrender::Resources resources{&runtime->images(), glyphs.get()};
        // Only the rows the new list can change: the same bytes as a full render.
        const auto rows = damage.update(list, w, hgt, options.scale, resources);
        if (!rows.empty()) {
            xrender::CpuOptions o;
            o.scale = options.scale;
            o.background = options.background;
            o.object_ids = true;
            o.rows = rows;
            if (options.parallel_raster) o.parallel = &raster_parallel();
            (void)renderer.render(list, surface, resources, o);
        }
        rendered = h;
        have_pixels = true;
    }
};

Headless::Headless(HeadlessOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    mlibs::text::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        d.own_fonts = std::make_unique<mlibs::text::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        fonts = d.own_fonts.get();
    }
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = fonts;
    ro.clock = hanim::Clock::virtual_time();
    ro.size = d.options.size;
    ro.scale = d.options.scale;
    ro.reduce_motion = d.options.reduce_motion;
    ro.stages = stages_of(d.options.level);
    ro.complete_frames = d.options.complete_frames;
    d.runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime->set_renderer("cpu");
    d.runtime->set_device_tier(d.options.tier, std::format("headless, drawn as on {}", mlibs::pipeline::name(d.options.tier)));
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<xrender::text::GlyphCache>(*fonts);
    // A file offered is taken (the runtime keeps it: offered_files); the
    // drawing in vectors is the agent's capture.
    d.runtime->set_file_offer([](const Runtime::OfferedFile&) { return true; });
    d.runtime->set_svg_capture([this](Rect region) -> std::expected<std::string, std::string> {
        auto capture = svg(region);
        if (!capture) return std::unexpected(capture.error());
        return std::move(capture->svg);
    });
    if (d.options.level == HeadlessLevel::session) {
        agent::EndpointOptions eo;
        eo.name = d.options.endpoint_name.empty() ? d.options.app : d.options.endpoint_name;
        eo.directory = d.options.endpoint_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.capture = capture();
        eo.protocol.capture_svg = capture_svg();
        eo.protocol.input = d.options.agent_input;
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
    std::optional<agent::AgentInput> given;
    const auto agent_begin = events.size();
    if (d.endpoint) {
        d.endpoint->pump();
        // An agent's input (--agent-input), a frame's worth, after the host's own.
        given = d.endpoint->take_input();
        if (given) events.insert(events.end(), std::make_move_iterator(given->events.begin()), std::make_move_iterator(given->events.end()));
    }
    ++d.frame;
    FrameInput in;
    in.time = d.options.fps.time_of(d.frame);
    in.events = std::move(events);
    std::array<InputAttribution, 1> attribution;
    if (given && in.events.size() > agent_begin) {
        attribution[0] = {agent_begin, in.events.size(), given->actor, given->session};
        in.attribution = attribution;
    }
    FrameOutput out = d.runtime->frame(in);
    // A frame with something new to show is shown (as a window's host shows
    // it), at its virtual time; its cost is not measured (it would make the
    // pixels differ from run to run).
    if (out.repainted)
        if (const std::uint64_t h = d.runtime->display_list().hash(); h != d.shown_list) {
            d.shown_list = h;
            d.runtime->frame_shown({*in.time});
        }
    return out;
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
        call.actor = {"person", "user", {}};
        (void)rt.act(n, "activate", call);
        step();
        return true;
    }
    NodeId n = rt.find(id);
    if (!rt.valid(n)) return false;
    const Rect b = rt.drawn_bounds(n);
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

const xrender::raster::Surface& Headless::pixels() {
    impl_->render_now();
    return impl_->surface;
}

namespace {

// The pixels of a region (logical coordinates), as an image view of the surface.
std::expected<mlibs::image::View, std::string> view_of(const xrender::raster::Surface& s, float scale, std::optional<Rect> region) {
    int x0 = 0, y0 = 0, x1 = s.width(), y1 = s.height();
    if (region) {
        const PixelRect r = mlibs::geom::snap(*region, scale);
        x0 = std::clamp(r.x, 0, s.width());
        y0 = std::clamp(r.y, 0, s.height());
        x1 = std::clamp(r.x + r.width, x0, s.width());
        y1 = std::clamp(r.y + r.height, y0, s.height());
    }
    if (x1 <= x0 || y1 <= y0) return std::unexpected("the region is empty");
    return mlibs::image::View{reinterpret_cast<const std::uint8_t*>(s.row(y0) + x0), x1 - x0, y1 - y0, s.width() * 4,
                         mlibs::image::Alpha::premultiplied};
}

}  // namespace

std::expected<std::vector<std::uint8_t>, std::string> Headless::png(std::optional<Rect> region) {
    Impl& d = *impl_;
    if (d.options.level < HeadlessLevel::pixels) return std::unexpected("no pixels below H2 (level pixels)");
    d.render_now();
    auto v = view_of(d.surface, d.options.scale, region);
    if (!v) return std::unexpected(v.error());
    return mlibs::image::encode_png(*v);
}

std::expected<void, std::string> Headless::save_png(const std::filesystem::path& file, std::optional<Rect> region) {
    Impl& d = *impl_;
    if (d.options.level < HeadlessLevel::pixels) return std::unexpected("no pixels below H2 (level pixels)");
    d.render_now();
    auto v = view_of(d.surface, d.options.scale, region);
    if (!v) return std::unexpected(v.error());
    return mlibs::image::save_png(file, *v);
}

void Headless::record(std::optional<Rect> region) {
    Impl& d = *impl_;
    if (d.options.level < HeadlessLevel::pixels) return;
    d.render_now();
    if (d.recording.empty()) d.recording_region = region;
    auto v = view_of(d.surface, d.options.scale, d.recording_region);
    if (!v) return;
    mlibs::image::Image img;
    img.width = v->width;
    img.height = v->height;
    img.alpha = mlibs::image::Alpha::premultiplied;
    img.rgba.resize(static_cast<std::size_t>(v->width) * v->height * 4);
    for (int y = 0; y < v->height; ++y)
        std::memcpy(img.rgba.data() + static_cast<std::size_t>(y) * v->width * 4, v->rgba + static_cast<std::size_t>(y) * v->stride, static_cast<std::size_t>(v->width) * 4);
    d.recording.push_back(std::move(img));
}

std::size_t Headless::recorded() const noexcept { return impl_->recording.size(); }

std::expected<void, std::string> Headless::save_recording(const std::filesystem::path& file, RecordingOptions options) {
    Impl& d = *impl_;
    if (d.recording.empty()) return std::unexpected("nothing recorded");
    std::vector<mlibs::image::View> views;
    for (const auto& img : d.recording) views.push_back(mlibs::image::view(img));
    // The format by the file's extension, in any case.
    const std::u8string ext = file.extension().u8string();
    const auto lower = [](char8_t a) { return a >= u8'A' && a <= u8'Z' ? static_cast<char8_t>(a - u8'A' + u8'a') : a; };
    const bool webp = std::ranges::equal(ext, std::u8string_view(u8".webp"), {}, lower);
    auto r = webp ? mlibs::image::save_animated_webp(file, views, options)
                  : mlibs::image::save_apng(file, views, {.delay_num = options.delay_num, .delay_den = options.delay_den, .loops = options.loops});
    d.recording.clear();
    return r;
}

NodeId Headless::node_at_pixel(Point p) {
    Impl& d = *impl_;
    d.render_now();
    const int x = static_cast<int>(p.x * d.options.scale), y = static_cast<int>(p.y * d.options.scale);
    if (x < 0 || y < 0 || x >= d.surface.width() || y >= d.surface.height()) return {};
    return d.runtime->node_at_index(d.renderer.object_at(x, y, d.surface.width()));
}

agent::CaptureFn Headless::capture() {
    return [this](Rect region) { return png(region); };
}

agent::CaptureSvgFn Headless::capture_svg() {
    return [this](Rect region) { return svg(region); };
}

std::expected<agent::VectorCapture, std::string> Headless::svg(std::optional<Rect> region) {
    Impl& d = *impl_;
    const xrender::Resources resources{&d.runtime->images(), d.glyphs.get()};
    return agent::vector_capture(*d.runtime, resources, region.value_or(Rect{0, 0, d.options.size.width, d.options.size.height}), d.options.scale,
                                 d.options.background);
}

}  // namespace omui
