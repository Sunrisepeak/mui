module omui.host.sdl3;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import omui.agent;
import omui.accessibility;
import mcpplibs.mpal;
import mrender.raster;
import mrender.vector;
import mrender.frame;
import mlibs.text;
import mlibs.image;
import mlibs.trace;
import mlibs.rhi;
import mlibs.compose;
import :gpu;
import mrender.effect;
import mcpplibs.os;
import :platform;
import :web;
import mrender.text;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::debug;
using mlibs::log::error;
using mlibs::log::info;
using mlibs::log::warn;
}

namespace omui {

namespace {

// A file choice as mpal asks the system for it.
mcpplibs::mpal::Pick pick_of(const Runtime::FileChoice& choice) {
    mcpplibs::mpal::Pick asked;
    asked.kind = choice.kind == Runtime::FileChoice::Kind::save     ? mcpplibs::mpal::Pick::Kind::save
                 : choice.kind == Runtime::FileChoice::Kind::folder ? mcpplibs::mpal::Pick::Kind::folder
                                                                    : mcpplibs::mpal::Pick::Kind::open;
    asked.title = choice.title;
    asked.many = choice.many;
    asked.start = choice.start;
    asked.name = choice.name;
    for (const Runtime::FileFilter& filter : choice.filters) {
        mcpplibs::mpal::Filter made{.name = filter.name, .extensions = {}};
        for (const auto part : std::views::split(std::string_view(filter.pattern), ';'))
            if (const std::string_view extension(part.begin(), part.end()); !extension.empty() && extension != "*") made.extensions.emplace_back(extension);
        asked.filters.push_back(std::move(made));
    }
    return asked;
}

// The system's notifications (mpal) and the person's answers: shown from the
// interface's thread, heard on a thread of its own over a second connection
// (a read waits there, never in a frame); a pressed notice's callback is run
// there, and posts its verb to the interface.
class NativeNotices {
public:
    // None where the system has no notification service.
    static std::unique_ptr<NativeNotices> open() {
        auto shower = mcpplibs::mpal::Notifier::open();
        if (!shower) {
            info("desktop: system notifications: {} (drawn instead)", shower.error().text);
            return nullptr;
        }
        auto notices = std::make_unique<NativeNotices>();
        notices->shower_.emplace(std::move(*shower));
        notices->listener_ = std::jthread([raw = notices.get()](std::stop_token stop) { raw->hear(stop); });
        return notices;
    }

    bool show(std::string_view application, const Runtime::Notice& notice, std::function<void()> pressed) {
        mcpplibs::mpal::Notification shown{.application = std::string(application), .title = notice.title, .body = notice.body};
        // "default": the notice itself, clicked.
        if (!notice.verb.empty()) shown.actions = {{"default", notice.title}};
        auto id = shower_->show(shown);
        if (!id) return false;
        if (!notice.verb.empty()) {
            const std::scoped_lock lock(mutex_);
            pressed_[*id] = std::move(pressed);
        }
        return true;
    }

private:
    // Waits for answers as long as the host lives (no wakeups while none comes, C1): stopping it wakes the wait.
    void hear(std::stop_token stop) {
        auto listener = mcpplibs::mpal::Notifier::listen();
        if (!listener) return;
        const std::stop_callback woken(stop, [&listener] { listener->stop(); });
        while (!stop.stop_requested()) {
            auto answer = listener->next(std::chrono::hours(24));
            if (!answer) return;  // the bus gone
            if (!*answer) continue;
            std::function<void()> pressed;
            {
                const std::scoped_lock lock(mutex_);
                const auto found = pressed_.find((*answer)->id);
                if (found == pressed_.end()) continue;  // not one of ours
                if ((*answer)->kind == mcpplibs::mpal::Answer::Kind::chosen) pressed = found->second;
                else pressed_.erase(found);
            }
            if (pressed) {
                pressed();
                platform::wake();
            }
        }
    }

    std::optional<mcpplibs::mpal::Notifier>                    shower_;
    std::mutex                                       mutex_;
    std::map<std::uint32_t, std::function<void()>>   pressed_;
    std::jthread                                     listener_;  // last: stopped first
};

}  // namespace

struct Desktop::Impl final : WindowHost {
    DesktopOptions                           options;
    std::unique_ptr<mlibs::text::FontCollection> own_fonts;
    std::unique_ptr<Runtime>                 own_runtime;
    Runtime*                                 runtime = nullptr;  // own_runtime's, or the one adopted
    std::unique_ptr<semantic::Tree>          tree;
    std::unique_ptr<mrender::text::GlyphCache>     glyphs;
    agent::Served                            endpoint;  // through the agent surface's slot (agent::endpoint_slot)
    // Assistive technology's view of the tree (D-21).
    std::unique_ptr<accessibility::Mirror>   mirror;
    std::unique_ptr<accessibility::Adapter>  access;
    std::array<float, 8>                     access_bounds{};  // the window's place last told it (frame, content)
    // The system's notifications, opened when the first is shown (none: drawn instead).
    std::optional<std::unique_ptr<NativeNotices>> notices;
    platform::WindowPtr                      window;
    // Web pages in the system's own view over the window (D-141; macOS's WKWebView), gone before it.
    std::unique_ptr<web::Pages, void (*)(web::Pages*)> web_pages{nullptr, [](web::Pages*) {}};
    mrender::CpuFrame                         composed;  // the frame's entry (mrender.frame): the host composes nothing itself
    mrender::DamageTracker                    damage;     // what the next list changes
    mrender::raster::Surface                          frame;
    std::uint64_t                            shown = 0;   // the list hash on screen
    std::size_t                              frames_shown = 0;
    std::size_t                              failed_frames = 0;  // in a row: a stage failed, the last good frame kept on screen
    bool                                     quitting = false;
    int                                      exit_code = 0;
    bool                                     idle = false;  // the last frame wanted no other
    FrameDemand                              demand;        // what the last frame said frames are wanted for (D-70)
    std::chrono::steady_clock::time_point    last_frame{};

