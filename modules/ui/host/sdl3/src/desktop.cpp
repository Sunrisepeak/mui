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
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.render.effect;
import mcpplibs.os;
import :platform;

namespace omui {

struct Desktop::Impl final : WindowHost {
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
    FrameDemand                              demand;        // what the last frame said frames are wanted for (D-70)
    std::chrono::steady_clock::time_point    last_frame{};

    // The GPU path: a device that claimed the window, the renderer, and the
    // frame texture the swapchain shows.
    std::unique_ptr<rhi::Device>             device;
    std::optional<render::GpuRenderer>       gpu;
    rhi::Texture                             gpu_frame;
    std::uint32_t                            gpu_w = 0, gpu_h = 0;
    std::optional<effect::Library>           effects;

    // The window as a window (D-75): who draws its frame, and its close.
    bool                                     framework = false;
    bool                                     close_requested = false;
    std::optional<std::chrono::steady_clock::time_point> closed_at;  // closed, dissolving until then

    bool framework_drawn() const noexcept override { return framework; }
    void minimize() override {
        if (window) platform::minimize(*window);
    }
    void toggle_maximize() override {
        if (window) platform::toggle_maximize(*window);
    }
    void set_fullscreen(bool on) override {
        if (window) platform::set_fullscreen(*window, on);
    }
    void request_close() override {
        close_requested = true;
        platform::wake();
    }
    bool pass_through(bool on) override { return window && platform::pass_through(*window, on); }
    bool set_framework_drawn(bool on) override {
        if (!window) return false;
        if (on == framework) return true;
        framework = on;
        platform::set_framed(*window, !on);
        hit_test();
        window_state();
        return true;
    }

    // The system asks what lies under a point of a frameless window.
    void hit_test() {
        if (!window) return;
        if (!framework) {
            (void)platform::set_hit_test(*window, {});
            return;
        }
        if (!platform::set_hit_test(*window, [this](Point p) { return window_area_at(*runtime, p, runtime->size()); }))
            debug("this system does not ask a frameless window what lies under the pointer: it cannot be moved or resized by its frame");
    }

    // What the system says of the window, for the interface.
    void window_state() {
        if (!window) return;
        omui::window().set_transparent(framework && platform::transparent(*window));
        omui::window().set_state(platform::state(*window));
    }

    // Closed at once, and gone when the dissolve ends: input from now on is
    // not the application's (it goes to what is beneath, where the system
    // can send it there), and the agent's requests see a closing window.
    void begin_close() {
        if (closed_at) return;
        const double d = options.close_dissolve;
        if (!framework || d <= 0 || runtime->reduce_motion()) {
            quitting = true;
            return;
        }
        omui::window().set_closing(true);
        (void)pass_through(true);
        runtime->set(runtime->root(), "pixel_effect", Value{}.set("name", "dissolve").set("duration", d));
        closed_at = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(d));
    }

    // A device for the window, when the options and the machine say the GPU
    // draws: none for a software rasteriser unless the GPU is insisted on.
    std::expected<void, std::string> open_gpu() {
        using R = DesktopOptions::Renderer;
        if (options.renderer == R::cpu) return {};
        rhi::sdl::Options o;
        o.formats = render::gpu_shader_formats();
        o.window = platform::window_id(*window);
        o.vsync = options.vsync;
        o.vulkan_library = options.vulkan_library;
        auto made = rhi::sdl::create(o);
        if (!made) {
            if (options.renderer == R::gpu) return std::unexpected(std::format("no GPU device for the window: {}", made.error().message));
            return {};
        }
        if ((*made)->caps().software && options.renderer == R::automatic) return {};  // the CPU renderer is faster
        auto r = render::GpuRenderer::create(**made);
        if (!r) {
            if (options.renderer == R::gpu) return std::unexpected(std::format("the GPU renderer: {}", r.error().message));
            return {};
        }
        device = std::move(*made);
        gpu.emplace(std::move(*r));
        return {};
    }

    // The frame as shown, read back (the GPU's texture, or the CPU's surface).
    std::expected<raster::Surface, std::string> pixels() {
        if (!gpu) {
            if (frame.width() <= 0) return std::unexpected("nothing shown yet");
            return frame;
        }
        if (!gpu_frame) return std::unexpected("nothing shown yet");
        auto bytes = device->read(gpu_frame, rhi::Region{0, 0, gpu_w, gpu_h});
        if (!bytes) return std::unexpected(bytes.error().message);
        raster::Surface out(static_cast<int>(gpu_w), static_cast<int>(gpu_h));
        std::memcpy(out.pixels().data(), bytes->data(), bytes->size());
        return out;
    }

