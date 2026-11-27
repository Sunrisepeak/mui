module omui.host.web;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import mrender.raster;
import mrender.vector;
import mrender.frame;
import mlibs.rhi;
import mrender.rhi.webgpu;
import mrender.rhi.webgl;
import mlibs.text;
import mlibs.trace;
import omui.agent;
import mlibs.image;
import omui.native;
import omui.native.store;
import mcpplibs.os;
import :platform;
import :mirror;
import mrender.text;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
using mlibs::log::warn;
}

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

// The files asked of the page (page_file) and not yet come: each asking's number, and who hears. The page's, as its
// events are: one page a program.
int files_asked = 0;
std::map<int, std::function<void(std::expected<std::string, std::string>)>> files_awaited;

}  // namespace

struct Web::Impl {
    WebOptions                               options;
    std::unique_ptr<mlibs::text::FontCollection> own_fonts;
    std::unique_ptr<Runtime>                 own_runtime;
    Runtime*                                 runtime = nullptr;
    std::unique_ptr<semantic::Tree>          tree;
    std::unique_ptr<mrender::text::GlyphCache>     glyphs;
    mrender::CpuFrame                         composed;  // the frame's entry (mrender.frame): the host composes nothing itself
    // The GPU, when one draws (WebOptions::renderer): its device (before the
    // renderer, which holds it), the renderer, the texture each frame is drawn in.
    std::unique_ptr<mlibs::rhi::Device>             device;
    std::optional<mrender::GpuFrame>          gpu;
    mlibs::rhi::Texture                             target{};
    int                                      target_w = 0, target_h = 0;
    ShownPictures                            shown_pictures;  // the viewports' pictures as textures on the device
    std::string_view                         drawn_by = "cpu";
    mrender::DamageTracker                    damage;
    mrender::raster::Surface                          frame;
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
    int                                      route_moves = -1;  // read the initial route too: it may move while wasm loads
    std::uint64_t                            route_shown = 0;   // the route revision the address shows
    bool                                     woken = false;  // the next paced step frames
    FrameDemand                              demand;
    std::chrono::steady_clock::time_point    last_frame{};
    Size                                     size;
    float                                    scale = 1.0f;
    // The page's other halves: the hidden DOM, window.omui, the store, the textarea.
    std::unique_ptr<DomMirror>               mirror;     // with a page
    // What answers the page's agents (window.omui, a linked agent, the page's WebMCP tools): the interface's
    // protocol and, by their operations' whole names, the runtime's other subjects — the process's session, as a
    // desktop endpoint's. Made at the first request.
    std::unique_ptr<agent::ProcessSession>   process;
    agent::Protocol*                         protocol = nullptr;  // the session's own
    std::unique_ptr<PageStore>               page_store; // with a page that has localStorage
    struct Asked {
        page::Request::Kind                    kind = page::Request::Kind::agent;
        std::string                            text;
        std::function<void(std::string_view)> reply;  // agent: where its records go
        int                                    x = 0, y = 0;  // scroll: the offset
    };
    std::vector<page::Request>               requests;   // this step's, from the page
    std::vector<Asked>                       asked;      // synthesized and request()ed, for the next step
    AgentLink                                link;
    std::function<void(const AgentLink&)>    link_heard;
    std::optional<Rect>                      caret;      // where the textarea is
    std::string                              caret_label;
    bool                                     caret_sent = false;

    // Joins the runtime to the page: its clipboard, the store, the mirror.
    // The agent link's state changed: the application's runtime and its listener hear it.
    void heard(AgentLink l) {
        link = std::move(l);
        link.guide = page::agent_guide();
        if (link_heard) link_heard(link);
        runtime->agent_link_changed(link);
    }
    std::string link_start() {
        if (!options.agent_link || !opened) return {};
        std::string code = page::link_start(options.app);
        if (!code.empty() && link.code != code) heard({"waiting", code, {}, {}, 0});
        return code;
    }

