// omui on Android (openmui.host.android): the loop — what the activity hands
// over becomes the runtime's input and size, each frame drawn on the CPU and
// shown in the window.
module omui.host.android;

import std;
import omui.core;
import omui.view;
import xrender.raster;
import xrender.vector;
import xrender.frame;
import xrender.text;
import mlibs.text;
import :native;

namespace omui {

namespace native = android::native;

namespace {

// Android's key codes the interface reads (android/keycodes.h).
std::optional<Key> key_of(std::int32_t code) {
    if (code >= 29 && code <= 54) return static_cast<Key>(static_cast<int>(Key::a) + (code - 29));          // A … Z
    if (code >= 7 && code <= 16) return static_cast<Key>(static_cast<int>(Key::digit0) + (code - 7));       // 0 … 9
    switch (code) {
        case 4: return Key::escape;       // Back
        case 111: return Key::escape;
        case 66: return Key::enter;
        case 67: return Key::backspace;   // DEL is Backspace
        case 112: return Key::delete_;    // FORWARD_DEL
        case 61: return Key::tab;
        case 62: return Key::space;
        case 19: return Key::up;
        case 20: return Key::down;
        case 21: return Key::left;
        case 22: return Key::right;
        case 122: return Key::home;
        case 123: return Key::end;
        case 92: return Key::page_up;
        case 93: return Key::page_down;
        case 82: return Key::menu;
        default: return std::nullopt;
    }
}

Modifiers modifiers_of(std::uint32_t meta) {
    Modifiers out;
    if (meta & 0x1) out = out | Modifier::shift;    // META_SHIFT_ON
    if (meta & 0x2) out = out | Modifier::alt;      // META_ALT_ON
    if (meta & 0x1000) out = out | Modifier::ctrl;  // META_CTRL_ON
    if (meta & 0x10000) out = out | Modifier::meta; // META_META_ON
    return out;
}

}  // namespace

struct AndroidHost::Impl {
    AndroidOptions                               options;
    std::unique_ptr<mlibs::text::FontCollection> own_fonts;
    std::unique_ptr<Runtime>                     runtime;
    std::unique_ptr<xrender::text::GlyphCache>   glyphs;
    xrender::CpuFrame                            composed;  // the frame's entry (xrender.frame)
    xrender::raster::Surface                     frame;
    std::uint64_t                                shown = 0;
    std::size_t                                  frames_shown = 0;
    bool                                         has_window = false, resumed = true;

    void fit() {
        // The window in logical pixels: the display's density is the scale.
        const native::Display d = native::display();
        if (d.width <= 0 || d.height <= 0) return;
        const float scale = d.density > 0 ? d.density : 1.0f;
        runtime->resize({static_cast<float>(d.width) / scale, static_cast<float>(d.height) / scale}, scale);
    }

    void show() {
        const xrender::DisplayList& list = runtime->display_list();
        const float scale = runtime->scale();
        const int width = static_cast<int>(std::lround(runtime->size().width * scale));
        const int height = static_cast<int>(std::lround(runtime->size().height * scale));
        if (width <= 0 || height <= 0) return;
        if (list.hash() == shown && frame.width() == width && frame.height() == height) return;
        if (frame.width() != width || frame.height() != height) frame = xrender::raster::Surface(width, height);
        const xrender::FrameLayer ui_layer{.name = "ui", .list = &list, .resources = {&runtime->images(), glyphs.get()}, .scale = scale};
        (void)composed.render({&ui_layer, 1}, frame, {.background = options.background});
        const auto* first = reinterpret_cast<const std::uint8_t*>(frame.row(0));
        const std::size_t stride = height > 1 ? static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(frame.row(1)) - first)
                                              : static_cast<std::size_t>(width) * 4;
        if (native::present(first, width, height, stride)) {
            shown = list.hash();
            ++frames_shown;
        }
    }
};

AndroidHost::AndroidHost(AndroidOptions options) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.options = std::move(options);
    // The platform's conventions: Android's (the context menu by a touch held, no stretch at a scroll's edge).
    set_platform(Platform::android);
    mlibs::text::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        // The default fonts travel in the APK (assets/fonts, mlibs.text's deploy): unpacked once into the
        // application's own directory, read from there.
        d.own_fonts = std::make_unique<mlibs::text::FontCollection>();
        const std::filesystem::path unpacked = native::started_by_activity() ? native::unpack_assets("fonts") : std::filesystem::path();
        if (auto loaded = unpacked.empty() ? d.own_fonts->add_defaults() : d.own_fonts->add_defaults(unpacked); !loaded)
            native::log(std::format("{}: the default fonts: {}", d.options.app, loaded.error()));
        fonts = d.own_fonts.get();
    }
    d.glyphs = std::make_unique<xrender::text::GlyphCache>(*fonts);
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = fonts;
    ro.clock = hanim::Clock::interactive();
    ro.reduce_motion = d.options.reduce_motion;
    d.runtime = std::make_unique<Runtime>(ro);
}