    void show() {
        const render::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        const float scale = runtime->scale();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int hgt = static_cast<int>(std::lround(runtime->size().height * scale));
        if (gpu) {
            show_gpu(list, h, scale, w, hgt);
            return;
        }
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

    void show_gpu(const render::DisplayList& list, std::uint64_t h, float scale, int w, int hgt) {
        if (w <= 0 || hgt <= 0) return;
        const auto uw = static_cast<std::uint32_t>(w), uh = static_cast<std::uint32_t>(hgt);
        if (h == shown && uw == gpu_w && uh == gpu_h) return;
        if (uw != gpu_w || uh != gpu_h) {
            if (gpu_frame) device->destroy(gpu_frame);
            auto t = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, uw, uh, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled,
                                                     "host.frame"});
            if (!t) {
                warn("the frame texture: {}", t.error().message);
                return;
            }
            gpu_frame = *t;
            gpu_w = uw;
            gpu_h = uh;
        }
        {
            trace::Span span("render", "host");
            const render::Resources resources{&runtime->images(), glyphs.get()};
            auto r = gpu->render(list, gpu_frame, resources, {.scale = scale, .clear = true, .background = options.background});
            if (!r) {
                warn("the GPU renderer: {}", r.error().message);
                return;
            }
        }
        trace::Span span("present", "host");
        auto p = device->present(gpu_frame);
        if (!p) warn("present: {}", p.error().message);
        shown = h;
        ++frames_shown;
    }

    // Effects whose files changed, loaded again; the next frame redrawn.
    void reload_effects() {
        if (!effects) return;
        for (const effect::Reload& r : effects->poll()) {
            if (r.ok()) {
                info("effect {} reloaded in {} ms", r.name, static_cast<double>(r.took.count()) / 1000.0);
                shown = 0;
            } else {
                for (const effect::Diagnostic& d : r.problems) warn("effect {}: {}", r.name, d.text());
            }
        }
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
        d.runtime->clock() = hanim::Clock::interactive();
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
    ro.clock = hanim::Clock::interactive();
    ro.size = d.options.size;
    ro.reduce_motion = d.options.reduce_motion;
    ro.reduce_transparency = d.options.reduce_transparency;
    d.own_runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime = d.own_runtime.get();
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
}

Desktop::~Desktop() {
    if (omui::window().host() == impl_.get()) omui::window().detach();
    if (impl_->endpoint) impl_->endpoint->stop();
    // The GPU's before the window it claimed.
    if (impl_->device && impl_->gpu_frame) impl_->device->destroy(impl_->gpu_frame);
    impl_->gpu.reset();
    impl_->device.reset();
    impl_->window.reset();
}