    void attach() {
        runtime->set_renderer(std::string(drawn_by));
        if (device) runtime->set_device(device.get());  // chosen before the runtime was: worlds drawn on it, shown as they are
        // The Web (performance tiers §1: T2), and the governor on where a page shows the frames to a person.
        runtime->set_device_tier(mlibs::pipeline::Tier::t2, "the Web");
        if (opened) runtime->set_governor(true);
        if (!opened) return;
        // The pictures of the page's own document, decoded by its browser, before any file's (D-284: a written
        // page is its own data — its program fetches no picture of its own).
        runtime->set_picture_reader([](std::string_view source) -> std::optional<std::expected<Picture, std::string>> {
            auto given = page::picture(source);
            if (!given) return std::nullopt;
            if (!*given) return std::unexpected(given->error());
            return Picture{(*given)->width, (*given)->height, std::move((*given)->rgba), false};
        });
        runtime->set_clipboard([] { return page::clipboard(); }, [](std::string_view t) { page::set_clipboard(t); });
        // A page has an address: a link without a scheme is one of its own site, followed as the browser follows it.
        runtime->set_url_opener([](std::string_view url) { return page::open_url(url); }, true);
        runtime->set_file_offer([](const Runtime::OfferedFile& file) { return page::offer_file(file.name, file.type, file.bytes); });
        runtime->set_svg_capture([this](Rect region) -> std::expected<std::string, ViewFailure> {
            const mrender::Resources resources{&runtime->images(), glyphs.get()};
            auto capture = agent::vector_capture(*runtime, resources, region, scale, std::nullopt);
            if (!capture) return std::unexpected(capture.error());
            return std::move(capture->svg);
        });
        // The person's own agent, for the application to offer (its actor log, a "copy the prompt").
        if (options.agent_link) {
            runtime->set_agent_linker({[this] { return link_start(); }, [] { page::link_stop(); }});
            heard({"idle", {}, {}, {}, 0});  // where its agent.md is, for the prompt
        }
        mirror = std::make_unique<DomMirror>(*runtime);
        if (page::storage()) {
            page_store = std::make_unique<PageStore>(options.app);
            use_store(page_store.get());
        }
        // Scripts reach the same store (openmui:native/store).
        if (!native::find("store")) (void)natives::store::define();
    }

    agent::ProcessSession& agents() {
        if (!process) {
            agent::ProtocolOptions o;
            o.app = options.app;
            o.default_actor = "agent:page";
            o.input = options.agent_input;
            o.remote = true;  // window.omui is the bus: openas decides, never the person (S-10, S-11)
            o.capture = [this](Rect region) -> std::expected<std::vector<std::uint8_t>, ViewFailure> {
                // The frame on the canvas, cut to the region (logical pixels).
                const mrender::raster::Surface& f = frame;
                const int x0 = std::clamp(static_cast<int>(std::floor(region.x * scale)), 0, f.width());
                const int y0 = std::clamp(static_cast<int>(std::floor(region.y * scale)), 0, f.height());
                const int x1 = std::clamp(static_cast<int>(std::ceil(region.right() * scale)), x0, f.width());
                const int y1 = std::clamp(static_cast<int>(std::ceil(region.bottom() * scale)), y0, f.height());
                if (x1 <= x0 || y1 <= y0) return std::unexpected(ViewFailure{ViewError::empty_region, "the region is outside the canvas (or nothing is shown yet)"});
                mlibs::image::View v{reinterpret_cast<const std::uint8_t*>(f.row(y0) + x0), x1 - x0, y1 - y0, f.width() * 4,
                                mlibs::image::Alpha::premultiplied};
                auto png = mlibs::image::encode_png(v);
                if (!png) return std::unexpected(ViewFailure{ViewError::capture_failed, std::move(png.error())});
                return std::move(*png);
            };
            o.capture_svg = [this](Rect region) {
                const mrender::Resources resources{&runtime->images(), glyphs.get()};
                return agent::vector_capture(*runtime, resources, region, scale, std::nullopt);
            };
            process = std::make_unique<agent::ProcessSession>(*runtime, std::move(o));
            protocol = &process->protocol();
        }
        return *process;
    }