    // The GPU path: a device that claimed the window, the renderer, and the
    // frame texture the swapchain shows.
    std::unique_ptr<mlibs::rhi::Device>             device;
    // The device being made on a thread of its own from the start (RC3.6): the driver's start overlaps the fonts,
    // the runtime and the window rather than following them; the window claimed when it is taken.
    std::unique_ptr<GpuMaking>                      making;
    // UI9: the CPU draws the first frames into the window's own framebuffer while the device is made; the device takes
    // over between frames once it is ready (its thread wakes the loop). The two paths are one contract (D-184): the
    // CPU's frame stays on screen until the list changes, and from then on the GPU draws.
    GpuDeviceOptions                                device_options;         // what the device is begun with
    bool                                            cpu_first = false;      // the CPU's first frames, the device begun after the first
    bool                                            taking_over = false;
    std::optional<std::chrono::steady_clock::time_point> ready_at;          // when the device was first seen ready
    bool                                            cpu_on_screen = false;  // taken over: the CPU's frame still shown
    std::unique_ptr<GpuRenderer>                    gpu;
    mlibs::rhi::Texture                             gpu_frame;
    std::size_t                                     devices_lost = 0;  // how many times the device was lost and made again
    std::uint32_t                            gpu_w = 0, gpu_h = 0;
    std::unique_ptr<GpuEffects>                       effects;  // the GPU path's (gpu_slot)

    // The window as a window (D-75): who draws its frame, and its close.
    bool                                     framework = false;
    bool                                     close_requested = false;
    bool                                     holds_sdl = false;  // it brought SDL up for its device: held until it ends
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
    std::expected<void, ViewFailure> open_gpu() {
        using R = DesktopOptions::Renderer;
        if (options.renderer == R::cpu) return {};
        // The GPU path is a provider's (gpu_slot): a program built without it draws with the CPU, as on a machine
        // with no GPU — unless the GPU was insisted on, which is refused saying how to have it.
        const GpuService* service = mlibs::compose::process().get(gpu_slot);
        if (!service) {
            if (options.renderer == R::gpu) return std::unexpected(ViewFailure{ViewError::device_failed, mlibs::compose::process().missing(gpu_slot.info).message});
            return {};
        }
        GpuDeviceOptions o;
        o.window = platform::window_id(*window);
        o.vsync = options.vsync;
        o.vulkan_library = options.vulkan_library;
        o.hardware_only = options.renderer == R::automatic;
        const bool made_early = making != nullptr;
        std::optional<mlibs::trace::Span> waited{std::in_place, made_early ? "start.device.wait" : "start.device", "host"};
        auto made = made_early ? making->get() : service->create(o);
        std::unique_ptr<GpuRenderer> early = made_early && made ? making->renderer() : nullptr;  // made on its thread
        making.reset();
        waited.reset();
        // Made before the window: claimed now (a software device is let go below, unclaimed, when the CPU is faster).
        if (made && made_early && !((*made)->caps().software && options.renderer == R::automatic)) {
            if (auto claimed = service->claim(**made, o.window, o.vsync); !claimed) made = std::unexpected(claimed.error());
        }
        if (!made) {
            if (options.renderer == R::gpu) return std::unexpected(ViewFailure{ViewError::device_failed, std::format("no GPU device for the window: {}", made.error().message)});
            return {};
        }
        if ((*made)->caps().software && options.renderer == R::automatic) return {};  // the CPU renderer is faster
        std::optional<mlibs::trace::Span> renderer{std::in_place, "start.renderer", "host"};
        auto r = early ? mlibs::rhi::Result<std::unique_ptr<GpuRenderer>>(std::move(early)) : service->renderer(**made);
        renderer.reset();
        if (!r) {
            if (options.renderer == R::gpu) return std::unexpected(ViewFailure{ViewError::device_failed, std::format("the GPU renderer: {}", r.error().message)});
            return {};
        }
        device = std::move(*made);
        gpu = std::move(*r);
        if (runtime) runtime->set_on_device(true), runtime->set_device(device.get());  // content as lists where it may be (a canvas's `content: auto`); worlds drawn on it
        return {};
    }

