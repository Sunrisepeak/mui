// The `_openmui` extension: PyInit__openmui makes the runtime the Python
// program builds on (and the window shows, when it runs one), and hands the
// module lang/python makes over it.
module openmui.entry.python;

import std;
import openmui.hash;
import nlohmann.json;
import openmui;
import openmui.abi;
import openmui.lang;
import openmui.lang.python;
import openmui.ui.markdown;
import openmui.image;
import openmui.native.fuzzy;
import openmui.theme.default_theme;
import openmui.theme.compact;

namespace omui::entry::python {

namespace {

// Everything the module lives on, made once, when Python imports it, on the
// thread that imports it: that thread is the runtime's.
struct World {
    std::unique_ptr<typeset::FontCollection> fonts;
    std::unique_ptr<Runtime>                 runtime;
    abi::OmuiRuntime                         handle = nullptr;
    std::unique_ptr<lang::Host>              host;
    std::unique_ptr<lang::Session>           session;
    std::unique_ptr<typeset::GlyphCache>     glyphs;
    render::CpuRenderer                      renderer;
    raster::Surface                          surface;
    double                                   time = 0;
    std::atomic<Desktop*>                    desktop{nullptr};

    World() {
        register_base_components();
        register_markdown_component();
        if (auto r = natives::fuzzy::define(); !r) warn("the native module fuzzy: {}", r.error());
        for (auto add : {themes::add_default, themes::add_compact})
            if (auto r = add(); !r) warn("a theme package: {}", r.error()[0].text());
        fonts = std::make_unique<typeset::FontCollection>();
        if (auto r = fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        RuntimeOptions o;
        o.fonts = fonts.get();
        o.clock = hanim::Clock::virtual_time();  // a window switches it to the interactive one
        runtime = std::make_unique<Runtime>(std::move(o));
        handle = abi::attach(*runtime, "org.openmui.python");
        abi::set_wake(handle, [this] {
            if (Desktop* d = desktop.load()) d->wake();
        });
        host = std::make_unique<lang::Host>(handle);
        session = std::make_unique<lang::Session>(*host);
        glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
    }

    FrameOutput step() {
        FrameInput in;
        in.time = seconds(time += 1.0 / 60.0);
        return runtime->frame(in);
    }

    // Frames until the scripts have nothing left to do and the runtime is
    // still, as the headless host settles: nothing animates, and no timer is
    // due within the next ten seconds of the clock.
    void settle() {
        const double end = time + 10.0;
        int still = 0;
        for (int i = 0; i < 100000 && still < 3 && time < end; ++i) {
            const FrameOutput out = step();
            const bool timer_soon = out.next_timer && out.next_timer->count() <= end;
            const bool quiet = session->idle() && !out.animating && !timer_soon;
            still = quiet ? still + 1 : 0;
            if (!session->idle()) std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    // The last frame, drawn into `surface` as the headless host draws it.
    void render() {
        const float scale = runtime->scale();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int h = static_cast<int>(std::lround(runtime->size().height * scale));
        if (surface.width() != w || surface.height() != h) surface = raster::Surface(w, h);
        render::CpuOptions o;
        o.scale = scale;
        o.background = Color::white();
        const render::Resources resources{&runtime->images(), glyphs.get()};
        (void)renderer.render(runtime->display_list(), surface, resources, o);
    }

    std::string png(const std::string& file) {
        render();
        imageio::View v{surface.bytes().data(), surface.width(), surface.height(), surface.width() * 4, imageio::Alpha::premultiplied};
        auto bytes = imageio::encode_png(v);
        if (!bytes) return bytes.error();
        std::ofstream out(std::filesystem::path(std::u8string(file.begin(), file.end())), std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        return out ? std::string() : std::format("cannot write {}", file);
    }

    // FNV-1a over the frame's RGBA rows: the hash the examples' tests pin.
    std::string pixels() {
        render();
        return omui::hash::Fnv1a().bytes(surface.bytes()).hex();
    }

    int run(const lang::Json& o) {
        Size size{960, 640};
        if (o.contains("size") && o["size"].is_array() && o["size"].size() == 2)
            size = {o["size"][0].get<float>(), o["size"][1].get<float>()};
        if (o.value("headless", false)) {
            runtime->resize(size, 1.0f);
            settle();
            if (o.contains("frames"))
                for (int i = 0; i < o["frames"].get<int>(); ++i) (void)step();
            if (o.contains("png")) {
                const std::string error = png(o["png"].get<std::string>());
                if (!error.empty()) {
                    std::println(std::cerr, "openmui: {}", error);
                    return 1;
                }
            }
            return 0;
        }
        DesktopOptions d;
        d.title = o.value("title", std::string("openmui"));
        d.size = size;
        d.app = "org.openmui.python";
        d.runtime = runtime.get();
        Desktop window(d);
        if (auto r = window.open(); !r) {
            std::println(std::cerr, "openmui: no window ({}); run with --headless", r.error());
            return 1;
        }
        desktop.store(&window);
        const int code = window.run();
        desktop.store(nullptr);
        return code;
    }
};

// Made on import and never destroyed: at exit the interpreter is finalised
// first, and the script thread's Python objects must not be released after it.
World& world() {
    static World& w = *new World;
    return w;
}

}  // namespace

}  // namespace omui::entry::python

// The module's entry, as CPython looks it up (PyInit_<name>).
extern "C" omui::lang::python::capi::PyObject* PyInit__openmui() {
    using namespace omui::entry::python;
    World& w = world();
    return omui::lang::python::create_module(*w.session, {
        .run = [&w](const omui::lang::Json& o) { return w.run(o); },
        .step = [&w] { (void)w.step(); },
        .settle = [&w] { w.settle(); },
        .png = [&w](const std::string& file) { return w.png(file); },
        .pixels = [&w] { return w.pixels(); },
    });
}