    // A request of the page's, or one synthesized: served before the frame.
    void serve(page::Request::Kind kind, const std::string& text, const std::function<void(std::string_view)>& reply, int x = 0, int y = 0) {
        if (kind == page::Request::Kind::file) {
            // A file asked of the page (page_file) came, or did not: its asker hears, once.
            if (const auto awaited = files_awaited.find(x); awaited != files_awaited.end()) {
                const auto done = std::move(awaited->second);
                files_awaited.erase(awaited);
                if (y != 0) done(text);
                else done(std::unexpected(text));
            }
            return;
        }
        if (kind == page::Request::Kind::media) {
            // A setting of the person's changed while the page is open: the runtime has it from now on.
            if (x == 0) runtime->set_dark_preferred(y != 0);
            else if (x == 1) runtime->set_reduce_motion(options.reduce_motion || y != 0);
            else if (x == 2) runtime->set_reduce_transparency(options.reduce_transparency || y != 0);
            return;
        }
        mlibs::trace::Span span("request", "host");
        if (kind == page::Request::Kind::link) {  // the agent link's state, as the page says it
            auto v = parse_json(text);
            if (!v) return;
            auto str = [&](std::string_view k) { return std::string(v->get(k) ? v->get(k)->as_string() : ""); };
            constexpr Log link_log{"agent"};
            link_log.info("the agent link: {}{}{}", str("state"), str("agent").empty() ? "" : " with ", str("agent"));
            AgentLink link{str("state"), str("code"), str("agent"), str("error"), static_cast<int>(v->get("port") ? v->get("port")->as_int() : 0)};
            link.version = str("version");
            link.platform = str("platform");
            heard(std::move(link));
            return;
        }
        if (kind == page::Request::Kind::agent) {
            agents().request(text, reply);
            return;
        }
        NodeId n = runtime->find(text);
        if (!runtime->valid(n)) n = runtime->reveal(text);
        if (!runtime->valid(n)) return;
        if (kind == page::Request::Kind::focus) {
            runtime->focus_visibly(n);  // where assistive technology or the skip link moved it: seen
            return;
        }
        if (kind == page::Request::Kind::scroll) {
            // The page's element was scrolled (the browser's find brought a match into view, an anchor was followed):
            // the view is there at once — a property, not the glide `scroll_to` takes; the view keeps it within its
            // content when it is laid out.
            if (runtime->semantic_node(n).role != Role::scroll_view) return;
            runtime->set(n, "x", Value(static_cast<double>(x)));
            runtime->set(n, "y", Value(static_cast<double>(y)));
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
            mlibs::trace::Span span("mirror", "host");
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

    // The device the options ask for, as the page has one (in a page only:
    // before anything is shown, as a canvas keeps the first kind of context
    // it gives).
    void choose_renderer() {
        const Renderer want = options.renderer;
        if (!opened || want == Renderer::cpu) return;
        auto take = [&](Renderer kind) {
            // A software adapter counts as none for `automatic`: asked first
            // without the canvas, so that the canvas keeps no context of a
            // device that will not draw (and the CPU can still show its frames).
            if (want == Renderer::automatic) {
                auto probe = kind == Renderer::webgpu ? mrender::rhi::webgpu::create({}) : mrender::rhi::webgl::create({});
                if (!probe || (*probe)->caps().software) return false;
            }
            const std::string canvas = options.present ? options.canvas : std::string();
            auto made = kind == Renderer::webgpu ? mrender::rhi::webgpu::create({.canvas = canvas}) : mrender::rhi::webgl::create({.canvas = canvas});
            const std::string_view name = kind == Renderer::webgpu ? "WebGPU" : "WebGL2";
            if (!made) {
                if (want == kind) warn("host.web: {} was asked for and the page has none ({}): the CPU draws", name, made.error().message);
                return false;
            }
            auto renderer = mrender::GpuFrame::create(**made);
            if (!renderer) {
                warn("host.web: {} cannot draw ({}): the CPU does", name, renderer.error().message);
                return false;
            }
            device = std::move(*made);
            gpu.emplace(std::move(*renderer));
            drawn_by = kind == Renderer::webgpu ? "webgpu" : "webgl";
            return true;
        };
        if (want != Renderer::webgl && take(Renderer::webgpu)) return;
        if (want != Renderer::webgpu && !take(Renderer::webgl) && want == Renderer::gpu) warn("host.web: a GPU was asked for and the page has none: the CPU draws");
    }

    // The frame on the GPU: drawn whole into a texture the canvas's size,
    // presented. False when the GPU failed it (said once).
    bool show_gpu(const mrender::DisplayList& list, int w, int hgt) {
        if (target_w != w || target_h != hgt) {
            if (target.valid()) device->destroy(target);
            auto t = device->create(mlibs::rhi::TextureDesc{.format = mlibs::rhi::Format::rgba8_unorm, .width = static_cast<std::uint32_t>(w),
                                                     .height = static_cast<std::uint32_t>(hgt),
                                                     .usage = mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled, .label = "host.web frame"});
            if (!t) return gpu_failed(t.error().message);
            target = *t;
            target_w = w, target_h = hgt;
        }
        {
            mlibs::trace::Span span("render", "host");
            const mrender::Resources resources{&runtime->images(), glyphs.get()};
            const mrender::FrameLayer ui_layer{.name = "ui", .list = &list, .resources = resources, .scale = scale};
            mrender::GpuFrameOptions how;
            how.background = options.background;
            how.external = [this](std::uint64_t number) {  // a world in a viewport
                auto texture = shown_pictures.texture(*runtime, *device, number);
                if (!shown_pictures.failed().empty()) warn("host.web: {}", shown_pictures.failed());
                return texture;
            };
            if (auto r = gpu->render({&ui_layer, 1}, target, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(hgt), how); !r)
                return gpu_failed(r.error().message);
        }
        if (!options.present) return true;
        mlibs::trace::Span span("present", "host");
        page::backing(options.canvas, w, hgt);  // shown pixel for pixel, as the CPU's frame is
        if (auto r = device->present(target); !r) return gpu_failed(r.error().message);
        return true;
    }
    bool gpu_failed(std::string_view why) {
        if (!gpu_said) warn("host.web: the {} frame failed: {}", drawn_by, why);
        gpu_said = true;
        return false;
    }
    bool gpu_said = false;

    void show() {
        const mrender::DisplayList& list = runtime->display_list();
        const std::uint64_t h = list.hash();
        const int w = static_cast<int>(std::lround(runtime->size().width * scale));
        const int hgt = static_cast<int>(std::lround(runtime->size().height * scale));
        if (w <= 0 || hgt <= 0) return;
        if (gpu) {
            if (h == shown && target_w == w && target_h == hgt) return;
            if (show_gpu(list, w, hgt)) {
                shown = h;
                ++frames_shown;
            }
            return;
        }
        if (h == shown && frame.width() == w && frame.height() == hgt) return;
        if (frame.width() != w || frame.height() != hgt) frame = mrender::raster::Surface(w, hgt);
        {
            mlibs::trace::Span span("render", "host");
            const mrender::Resources resources{&runtime->images(), glyphs.get()};
            const auto rows = damage.update(list, w, hgt, scale, resources);
            const mrender::FrameLayer ui_layer{.name = "ui", .list = &list, .resources = resources, .scale = scale};
            mrender::FrameOptions how;
            how.background = options.background;
            how.rows = rows;
            how.external = [this](std::uint64_t number) { return runtime->picture(number); };  // a world in a viewport
            if (!rows.empty()) (void)composed.render({&ui_layer, 1}, frame, how);
        }
        mlibs::trace::Span span("present", "host");
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
    // Fonts the page fetched once the program was drawn (dist-toy's later
    // fonts, /later/fonts, each batch announced by /later/.ready — its
    // content counts the batches): tried as they come, every text shaped
    // again with them. Looked for twice a second. Faces the page holds
    // until asked (another language's) are asked for when text first has a
    // glyph no face covers, or a reader wants every face.
    std::string later_generation;
    std::set<std::filesystem::path> later_added;
    std::size_t later_looks = 0;
    std::uint64_t uncovered_seen = 0;
    std::set<char32_t> fonts_asked_for;
    std::uint64_t missed_seen = 0;
    std::set<std::pair<std::string, char32_t>> faces_asked_for;
    void want_deferred_fonts() {
        if (!opened) return;
        if (runtime->take_font_request()) page::want_fonts(std::nullopt);
        const std::uint64_t uncovered = runtime->fonts().uncovered();
        // A new character no face covers: the page fetches a face that could
        // draw it, if it deferred one (an emoji no face has asks nothing).
        if (uncovered > uncovered_seen)
            for (const char32_t cp : runtime->fonts().uncovered_points())
                if (fonts_asked_for.insert(cp).second) page::want_fonts(cp);
        uncovered_seen = uncovered;
        // A character the family asked for has no face for here: that family's full face, if the page holds it back.
        const std::uint64_t missed = runtime->fonts().missed();
        if (missed > missed_seen)
            for (const auto& miss : runtime->fonts().missed_points())
                if (faces_asked_for.insert({miss.family, miss.code_point}).second) page::want_face(miss.family, miss.code_point);
        missed_seen = missed;
    }
    void take_later_fonts() {
        if (!opened || ++later_looks % 30 != 0) return;
        std::error_code ec;
        if (!std::filesystem::exists("/later/.ready", ec)) return;
        std::string generation;
        if (std::ifstream in("/later/.ready", std::ios::binary); in) generation.assign(std::istreambuf_iterator<char>(in), {});
        if (!later_added.empty() && generation == later_generation) return;
        later_generation = generation;
        bool any = false;
        for (const auto& e : std::filesystem::directory_iterator("/later/fonts", ec))
            if (e.path().extension() == ".ttf" && later_added.insert(e.path().filename()).second) {
                auto& fonts = runtime->fonts();
                if (const auto added = fonts.add_file(e.path()); added) {
                    // Keep each family's original fallback priority whatever
                    // order the deferred files arrive in. Loading adds a new
                    // FaceId; old shaped runs and morphs retain their face.
                    const auto family = fonts.find_family(fonts.face(*added).family);
                    std::vector<mlibs::text::FaceId> chain(fonts.fallback().begin(), fonts.fallback().end());
                    // A previously absent family keeps add_fallback's old
                    // priority after the primary, now after that entire
                    // family block rather than between its tiny/full faces.
                    std::size_t after = 0;
                    if (!chain.empty()) {
                        const auto primary = fonts.find_family(fonts.face(chain.front()).family);
                        do { ++after; } while (after < chain.size() && fonts.find_family(fonts.face(chain[after]).family) == primary);
                    }
                    for (std::size_t i = 0; i < chain.size(); ++i)
                        if (fonts.find_family(fonts.face(chain[i]).family) == family) after = i + 1;
                    chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(after), *added);
                    fonts.set_fallback(std::move(chain));
                    any = true;
                } else warn("a later font: {}", added.error());
            }
        if (any) {
            uncovered_seen = runtime->fonts().uncovered();
            runtime->fonts_changed();
        }
    }

    bool step(bool paced) {
        events.clear();
        requests.clear();
        follow_canvas();
        take_later_fonts();
        want_deferred_fonts();
        if (opened && frames_shown == 0) page::reading();
        if (opened) page::take(events, requests);
        events.insert(events.end(), std::make_move_iterator(pending.begin()), std::make_move_iterator(pending.end()));
        pending.clear();
        // What the hidden DOM and window.omui asked, between frames.
        const bool asked_any = !requests.empty() || !asked.empty();
        for (const page::Request& r : requests) serve(r.kind, r.text, [](std::string_view line) { page::agent_reply(line); }, r.x, r.y);
        for (const Asked& a : std::exchange(asked, {})) serve(a.kind, a.text, a.reply, a.x, a.y);
        const auto agent_begin = events.size();
        std::optional<agent::AgentInput> given;
        if (protocol) {
            given = protocol->take_input();
            if (given) events.insert(events.end(), std::make_move_iterator(given->events.begin()), std::make_move_iterator(given->events.end()));
        }
        const auto agent_end = events.size();
        if (asked_any || given) woken = true;
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
        // The person moved the page's route (back, forward, a typed address): the application's, before the frame.
        if (opened)
            if (const int moves = page::route_moves(); moves != route_moves) {
                route_moves = moves;
                runtime->set_route(page::route());
                route_shown = runtime->route_revision();
            }
        FrameInput in;
        in.events = std::move(events);
        in.present_at = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60));
        in.display_hz = 60;
        last_frame = now;
        std::array<InputAttribution, 1> attribution;
        if (given && agent_end > agent_begin) {
            attribution[0] = {agent_begin, agent_end, given->actor, given->session};
            in.attribution = attribution;
        }
        FrameOutput out = runtime->frame(in);
        events = std::move(in.events);
        demand = out.demand;
        const std::size_t before = frames_shown;
        show();
        // A frame shown: when, and what it cost from taking the input to presenting it (D-59).
        if (frames_shown != before) {
            runtime->frame_shown({runtime->clock().now(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count()});
            // The first: the page may now fetch what comes after it (dist-toy's later files).
            if (before == 0 && opened) page::first_frame();
        }
        sync_page();
        // The application moved its route: a step of the browser's history (the address follows it).
        if (opened && runtime->route_revision() != route_shown) {
            route_shown = runtime->route_revision();
            page::set_route(runtime->route());
        }
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
    d.choose_renderer();
    if (d.options.runtime) {
        d.runtime = d.options.runtime;
        d.runtime->clock() = hanim::Clock::interactive();
        d.runtime->resize(d.size, d.scale);
        d.tree = std::make_unique<semantic::Tree>(*d.runtime);
        d.glyphs = std::make_unique<mrender::text::GlyphCache>(d.runtime->fonts());
        d.attach();
        return;
    }
    mlibs::text::FontCollection* fonts = d.options.fonts;
    if (!fonts) {
        d.own_fonts = std::make_unique<mlibs::text::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        fonts = d.own_fonts.get();
    }
    RuntimeOptions ro;
    ro.theme = d.options.theme;
    ro.fonts = fonts;
    ro.clock = hanim::Clock::interactive();
    ro.size = d.size;
    ro.scale = d.scale;
    // The person's settings as the page has them now (what changes later: serve's media).
    ro.reduce_motion = d.options.reduce_motion || (d.opened && page::matches("(prefers-reduced-motion: reduce)"));
    ro.reduce_transparency = d.options.reduce_transparency || (d.opened && page::matches("(prefers-reduced-transparency: reduce)"));
    ro.dark_preferred = d.opened && page::matches("(prefers-color-scheme: dark)");
    d.own_runtime = std::make_unique<Runtime>(std::move(ro));
    d.runtime = d.own_runtime.get();
    d.tree = std::make_unique<semantic::Tree>(*d.runtime);
    d.glyphs = std::make_unique<mrender::text::GlyphCache>(*fonts);
    d.attach();
}

Web::~Web() {
    if (impl_->page_store && &omui::store() == impl_->page_store.get()) use_store(nullptr);
    // The components drawn on the device (a world's targets) before the device: what reads the runtime, then an
    // own runtime; a borrowed one is told there is no device any more.
    Impl& d = *impl_;
    d.protocol = nullptr;
    d.process.reset();
    d.mirror.reset();
    if (d.device) d.shown_pictures.release(*d.device);
    if (d.own_runtime) {
        d.tree.reset();
        d.runtime = nullptr;
        d.own_runtime.reset();
    } else if (d.runtime) {
        d.runtime->set_device(nullptr);
    }
}

Runtime& Web::runtime() noexcept { return *impl_->runtime; }
semantic::Tree& Web::tree() noexcept { return *impl_->tree; }
NodeId Web::mount(std::string_view slot, View view) { return impl_->runtime->mount(slot, std::move(view)); }
const mrender::raster::Surface& Web::pixels() const noexcept { return impl_->frame; }
std::string_view Web::renderer() const noexcept { return impl_->drawn_by; }

std::expected<mrender::raster::Surface, ViewFailure> Web::read_frame() {
    Impl& d = *impl_;
    if (!d.gpu) return d.frame;
    if (!d.target.valid()) return std::unexpected(ViewFailure{ViewError::nothing_shown});
    const auto w = static_cast<std::uint32_t>(d.target_w), h = static_cast<std::uint32_t>(d.target_h);
    auto bytes = d.device->read(d.target, mlibs::rhi::Region{0, 0, w, h});
    if (!bytes) return std::unexpected(ViewFailure{ViewError::capture_failed, bytes.error().message});
    mrender::raster::Surface s(d.target_w, d.target_h);
    for (int y = 0; y < d.target_h; ++y)
        std::memcpy(s.row(y), bytes->data() + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4);
    return s;
}
std::size_t Web::frames_shown() const noexcept { return impl_->frames_shown; }
bool Web::hidden() const noexcept { return impl_->is_hidden; }
void Web::on_visibility(std::function<void(bool)> fn) { impl_->visibility = std::move(fn); }

void Web::synthesize(std::span<const InputEvent> events) { impl_->pending.insert(impl_->pending.end(), events.begin(), events.end()); }

void Web::synthesize(std::span<const PageAction> actions) {
    for (const PageAction& a : actions)
        impl_->asked.push_back({a.kind == PageAction::Kind::focus    ? page::Request::Kind::focus
                                : a.kind == PageAction::Kind::scroll ? page::Request::Kind::scroll
                                : a.kind == PageAction::Kind::media  ? page::Request::Kind::media
                                                                     : page::Request::Kind::activate,
                                a.node, {}, a.x, a.y});
}

void Web::request(std::string_view line, std::function<void(std::string_view)> reply) {
    impl_->asked.push_back({page::Request::Kind::agent, std::string(line), std::move(reply)});
}

Store& Web::store() noexcept { return omui::store(); }

std::string Web::link_agent() { return impl_->link_start(); }

void Web::unlink_agent() {
    if (impl_->opened) page::link_stop();
}

const AgentLink& Web::agent_link() const noexcept { return impl_->link; }

void Web::on_agent_link(std::function<void(const AgentLink&)> fn) { impl_->link_heard = std::move(fn); }

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

std::string page_query(std::string_view name) { return page::exists() ? page::query(name) : std::string(); }

bool page_prefers(std::string_view media) { return page::exists() && page::matches(media); }

bool reload_page_with(std::string_view name, std::string_view value) { return page::exists() && page::reload_with(name, value); }

std::vector<std::string> browser_locales() { return page::exists() ? page::locales() : std::vector<std::string>{}; }

std::string page_projection() { return page::exists() ? page::projection() : std::string(); }

std::string page_projected(std::string_view id) { return page::exists() ? page::projected(id) : std::string(); }

std::string page_link(std::string_view relation) { return page::exists() ? page::link(relation) : std::string(); }

void page_file(std::string_view address, std::function<void(std::expected<std::string, std::string>)> done) {
    if (!done) return;
    if (!page::exists()) return done(std::unexpected(std::string("no page to fetch a file through")));
    const int number = ++files_asked;
    files_awaited.emplace(number, std::move(done));
    page::fetch(address, number);
}

}  // namespace omui