    // UI9: the device made while the CPU drew the first frames, taken over between frames once it is ready (its thread
    // woke the loop): the window claimed — then its framebuffer let go —, the renderer its thread made. What is on
    // screen is the CPU's frame of the same list, the same picture by the one contract (D-184): nothing is drawn again
    // until the list changes. No device, a software one (the CPU renderer is faster), or a window the device cannot
    // claim (a transparent one): the CPU goes on drawing, as it would have without a GPU.
    // The claim holds the loop: SDL syncs the window with its manager and the driver makes the swapchain (75 ms on an
    // RTX 4080 under X11). So it is taken at rest — the loop asleep, no input come — or, where something keeps
    // moving, a second after the device is ready: one pause in place of CPU frames that go on.
    void take_over(bool at_rest) {
        if (!taking_over || !making || !making->ready()) return;
        const auto now = std::chrono::steady_clock::now();
        if (!ready_at) ready_at = now;
        if (!at_rest && now - *ready_at < std::chrono::seconds(1)) return;
        taking_over = false;
        mlibs::trace::Span span("takeover", "host");
        const GpuService* service = mlibs::compose::process().get(gpu_slot);
        auto made = making->get();
        std::unique_ptr<GpuRenderer> early = made ? making->renderer() : nullptr;
        making.reset();
        if (!made) {
            info("no GPU device ({}): the CPU draws", made.error().message);
            return;
        }
        const auto let_go = [&] { early.reset(); };  // the renderer before its device
        if ((*made)->caps().software) return let_go();  // automatic: the CPU renderer is faster
        std::optional<mlibs::trace::Span> claiming{std::in_place, "takeover.claim", "host"};
        if (auto claimed = service->claim(**made, platform::window_id(*window), options.vsync); !claimed) {
            info("the GPU device does not take the window ({}): the CPU draws", claimed.error().message);
            return let_go();
        }
        claiming.reset();
        auto r = early ? mlibs::rhi::Result<std::unique_ptr<GpuRenderer>>(std::move(early)) : service->renderer(**made);
        if (!r) {
            warn("the GPU renderer: {}; the CPU draws", r.error().message);
            return;
        }
        platform::release_framebuffer(*window);
        device = std::move(*made);
        gpu = std::move(*r);
        // What is on screen — the CPU's frame of the list — becomes the device's frame texture: captures read it there,
        // and the CPU's side goes (its surface, its renderer's caches): at rest the window holds the device's alone.
        if (frame.width() > 0) {
            mlibs::trace::Span adopting("takeover.adopt", "host");
            if (adopt_cpu_frame()) {
                frame = mrender::raster::Surface();
                composed = mrender::CpuFrame();
                damage = mrender::DamageTracker();
            } else {
                cpu_on_screen = true;
            }
        }
        runtime->set_on_device(true);
        runtime->set_device(device.get());
        tell_renderer();
        debug("the GPU took over from the CPU after {} frames", frames_shown);
    }