std::expected<void, std::string> Desktop::open() {
    Impl& d = *impl_;
    if (d.window) return {};
    // A window for the GPU first (when it may draw); the CPU's otherwise.
    const bool try_gpu = d.options.renderer != DesktopOptions::Renderer::cpu;
    d.framework = d.options.frame == DesktopOptions::Frame::framework;
    platform::WindowOptions wo{d.options.title, d.options.size, d.options.resizable, d.options.vsync, d.options.video_driver,
                               d.options.render_driver, try_gpu, d.framework};
    // Where the interface draws nothing, a frameless window shows what is behind it.
    if (d.framework) d.options.background = Color::transparent();
    auto w = platform::open(wo);
    if (!w) return std::unexpected(w.error());
    d.window = std::move(*w);
    if (try_gpu) {
        if (auto g = d.open_gpu(); !g) return std::unexpected(g.error());
        if (!d.gpu) {
            // No GPU after all: the window again, with an SDL renderer for the CPU's frames.
            d.window.reset();
            wo.gpu = false;
            auto again = platform::open(wo);
            if (!again) return std::unexpected(again.error());
            d.window = std::move(*again);
        }
    }
    if (!d.options.effects.empty()) {
        d.effects.emplace(effect::Options{.formats = d.device ? d.device->caps().formats : render::gpu_shader_formats()});
        if (auto loaded = d.effects->load_directory(d.options.effects); !loaded)
            for (const effect::Diagnostic& p : loaded.error()) warn("effect: {}", p.text());
        if (!d.gpu) warn("effects in {} are drawn by the GPU renderer; the CPU renderer draws their layers without them", mcpplibs::os::fs::utf8(d.options.effects));
    }
    d.runtime->resize(platform::logical_size(*d.window), platform::pixel_scale(*d.window));
    // The window's operations are verbs (window.*), its state the interface's to read.
    omui::window().attach(&d, d.runtime->verbs());
    d.window_state();
    d.hit_test();
    d.runtime->set_clipboard([] { return platform::clipboard(); }, [](std::string_view t) { platform::set_clipboard(t); });
    if (d.options.agent) {
        agent::EndpointOptions eo;
        eo.name = d.options.app;
        eo.directory = d.options.agent_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.capture = [&d](Rect region) -> std::expected<std::vector<std::uint8_t>, std::string> {
            // The frame on screen, cut to the region (logical pixels).
            auto shown_frame = d.pixels();
            if (!shown_frame) return std::unexpected(shown_frame.error());
            const raster::Surface& f = *shown_frame;
            const PixelRect r = geom::snap(region, d.runtime->scale());
            const int x0 = std::clamp(r.x, 0, f.width()), y0 = std::clamp(r.y, 0, f.height());
            const int x1 = std::clamp(r.x + r.width, x0, f.width()), y1 = std::clamp(r.y + r.height, y0, f.height());
            if (x1 <= x0 || y1 <= y0) return std::unexpected("the region is outside the window");
            imageio::View v{reinterpret_cast<const std::uint8_t*>(f.row(y0) + x0), x1 - x0, y1 - y0, f.width() * 4, imageio::Alpha::premultiplied};
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
bool Desktop::gpu() const noexcept { return impl_->gpu.has_value(); }
effect::Library* Desktop::effects() noexcept { return impl_->effects ? &*impl_->effects : nullptr; }
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
    // Frames as often as they are wanted (D-70): none while nothing moves —
    // sleep until an event or the next timer —, and while something does, at
    // the rate its demand asks within the display's (a stream's 60 on a 120 Hz
    // panel: half the frames, and the display may follow down).
    const double display = d.window ? platform::refresh_hz(*d.window) : 0;
    const double interval = frame_interval(d.demand, display);
    std::optional<std::chrono::milliseconds> timeout;
    bool sleep = wait && d.idle;
    if (sleep) {
        if (auto next = d.runtime->next_timer()) {
            const double left = (*next - d.runtime->now()).count();
            timeout = std::chrono::milliseconds(static_cast<std::int64_t>(std::max(0.0, std::ceil(left * 1000))));
        }
    } else if (wait && interval > 0) {
        // Slower than the display — or with no vsync to hold the loop to it:
        // wait out the interval (an event still wakes it).
        const double period = display > 0 ? 1.0 / display : 1.0 / 60;
        const auto due = d.last_frame + std::chrono::duration<double>(interval);
        const auto now = std::chrono::steady_clock::now();
        if ((interval > period * 1.5 || !d.options.vsync) && due > now) {
            sleep = true;
            timeout = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(std::chrono::duration<double, std::milli>(due - now).count())));
        }
    }
    if (d.closed_at) {
        // Dissolving: frames as fast as the display shows them, until it is gone.
        sleep = false;
        timeout.reset();
    }
    platform::Polled polled = platform::poll(*d.window, sleep, timeout);
    if (polled.state) omui::window().set_state(*polled.state);
    if (polled.quit || d.close_requested) {
        d.close_requested = false;
        polled.quit = false;
        d.begin_close();
    }
    if (d.closed_at) {
        // Closed: the application takes no more input.
        polled.events.clear();
        if (std::chrono::steady_clock::now() >= *d.closed_at) d.quitting = true;
    }
    if (d.endpoint) d.endpoint->pump();
    d.reload_effects();
    FrameInput in;
    in.events = std::move(polled.events);
    // Evaluated where it will be seen: the next refresh (D-70).
    const auto start = std::chrono::steady_clock::now();
    in.present_at = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(display > 0 ? 1.0 / display : 1.0 / 60));
    in.display_hz = display;
    d.last_frame = start;
    FrameOutput out = d.runtime->frame(in);
    d.demand = out.demand;
    d.show();
    platform::text_input(*d.window, d.runtime->text_input_rect());
    platform::cursor(*d.window, d.runtime->cursor());
    d.idle = !out.animating && out.demand.idle() && !d.closed_at;
    return !d.quitting;
}

void Desktop::wake() { platform::wake(); }

void Desktop::close() {
    impl_->close_requested = true;
    platform::wake();
}

bool Desktop::closing() const noexcept { return impl_->closed_at.has_value(); }

int Desktop::run() {
    while (step(true)) {}
    return impl_->exit_code;
}

void Desktop::synthesize(std::span<const InputEvent> events) {
    if (impl_->window) platform::synthesize(*impl_->window, events);
}

std::expected<raster::Surface, std::string> Desktop::window_pixels() {
    if (!impl_->window) return std::unexpected("the window is not open");
    if (impl_->gpu) return impl_->pixels();  // the frame the swapchain was given, exactly
    return platform::read_back(*impl_->window);
}

std::vector<std::string> preferred_locales() { return platform::preferred_locales(); }

}  // namespace omui
