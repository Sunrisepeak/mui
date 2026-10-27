module openmui.host.sdl3;

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
import mcpplibs.os;
import :platform;

namespace omui {

struct Desktop::Impl {
    DesktopOptions                           options;
    std::unique_ptr<typeset::FontCollection> own_fonts;
    std::unique_ptr<Runtime>                 own_runtime;
    Runtime*                                 runtime = nullptr;  // own_runtime's, or the one adopted
    std::unique_ptr<semantic::Tree>          tree;
    std::unique_ptr<typeset::GlyphCache>     glyphs;
    std::unique_ptr<agent::Endpoint>         endpoint;
    platform::WindowPtr                      window;
    render::CpuRenderer                      renderer;
    render::DamageTracker                    damage;     // what the next list changes
    raster::Surface                          frame;
    std::uint64_t                            shown = 0;   // the list hash on screen
    std::size_t                              frames_shown = 0;
    bool                                     quitting = false;
    int                                      exit_code = 0;
    bool                                     idle = false;  // the last frame wanted no other

    void show() {
        const render::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        const float scale = runtime->scale();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int hgt = static_cast<int>(std::lround(runtime->size().height * scale));
        if (h == shown && frame.width() == w && frame.height() == hgt) return;
        if (frame.width() != w || frame.height() != hgt) frame = raster::Surface(w, hgt);
        {
            trace::Span span("render", "host");
            const render::Resources resources{&runtime->images(), glyphs.get()};
            // Only the rows the new list can change: the same bytes as a full render.
            const auto rows = damage.update(list, w, hgt, scale, resources);
            render::CpuOptions o;
            o.scale = scale;
            o.background = options.background;
            o.rows = rows;
            if (options.parallel_raster) o.parallel = &raster_parallel();
            if (!rows.empty()) (void)renderer.render(list, frame, resources, o);
        }
        trace::Span span("present", "host");
        platform::present(*window, frame);
        shown = h;
        ++frames_shown;
    }
};

Desktop::Desktop(DesktopOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    // Shortcuts use Command where the platform does.
    if constexpr (mcpplibs::os::is_macos) set_primary_modifier(Modifier::meta);
    if (d.options.runtime) {
        // A runtime the caller made (a script binding's, which existed first):
        // the window shows it, on the interactive clock.
        d.runtime = d.options.runtime;
        d.runtime->clock() = anim::Clock::interactive();
        d.runtime->resize(d.options.size, d.runtime->scale());
        d.tree = std::make_unique<semantic::Tree>(*d.runtime);
        d.glyphs = std::make_unique<typeset::GlyphCache>(d.runtime->fonts());
        return;
    }
    typeset::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        d.own_fonts = std::make_unique<typeset::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        fonts = d.own_fonts.get();
    }
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = fonts;
    ro.clock = anim::Clock::interactive();
    ro.size = d.options.size;
    ro.reduce_motion = d.options.reduce_motion;
    d.own_runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime = d.own_runtime.get();
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
}

Desktop::~Desktop() {
    if (impl_->endpoint) impl_->endpoint->stop();
    impl_->window.reset();
}

std::expected<void, std::string> Desktop::open() {
    Impl& d = *impl_;
    if (d.window) return {};
    platform::WindowOptions wo{d.options.title, d.options.size, d.options.resizable, d.options.vsync, d.options.video_driver,
                               d.options.render_driver};
    auto w = platform::open(wo);
    if (!w) return std::unexpected(w.error());
    d.window = std::move(*w);
    d.runtime->resize(platform::logical_size(*d.window), platform::pixel_scale(*d.window));
    d.runtime->set_clipboard([] { return platform::clipboard(); }, [](std::string_view t) { platform::set_clipboard(t); });
    if (d.options.agent) {
        agent::EndpointOptions eo;
        eo.name = d.options.app;
        eo.directory = d.options.agent_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.capture = [&d](Rect region) -> std::expected<std::vector<std::uint8_t>, std::string> {
            // The frame on screen, cut to the region (logical pixels).
            if (d.frame.width() <= 0) return std::unexpected("nothing shown yet");
            const PixelRect r = geom::snap(region, d.runtime->scale());
            const int x0 = std::clamp(r.x, 0, d.frame.width()), y0 = std::clamp(r.y, 0, d.frame.height());
            const int x1 = std::clamp(r.x + r.width, x0, d.frame.width()), y1 = std::clamp(r.y + r.height, y0, d.frame.height());
            if (x1 <= x0 || y1 <= y0) return std::unexpected("the region is outside the window");
            imageio::View v{reinterpret_cast<const std::uint8_t*>(d.frame.row(y0) + x0), x1 - x0, y1 - y0, d.frame.width() * 4,
                            imageio::Alpha::premultiplied};
            return imageio::encode_png(v);
        };
        eo.wake = [] { platform::wake(); };
        d.endpoint = std::make_unique<agent::Endpoint>(*d.runtime, std::move(eo));
        if (auto r = d.endpoint->start(); !r) {
            warn("the agent endpoint did not start: {}", r.error());
            d.endpoint.reset();
        }
    }
    return {};
}

bool Desktop::is_open() const noexcept { return impl_->window != nullptr; }
Runtime& Desktop::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Desktop::tree() noexcept { return *impl_->tree; }
agent::Endpoint* Desktop::endpoint() noexcept { return impl_->endpoint.get(); }
std::size_t Desktop::frames_shown() const noexcept { return impl_->frames_shown; }
float Desktop::scale() const noexcept { return impl_->runtime->scale(); }
NodeId Desktop::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }

void Desktop::quit(int code) {
    impl_->quitting = true;
    impl_->exit_code = code;
}

bool Desktop::step(bool wait) {
    Impl& d = *impl_;
    if (!d.window) {
        if (auto r = open(); !r) {
            error("the window did not open: {}", r.error());
            d.exit_code = 1;
            return false;
        }
    }
    // Sleep only when nothing moves: until an event, or the next timer.
    std::optional<std::chrono::milliseconds> timeout;
    const bool sleep = wait && d.idle;
    if (sleep) {
        if (auto next = d.runtime->next_timer()) {
            const double left = (*next - d.runtime->now()).count();
            timeout = std::chrono::milliseconds(static_cast<std::int64_t>(std::max(0.0, std::ceil(left * 1000))));
        }
    }
    platform::Polled polled = platform::poll(*d.window, sleep, timeout);
    if (d.endpoint) d.endpoint->pump();
    FrameInput in;
    in.events = std::move(polled.events);
    FrameOutput out = d.runtime->frame(in);
    d.show();
    platform::text_input(*d.window, d.runtime->text_input_rect());
    platform::cursor(*d.window, d.runtime->cursor());
    d.idle = !out.animating;
    if (polled.quit) d.quitting = true;
    return !d.quitting;
}

void Desktop::wake() { platform::wake(); }

int Desktop::run() {
    while (step(true)) {}
    return impl_->exit_code;
}

void Desktop::synthesize(std::span<const InputEvent> events) {
    if (impl_->window) platform::synthesize(*impl_->window, events);
}

std::expected<raster::Surface, std::string> Desktop::window_pixels() {
    if (!impl_->window) return std::unexpected("the window is not open");
    return platform::read_back(*impl_->window);
}

}  // namespace omui
