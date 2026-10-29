module openmui.host.web;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.raster;
import openmui.render.vector;
import openmui.text;
import openmui.trace;
import openmui.agent;
import openmui.image;
import openmui.native;
import openmui.native.store;
import mcpplibs.os;
import :platform;
import :mirror;

namespace omui {

namespace {

// The page's localStorage as the program's store: its keys under the
// application's id, so the programs of one origin (a site of several) keep
// apart.
class PageStore final : public Store {
public:
    explicit PageStore(std::string_view app) : prefix_(std::format("omui/{}/", app)) {}

    [[nodiscard]] std::string_view kind() const noexcept override { return "localStorage"; }
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override {
        if (!valid_store_key(key)) return std::nullopt;
        return page::storage_get(prefix_ + std::string(key));
    }
    std::expected<void, std::string> set(std::string_view key, std::string_view value) override {
        if (!valid_store_key(key)) return std::unexpected(std::format("a key is 1 to {} bytes, not {}", store_key_bytes, key.size()));
        if (!page::storage_set(prefix_ + std::string(key), value)) return std::unexpected("the page's storage is full (or the person turned it off)");
        return {};
    }
    bool remove(std::string_view key) override { return valid_store_key(key) && page::storage_remove(prefix_ + std::string(key)); }
    [[nodiscard]] std::vector<std::string> keys(std::string_view prefix) const override {
        std::vector<std::string> out;
        for (std::string& k : page::storage_keys(prefix_ + std::string(prefix)))
            if (k.starts_with(prefix_)) out.push_back(k.substr(prefix_.size()));
        std::ranges::sort(out);
        return out;
    }

private:
    std::string prefix_;
};

bool same(const std::optional<Rect>& a, const std::optional<Rect>& b) {
    if (!a || !b) return !a && !b;
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

}  // namespace

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
    // The page's other halves: the hidden DOM, window.omui, the store, the textarea.
    std::unique_ptr<DomMirror>               mirror;     // with a page
    std::unique_ptr<agent::Protocol>         protocol;   // made at the first request
    std::unique_ptr<PageStore>               page_store; // with a page that has localStorage
    struct Asked {
        page::Request::Kind                    kind = page::Request::Kind::agent;
        std::string                            text;
        std::function<void(std::string_view)> reply;  // agent: where its records go
    };
    std::vector<page::Request>               requests;   // this step's, from the page
    std::vector<Asked>                       asked;      // synthesized and request()ed, for the next step
    std::optional<Rect>                      caret;      // where the textarea is
    std::string                              caret_label;
    bool                                     caret_sent = false;

    // Joins the runtime to the page: its clipboard, the store, the mirror.
    void attach() {
        if (!opened) return;
        runtime->set_clipboard([] { return page::clipboard(); }, [](std::string_view t) { page::set_clipboard(t); });
        mirror = std::make_unique<DomMirror>(*runtime);
        if (page::storage()) {
            page_store = std::make_unique<PageStore>(options.app);
            use_store(page_store.get());
        }
        // Scripts reach the same store (openmui:native/store).
        if (!native::find("store")) (void)natives::store::define();
    }

    agent::Protocol& agents() {
        if (!protocol) {
            agent::ProtocolOptions o;
            o.app = options.app;
            o.default_actor = "agent:page";
            o.remote = true;  // window.omui is the bus: openas decides, never the person (S-10, S-11)
            o.capture = [this](Rect region) -> std::expected<std::vector<std::uint8_t>, std::string> {
                // The frame on the canvas, cut to the region (logical pixels).
                const raster::Surface& f = frame;
                const int x0 = std::clamp(static_cast<int>(std::floor(region.x * scale)), 0, f.width());
                const int y0 = std::clamp(static_cast<int>(std::floor(region.y * scale)), 0, f.height());
                const int x1 = std::clamp(static_cast<int>(std::ceil(region.right() * scale)), x0, f.width());
                const int y1 = std::clamp(static_cast<int>(std::ceil(region.bottom() * scale)), y0, f.height());
                if (x1 <= x0 || y1 <= y0) return std::unexpected("the region is outside the canvas (or nothing is shown yet)");
                imageio::View v{reinterpret_cast<const std::uint8_t*>(f.row(y0) + x0), x1 - x0, y1 - y0, f.width() * 4,
                                imageio::Alpha::premultiplied};
                return imageio::encode_png(v);
            };
            protocol = std::make_unique<agent::Protocol>(*runtime, std::move(o));
        }
        return *protocol;
    }