AndroidHost::~AndroidHost() = default;

Runtime& AndroidHost::runtime() noexcept { return *impl_->runtime; }
NodeId AndroidHost::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }
bool AndroidHost::in_activity() { return native::started_by_activity(); }
std::size_t AndroidHost::frames_shown() const noexcept { return impl_->frames_shown; }

int AndroidHost::run() {
    Impl& d = *impl_;
    if (!native::started_by_activity()) {
        native::log(std::format("{}: no activity started this program: on Android an application is opened by the platform (mcpp run --pkg-format apk)",
                                d.options.app));
        return 1;
    }
    native::log(std::format("{}: running", d.options.app));
    std::optional<seconds> next_timer;
    bool animating = true;  // the first frame
    for (;;) {
        // Frames while something moves; else asleep until something happens, or the next timer.
        std::chrono::milliseconds wait{-1};
        if (d.has_window && d.resumed) {
            if (animating) wait = std::chrono::milliseconds(16);
            else if (next_timer) wait = std::chrono::milliseconds(std::max<std::int64_t>(0, static_cast<std::int64_t>((*next_timer - d.runtime->now()).count() * 1000)));
        }
        FrameInput in;
        bool ended = false;
        bool back = false;  // Back pressed: what is open closes; with nothing open, the activity ends
        for (const native::Event& e : native::next(wait)) {
            const float scale = d.runtime->scale();
            const Point at{e.x / scale, e.y / scale};
            switch (e.kind) {
                case native::Event::Kind::window:
                    d.has_window = true;
                    d.shown = 0;
                    d.fit();
                    break;
                case native::Event::Kind::no_window: d.has_window = false; break;
                case native::Event::Kind::resized:
                    d.fit();
                    d.shown = 0;
                    break;
                case native::Event::Kind::paused: d.resumed = false; break;
                case native::Event::Kind::resumed:
                    d.resumed = true;
                    d.shown = 0;
                    break;
                case native::Event::Kind::destroyed: ended = true; break;
                case native::Event::Kind::focus: in.events.emplace_back(FocusEvent{.gained = e.pointer != 0}); break;
                case native::Event::Kind::touch_down:
                case native::Event::Kind::touch_up:
                case native::Event::Kind::touch_move:
                case native::Event::Kind::touch_cancel: {
                    PointerEvent p;
                    p.kind = e.kind == native::Event::Kind::touch_down ? PointerKind::down
                             : e.kind == native::Event::Kind::touch_up ? PointerKind::up
                             : e.kind == native::Event::Kind::touch_move ? PointerKind::move
                                                                          : PointerKind::cancel;
                    p.position = at;
                    p.pointer = static_cast<std::uint32_t>(e.pointer);
                    p.type = PointerType::touch;
                    p.button = MouseButton::left;
                    p.buttons = p.kind == PointerKind::up || p.kind == PointerKind::cancel ? 0 : 1;
                    in.events.emplace_back(p);
                    break;
                }
                case native::Event::Kind::key_down:
                case native::Event::Kind::key_up:
                    if (e.key == 4 && e.kind == native::Event::Kind::key_down) back = true;
                    if (const auto key = key_of(e.key))
                        in.events.emplace_back(KeyEvent{.kind = e.kind == native::Event::Kind::key_down ? KeyKind::down : KeyKind::up, .key = *key,
                                                        .mods = modifiers_of(e.meta)});
                    break;
            }
        }
        if (ended) break;
        if (!d.has_window || !d.resumed) continue;
        const std::uint64_t before = d.runtime->semantic_revision();
        const FrameOutput out = d.runtime->frame(in);
        if (back && out.revision == before) {
            // Escape closed nothing: the person leaves, as Back does everywhere on Android.
            native::finish();
        }
        animating = out.animating;
        next_timer = out.next_timer;
        d.show();
    }
    native::log(std::format("{}: ended after {} frames", d.options.app, d.frames_shown));
    return 0;
}

}  // namespace omui
