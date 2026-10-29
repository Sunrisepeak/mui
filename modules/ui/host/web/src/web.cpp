module openmui.host.web;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.raster;
import openmui.render.vector;
import openmui.text;
import openmui.trace;
import mcpplibs.os;
import :platform;

namespace omui {

struct Web::Impl {
    WebOptions                               options;
    std::unique_ptr<typeset::FontCollection> own_fonts;
    std::unique_ptr<Runtime>                 own_runtime;
    Runtime*                                 runtime = nullptr;
    std::unique_ptr<semantic::Tree>          tree;
    std::unique_ptr<typeset::GlyphCache>     glyphs;
    render::CpuRenderer                      renderer;
    render::DamageTracker                    damage;
    raster::Surface                          frame;
    std::uint64_t                            shown = 0;   // the list hash on the canvas
    std::size_t                              frames_shown = 0;
    std::vector<InputEvent>                  pending;     // synthesized, for the next step
    std::vector<InputEvent>                  events;      // this step's (kept: no allocation a frame)
    std::function<void(bool)>                visibility;
    bool                                     opened = false;
    bool                                     is_hidden = false;
    bool                                     quitting = false;
    int                                      exit_code = 0;
    bool                                     idle = false;
    bool                                     woken = false;  // the next paced step frames
    FrameDemand                              demand;
    std::chrono::steady_clock::time_point    last_frame{};
    Size                                     size;
    float                                    scale = 1.0f;

    void show() {
        const render::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int hgt = static_cast<int>(std::lround(runtime->size().height * scale));
        if (w <= 0 || hgt <= 0) return;
        if (h == shown && frame.width() == w && frame.height() == hgt) return;
        if (frame.width() != w || frame.height() != hgt) frame = raster::Surface(w, hgt);
        {
            trace::Span span("render", "host");
            const render::Resources resources{&runtime->images(), glyphs.get()};
            const auto rows = damage.update(list, w, hgt, scale, resources);
            render::CpuOptions o;
            o.scale = scale;
            o.background = options.background;
            o.rows = rows;
            if (!rows.empty()) (void)renderer.render(list, frame, resources, o);
        }
        trace::Span span("present", "host");
        if (opened) page::present(options.canvas, frame);
        shown = h;
        ++frames_shown;
    }

    // The canvas's size and the device's ratio, as the page has them now.
    void follow_canvas() {
        if (!opened) return;
        const page::Canvas c = page::canvas(options.canvas);
        if (c.size.width <= 0 || c.size.height <= 0) return;
        if (c.size.width == size.width && c.size.height == size.height && c.scale == scale) return;
        size = c.size;
        scale = c.scale;
        events.push_back(ResizeEvent{size, scale});
    }

    // `paced`: the page's call, once a display frame — a frame only when one
    // is wanted; else always one (step(), as the desktop's step without waiting).
    bool step(bool paced) {
        events.clear();
        follow_canvas();
        if (opened) page::take(events);
        events.insert(events.end(), std::make_move_iterator(pending.begin()), std::make_move_iterator(pending.end()));
        pending.clear();
        // The page hidden or shown again: the application's hook, then the window's focus.
        if (const bool h = opened && page::hidden(); h != is_hidden) {
            is_hidden = h;
            if (visibility) visibility(h);
            events.push_back(FocusEvent{!h});
        }
        // Frames as often as they are wanted (D-70): none while nothing moves
        // and no timer is due; while something does, at its demand's rate
        // within the display's (the page calls at the display's).
        // A change made outside a frame without wake() is seen within half a second.
        const auto now = std::chrono::steady_clock::now();
        if (paced && events.empty() && !std::exchange(woken, false)) {
            if (idle) {
                const auto next = runtime->next_timer();
                const bool due = next && *next <= runtime->now();
                if (!due && now - last_frame < std::chrono::milliseconds(500)) return false;
            } else if (const double interval = frame_interval(demand, 60); interval > 1.5 / 60) {
                if (now - last_frame < std::chrono::duration<double>(interval * 0.98)) return false;
            }
        }
        FrameInput in;
        in.events = std::move(events);
        in.present_at = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60));
        in.display_hz = 60;
        last_frame = now;
        FrameOutput out = runtime->frame(in);
        events = std::move(in.events);
        demand = out.demand;
        show();
        idle = !out.animating && out.demand.idle();
        return true;
    }
};

namespace {

void tick(void* user) {
    auto& d = *static_cast<Web::Impl*>(user);
    if (!d.quitting) (void)d.step(true);
}

}  // namespace

Web::Web(WebOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    d.size = d.options.size;
    d.opened = page::open(d.options.canvas, d.options.title);
    if (d.opened) {
        const page::Canvas c = page::canvas(d.options.canvas);
        if (c.size.width > 0 && c.size.height > 0) d.size = c.size, d.scale = c.scale;
    }
    if (d.options.runtime) {
        d.runtime = d.options.runtime;
        d.runtime->clock() = hanim::Clock::interactive();
        d.runtime->resize(d.size, d.scale);
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
    ro.size = d.size;
    ro.scale = d.scale;
    ro.reduce_motion = d.options.reduce_motion;
    ro.reduce_transparency = d.options.reduce_transparency;
    d.own_runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime = d.own_runtime.get();
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
}

Web::~Web() = default;

Runtime& Web::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Web::tree() noexcept { return *impl_->tree; }
NodeId Web::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }
const raster::Surface& Web::pixels() const noexcept { return impl_->frame; }
std::size_t Web::frames_shown() const noexcept { return impl_->frames_shown; }
bool Web::hidden() const noexcept { return impl_->is_hidden; }
void Web::on_visibility(std::function<void(bool)> fn) { impl_->visibility = std::move(fn); }

void Web::synthesize(std::span<const InputEvent> events) { impl_->pending.insert(impl_->pending.end(), events.begin(), events.end()); }

bool Web::step() { return impl_->step(false); }

void Web::wake() { impl_->woken = true; }

void Web::quit(int code) {
    impl_->quitting = true;
    impl_->exit_code = code;
}

int Web::run() {
    Impl& d = *impl_;
    // The page's loop: on the Web this does not return (the stack, and this
    // host on it, stay as they are while the page runs).
    if (d.opened && page::loop(&tick, &d)) return d.exit_code;
    while (!d.quitting) {
        if (!d.step(true)) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return d.exit_code;
}

bool in_browser() noexcept { return page::exists(); }

std::vector<std::string> browser_locales() { return page::exists() ? page::locales() : std::vector<std::string>{}; }

}  // namespace omui