    // The frame texture the swapchain is shown from, at this size (made again when the size changes).
    bool frame_texture(std::uint32_t width, std::uint32_t height) {
        if (gpu_frame && width == gpu_w && height == gpu_h) return true;
        if (gpu_frame) device->destroy(gpu_frame);
        gpu_frame = {};
        gpu_w = gpu_h = 0;
        auto made = device->create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, width, height,
                                                           mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled, "host.frame"});
        if (!made) {
            warn("the frame texture: {}", made.error().message);
            return false;
        }
        gpu_frame = *made;
        gpu_w = width;
        gpu_h = height;
        return true;
    }

    // The CPU's frame on screen, sent to the device's frame texture as it is (the same premultiplied bytes): the frame
    // shown is the device's from now on, with nothing drawn again. In bands of about 256 KB, each waited for: the
    // device's staging buffer is written that much and no more (the whole frame at once would leave all of its pages
    // written for as long as the window lives — 1.9 MB at 800 × 600, 7.8 at 1896 × 1030).
    bool adopt_cpu_frame() {
        const auto width = static_cast<std::uint32_t>(frame.width()), height = static_cast<std::uint32_t>(frame.height());
        if (!frame_texture(width, height)) return false;
        const std::uint32_t band = std::max<std::uint32_t>(1, (256u << 10) / (width * 4));
        const auto bytes = std::as_bytes(frame.pixels());
        for (std::uint32_t top = 0; top < height; top += band) {
            const std::uint32_t rows = std::min(band, height - top);
            mlibs::rhi::CommandList sent;
            sent.upload_borrowed(gpu_frame, mlibs::rhi::Region{0, top, width, rows},
                                 bytes.subspan(static_cast<std::size_t>(top) * width * 4, static_cast<std::size_t>(rows) * width * 4));
            auto submitted = device->submit(sent);
            if (!submitted || !device->wait(*submitted)) {
                warn("the CPU's frame did not reach the device: the GPU draws from the next change");
                return false;
            }
        }
        return true;
    }

    // Who draws, as the runtime and its tools see it: the renderer's name and the machine's tier (performance tiers
    // §1: a discrete GPU T4, integrated graphics T3, the CPU drawing T2).
    void tell_renderer() {
        runtime->set_renderer(gpu ? std::string(mlibs::rhi::name(device->caps().backend)) : std::string("cpu"));
        mlibs::pipeline::Tier tier = mlibs::pipeline::Tier::t2;
        std::string why = "the CPU draws";
        if (gpu) {
            const std::string name = device->caps().device;
            const bool integrated = std::ranges::any_of(std::array<std::string_view, 6>{"Intel", "UHD", "Iris", "Mali", "Adreno", "Vega"},
                                                        [&](std::string_view s) { return name.find(s) != std::string::npos; });
            tier = integrated ? mlibs::pipeline::Tier::t3 : mlibs::pipeline::Tier::t4;
            why = std::format("{}: {}", integrated ? "integrated graphics" : "a GPU", name);
        }
        runtime->set_device_tier(tier, std::move(why));
    }

    // The device lost (a driver reset, a resume from sleep, a GPU taken away): what it held is gone, and what the
    // components made on it (a world's buffers, a viewport's textures) cannot be told so — the runtime has no way to
    // hand them another device yet. Rather than draw with what was lost, or leave the window blank, the application
    // ends once, saying why (exit code 3); nothing is presented again. Making the device again waits for the
    // runtime to tell its components (README: Known limitations).
    void lost(std::string_view where, std::string_view why) {
        if (quitting) return;
        error("{}: the GPU device was lost ({}); the application ends rather than draw with it — start it again", where, why);
        ++devices_lost;
        quitting = true;
        exit_code = 3;
    }

    // The frame as shown, read back (the GPU's texture, or the CPU's surface).
    std::expected<mrender::raster::Surface, ViewFailure> pixels() {
        if (!gpu || cpu_on_screen) {
            if (frame.width() <= 0) return std::unexpected(ViewFailure{ViewError::nothing_shown});
            return frame;
        }
        if (!gpu_frame) return std::unexpected(ViewFailure{ViewError::nothing_shown});
        auto bytes = device->read(gpu_frame, mlibs::rhi::Region{0, 0, gpu_w, gpu_h});
        if (!bytes) return std::unexpected(ViewFailure{ViewError::capture_failed, bytes.error().message});
        mrender::raster::Surface out(static_cast<int>(gpu_w), static_cast<int>(gpu_h));
        std::memcpy(out.pixels().data(), bytes->data(), bytes->size());
        return out;
    }

    void show() {
        const mrender::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        const float scale = runtime->scale();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int hgt = static_cast<int>(std::lround(runtime->size().height * scale));
        if (gpu) {
            show_gpu(list, h, scale, w, hgt);
            return;
        }
        if (h == shown && frame.width() == w && frame.height() == hgt) return;
        if (frame.width() != w || frame.height() != hgt) frame = mrender::raster::Surface(w, hgt);
        {
            mlibs::trace::Span span("render", "host");
            const mrender::Resources resources{&runtime->images(), glyphs.get()};
            // Only the rows the new list can change: the same bytes as a full render.
            const auto rows = damage.update(list, w, hgt, scale, resources);
            const mrender::FrameLayer ui_layer{.name = "ui", .list = &list, .resources = resources, .scale = scale};
            mrender::FrameOptions how;
            how.background = options.background;
            how.rows = rows;
            // In bands at once — but not the first frames of a window the device takes over (UI9): the pool's threads
            // would stay for frames the CPU no longer draws.
            if (options.parallel_raster && !taking_over) how.parallel = &raster_parallel();
            how.external = [this](std::uint64_t number) { return runtime->picture(number); };  // a world in a viewport
            if (!rows.empty()) (void)composed.render({&ui_layer, 1}, frame, how);
        }
        mlibs::trace::Span span("present", "host");
        platform::present(*window, frame);
        shown = h;
        ++frames_shown;
    }

    // A picture a component shows from elsewhere (Services::add_picture: a world in a viewport), as a texture of
    // this device: its pixels sent when the number it is drawn by changes, kept until it is another size.
    ShownPictures shown_pictures;  // the viewports' pictures as textures on the device
    std::optional<mlibs::rhi::Texture> shown_texture(std::uint64_t number) {
        auto texture = shown_pictures.texture(*runtime, *device, number);
        if (!shown_pictures.failed().empty()) warn("{}", shown_pictures.failed());
        return texture;
    }

    void show_gpu(const mrender::DisplayList& list, std::uint64_t h, float scale, int w, int hgt) {
        if (w <= 0 || hgt <= 0) return;
        const auto uw = static_cast<std::uint32_t>(w), uh = static_cast<std::uint32_t>(hgt);
        if (h == shown && uw == gpu_w && uh == gpu_h) return;
        if (cpu_on_screen && h == shown && frame.width() == w && frame.height() == hgt) return;  // the CPU's frame of this list (UI9)
        if (!frame_texture(uw, uh)) return;
        {
            mlibs::trace::Span span("render", "host");
            const mrender::Resources resources{&runtime->images(), glyphs.get()};
            const mrender::FrameLayer ui_layer{.name = "ui", .list = &list, .resources = resources, .scale = scale};
            mrender::GpuFrameOptions how;
            how.background = options.background;
            how.external = [this](std::uint64_t number) { return shown_texture(number); };
            how.glyph_masks = mrender::glyph_masks_a_live_frame;  // a window's frames: text is its outlines', in motion and at rest
            auto r = gpu->render({&ui_layer, 1}, gpu_frame, uw, uh, how);
            if (!r) {
                if (r.error().code == mlibs::rhi::Errc::device_lost) return lost("the GPU renderer", r.error().message);
                warn("the GPU renderer: {}", r.error().message);
                return;
            }
        }
        mlibs::trace::Span span("present", "host");
        auto p = device->present(gpu_frame);
        if (!p) {
            if (p.error().code == mlibs::rhi::Errc::device_lost) return lost("present", p.error().message);
            warn("present: {}", p.error().message);
        }
        shown = h;
        ++frames_shown;
        if (cpu_on_screen) {
            cpu_on_screen = false;
            frame = mrender::raster::Surface();  // the CPU's frame is no longer what is shown
        }
    }

    // Effects whose files changed, loaded again; the next frame redrawn.
    void reload_effects() {
        if (!effects) return;
        const GpuEffects::Reloaded again = effects->reload();
        for (const std::string& line : again.reloaded) info("{}", line);
        for (const std::string& line : again.problems) warn("{}", line);
        if (!again.reloaded.empty()) shown = 0;
    }
};