    // A request of the page's, or one synthesized: served before the frame.
    void serve(page::Request::Kind kind, const std::string& text, const std::function<void(std::string_view)>& reply) {
        trace::Span span("request", "host");
        if (kind == page::Request::Kind::agent) {
            agents().request(text, reply);
            return;
        }
        NodeId n = runtime->find(text);
        if (!runtime->valid(n)) n = runtime->reveal(text);
        if (!runtime->valid(n)) return;
        if (kind == page::Request::Kind::focus) {
            runtime->focus(n);
            return;
        }
        // Activated by assistive technology: as the person's press of its middle.
        const Point c = runtime->bounds(n).center();
        PointerEvent p;
        p.position = c;
        events.push_back(p);
        p.kind = PointerKind::down, p.button = MouseButton::left, p.buttons = 1;
        events.push_back(p);
        p.kind = PointerKind::up, p.buttons = 0;
        events.push_back(p);
    }

    // After a frame: the hidden DOM brought up to date, the textarea moved.
    void sync_page() {
        if (!opened) return;
        if (mirror) {
            trace::Span span("mirror", "host");
            if (auto batch = mirror->sync()) page::mirror(batch->json());
        }
        const std::optional<Rect> at = runtime->text_input_rect();
        const std::string_view label = mirror ? mirror->focused_name() : std::string_view{};
        if (caret_sent && same(at, caret) && label == caret_label) return;
        page::text_input(at, label);
        caret = at;
        caret_label = std::string(label);
        caret_sent = true;
    }

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
        requests.clear();
        follow_canvas();
        if (opened) page::take(events, requests);
        events.insert(events.end(), std::make_move_iterator(pending.begin()), std::make_move_iterator(pending.end()));
        pending.clear();
        // What the hidden DOM and window.omui asked, between frames.
        const bool asked_any = !requests.empty() || !asked.empty();
        for (const page::Request& r : requests) serve(r.kind, r.text, [](std::string_view line) { page::agent_reply(line); });
        for (const Asked& a : std::exchange(asked, {})) serve(a.kind, a.text, a.reply);
        if (asked_any) woken = true;
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
        sync_page();
        // An agent waiting for a condition: checked after each frame, frames until it holds or times out.
        if (protocol) {
            protocol->poll();
            if (protocol->pending() > 0) woken = true;
        }
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
        d.attach();
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
    d.attach();
}

Web::~Web() {
    if (impl_->page_store && &omui::store() == impl_->page_store.get()) use_store(nullptr);
}

Runtime& Web::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Web::tree() noexcept { return *impl_->tree; }
NodeId Web::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }
const raster::Surface& Web::pixels() const noexcept { return impl_->frame; }
std::size_t Web::frames_shown() const noexcept { return impl_->frames_shown; }
bool Web::hidden() const noexcept { return impl_->is_hidden; }
void Web::on_visibility(std::function<void(bool)> fn) { impl_->visibility = std::move(fn); }

void Web::synthesize(std::span<const InputEvent> events) { impl_->pending.insert(impl_->pending.end(), events.begin(), events.end()); }

void Web::synthesize(std::span<const PageAction> actions) {
    for (const PageAction& a : actions)
        impl_->asked.push_back({a.kind == PageAction::Kind::focus ? page::Request::Kind::focus : page::Request::Kind::activate, a.node, {}});
}

void Web::request(std::string_view line, std::function<void(std::string_view)> reply) {
    impl_->asked.push_back({page::Request::Kind::agent, std::string(line), std::move(reply)});
}

Store& Web::store() noexcept { return omui::store(); }

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