Desktop::Desktop(DesktopOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    // When the GPU may draw, SDL is brought up here and the device is made on a thread of its own. A window that
    // waits for it (the GPU insisted on, effects, a window with no framebuffer of its own) begins it at once, so the
    // driver's start overlaps the fonts, the runtime and the window (RC3.6). A window whose first frames the CPU draws
    // (UI9) begins it once the first frame is shown: the driver's start — its libraries read and loaded, the device
    // made — is then wholly off the first frame's path, cold or warm. Where SDL cannot start, the window's opening
    // says so. The start's parts as spans (a run's --profile): where a cold start's time goes before the first frame.
    std::optional<mlibs::trace::Span> sdl{std::in_place, "start.sdl", "host"};
    if (const GpuService* service = mlibs::compose::process().get(gpu_slot);
        service && d.options.renderer != DesktopOptions::Renderer::cpu && platform::start(d.options.video_driver)) {
        d.device_options.vulkan_library = d.options.vulkan_library;
        d.device_options.hardware_only = d.options.renderer == DesktopOptions::Renderer::automatic;
        d.device_options.video_driver = d.options.video_driver;
        d.device_options.made = [] { platform::wake(); };  // a loop asleep with the CPU's frame on screen takes the device (UI9)
        platform::hold();  // no other host's end, and no last window closing, quits SDL under this making
        d.holds_sdl = true;
        d.cpu_first = d.options.renderer == DesktopOptions::Renderer::automatic && d.options.effects.empty() && d.options.render_driver.empty() &&
                      platform::own_framebuffer();
        if (!d.cpu_first) d.making = service->begin(d.device_options);
    }
    sdl.reset();
    // The interface answers as the platform it runs on does (D6): Command for shortcuts, a scroll's
    // edges, when a right button asks for a context menu.
    if constexpr (mcpplibs::os::is_macos) set_platform(Platform::macos);
    else if constexpr (mcpplibs::os::is_windows) set_platform(Platform::windows);
    else set_platform(Platform::freedesktop);
    if (d.options.runtime) {
        // A runtime the caller made (a script binding's, which existed first):
        // the window shows it, on the interactive clock.
        d.runtime = d.options.runtime;
        d.runtime->clock() = hanim::Clock::interactive();
        d.runtime->resize(d.options.size, d.runtime->scale());
        d.tree = std::make_unique<semantic::Tree>(*d.runtime);
        d.glyphs = std::make_unique<mrender::text::GlyphCache>(d.runtime->fonts());
        return;
    }
    mlibs::text::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        mlibs::trace::Span span("start.fonts", "host");
        d.own_fonts = std::make_unique<mlibs::text::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        fonts = d.own_fonts.get();
    }
    mlibs::trace::Span made("start.runtime", "host");
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
    d.glyphs = std::make_unique<mrender::text::GlyphCache>(*fonts);
}

Desktop::~Desktop() {
    if (omui::window().host() == impl_.get()) omui::window().detach();
    if (impl_->endpoint) impl_->endpoint.stop();
    // The adapter lets go of the window before the window goes.
    impl_->access.reset();
    impl_->mirror.reset();
    // The components drawn on the device (a world's targets) before the device: an own runtime goes now, with
    // what reads it; a borrowed one is told there is no device any more.
    if (impl_->own_runtime) {
        impl_->endpoint.reset();
        impl_->tree.reset();
        impl_->runtime = nullptr;
        impl_->own_runtime.reset();
    } else if (impl_->runtime) {
        impl_->runtime->set_device(nullptr);
    }
    // The GPU's before the window it claimed (one still being made: waited for, then let go).
    impl_->making.reset();
    if (impl_->device && impl_->gpu_frame) impl_->device->destroy(impl_->gpu_frame);
    if (impl_->device) impl_->shown_pictures.release(*impl_->device);
    impl_->gpu.reset();
    impl_->device.reset();
    impl_->window.reset();
    // SDL brought up for a device: its hold given back (and SDL let go when no window and no other host keeps it).
    if (impl_->holds_sdl) platform::release();
    else platform::stop();
}

std::expected<void, ViewFailure> Desktop::open() {
    Impl& d = *impl_;
    if (d.window) return {};
    // A window for the GPU first (when it may draw); the CPU's otherwise.
    // (No provider of the GPU path: the CPU's window at once — unless the GPU is insisted on, which open_gpu refuses.)
    const bool try_gpu = d.options.renderer != DesktopOptions::Renderer::cpu &&
                         (mlibs::compose::process().get(gpu_slot) != nullptr || d.options.renderer == DesktopOptions::Renderer::gpu);
    // UI9: the CPU draws the first frames into the window's own framebuffer, the device is made meanwhile and takes
    // over when it is ready — where the window keeps a framebuffer (X11, Windows, offscreen; elsewhere the CPU's frames
    // need a renderer the device could not share the window with). Not when the GPU is insisted on, nor with effects
    // (only the GPU draws them): the first frames would not be the picture the GPU draws.
    const bool cpu_first = try_gpu && d.cpu_first;
    d.framework = d.options.frame == DesktopOptions::Frame::framework;
    platform::WindowOptions wo{.title = d.options.title,
                               .size = d.options.size,
                               .resizable = d.options.resizable,
                               .vsync = d.options.vsync,
                               .video_driver = d.options.video_driver,
                               .render_driver = d.options.render_driver,
                               .gpu = try_gpu && !cpu_first,
                               .framework = d.framework,
                               .hidden = true,
                               // The CPU's frames into the window's own framebuffer where its driver keeps one (X11's,
                               // Windows', the offscreen driver's), unless a renderer is named: no graphics library is
                               // loaded for them (C1: 46.7 → 13.1 MiB private dirty, half the cold start, where OpenGL
                               // would be llvmpipe). A GPU's window ignores it.
                               .framebuffer = d.options.render_driver.empty()};
    // Where the interface draws nothing, a frameless window shows what is behind it.
    if (d.framework) d.options.background = Color::transparent();
    std::optional<mlibs::trace::Span> opening{std::in_place, "start.window", "host"};
    auto w = platform::open(wo);
    opening.reset();
    if (!w) return std::unexpected(ViewFailure{ViewError::window_failed, w.error()});
    d.window = std::move(*w);
    if (cpu_first) d.taking_over = true;
    else if (try_gpu) {
        if (auto g = d.open_gpu(); !g) return std::unexpected(g.error());
        if (!d.gpu) {
            // No GPU after all: the window again, for the CPU's frames (into its own framebuffer, as above).
            d.window.reset();
            mlibs::trace::Span reopened("start.window.cpu", "host");
            wo.gpu = false;
            auto again = platform::open(wo);
            if (!again) return std::unexpected(ViewFailure{ViewError::window_failed, again.error()});
            d.window = std::move(*again);
        }
    }
    if (!d.options.effects.empty()) {
        // Effects are the GPU path's: a program built without it loads none, and says so once.
        if (const GpuService* service = mlibs::compose::process().get(gpu_slot)) {
            d.effects = service->effects(d.device.get(), d.options.effect_data);
            for (const std::string& line : d.effects->load(d.options.effects)) warn("{}", line);
        } else {
            warn("effects in {} are not loaded: {}", mcpplibs::os::fs::utf8(d.options.effects), mlibs::compose::process().missing(gpu_slot.info).message);
        }
        if (d.effects && !d.gpu) warn("effects in {} are drawn by the GPU renderer; the CPU renderer draws their layers without them", mcpplibs::os::fs::utf8(d.options.effects));
    }
    d.runtime->resize(platform::logical_size(*d.window), platform::pixel_scale(*d.window));
    // Who draws, and the machine's tier (told again when a device takes over); the governor on: these frames are a person's.
    d.tell_renderer();
    d.runtime->set_governor(true);
    // The window's operations are verbs (window.*), its state the interface's to read.
    omui::window().attach(&d, d.runtime->verbs());
    d.window_state();
    d.hit_test();
    d.runtime->set_clipboard([] { return platform::clipboard(); }, [](std::string_view t) { platform::set_clipboard(t); });
    d.runtime->set_pointer_lock([](bool held) { return platform::lock_pointer(held); });
    d.runtime->set_url_opener([](std::string_view url) { return platform::open_url(url); });
    d.runtime->set_file_offer([](const Runtime::OfferedFile& file) { return platform::save_download(file.name, file.bytes); });
    // The system's file chooser and tray (W7: the SDK's System draws what is missing).
    d.runtime->set_native_surfaces({.choose_files = [&d](const Runtime::FileChoice& choice, Runtime::FilesChosen chosen) {
                                        // The system's chooser through mpal where it is here (Linux's desktop portal),
                                        // the person answering on a thread of its own; else SDL's.
                                        if (mcpplibs::mpal::can_pick()) {
                                            std::thread([asked = pick_of(choice), chosen = std::move(chosen)] {
                                                auto picked = mcpplibs::mpal::pick(asked);
                                                if (!picked) warn("desktop: choosing files: {}", picked.error().text);
                                                chosen(picked ? std::move(*picked) : std::vector<std::filesystem::path>{});
                                                platform::wake();
                                            }).detach();
                                            return true;
                                        }
                                        return d.window && platform::choose_files(*d.window, choice, std::move(chosen));
                                    },
                                    // The system's notification (mpal); the person pressing it runs its verb.
                                    .notify = [&d](const Runtime::Notice& notice, std::function<void()> pressed) {
                                        if (!d.notices) d.notices = NativeNotices::open();
                                        return *d.notices && (*d.notices)->show(d.options.title, notice, std::move(pressed));
                                    },
                                    .tray = [](const std::vector<Runtime::MenuEntry>& entries, std::string_view tooltip,
                                               std::function<void(std::string_view)> chosen) { return platform::tray(entries, tooltip, std::move(chosen)); }});
    // Web pages in the system's own view (D-141), where it has one: macOS's WKWebView.
    if constexpr (mcpplibs::os::is_macos) {
        if (d.window) d.web_pages = web::open_pages(platform::native_window(*d.window));
        if (d.web_pages) {
            Runtime::NativeSurfaces surfaces = d.runtime->native_surfaces();
            surfaces.web_open = [&d](std::string_view url, std::function<void(const WebPage&)> changed) {
                return web::open(*d.web_pages, url, std::move(changed));
            };
            surfaces.web_place = [&d](std::uint64_t page, Rect bounds, bool shown) { web::place(*d.web_pages, page, bounds, shown, d.runtime->size().height); };
            surfaces.web_command = [&d](std::uint64_t page, std::string_view what, std::string_view argument) {
                return web::command(*d.web_pages, page, what, argument);
            };
            surfaces.web_close = [&d](std::uint64_t page) { web::close(*d.web_pages, page); };
            d.runtime->set_native_surfaces(std::move(surfaces));
        }
    }
    // The agent surface is a provider's (agent::endpoint_slot): a program built without it has no captures and
    // serves no agent, as a platform without local endpoints does not.
    const agent::EndpointService* surface = mlibs::compose::process().get(agent::endpoint_slot);
    const agent::CaptureService* captures = agent::captures();  // installed with it (agent::install)
    if (surface && captures)
        d.runtime->set_svg_capture([&d, captures](Rect region) -> std::expected<std::string, ViewFailure> {
            const mrender::Resources resources{&d.runtime->images(), d.glyphs.get()};
            auto capture = captures->vector_capture(*d.runtime, resources, region, d.runtime->scale(), std::nullopt);
            if (!capture) return std::unexpected(capture.error());
            return std::move(capture->svg);
        });
    // The frame on screen as PNG, cut to a region: the runtime's capture_png and the agent's capture.
    const auto shown_png = [&d, captures](Rect region) -> std::expected<std::vector<std::uint8_t>, ViewFailure> {
        if (!captures) return std::unexpected(ViewFailure{ViewError::no_pixel_capture});
        // The frame on screen, cut to the region (logical pixels).
        auto shown_frame = d.pixels();
        if (!shown_frame) return std::unexpected(shown_frame.error());
        const mrender::raster::Surface& f = *shown_frame;
        const PixelRect r = mlibs::geom::snap(region, d.runtime->scale());
        const int x0 = std::clamp(r.x, 0, f.width()), y0 = std::clamp(r.y, 0, f.height());
        const int x1 = std::clamp(r.x + r.width, x0, f.width()), y1 = std::clamp(r.y + r.height, y0, f.height());
        if (x1 <= x0 || y1 <= y0) return std::unexpected(ViewFailure{ViewError::empty_region, "the region is outside the window"});
        mlibs::image::View v{reinterpret_cast<const std::uint8_t*>(f.row(y0) + x0), x1 - x0, y1 - y0, f.width() * 4, mlibs::image::Alpha::premultiplied};
        auto png = captures->png(v);
        if (!png) return std::unexpected(ViewFailure{ViewError::capture_failed, std::move(png.error())});
        return std::move(*png);
    };
    if (surface && captures) d.runtime->set_png_capture(shown_png);
    if (d.options.agent && surface) {
        agent::EndpointOptions eo;
        eo.name = d.options.app;
        eo.directory = d.options.agent_directory;
        eo.protocol.app = d.options.app;
        eo.protocol.input = d.options.agent_input;
        eo.protocol.capture = shown_png;
        eo.protocol.capture_svg = [&d, captures](Rect region) -> std::expected<agent::VectorCapture, ViewFailure> {
            if (!captures) return std::unexpected(ViewFailure{ViewError::no_vector_capture});
            const mrender::Resources resources{&d.runtime->images(), d.glyphs.get()};
            return captures->vector_capture(*d.runtime, resources, region, d.runtime->scale(), std::nullopt);
        };
        eo.wake = [] { platform::wake(); };
        d.endpoint = agent::Served(*surface, *d.runtime, std::move(eo));
        if (auto r = d.endpoint.start(); !r) {
            warn("the agent endpoint did not start: {}", r.error());
            d.endpoint.reset();
        }
    }
    // Assistive technology's adapter, attached before the window is shown.
    if (d.options.accessibility && accessibility::available()) {
        d.mirror = std::make_unique<accessibility::Mirror>(*d.runtime, accessibility::MirrorOptions{.name = d.options.title});
        d.mirror->set_wake([] { platform::wake(); });
        auto adapter = accessibility::Adapter::attach(*d.mirror, platform::native_window(*d.window), {.scale = d.runtime->scale()});
        if (adapter) {
            d.access = std::move(*adapter);
        } else {
            warn("assistive technology is not served: {}", adapter.error());
            d.mirror.reset();
        }
    }
    platform::show(*d.window);
    return {};
}

bool Desktop::is_open() const noexcept { return impl_->window != nullptr; }
Runtime& Desktop::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Desktop::tree() noexcept { return *impl_->tree; }
agent::Endpoint* Desktop::endpoint() noexcept { return impl_->endpoint.get(); }
accessibility::Mirror* Desktop::accessibility_mirror() noexcept { return impl_->mirror.get(); }
accessibility::Adapter* Desktop::accessibility_adapter() noexcept { return impl_->access.get(); }
std::size_t Desktop::frames_shown() const noexcept { return impl_->frames_shown; }
bool Desktop::gpu() const noexcept { return impl_->gpu != nullptr; }
bool Desktop::gpu_pending() const noexcept { return impl_->taking_over; }
mrender::effect::Library* Desktop::effects() noexcept { return impl_->effects ? &impl_->effects->library() : nullptr; }
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
        // Slower than the display — or with no vsync to hold the loop to it (none asked, or a window's own
        // framebuffer, which shows a frame at once): wait out the interval (an event still wakes it).
        const double period = display > 0 ? 1.0 / display : 1.0 / 60;
        const auto due = d.last_frame + std::chrono::duration<double>(interval);
        const auto now = std::chrono::steady_clock::now();
        const bool held = d.options.vsync && platform::held_to_refresh(*d.window);
        if ((interval > period * 1.5 || !held) && due > now) {
            sleep = true;
            timeout = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(std::chrono::duration<double, std::milli>(due - now).count())));
        }
    }
    if (d.closed_at) {
        // Dissolving: frames as fast as the display shows them, until it is gone.
        sleep = false;
        timeout.reset();
    }
    // A device that was ready while input came waits for the next rest (UI9): a wait for an event alone would not
    // come back to take it, so the loop looks again soon.
    if (sleep && d.taking_over && d.ready_at) timeout = std::min(timeout.value_or(std::chrono::milliseconds(50)), std::chrono::milliseconds(50));
    // Gamepads once the application takes their input (an idle window polls no devices).
    if (d.runtime->gamepads_wanted()) platform::start_gamepads();
    const bool was_idle = d.idle;
    platform::Polled polled = platform::poll(*d.window, sleep, timeout);
    d.take_over(was_idle && polled.events.empty());  // the device, when it became ready meanwhile (UI9)
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
    std::optional<agent::AgentInput> given;
    const auto agent_begin = polled.events.size();
    if (d.endpoint) {
        d.endpoint.pump();
        // An agent's input, a frame's worth, after the person's own.
        given = d.endpoint.take_input();
        if (given) polled.events.insert(polled.events.end(), std::make_move_iterator(given->events.begin()), std::make_move_iterator(given->events.end()));
    }
    d.reload_effects();
    // What the system's web views show now, told where it changed (posted to the runtime).
    if constexpr (mcpplibs::os::is_macos)
        if (d.web_pages) web::poll(*d.web_pages);
    FrameInput in;
    in.events = std::move(polled.events);
    // Evaluated where it will be seen: the next refresh (D-70).
    const auto start = std::chrono::steady_clock::now();
    in.present_at = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(display > 0 ? 1.0 / display : 1.0 / 60));
    in.display_hz = display;
    // The frame's place on its schedule: one the timer woke a little late (its millisecond) keeps the rate — the
    // next waits that much less —; one later than a whole interval (idle, or slow) starts the schedule again.
    const auto every = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(interval));
    const auto slot = d.last_frame + every;
    d.last_frame = interval > 0 && start >= slot && start - slot < every ? slot : start;
    if (d.access) {
        // What a screen reader asked, performed as the person's input before
        // the frame shows it; the window's focus, as assistive technology
        // must know it.
        for (const InputEvent& e : in.events)
            if (const auto* f = std::get_if<FocusEvent>(&e)) d.access->window_focus(f->gained);
        if (!d.closed_at) (void)d.mirror->serve();
    }
    std::array<InputAttribution, 1> attribution;
    if (given && in.events.size() > agent_begin) {
        attribution[0] = {agent_begin, in.events.size(), given->actor, given->session};
        in.attribution = attribution;
    }
    FrameOutput out = d.runtime->frame(in);
    if (d.access) {
        d.access->set_scale(d.runtime->scale());
        // Where the window is on the screen, told when it moved: a node's place on the screen is the window's plus its
        // own (AT-SPI's screen coordinates; the window at 0,0 otherwise, RM2).
        if (const auto placed = platform::screen_bounds(*d.window)) {
            const auto& [frame, content] = *placed;
            const std::array<float, 8> now{frame.x, frame.y, frame.width, frame.height, content.x, content.y, content.width, content.height};
            if (now != d.access_bounds) {
                d.access_bounds = now;
                d.access->window_bounds(frame, content);
            }
        }
        d.access->update();
    }
    d.demand = out.demand;
    const std::size_t before = d.frames_shown;
    // A stage failed: what it would show is not finished — the last frame shown stays, and it is said once
    // (and again if it keeps failing for a second), not every frame.
    if (!out.failure.empty()) {
        if (d.failed_frames++ % 60 == 0)
            warn("a frame failed ({}{}): the last frame shown stays", out.failure,
                 d.failed_frames > 1 ? std::format(", {} in a row", d.failed_frames) : std::string());
    } else {
        if (d.failed_frames > 0) warn("frames are drawn again after {} that failed", d.failed_frames);
        d.failed_frames = 0;
    }
    if (out.failure.empty()) d.show();
    // A frame shown: when, and what it cost from taking the input to presenting it (D-59).
    if (d.frames_shown != before)
        d.runtime->frame_shown({d.runtime->clock().now(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()});
    platform::text_input(*d.window, d.runtime->text_input_rect());
    platform::cursor(*d.window, d.runtime->cursor());
    d.idle = !out.animating && out.demand.idle() && !d.closed_at;
    // The first frame shown by the CPU: the device begun now, off its path (UI9).
    if (d.taking_over && !d.making && d.frames_shown > 0)
        if (const GpuService* service = mlibs::compose::process().get(gpu_slot)) d.making = service->begin(d.device_options);
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

std::expected<mrender::raster::Surface, ViewFailure> Desktop::window_pixels() {
    if (!impl_->window) return std::unexpected(ViewFailure{ViewError::not_open});
    if (impl_->gpu) return impl_->pixels();  // the frame the swapchain was given, exactly
    auto read = platform::read_back(*impl_->window);
    if (!read) return std::unexpected(ViewFailure{ViewError::capture_failed, std::move(read.error())});
    return std::move(*read);
}

std::vector<std::string> preferred_locales() { return platform::preferred_locales(); }

}  // namespace omui
