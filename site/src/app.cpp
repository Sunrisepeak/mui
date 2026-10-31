// The site: a bar with the lens and the language, the places down the side,
// the place in the middle (a document, or the live timeline editor) and over
// it, as the lens says, the tree's wireframe, the JSON an agent reads or the
// assertions a test writes; at the foot, the tree's hash.
module openmui.site;

import std;
import openmui;
import openmui.hash;
import openmui.ui.markdown;
import openmui.ui.timeline;
import openmui.ui.canvas;
import openmui.showcase.markdown_wall;
import openmui.showcase.style_gallery;
import openmui.theme;
import openmui.theme.styles;
import openmui.showcase.fighting;
import hello;
import openmui.semantic;
import openmui.trace;

namespace omui::site {

namespace {

constexpr Log site_log{"site"};

// Where the places are in the tree (the window mounts the site as "main";
// the places are the atlas canvas's live windows).
constexpr std::string_view atlas_path = "main/body/view/atlas";
constexpr std::string_view place_root = "main/body/view/atlas/";

constexpr std::array<Place, 8> all{{{"home", "Home", "首页"},
                                     {"features", "Features", "特性"},
                                     {"practices", "Practices", "实践"},
                                     {"timeline", "Timeline", "时间线"},
                                     {"wall", "Document wall", "文档墙"},
                                     {"styles", "Style workshop", "风格工坊"},
                                     {"hello", "Example 01", "示例 01"},
                                     {"moves", "Move list", "出招表"}}};

std::string_view embedded(std::span<const std::pair<std::string_view, std::string_view>> table, std::string_view file) {
    for (const auto& [name, text] : table)
        if (name == file) return text;
    return {};
}

constexpr std::string_view sample = R"([timeline]
name = "a short"
rate = 30

[[marker]]
at = "4s"
name = "the beat"

[[track]]
name = "picture"
kind = "video"

[[track.clip]]
name = "title"
at = "0f"
length = "90f"
fade = { in = "15f", out = "15f" }

[[track.clip.property]]
name = "y"
keys = [["0f", 40, "out_cubic"], ["20f", 0]]

[[track.clip]]
name = "the fight"
at = "120f"
length = "150f"

[[track]]
name = "music"
kind = "audio"

[[track.clip]]
name = "main theme"
at = "0f"
length = "270f"
fade = { out = "30f" }
)";

// What the site keeps between its setup and its view.
// What the far-away pictures of the live works show: their own structure,
// taken once on the UI thread and never written again (textures are drawn on
// other threads) — the wall's workspaces and windows as its blocks, the
// workshop's packs in their own colours.
struct Thumbs {
    struct Block {
        world::WorldRect r;
        Color            fill;
        std::string      title;
        bool             group = false;
    };
    std::vector<Block> wall;
    world::WorldRect   wall_bounds{};
    struct Pack {
        std::string name, doc;
        Color       background, surface, accent, text;
    };
    std::vector<Pack> packs;
};

// The wall's blocks drawn into a box (each workspace and window, its title
// where it fits), as the wall itself draws them from far away.
Element wall_picture(std::shared_ptr<const Thumbs> t) {
    return painter({.id = "picture", .width = Dim(layout::pct(100)), .grow = 1, .role = Role::image, .label = "The wall from far away",
                    .paint = [t](PaintContext& c) {
                        const auto& b = t->wall_bounds;
                        if (b.width <= 0 || b.height <= 0) return;
                        const float k = std::min(c.size.width / static_cast<float>(b.width), c.size.height / static_cast<float>(b.height)) * 0.94f;
                        const float ox = (c.size.width - static_cast<float>(b.width) * k) / 2, oy = (c.size.height - static_cast<float>(b.height) * k) / 2;
                        auto at = [&](const world::WorldRect& r) {
                            return Rect{ox + static_cast<float>(r.x - b.x) * k, oy + static_cast<float>(r.y - b.y) * k, static_cast<float>(r.width) * k,
                                        static_cast<float>(r.height) * k};
                        };
                        for (const auto& x : t->wall) {
                            const Rect r = at(x.r);
                            if (x.group) {
                                c.list.fill_round_rect(r, CornerRadii::all(6), render::Paint::solid(x.fill.with_alpha(0.12f)));
                                c.list.stroke_round_rect(r, CornerRadii::all(6), 1, render::Paint::solid(x.fill.with_alpha(0.45f)));
                            } else {
                                c.list.fill_round_rect(r, CornerRadii::all(2), render::Paint::solid(c.theme.color("surface")));
                                c.list.fill_rect({r.x, r.y, r.width, std::max(1.5f, r.height * 0.08f)}, x.fill);
                            }
                        }
                    }});
}

// The five packs side by side, each a small panel in its own colours.
Element packs_picture(std::shared_ptr<const Thumbs> t) {
    std::vector<Element> tiles;
    for (const auto& p : t->packs)
        tiles.push_back(column({.min_width = Dim(0.0f), .padding = Edges(14.0f), .gap = 8, .grow = 1, .background = Fill(p.background), .radius = 10},
                               {text({.text = p.name, .style = "heading", .color = Fill(p.text)}),
                                box({.width = Dim(layout::pct(60)), .height = Dim(6.0f), .background = Fill(p.accent), .radius = 3}),
                                column({.padding = Edges(10.0f), .gap = 6, .background = Fill(p.surface), .radius = 8},
                                       {box({.width = Dim(layout::pct(80)), .height = Dim(8.0f), .background = Fill(p.text.with_alpha(0.5f)), .radius = 4}),
                                        box({.width = Dim(layout::pct(55)), .height = Dim(8.0f), .background = Fill(p.text.with_alpha(0.3f)), .radius = 4}),
                                        box({.width = Dim(70.0f), .height = Dim(18.0f), .background = Fill(p.accent), .radius = 6})}),
                                text({.text = p.doc, .style = "caption", .color = Fill(p.text.with_alpha(0.7f))})}));
    return row({.id = "picture", .width = Dim(layout::pct(100)), .gap = 10, .align = "stretch", .grow = 1}, std::move(tiles));
}

struct Site {
    Runtime*            runtime = nullptr;
    Signal<std::string> place = signal(std::string("home")), lang = signal(std::string("en")), lens = signal(std::string("pixels"));
    Signal<Value>       nodes = signal(Value{});
    Signal<std::string> agent = signal(std::string()), test = signal(std::string()), hash = signal(std::string());

    Signal<bool>        panel = signal(false);  // the agent panel open
    std::shared_ptr<const Thumbs> thumbs = std::make_shared<Thumbs>();  // the far-away pictures' (set up once)
    // How the page draws, as the person chose it (the drawing panel): the
    // places live only at rest, always, or never (the atlas's `promotion`),
    // and who draws (choose_renderer: on the Web the page loads again).
    Signal<bool>        drawing = signal(false);
    // A narrow window (a phone): the places take the whole width, the
    // navigation and the bar's switches are in a menu, panels in its place.
    static constexpr float compact_below = 720;
    Signal<bool>        compact = signal(false), menu = signal(false);

    Signal<std::string> promotion = signal(std::string("at_rest")), renderer_choice = signal(std::string("automatic")),
                        drawing_note = signal(std::string());
    Signal<int>         switches = signal(0);   // the language switched this many times
    double              switched_at = -1;       // when it last did (the runtime's seconds)
    Signal<std::string> timeline = signal(std::string(sample));  // the timeline as last edited
    hello::State        hello;                 // example 01's, as the person left it
    std::unique_ptr<wall::Model> wall;         // S1's wall of the site's documents
    styles::Studio      studio;                // S2's workshop: its packs in a theme scope, the site in its own theme
    Hero                hero;                  // the home page's opening: its checklist, the node its panels show
    HeroSite            hero_site;             // what the opening reads of the site
    // The reference run (the foot's check against CI): played here, on a
    // headless runtime of its own, when asked; the frame shown, its hash as
    // this machine drew it, and CI's.
    Signal<bool>        checking = signal(false);
    Signal<double>      ref_frame = signal(0.0);
    Signal<std::string> ref_mine = signal(std::string()), ref_ci = signal(std::string());
    Signal<int>         ref_verdict = signal(0), ref_shown = signal(0);  // verdict: 1 the same, −1 not, 0 none pinned
    std::unique_ptr<ReferenceReplay> replay;
    render::ImageRef    ref_image{};
    // A frame asked for (a handler's request, played by the frame stage
    // after paint: driving another runtime inside a handler would run in the
    // page's batch, its own effects held back until the batch ends).
    std::optional<std::int32_t> ref_wanted;
    void want_frame(std::int32_t f) {
        ref_wanted = f;
        if (runtime) runtime->request_frame();
    }
    void check_frame(std::int32_t f) {
        if (!runtime) return;
        trace::Span span("reference frame", "site");
        if (!replay) replay = std::make_unique<ReferenceReplay>();
        const std::uint64_t mine = replay->hash(f);
        if (ref_image.id != 0) runtime->images().remove(ref_image);
        ref_image = runtime->images().add(std::make_shared<const raster::Surface>(replay->pixels()));
        const auto pinned = reference_pinned();
        ref_mine.set(std::format("{:016x}", mine));
        ref_ci.set(static_cast<std::size_t>(f) < pinned.size() ? std::format("{:016x}", pinned[static_cast<std::size_t>(f)]) : std::string());
        ref_verdict.set(static_cast<std::size_t>(f) < pinned.size() ? (pinned[static_cast<std::size_t>(f)] == mine ? 1 : -1) : 0);
        ref_frame.set(static_cast<double>(f));
        ref_shown.set(ref_shown.peek() + 1);
    }
    // The fighting game's move list: the fighter, the move shown, how the
    // dummy meets it (standing or guarding); each fighter's moves measured once.
    Signal<std::string> fighter = signal(std::string("mo")), move = signal(std::string("5LP"));
    Signal<bool>        guard = signal(false);
    Signal<double>      demo_at = signal(0.0);
    std::map<std::string, std::vector<gongfu::training::FrameData>, std::less<>> measured;
    [[nodiscard]] const std::vector<gongfu::training::FrameData>& moves_of(const std::string& name) {
        auto it = measured.find(name);
        if (it == measured.end()) {
            trace::Span span("frame data", "site");
            const fighting::Roster& r = fighting::roster();
            const gongfu::Fighter& f = r.fighters[fighting::fighter_index(name)];
            std::vector<gongfu::training::FrameData> rows;
            for (std::size_t m = 0; m < f.def.moves.size(); ++m) rows.push_back(gongfu::training::frame_data(r.system, f, static_cast<std::int32_t>(m)));
            it = measured.emplace(name, std::move(rows)).first;
        }
        return it->second;
    }
    // The move list's stage, told to show its demonstration at a frame (paused) or to play.
    void stage_act(std::string_view action, Value args = {}) {
        if (!runtime) return;
        if (const NodeId n = runtime->find(std::string(place_root) + "moves/page/show/frame_box/stage")) {
            Invocation call;
            call.args = std::move(args);
            (void)runtime->act(n, action, call);
        }
    }
    std::unique_ptr<Guide> guide;

    // The atlas: its scene (by a name of this site's own) and its layout.
    std::string         scene_name;
    AtlasLayout         layout;
    std::function<void()> kinds;  // registers the atlas's kinds again (their tiers follow the layout)

    Site() = default;
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;
    // What the pool still draws of the windows (and of the wall's) is done
    // before what it reads goes; then the atlas's scene goes too.
    ~Site() {
        Tasks::shared().wait_idle();
        wall.reset();
        if (!scene_name.empty()) world::drop_scene(scene_name);
    }

    [[nodiscard]] Canvas* atlas() const { return runtime ? runtime->component<Canvas>(runtime->find(atlas_path)) : nullptr; }
    [[nodiscard]] world::Scene& scene() const { return world::scene(scene_name); }
    // Where the tree is read: the place's window while it is live, else the whole atlas.
    [[nodiscard]] std::string root() const {
        const std::string place_path = std::string(place_root) + place.get() + "/page";
        return runtime && runtime->find(place_path) ? place_path : std::string(atlas_path);
    }
    // The camera flies to a place (or the overview), as the person's own request of the canvas.
    void fly(std::string_view to) {
        if (to != place.peek()) {
            const auto it = std::ranges::find_if(all, [&](const Place& p) { return p.id == to; });
            const bool zh = lang.peek() == "zh";
            mark(it != all.end() ? std::string(zh ? it->zh : it->en) : std::string(zh ? "全景" : "Overview"));
        }
        place.set(std::string(to));
        if (const auto c = runtime ? runtime->find(atlas_path) : NodeId{}) {
            Invocation call;
            call.args = Value{}.set("name", Value(std::string(to)));
            (void)runtime->act(c, "camera_preset", call);
        }
        soon();
    }
    // Windows the size of the view (a place read at zoom 1); laid out again when the view's size changes.
    void relayout(Size view) {
        layout = atlas_layout(all, view);
        lay_out(scene(), layout, all, lang.get());
        if (kinds) kinds();
        if (Canvas* c = atlas())
            if (const auto p = scene().preset(place.get())) c->set_camera({p->x, p->y, p->zoom});
    }

    // The page's time (the timebar, D-60): live, it is the host's; dragged,
    // paused, played or played backwards, the runtime's time control. What
    // happened is marked where it did (the channels keep 30 s of history).
    Signal<Value>       marks = signal(Value{});
    Signal<double>      time_start = signal(0.0), time_end = signal(0.0), playhead = signal(0.0);
    Signal<std::string> time_mode = signal(std::string("live"));  // live, paused, playing, backwards
    double              host_last = 0;  // the host's time at the last frame (playing backwards)
    static constexpr double time_window = 20;

    [[nodiscard]] double page_time() const { return runtime ? runtime->now().count() : 0; }
    // The strip ends at `end`, and reaches back `time_window` (never before the page began).
    void strip_to(double end) {
        time_end.set(end);
        time_start.set(std::max(0.0, end - time_window));
    }
    void mark(std::string label) {
        if (!runtime) return;
        const double at = page_time();
        Value::List kept;
        if (const Value::List* old = marks.peek().list())
            for (const Value& m : *old)
                if (m.get("at") && m.get("at")->as_double(0) > at - 30) kept.push_back(m);
        kept.push_back(Value{}.set("at", at).set("label", std::move(label)));
        marks.set(Value(std::move(kept)));
        if (time_mode.peek() == "live") {
            strip_to(at);
            playhead.set(at);
        }
    }
    // The playhead dragged (or set by an agent): the page shown as it was then.
    void scrub(double t) {
        if (!runtime) return;
        if (time_mode.peek() == "live") strip_to(page_time());
        runtime->pause_time(true);
        runtime->seek_time(seconds(t));
        playhead.set(t);
        time_mode.set("paused");
    }
    void pause() {
        if (!runtime || time_mode.peek() == "live") return;
        runtime->pause_time(true);
        playhead.set(page_time());
        time_mode.set("paused");
    }
    // From the playhead on, at its own pace; at the strip's end, live again.
    void play() {
        if (!runtime || time_mode.peek() == "live") return;
        runtime->set_time_rate(1);
        runtime->pause_time(false);
        time_mode.set("playing");
    }
    // The page's time run backwards: the frame stage seeks a little earlier each frame.
    void backwards() {
        if (!runtime) return;
        if (time_mode.peek() == "live") {
            strip_to(page_time());
            playhead.set(page_time());
        }
        runtime->pause_time(true);
        host_last = runtime->time_state().host.count();
        time_mode.set("backwards");
        runtime->request_frame();
    }
    void live() {
        if (!runtime) return;
        runtime->release_time();
        strip_to(page_time());
        playhead.set(page_time());
        time_mode.set("live");
    }
    // Each frame (the tree-watch stage): the playhead follows the time being played.
    void tick_time() {
        if (!runtime) return;
        const std::string mode = time_mode.peek();
        if (mode == "backwards") {
            const double host = runtime->time_state().host.count();
            const double t = std::max(time_start.peek(), playhead.peek() - std::max(0.0, host - host_last));
            host_last = host;
            runtime->seek_time(seconds(t));
            playhead.set(t);
            if (t <= time_start.peek()) time_mode.set("paused");
            else runtime->request_frame();
        } else if (mode == "playing") {
            const double t = page_time();
            if (t >= time_end.peek()) live();
            else {
                playhead.set(t);
                runtime->request_frame();
            }
        } else if (mode == "live") {
            // The strip runs on a second at a time, on frames there are anyway:
            // what it draws is a function of its properties, the same whenever
            // something else has it drawn again (a texture arriving, a hash),
            // and an idle page stays idle.
            const double second = std::floor(page_time());
            if (second > time_end.peek()) strip_to(second);
        }
    }

    std::uint64_t       seen = ~0ull;    // the semantic revision last read
    bool                watching = false;

    // The place's tree read again, when it may have changed.
    void look() {
        if (!runtime || runtime->semantic_revision() == seen) return;
        seen = runtime->semantic_revision();
        trace::Span span("look", "site");
        semantic::Tree tree(*runtime);
        const Value snapshot = tree.snapshot({.root = root()}).to_value();
        nodes.set(snapshot.get("nodes") ? *snapshot.get("nodes") : Value{});
        agent.set(agent_text(snapshot));
        test.set(test_text(snapshot));
        const std::uint64_t h = tree_hash(snapshot);
        const bool pinned = place.get() == "home" && lang.get() == "en" && pinned_home_hash != 0;
        // Eight digits of each (they are compared whole).
        hash.set(pinned ? std::format("tree {:08x} · CI {:08x} {}", h >> 32, pinned_home_hash >> 32, h == pinned_home_hash ? "✓ the same" : "✗ not the pinned tree")
                        : std::format("tree {:08x}", h >> 32));
    }
    // After the frame that shows what changed: once, and then, while a lens
    // shows the tree, a few times a second (the page moves under it: a
    // scroll, a resize, an edit).
    void soon() {
        if (runtime) (void)runtime->after(seconds(0.02), [this] { look(); });
        if (lens.get() != "pixels" && !watching && runtime) {
            watching = true;
            (void)runtime->after(seconds(0.25), [this] { watch(); });
        }
    }
    void watch() {
        look();
        watching = lens.get() != "pixels";
        if (watching) (void)runtime->after(seconds(0.25), [this] { watch(); });
    }
};

// An element in the layout only while `cond` holds: out of it (and hidden
// from the tree) otherwise, its state kept.
Element only_when(Element e, std::function<bool()> cond) {
    e.prop("display", derive([cond = std::move(cond)] { return std::string(cond() ? "flex" : "none"); }));
    return e;
}

// The same, fading in over the page when it comes (a lens's rendering
// arrives over the pixels rather than replacing them at once).
Element fades_in(Element e, std::function<bool()> cond) {
    e.prop("opacity", derive([cond] { return cond() ? 1.0f : 0.0f; }));
    e.animate("opacity", Transition::tween(seconds(0.28), hanim::ease::out_cubic));
    return only_when(std::move(e), std::move(cond));
}

// Text arriving in the other language (D-68's glyph motion, restarted with
// each switch): a label's letters gather into place; a document's lines rise
// in, in reading order.
enum class Arrival : std::uint8_t { letters, lines };
Element retyped(Element e, const std::shared_ptr<Site>& s, Arrival how = Arrival::letters) {
    e.prop("glyph_motion", derive([s, how] {
        const int n = s->switches.get();
        // Only right after a switch: a window made live later shows its text at rest.
        if (n == 0 || !s->runtime || s->runtime->now().count() - s->switched_at > 0.25) return Value{};
        GlyphMotion m = GlyphMotion::gather(static_cast<std::uint64_t>(n));
        m.stagger = hanim::seconds(0.018);
        if (how == Arrival::lines) {
            m = GlyphMotion{};
            m.domain = GlyphDomain::line;
            m.from = {.y = 10, .opacity = 0};
            m.transition = Transition::tween(seconds(0.32), hanim::ease::out_cubic);
            m.stagger = hanim::seconds(0.025);
            m.seed = static_cast<std::uint64_t>(n);  // a new value each switch: the motion starts again
        }
        return to_value(m);
    }));
    return e;
}

// Where a document's link goes: another place of the site, or outside.
void follow(Site& s, std::string_view href) {
    for (const auto& [file, place] : {std::pair{"features.md", "features"}, std::pair{"best-practices.md", "practices"}})
        if (href.ends_with(file)) {
            s.fly(place);
            return;
        }
    if (s.runtime) (void)s.runtime->open_url(href);
}

// A stage of the window's frame (D-66) after paint: when the tree changed
// (a resize, the fonts arriving, an agent's edit), the place is read again
// once the frame is done. The check is one comparison a frame.
class TreeWatch final : public pipeline::Stage {
public:
    explicit TreeWatch(Site& site) : site_(site) {}
    [[nodiscard]] std::vector<pipeline::Port> reads() const override { return {}; }
    [[nodiscard]] std::vector<pipeline::Port> writes() const override { return {}; }
    std::expected<void, std::string> run(pipeline::Context&) override {
        // A phone, or a window made narrow: compact (and back).
        if (const bool narrow = site_.runtime->size().width < Site::compact_below; narrow != site_.compact.peek()) {
            site_.compact.set(narrow);
            if (!narrow) site_.menu.set(false);
        }
        // The atlas's windows follow the view's size (a resized window, a phone turned).
        if (const Canvas* c = site_.atlas(); c && !resizing_) {
            const Size v = c->viewport();
            if (v.width > 0 && v.height > 0 && (std::ceil(v.width) != site_.layout.view.width || std::ceil(v.height) != site_.layout.view.height)) {
                resizing_ = true;
                (void)site_.runtime->after(seconds(0.0), [this, v] {
                    resizing_ = false;
                    site_.relayout(v);
                });
            }
        }
        site_.tick_time();
        if (const auto f = std::exchange(site_.ref_wanted, std::nullopt)) site_.check_frame(*f);
        if (site_.runtime && site_.runtime->semantic_revision() != site_.seen && !queued_) {
            queued_ = true;
            (void)site_.runtime->after(seconds(0.0), [this] {
                queued_ = false;
                site_.look();
            });
        }
        return {};
    }

private:
    Site& site_;
    bool  queued_ = false, resizing_ = false;
};

// A lens's button: highlighted when it is the lens.
// A lens's switch: in the bar, or (in_menu) in the compact menu.
Element lens_button(const std::shared_ptr<Site>& s, std::string_view id, std::string_view name, bool in_menu = false) {
    const std::string key(id), label(name);
    return only_when(button({.id = "lens_" + key,
                   .text = std::string(label),
                   .variant = computed([s, key] { return s->lens.get() == key ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                   .on_activated = [s, key, label] {
                       if (s->lens.peek() != key) s->mark(std::format("{} {}", s->lang.peek() == "zh" ? "镜头" : "Lens", label));
                       s->lens.set(key);
                       s->menu.set(false);
                       s->soon();
                   },
                   }),
                     [s, in_menu] { return s->compact.get() == in_menu; });
}

}  // namespace

std::span<const Place> places() { return all; }

std::string_view document(std::string_view place, std::string_view lang) {
    const bool zh = lang == "zh";
    if (place == "home") return embedded(files::content_files, zh ? "home.zh.md" : "home.en.md");
    if (place == "features") return embedded(zh ? files::zh_files : files::en_files, "features.md");
    if (place == "practices") return embedded(zh ? files::zh_files : files::en_files, "best-practices.md");
    return {};
}

std::string_view sample_timeline() { return sample; }

std::string agent_text(const Value& snapshot) {
    std::string out;
    if (const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr)
        for (const Value& n : *nodes) out += n.json() + "\n";
    return out;
}

std::string test_text(const Value& snapshot) {
    std::string out = "// What a test of this page asserts, read from the same tree:\n";
    const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr;
    if (!nodes) return out;
    std::size_t lines = 0;
    for (const Value& n : *nodes) {
        const std::string id = n.get("id") ? std::string(n.get("id")->as_string()) : std::string();
        const std::string role = n.get("role") ? std::string(n.get("role")->as_string()) : std::string();
        if (role == "generic" || id.empty()) continue;
        out += std::format("check_eq(node(\"{}\").role, \"{}\");\n", id, role);
        if (const Value* name = n.get("name"); name && !name->as_string().empty()) {
            const std::string_view text = name->as_string();
            if (text.size() <= 60) out += std::format("check_eq(node(\"{}\").name, {});\n", id, Value(std::string(text)).json());
            else {
                // A long name: what it starts with, said as that (cut where a character begins).
                std::size_t cut = 57;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                out += std::format("check(node(\"{}\").name.starts_with({}));\n", id, Value(std::string(text.substr(0, cut))).json());
            }
        }
        if (++lines >= 60) break;
    }
    return out;
}

std::uint64_t tree_hash(const Value& snapshot) {
    hash::Fnv1a h;
    const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr;
    if (!nodes) return h.value();
    for (const Value& n : *nodes) {
        Value kept;  // what the tree says, not where the window put it
        const bool viewport = n.get("role") && n.get("role")->as_string() == "scroll_view";  // its value is how far it scrolls: the window's
        for (std::string_view key : {"id", "role", "name", "description", "value"})
            if (const Value* v = n.get(key); v && !(viewport && key == "value")) kept.set(key, *v);
        // Its states, but not those of the moment: where the window cuts it, the pointer, a motion.
        if (const Value* states = n.get("state"); states && states->list()) {
            Value::List lasting;
            for (const Value& st : *states->list())
                if (const std::string_view x = st.as_string(); x != "offscreen" && x != "hovered" && x != "pressed" && x != "animating") lasting.push_back(st);
            if (!lasting.empty()) kept.set("state", Value(std::move(lasting)));
        }
        h.text(kept.json());
    }
    return h.value();
}

namespace {

// The agent panel: what a visitor can ask the agent, what it asks the person,
// the grants it holds, and every request and record it exchanged.
Element guide_panel(const std::shared_ptr<Site>& s) {
    auto zh = [s] { return s->lang.get() == "zh"; };
    std::vector<Element> asks;
    for (const GuideTask& t : guide_tasks()) {
        const std::string id(t.id);
        asks.push_back(button({.id = "task_" + id,
                               .disabled = computed([s, id] { return s->guide && (s->guide->busy.get() || (!s->guide->asking.get().empty() && id != "self")); }),
                               .text = computed([zh, t] { return std::string(zh() ? t.zh : t.en); }),
                               .variant = ButtonVariant::secondary,
                               .on_activated = [s, id] {
                                   if (s->guide) s->guide->run(id);
                               }}));
    }
    // The person's own answers (S-10): an agent pressing them is refused, whatever it claims to be.
    auto mine = [](Element e) {
        e.prop("person", Source{Value(true), {}, {}});
        return e;
    };
    Element answer = row({.id = "answer", .gap = 6, .wrap = "wrap"},
                         {mine(button({.id = "allow_once",
                                       .text = computed([zh] { return std::string(zh() ? "允许这一次" : "Allow once"); }),
                                       .variant = ButtonVariant::primary,
                                       .on_activated = [s] { s->guide->allow_once(); }})),
                          mine(button({.id = "allow",
                                       .text = computed([zh] { return std::string(zh() ? "本次访问都允许" : "Allow for this visit"); }),
                                       .variant = ButtonVariant::secondary,
                                       .on_activated = [s] { s->guide->allow(); }})),
                          mine(button({.id = "deny",
                                       .text = computed([zh] { return std::string(zh() ? "不允许" : "No"); }),
                                       .variant = ButtonVariant::ghost,
                                       .on_activated = [s] { s->guide->deny(); }}))});
    return column(
        {.id = "guide", .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(380.0f); }), .padding = Edges(20.0f, 16.0f), .gap = 10, .background = "surface"},
        {text({.id = "guide_title", .text = computed([zh] { return std::string(zh() ? "这一页上的 agent" : "An agent on this page"); }), .style = "heading"}),
         text({.id = "guide_about",
               .text = computed([zh] {
                   return std::string(zh() ? "一个按脚本行动的 agent：它像 Claude Code 经 omui mcp 那样，用 openmui.tree@0.1 经总线发请求，每一个都由 openas 决定。"
                                           : "A scripted agent. It sends openmui.tree@0.1 requests over the bus, as Claude Code does through omui mcp, and openas decides each one.");
               }),
               .style = "caption"}),
         column({.id = "tasks", .gap = 4}, std::move(asks)),
         only_when(column({.id = "asking", .padding = Edges(10.0f), .gap = 6, .background = "background"},
                          {text({.id = "question",
                                 .text = computed([s, zh] {
                                     const std::string what = s->guide ? s->guide->asking.get() : std::string();
                                     return zh() ? std::format("agent 请求：{}。这是外部的动作，要先问你。", what)
                                                 : std::format("The agent asks: {}. It is external, so it asks you first.", what);
                                 }),
                                 .style = "body"}),
                           std::move(answer)}),
                   [s] { return s->guide && !s->guide->asking.get().empty(); }),
         row({.id = "held", .gap = 8, .align = "center"},
             {text({.id = "grants",
                    .text = computed([s, zh] {
                        const std::string g = s->guide ? s->guide->grants.get() : std::string();
                        return g.empty() ? std::string(zh() ? "授权：无" : "Grants: none") : std::string(zh() ? "授权：" : "Grants: ") + g;
                    }),
                    .style = "caption",
                    .grow = 1}),
              mine(button({.id = "revoke",
                           .text = computed([zh] { return std::string(zh() ? "收回" : "Revoke"); }),
                           .variant = ButtonVariant::ghost,
                           .on_activated = [s] { s->guide->revoke(); }}))}),
         scroll({.id = "said", .grow = 1, .padding = Edges(10.0f), .background = "background"},
                {text({.id = "transcript", .text = computed([s] { return s->guide ? s->guide->transcript.get() : std::string(); }), .style = "code"})})});
}

// Shaped labels, kept per text (shaping is not per frame).
struct Labels {
    // By text and size: a label is drawn at the size the camera gives it, in whole pixels.
    std::map<std::pair<std::string, int>, typeset::Paragraph, std::less<>> shaped;
    bool                                                                  dark = false;
    const typeset::Paragraph& get(world::DrawContext& d, std::string_view text, int size, Color colour) {
        if (d.theme.dark != dark) {
            shaped.clear();
            dark = d.theme.dark;
        }
        std::pair<std::string, int> key{std::string(text), size};
        auto it = shaped.find(key);
        if (it == shaped.end()) {
            typeset::ParagraphStyle ps;
            ps.style.size = static_cast<float>(size);
            ps.style.weight = 650;
            ps.style.color = colour;
            ps.style.families = d.theme.text_style("title").families;
            ps.wrap = false;
            typeset::Paragraph p(*d.fonts, std::string(text), ps);
            p.layout(std::numeric_limits<float>::infinity());
            it = shaped.emplace(std::move(key), std::move(p)).first;
        }
        return it->second;
    }
};

// A work's window: the work itself, running, when the window is live; far
// away (a texture, drawn away from the UI thread, values only) what it is.
// A fighter's name as the game writes it: its calligraphy's characters in Chinese.
std::string fighter_name(std::string_view file, std::string_view en, bool zh) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> han{
        {{"kata", "型"}, {"ke", "刻"}, {"kun", "坤"}, {"li", "璃"}, {"mo", "墨"}, {"ni", "霓"}, {"yue", "岳"}}};
    if (zh)
        for (const auto& [f, h] : han)
            if (f == file) return std::string(h);
    return std::string(en);
}

// Frames of advantage as a move list writes them: +3, −2, 0; a knockdown's
// counts the getting up; none measured: a dash.
std::string advantage(const std::optional<std::int32_t>& a, bool down, bool zh) {
    if (!a) return "—";
    std::string n = *a > 0 ? std::format("+{}", *a) : *a < 0 ? std::format("−{}", -*a) : std::string("0");
    return down ? std::format("{} {}", zh ? "倒地" : "KD", n) : n;
}

// An id of a move by its name (5LP → move_5lp, j.HK → move_j_hk): the data's key, not its display.
std::string move_id(std::string_view name) {
    std::string id = "move_";
    for (const char c : name) id += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    return id;
}

// The fighting game's move list, a real frame-data station: every number
// measured by the game itself — each move played against a dummy standing
// still, then guarding, from where it reaches (gongfu.training) — and the
// move chosen shown on the game's own training stage, frame by frame.
Element moves_window(const std::shared_ptr<Site>& s, bool live) {
    const Size v = s->layout.view;
    const bool zh = s->lang.peek() == "zh";
    auto say = [zh](std::string_view en, std::string_view han) { return std::string(zh ? han : en); };
    Element title = text({.id = "title", .text = say("Move list — frame data, measured", "出招表：帧数据，实测"), .style = "title"});
    Element about = text({.id = "about",
                          .text = say("Every number here is measured: the game plays each move against a dummy standing still, then guarding, from where it "
                                      "reaches. Choose a move to see it on the game's own training stage, frame by frame, with its boxes.",
                                      "这里的每个数字都是实测的：游戏自己把每个招式对着站立的、再对着防御的木桩出一遍，从刚好够得着的距离出。"
                                      "选一个招式，在游戏自己的训练场上逐帧看它，连同判定框。"),
                          .style = "body"});
    if (!live)  // far away (a texture, drawn away from the UI thread): what it is, values only
        return column({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(32.0f, 40.0f), .gap = 14, .background = "background"},
                      {std::move(title), std::move(about),
                       box({.id = "later", .justify = "center", .align = "center", .grow = 1, .background = "surface", .radius = 12},
                           {text({.text = say("Close up, the game measures and plays every move.", "走近，游戏会实测并演示每个招式。"), .style = "caption",
                                  .color = "text_muted"})})});
    const fighting::Roster& r = fighting::roster();
    std::vector<Element> tabs;
    for (std::size_t i = 0; i < r.names.size(); ++i) {
        const std::string file = r.names[i];
        tabs.push_back(button({.id = "fighter_" + file,
                               .text = fighter_name(file, r.fighters[i].def.name, zh),
                               .variant = computed([s, file] { return s->fighter.get() == file ? ButtonVariant::primary : ButtonVariant::ghost; }),
                               .on_activated = [s, file] {
                                   s->fighter.set(file);
                                   // Its first move with a strike, shown from its beginning.
                                   const gongfu::Fighter& f = fighting::roster().fighters[fighting::fighter_index(file)];
                                   const auto first = std::ranges::find_if(f.def.moves, [](const gongfu::MoveDef& m) { return !m.hits.empty() && m.asked; });
                                   s->move.set(first != f.def.moves.end() ? first->name : f.def.moves.front().name);
                                   s->demo_at.set(0);
                               }}));
    }
    // The table: a header, then a row a move (rebuilt for another fighter, or another move chosen).
    // Static: `cells` runs again in the dynamic below, after this function has returned.
    static constexpr std::array<float, 7> widths{64, 56, 48, 68, 64, 72, 56};
    auto cells = [](std::vector<std::string> values, std::string_view style) {
        std::vector<Element> out;
        for (std::size_t c = 1; c < values.size(); ++c)
            out.push_back(text({.text = std::move(values[c]), .style = std::string(style), .width = Dim(widths[c])}));
        return out;
    };
    std::vector<Element> head{text({.text = say("Move", "招式"), .style = "label", .width = Dim(widths[0])})};
    for (Element& e : cells({"", say("Startup", "发生"), say("Active", "持续"), say("Recovery", "收招"), say("On hit", "命中"), say("On block", "被防"),
                             say("Damage", "伤害")},
                            "label"))
        head.push_back(std::move(e));
    Element table = column({.id = "table", .gap = 2},
                           {row({.id = "head", .padding = Edges(4.0f, 8.0f), .gap = 6, .align = "center"}, std::move(head)),
                            dynamic([s, zh, cells] {
                                const std::string who = s->fighter.get();
                                const std::string chosen = s->move.get();
                                const gongfu::Fighter& f = fighting::roster().fighters[fighting::fighter_index(who)];
                                std::vector<Element> rows;
                                untracked([&] {
                                    const auto& data = s->moves_of(who);
                                    for (std::size_t m = 0; m < f.def.moves.size() && m < data.size(); ++m) {
                                        const gongfu::MoveDef& md = f.def.moves[m];
                                        if (!md.asked) continue;  // started by something else (a piece's shard): not a move to ask for
                                        const gongfu::training::FrameData& d = data[m];
                                        const std::string name = md.name;
                                        std::vector<Element> line{box({.id = "name", .width = Dim(widths[0])},
                                                                      {button({.id = "pick",
                                                                               .text = name,
                                                                               .variant = name == chosen ? ButtonVariant::primary : ButtonVariant::ghost,
                                                                               .on_activated = [s, name] {
                                                                                   s->move.set(name);
                                                                                   s->demo_at.set(0);
                                                                               }})})};
                                        const std::string kind = d.throw_ ? (zh ? "投" : "throw") : d.air ? (zh ? "空中" : "air") : "";
                                        for (Element& e : cells({"", std::format("{}", d.startup), std::format("{}", d.active), std::format("{}", d.recovery),
                                                                 kind.empty() ? advantage(d.on_hit, d.knockdown, zh) : kind,
                                                                 kind.empty() ? advantage(d.on_block, false, zh) : "—", std::format("{}", d.damage)},
                                                                "code"))
                                            line.push_back(std::move(e));
                                        Element e = row({.id = move_id(name), .padding = Edges(2.0f, 8.0f), .gap = 6, .align = "center",
                                                         .background = name == chosen ? "surface_variant" : "transparent", .radius = 6},
                                                        std::move(line));
                                        e.key = move_id(name);
                                        rows.push_back(std::move(e));
                                    }
                                });
                                return rows;
                            })});
    // The stage: the game's training mode demonstrating the move chosen, its boxes and frame meter over it.
    auto frames = [s] {
        const auto& data = s->moves_of(s->fighter.get());
        const gongfu::Fighter& f = fighting::roster().fighters[fighting::fighter_index(s->fighter.get())];
        const std::int32_t m = f.find(s->move.get());
        if (m < 0 || static_cast<std::size_t>(m) >= data.size()) return 0.0;
        return static_cast<double>(s->guard.get() ? data[static_cast<std::size_t>(m)].shown_on_block : data[static_cast<std::size_t>(m)].shown_on_hit);
    };
    // A phone: the stage first, as wide as the page, the list under it, the page scrolling.
    const bool narrow = v.width < 600;
    const float stage_w = narrow ? std::min(512.0f, v.width - 32) : 512.0f;
    Element stage = fighting::stage({.id = "stage",
                                     .arena = "plain",
                                     .demo = s->move,
                                     .demo_guard = s->guard,
                                     .mode = "training",
                                     .player1 = s->fighter,
                                     .player2 = s->fighter,
                                     .width = Dim(stage_w),
                                     .height = Dim(stage_w * 288 / 512)});
    Element controls = row({.id = "controls", .gap = 6, .align = "center"},
                           {button({.id = "on_hit",
                                    .text = say("On hit", "命中"),
                                    .variant = computed([s] { return s->guard.get() ? ButtonVariant::ghost : ButtonVariant::primary; }),
                                    .on_activated = [s] { s->guard.set(false), s->demo_at.set(0); }}),
                            button({.id = "on_block",
                                    .text = say("On block", "被防"),
                                    .variant = computed([s] { return s->guard.get() ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                    .on_activated = [s] { s->guard.set(true), s->demo_at.set(0); }}),
                            button({.id = "play", .text = say("Play", "播放"), .variant = ButtonVariant::ghost, .on_activated = [s] { s->stage_act("resume"); }}),
                            slider({.id = "frame",
                                    .label = say("Frame", "帧"),
                                    .max = computed(frames),
                                    .min = 0,
                                    .step = 1,
                                    .value = s->demo_at,
                                    .on_changed = [s](double f) { s->stage_act("demo_frame", Value{}.set("frame", static_cast<std::int64_t>(f))); },
                                    .grow = 1}),
                            text({.id = "frame_of",
                                  .text = computed([s, frames, zh] { return std::format("{} {} / {}", zh ? "帧" : "frame", static_cast<int>(s->demo_at.get()), static_cast<int>(frames())); }),
                                  .style = "caption",
                                  .width = Dim(96.0f)})});
    // The move chosen, in words: what it is, what it costs and does, what it cancels into, what it passes through.
    // (Its sentences in the reader's language: the format chosen at run time.)
    const auto say_f = [zh](std::string_view en, std::string_view han, const auto&... args) { return std::vformat(zh ? han : en, std::make_format_args(args...)); };
    Element details = dynamic([s, zh, say_f] {
        const gongfu::Fighter& f = fighting::roster().fighters[fighting::fighter_index(s->fighter.get())];
        const std::int32_t m = f.find(s->move.get());
        std::vector<Element> out;
        if (m < 0) return out;
        const gongfu::MoveDef& md = f.def.moves[static_cast<std::size_t>(m)];
        const auto& data = untracked([&] { return s->moves_of(s->fighter.peek()); });
        const gongfu::training::FrameData& d = data[static_cast<std::size_t>(m)];
        std::vector<std::string> lines;
        // Its first strike's height: how it must be guarded.
        const std::optional<gongfu::Level> height = !md.hits.empty() ? std::optional(md.hits.front().level)
                                                  : md.projectile ? std::optional(md.projectile->hit.level) : std::nullopt;
        const std::string guarded = !height ? std::string()
                                  : *height == gongfu::Level::low      ? std::string(zh ? "（下段）" : " (low)")
                                  : *height == gongfu::Level::overhead ? std::string(zh ? "（中段，须站防）" : " (overhead)")
                                                                       : std::string(zh ? "（上段）" : " (mid)");
        std::string what = md.level > 0 ? say_f("Level {} super", "{} 级超必杀", md.level)
                         : d.throw_ ? (zh ? "投技" : "Throw") : d.projectile ? (zh ? "飞行道具" : "Projectile") : d.air ? (zh ? "空中技" : "Air move")
                                                                                                                : (zh ? "打击" : "Strike");
        what += guarded;
        if (md.meter_cost > 0) what += say_f(", {} meter", "，消耗 {} 能量", md.meter_cost);
        lines.push_back(std::format("{}{}{}", md.name, zh ? "：" : " — ", what));
        lines.push_back(say_f("Damage {}, chip {}; {} frames in all", "伤害 {}，削血 {}；共 {} 帧", d.damage, d.chip, d.total));
        if (d.knockdown) lines.push_back(zh ? "命中后对手倒地（命中一栏含起身）" : "Knocks down on hit (on hit counts the getting up)");
        if (d.launch) lines.push_back(zh ? "命中后把对手打上空中" : "Launches on hit");
        for (const gongfu::CancelDef& c : md.cancels) {
            std::string into;
            for (const std::string& i : c.into) into += (into.empty() ? "" : ", ") + i;
            const std::string_view after = c.on_contact ? (zh ? "接触后" : " after contact") : "";
            lines.push_back(say_f("Cancels into {3} on frames {0}–{1}{2}", "第 {}–{} 帧{}可取消为 {}", c.frames.from, c.frames.to, after, into));
        }
        for (const gongfu::Invulnerable& i : md.invulnerable) {
            std::string to;
            const auto add = [&](std::uint8_t bit, std::string_view en, std::string_view han) {
                if (i.to & bit) to += (to.empty() ? "" : (zh ? "、" : ", ")) + std::string(zh ? han : en);
            };
            add(gongfu::to_strike, "strikes", "打击");
            add(gongfu::to_air, "air attacks", "空中攻击");
            add(gongfu::to_projectile, "projectiles", "飞行道具");
            add(gongfu::to_throw, "throws", "投技");
            lines.push_back(say_f("Invulnerable to {2} on frames {0}–{1}", "第 {}–{} 帧无敌：{}", i.frames.from, i.frames.to, to));
        }
        for (std::size_t i = 0; i < lines.size(); ++i)
            out.push_back(text({.id = std::format("line{}", i), .text = lines[i], .style = i == 0 ? "label" : "body"}));
        return out;
    });
    if (narrow)
        return scroll({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(20.0f, 16.0f), .background = "background"},
                      {column({.id = "intro", .padding = Edges(Insets{0, 0, 12, 0}), .gap = 10}, {std::move(title), std::move(about)}),
                       column({.id = "show", .width = Dim(stage_w), .gap = 10},
                              {box({.id = "frame_box", .radius = 12, .clip = true}, {std::move(stage)}), std::move(controls),
                               column({.id = "details", .gap = 4}, {std::move(details)})}),
                       column({.id = "list", .padding = Edges(Insets{16, 0, 0, 0}), .gap = 12},
                              {row({.id = "fighters", .gap = 4, .wrap = "wrap"}, std::move(tabs)),
                               scroll({.id = "moves", .horizontal = true, .height = Dim(360.0f), .background = "surface"}, {std::move(table)})})});
    return row({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(28.0f, 32.0f), .gap = 28, .align = "stretch", .background = "background"},
               {column({.id = "list", .min_width = Dim(0.0f), .gap = 12, .grow = 1},
                       {std::move(title), std::move(about), row({.id = "fighters", .gap = 4, .wrap = "wrap"}, std::move(tabs)),
                        scroll({.id = "moves", .grow = 1, .background = "surface"}, {std::move(table)})}),
                column({.id = "show", .width = Dim(stage_w), .gap = 10},
                       {box({.id = "frame_box", .radius = 12, .clip = true}, {std::move(stage)}), std::move(controls),
                        column({.id = "details", .gap = 4}, {std::move(details)})})});
}

Element work_window(const std::shared_ptr<Site>& s, const std::string& id, bool live) {
    const Size v = s->layout.view;
    const bool zh = s->lang.peek() == "zh";
    auto say = [zh](std::string_view en, std::string_view han) { return std::string(zh ? han : en); };
    auto later = [&](std::string_view what) {
        return box({.id = "later", .justify = "center", .align = "center", .grow = 1, .background = "surface", .radius = 12},
                   {text({.text = std::string(what), .style = "caption", .color = "text_muted"})});
    };
    if (id == "hello") {
        // Example 01, the shortest openmui program, running beside its own source (embedded by its build).
        // Far away the same program as it starts (a state of its own, never written: the texture is drawn on another thread).
        static hello::State still;
        Element run = live ? hello::app(s->hello).view() : hello::app(still).view();
        if (v.width < 600) {  // a phone: the program, then its source, the page scrolling
            const float w = v.width - 32;
            return scroll({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(20.0f, 16.0f), .background = "background"},
                          {column({.width = Dim(w), .gap = 14},
                                  {text({.id = "title", .text = say("Example 01 — hello, immediate mode", "示例 01：你好，立即模式"), .style = "title"}),
                                   text({.id = "about",
                                         .text = say("A whole openmui program, running here: type a name, click the button. Its interface is declared each frame "
                                                     "in plain C++; an agent finds the field by its role, and renaming the button keeps its id.",
                                                     "一个完整的 openmui 程序，就在这里运行：输入名字，点按钮。界面每一帧用普通的 C++ 声明；agent 按角色找到输入框，"
                                                     "按钮改名后 id 不变。"),
                                         .style = "body"}),
                                   box({.id = "run", .width = Dim(w), .height = Dim(280.0f), .background = "surface", .border_color = "border", .border_width = 1,
                                        .radius = 12, .clip = true},
                                       {std::move(run)}),
                                   scroll({.id = "code", .horizontal = true, .vertical = false, .width = Dim(w), .padding = Edges(16.0f, 16.0f), .background = "surface"},
                                          {text({.id = "source", .text = std::string(hello::hello_source), .style = "code", .wrap = false})})})});
        }
        return row({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(32.0f, 40.0f), .gap = 32, .align = "stretch", .background = "background"},
                   {column({.width = Dim(440.0f), .gap = 14},
                           {text({.id = "title", .text = say("Example 01 — hello, immediate mode", "示例 01：你好，立即模式"), .style = "title"}),
                            text({.id = "about",
                                  .text = say("A whole openmui program, running here: type a name, click the button. Its interface is declared each frame "
                                              "in plain C++; an agent finds the field by its role, and renaming the button keeps its id.",
                                              "一个完整的 openmui 程序，就在这里运行：输入名字，点按钮。界面每一帧用普通的 C++ 声明；agent 按角色找到输入框，"
                                              "按钮改名后 id 不变。"),
                                  .style = "body"}),
                            box({.id = "run", .width = Dim(420.0f), .height = Dim(280.0f), .background = "surface", .border_color = "border", .border_width = 1, .radius = 12,
                                 .clip = true},
                                {std::move(run)})}),
                    // The source as written: lines kept whole (indentation is meaning), scrolled sideways when long.
                    scroll({.id = "code", .horizontal = true, .grow = 1, .padding = Edges(16.0f, 20.0f), .background = "surface"},
                           {text({.id = "source", .text = std::string(hello::hello_source), .style = "code", .wrap = false})})});
    }
    // S1's wall, small: the site's documents in both languages and the benchmark's, on a canvas of their own.
    Element wall = live && s->wall ? canvas({.id = "wall",
                                             .behaviors = std::vector<std::string>{"select", "drag", "snap", "minimap", "group-drag", "views"},
                                             .label = say("openmui's documents as a wall", "openmui 的文档墙"),
                                             .max_zoom = 8,
                                             .min_zoom = 0.004,
                                             .scene = s->wall->scene,
                                             .start = "fit",
                                             .grow = 1})
                                   : wall_picture(s->thumbs);
    return column({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(24.0f, 32.0f), .gap = 12, .background = "background"},
                  {text({.id = "title", .text = say("The document wall (S1)", "文档墙（S1）"), .style = "title"}),
                   text({.id = "about",
                         .text = say("Every document on one infinite canvas: far away a block, nearer a texture drawn offscreen, close up a live document. "
                                     "Drag a workspace by its label, a window by its title bar; 2 shows them all, 1 is one to one.",
                                     "所有文档在一块无限画布上：远处是色块，近一些是离屏画好的纹理，再近就是活的文档。拖工作区的标签、窗口的标题栏；"
                                     "按 2 看全部，按 1 回到一比一。"),
                         .style = "body"}),
                   std::move(wall)});
}

// S2's workshop: one real interface in a style pack, the pack edited live.
// The place is a theme scope (styles::Studio::scope): the pack styles it
// alone, and the site around it keeps its own theme.
Element styles_window(const std::shared_ptr<Site>& s, bool live) {
    const Size v = s->layout.view;
    const bool zh = s->lang.peek() == "zh";
    auto say = [zh](std::string_view en, std::string_view han) { return std::string(zh ? han : en); };
    Element work = live ? styles::view(s->studio) : packs_picture(s->thumbs);
    return column({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(24.0f, 32.0f), .gap = 12, .background = "background"},
                  {text({.id = "title", .text = say("The style workshop (S2)", "风格工坊（S2）"), .style = "title"}),
                   text({.id = "about",
                         .text = say("One real interface in each of openmui's style packs — glass, neon, retro, ink, liquid — and the pack shown edited "
                                     "as you go. Only pixels change: the semantic tree is the same in every pack. The pack styles this place alone; "
                                     "the site around it keeps its own theme.",
                                     "同一个真实的界面，放进 openmui 的每个风格包：玻璃、霓虹、复古、水墨、液态；当前的风格包可以边看边改。"
                                     "变的只有像素：每个风格包下语义树都一样。风格包只作用于这个地方，周围的网站保持自己的主题。"),
                         .style = "body"}),
                   box({.id = "workshop", .grow = 1, .radius = 12, .clip = true}, {std::move(work)})});
}

// The check against CI: the reference run's frame as this browser drew it,
// its pixels' hash, and the hash CI pinned for the same frame.
// How the page draws, and the person's switches for it: who draws (the
// GPU, the CPU), and when the places go live — only once the camera rests
// (the default: a flight shows their textures, smooth whatever a place
// costs), always (what a live place costs while moving, on the meter), or
// never (textures only). The meter in the bar says what each costs.
Element drawing_panel(const std::shared_ptr<Site>& s) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    auto labels = [s](std::vector<std::string> en, std::vector<std::string> han) {
        return computed([s, en = std::move(en), han = std::move(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    return column({.id = "drawing", .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(340.0f); }), .padding = Edges(16.0f, 16.0f), .gap = 10, .background = "surface"},
                  {text({.id = "title", .text = say("How this page draws", "这页怎么画"), .style = "heading"}),
                   text({.id = "now",
                         .text = computed([s] {
                             const std::string by(s->runtime ? s->runtime->renderer() : std::string_view("cpu"));
                             const std::string name = by == "cpu" ? "the CPU" : by == "webgpu" ? "WebGPU" : by == "webgl" ? "WebGL 2" : by;
                             return s->lang.get() == "zh" ? std::format("现在由 {} 绘制。", by == "cpu" ? "CPU" : name)
                                                          : std::format("Drawn by {} now.", name);
                         })}),
                   select({.id = "renderer",
                           .label = say("Who draws", "谁来画"),
                           .labels = labels({"Automatic (a GPU when there is one)", "The GPU", "The CPU (the reference)"},
                                            {"自动（有 GPU 就用 GPU）", "GPU", "CPU（参考实现）"}),
                           .options = std::vector<std::string>{"automatic", "gpu", "cpu"},
                           .value = s->renderer_choice,
                           .on_changed = [s](std::string v) {
                               s->renderer_choice.set(v);
                               if (auto r = choose_renderer(v); !r) s->drawing_note.set(r.error());
                               else s->drawing_note.set(std::string());
                           }}),
                   select({.id = "promotion",
                           .label = say("Places while the camera moves", "镜头移动时的地方"),
                           .labels = labels({"Pictures, live once at rest", "Live, always", "Pictures only"},
                                            {"先用图片，停下后变成实时", "一直实时", "只用图片"}),
                           .options = std::vector<std::string>{"at_rest", "always", "never"},
                           .value = s->promotion,
                           .on_changed = [s](std::string v) { s->promotion.set(std::move(v)); }}),
                   text({.id = "about",
                         .text = say("While the camera flies, each place is drawn from a picture of it and goes live 120 ms after the camera rests: a "
                                     "flight stays smooth whatever a place costs to lay out. \"Live, always\" draws them live in flight too — the frame "
                                     "meter above shows what that costs on this machine; \"Pictures only\" keeps them pictures (the one you use still "
                                     "goes live).",
                                     "镜头飞行时，每个地方先用它的图片来画，镜头停下 120 毫秒后才变成实时的；所以不管一个地方排版多贵，飞行都是流畅的。"
                                     "“一直实时”让它们在飞行中也实时绘制，上面的帧率表会显示这台机器上的代价；“只用图片”让它们一直是图片（正在用的那个仍然是实时的）。"),
                         .style = "caption"}),
                   text({.id = "note", .text = s->drawing_note, .style = "caption", .color = "text_muted"})});
}

Element check_panel(const std::shared_ptr<Site>& s) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    return column({.id = "check", .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(360.0f); }), .padding = Edges(16.0f, 16.0f), .gap = 10, .background = "surface"},
                  {text({.id = "title", .text = say("The reference run", "参考回放"), .style = "heading"}),
                   text({.id = "about",
                         .text = say("A few seconds of this site, played here offscreen at CI's size (1280 × 800): a flight, the language switched, a lens. "
                                     "This machine draws each frame and hashes its pixels; CI pinned the same frames' hashes.",
                                     "这个网站的几秒钟，在这里离屏按 CI 的尺寸（1280 × 800）重放：一次飞行、切换语言、切换镜头。"
                                     "这台机器画出每一帧并对像素求哈希；CI 固定了同样这些帧的哈希。"),
                         .style = "caption"}),
                   dynamic([s] {
                       (void)s->ref_shown.get();  // a new frame: drawn again
                       return std::vector<Element>{painter({.id = "preview",
                                                          .width = Dim(328.0f),
                                                          .height = Dim(205.0f),
                                                          .label = "The reference run's frame",
                                                          .paint = [s](PaintContext& c) {
                                                              if (s->ref_image.id != 0)
                                                                  c.list.draw_image(s->ref_image, {0, 0, reference_size.width, reference_size.height},
                                                                                    {0, 0, c.size.width, c.size.height});
                                                          }})};
                   }),
                   slider({.id = "ref_frame",
                           .label = say("Frame", "帧"),
                           .max = static_cast<double>(reference_frames() - 1),
                           .min = 0,
                           .step = 1,
                           .value = s->ref_frame,
                           .on_changed = [s](double f) { s->want_frame(static_cast<std::int32_t>(f)); }}),
                   text({.id = "at",
                         .text = computed([s] {
                             const int f = static_cast<int>(s->ref_frame.get());
                             return std::format("{} {} · {:.1f} s", s->lang.get() == "zh" ? "帧" : "frame", f, f / reference_fps);
                         }),
                         .style = "caption"}),
                   text({.id = "mine",
                         .text = computed([s] { return std::format("{} {}", s->lang.get() == "zh" ? "这台机器" : "this machine", s->ref_mine.get()); }),
                         .style = "code"}),
                   text({.id = "ci",
                         .text = computed([s] { return std::format("CI {}", s->ref_ci.get().empty() ? std::string("—") : s->ref_ci.get()); }),
                         .style = "code"}),
                   text({.id = "verdict",
                         .text = computed([s] {
                             const bool zh = s->lang.get() == "zh";
                             const int v = s->ref_verdict.get();
                             return std::string(v > 0 ? (zh ? "✓ 同样的像素，逐位相同" : "✓ the same pixels, bit for bit")
                                                : v < 0 ? (zh ? "✗ 像素不同" : "✗ not the same pixels")
                                                        : (zh ? "这一帧 CI 还没有固定" : "CI has not pinned this frame"));
                         }),
                         .style = "label"})});
}

// A place's window: what it shows, at the view's size. Live, it is bound to
// the site (the language, the timeline as edited, links); as a texture
// (drawn away from the UI thread) it holds values only.
Element place_window(const std::shared_ptr<Site>& s, const std::string& id, bool live) {
    const Size v = s->layout.view;
    const bool zh = s->lang.peek() == "zh";
    if (id == "timeline") {
        Element editor = timeline_editor({.id = "editor", .document = s->timeline.peek(), .zoom = 60});
        if (live) {
            // The live work: its own history (S-12); an edit is kept for the next time it is live, and changes the tree a lens shows.
            editor.prop("undo_scope", Source{Value(true), {}, {}});
            editor.on("edited", [s](const Value& e) {
                if (const Value* d = e.get("document")) s->timeline.set(std::string(d->as_string()));
                s->soon();
            });
        }
        auto words = [s, live, zh](std::string_view en, std::string_view han) -> Prop<std::string> {
            if (!live) return std::string(zh ? han : en);
            return computed([s, en, han] { return std::string(s->lang.get() == "zh" ? han : en); });
        };
        Element title = text({.id = "title", .text = words("A timeline editor", "时间线编辑器"), .style = "title"});
        return column({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(32.0f, 40.0f), .gap = 12, .background = "background"},
                      {live ? retyped(std::move(title), s) : std::move(title),
                       text({.id = "how",
                             .text = words("Drag a clip, trim its edge, press S to split it at the playhead, Ctrl+Z to undo. Every edit has its exact inverse; switch the lens to see the same timeline as an agent reads it.",
                                           "拖动片段、拖边修剪、按 S 在播放头处切开，Ctrl+Z 撤销。每次编辑都有它精确的反向；切换镜头，看 agent 读到的同一份时间线。"),
                             .style = "body"}),
                       std::move(editor)});
    }
    if (id == "hello" || id == "wall") return work_window(s, id, live);
    if (id == "moves") return moves_window(s, live);
    if (id == "styles") return styles_window(s, live);
    std::vector<Element> content;
    const bool narrow = v.width < 600;  // a phone: less margin
    if (id == "home") content.push_back(hero(s->hero, s->hero_site, live, v.width - (narrow ? 40.0f : 80.0f)));
    Element doc = live ? retyped(markdown({.id = "doc",
                                           .origin = MarkdownOrigin::app,
                                           .source = computed([s, id] { return std::string(document(id, s->lang.get())); }),
                                           .on_link_activated = [s](std::string href) { follow(*s, href); },
                                           .max_width = Dim(640.0f)}),
                                 s, Arrival::lines)
                       : markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = std::string(document(id, zh ? "zh" : "en")), .max_width = Dim(640.0f)});
    content.push_back(std::move(doc));
    return scroll({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = narrow ? Edges(20.0f, 20.0f) : Edges(32.0f, 40.0f), .background = "background"},
                  std::move(content));
}

// The atlas's kinds: a place's window (a block far away, its texture nearer,
// live when it nearly fills the view) and its label (drawn at a size fixed
// on screen, above its window).
void atlas_kinds(const std::shared_ptr<Site>& s) {
    world::Scene& scene = s->scene();
    using world::Tier;
    auto labels = std::make_shared<Labels>();
    scene.kind("place", world::Kind{.tiers = {{0, Tier::Draw::custom}, {48, Tier::Draw::texture}, {live_from(s->layout), Tier::Draw::promote}},
                                    .draw =
                                        [](world::DrawContext& d) {
                                            const float r = static_cast<float>(14 * d.zoom);
                                            d.list.fill_round_rect(d.screen, CornerRadii::all(r), render::Paint::solid(d.theme.color("surface")));
                                        },
                                    // The scene is the process's (by name): what it holds of the site is a weak reference, the site its owner.
                                    .promote =
                                        [w = std::weak_ptr<Site>(s)](const world::Scene& sc, world::NodeRef n) {
                                            const auto p = w.lock();
                                            return p ? place_window(p, std::string(sc.key(n)), true) : box({});
                                        },
                                    .view =
                                        [w = std::weak_ptr<Site>(s)](const world::Scene& sc, world::NodeRef n) {
                                            const auto p = w.lock();
                                            return p ? place_window(p, std::string(sc.key(n)), false) : box({});
                                        },
                                    .role = Role::article});
    scene.kind("label", world::Kind{.tiers = {{0, Tier::Draw::custom}},
                                    .draw =
                                        [labels](world::DrawContext& d) {
                                            if (!d.fonts || d.screen.height < 6) return;
                                            const int size = static_cast<int>(std::lround(std::clamp(d.screen.height * 0.42f, 13.0f, 26.0f)));
                                            const auto& p = labels->get(d, d.scene.text(d.node, "title"), size, d.theme.color("text"));
                                            const Size t = p.size();
                                            p.paint(d.list, {d.screen.x + 4, d.screen.y + d.screen.height - t.height - 6});
                                        },
                                    .role = Role::heading});
}

}  // namespace

AppSpec app(std::string_view place, std::string_view lang) {
    AppSpec a;
    a.id = "org.openmui.site";
    a.title = "openmui — one tree, many renderings";
    a.size = {1280, 800};
    a.theme = Theme::dark_theme();
    // Drawn by the machine's GPU where it has one (WebGPU, else WebGL 2; a
    // software adapter counts as none), else by the CPU: a camera's flight at
    // the display's rate. The CPU's pixels are the reference, the same bytes
    // on every machine — the check against CI draws its frames with the CPU
    // whoever draws the page — and a GPU's are within tolerances of them.
    // The bar's meter says who draws, and how fast.
    a.renderer = "automatic";
    auto s = std::make_shared<Site>();
    if (place == "overview" || std::ranges::any_of(all, [&](const Place& p) { return p.id == place; })) s->place.set(std::string(place));
    s->lang.set(lang == "zh" ? "zh" : "en");
    styles::add_verbs(a, s->studio);  // styles.show: an agent switches the workshop's pack as a person does
    a.setup = [s](Runtime& rt) {
        s->runtime = &rt;
        register_type(component_type<Lens>());
        register_base_components();  // example 01's immediate controls ask for them by name
        // The atlas: a scene of this site's own, laid out for the window's size until the canvas has its own.
        static std::atomic<int> sites{0};
        s->scene_name = std::format("openmui.site.{}", sites++);
        s->hero_site = {.runtime = &s->runtime,
                        .lang = &s->lang,
                        .nodes = &s->nodes,
                        .root = [] { return std::string(place_root) + "home/page"; },
                        .go = [p = s.get()](std::string id) { p->fly(id); },
                        .tree_lens = [p = s.get()] {
                            if (p->lens.peek() != "tree") p->mark(p->lang.peek() == "zh" ? "镜头 Tree" : "Lens Tree");
                            p->lens.set("tree");
                            p->soon();
                        }};
        s->kinds = [site = std::weak_ptr<Site>(s)] {
            if (auto p = site.lock()) atlas_kinds(p);
        };
        s->compact.set(rt.size().width < Site::compact_below);
        s->relayout({rt.size().width - (s->compact.peek() ? 0.0f : 188.0f), rt.size().height - 56 - 44});
        // S1's wall: the site's documents in both languages, and the benchmark's set beside them.
        s->wall = std::make_unique<wall::Model>();
        s->wall->title = "openmui's documents";
        s->wall->scene = s->scene_name + ".wall";
        s->wall->runtime = &rt;
        s->wall->canvas_path = std::string(place_root) + "wall/page/wall";  // a link in a live window flies the wall's own camera
        for (const std::string_view lang : {"en", "zh"})
            for (const Place& p : all)
                if (const std::string_view src = document(p.id, lang); !src.empty()) {
                    const std::string path = std::format("{}/{}.md", lang, p.id);
                    std::string title(lang == "zh" ? p.zh : p.en);
                    if (src.starts_with("# ")) title = std::string(src.substr(2, src.find('\n') - 2));
                    s->wall->docs.push_back({wall::key_of(path), std::move(title), path, std::string(lang), std::string(src)});
                }
        for (wall::Document& d : wall::synthetic(18, 7, 2)) {
            d.group = "benchmark/" + d.group;
            s->wall->docs.push_back(std::move(d));
        }
        wall::register_all();
        wall::build(*s->wall, world::scene(s->wall->scene));
        // S2's workshop: a pack styles its place alone (a theme scope); the site keeps its own.
        s->studio.scope = "workshop";
        s->studio.can_export = false;  // a page has nowhere to write the file
        s->studio.runtime = &rt;
        if (auto r = s->studio.show("glass", false); !r) warn("site: {}", r.error());
        // The far-away pictures: the wall's blocks and the packs' colours, taken now, never written again.
        {
            auto t = std::make_shared<Thumbs>();
            const world::Scene& ws = world::scene(s->wall->scene);
            bool first = true;
            ws.each_unordered([&](world::NodeRef n) {
                const std::string_view kind = ws.kind_name(n);
                if (kind != "workspace" && kind != "window") return;
                const auto r = ws.rect(n);
                t->wall.push_back({r, ws.fill(n), std::string(ws.text(n, "title")), kind == "workspace"});
                if (first) t->wall_bounds = r, first = false;
                const double x0 = std::min(t->wall_bounds.x, r.x), y0 = std::min(t->wall_bounds.y, r.y);
                const double x1 = std::max(t->wall_bounds.x + t->wall_bounds.width, r.x + r.width), y1 = std::max(t->wall_bounds.y + t->wall_bounds.height, r.y + r.height);
                t->wall_bounds = {x0, y0, x1 - x0, y1 - y0};
            });
            std::ranges::stable_sort(t->wall, [](const Thumbs::Block& a, const Thumbs::Block& b) { return a.group > b.group; });  // workspaces under their windows
            for (std::string_view name : themes::style_names)
                if (const auto st = theme::library().find_style(name)) {
                    const bool dark = std::ranges::contains(st->layers, std::string("dark"));
                    auto colour = [&](std::string_view token, Color otherwise) {
                        const auto it = st->tokens.colors.find(std::string(token));
                        return it != st->tokens.colors.end() ? it->second : otherwise;
                    };
                    t->packs.push_back({std::string(name), st->doc, colour("background", dark ? Color::hex(0x0b0f16) : Color::hex(0xf8fafc)),
                                        colour("surface", dark ? Color::hex(0x161c27) : Color::white()), colour("accent", Color::hex(0x6366f1)),
                                        colour("text", dark ? Color::hex(0xe5e7eb) : Color::hex(0x111827))});
                }
            s->thumbs = std::move(t);
        }
        s->guide = std::make_unique<Guide>(rt, [site = s.get()] { return site->place.get(); });
        register_type(component_type<Timebar>());
        register_type(component_type<FrameMeter>());
        fighting::register_components();  // the move list's stage is the game's own
        {
            const auto it = std::ranges::find_if(all, [&](const Place& p) { return p.id == s->place.peek(); });
            const bool zh = s->lang.peek() == "zh";
            s->mark(it != all.end() ? std::string(zh ? it->zh : it->en) : std::string(zh ? "全景" : "Overview"));
        }
        rt.stage_plugins().add({.name = "openmui.site.tree_watch", .kind = "stage", .description = "the site's place read again when its tree changed"},
                               [site = s.get()](const pipeline::Config&) -> std::expected<std::unique_ptr<pipeline::Stage>, std::string> {
                                   return std::make_unique<TreeWatch>(*site);
                               });
        if (auto in = rt.frame_pipeline().insert({"tree_watch", "openmui.site.tree_watch", {"paint"}, {}}, rt.stage_plugins()); !in)
            site_log.warn("the tree watch is not in the frame: {}", pipeline::to_text(in.error()));
        s->soon();
    };
    a.view = [s] {
        auto wide = [s] { return !s->compact.get(); };
        auto narrow = [s] { return s->compact.get(); };
        // The bar: the name, the lens, the language.
        Element bar = row({.id = "bar", .padding = Edges(10.0f, 16.0f), .gap = 6, .align = "center", .background = "surface"},
                          {brand_mark("logo", 28), text({.id = "mark", .text = "openmui", .style = "title"}),
                           only_when(row({.id = "tagline", .grow = 1},
                                         {retyped(text({.id = "motto", .text = computed([s] { return std::string(s->lang.get() == "zh" ? "一棵树，多种呈现" : "one tree, many renderings"); }),
                                                        .style = "caption"}),
                                                  s)}),
                                     wide),
                           only_when(box({.id = "fill", .grow = 1}), narrow),
                           only_when(frame_meter({.id = "frames", .label = computed([s] { return std::string(s->lang.get() == "zh" ? "帧率" : "Frame rate"); })}), wide),
                           only_when(button({.id = "drawing_toggle",
                                             .text = computed([s] { return std::string(s->lang.get() == "zh" ? "绘制" : "Drawing"); }),
                                             .variant = computed([s] { return s->drawing.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                             .on_activated = [s] { s->drawing.set(!s->drawing.get()); },
                                             .margin = Edges(Insets{0, 18, 0, 0})}),  // the meter's switches, apart from the lenses
                                     wide),
                           lens_button(s, "pixels", "Pixels"), lens_button(s, "tree", "Tree"), lens_button(s, "agent", "Agent"), lens_button(s, "test", "Test"),
                           only_when(button({.id = "guide_toggle",
                                             .text = computed([s] { return std::string(s->lang.get() == "zh" ? "让 agent 来" : "Ask an agent"); }),
                                             .variant = computed([s] { return s->panel.get() ? ButtonVariant::primary : ButtonVariant::secondary; }),
                                             .on_activated = [s] { s->panel.set(!s->panel.get()); }}),
                                     wide),
                           // Compact: the places, the lenses and the switches in a menu.
                           only_when(button({.id = "menu",
                                   .text = computed([s] {
                                       const bool zh = s->lang.get() == "zh";
                                       return std::string(s->menu.get() ? (zh ? "关闭" : "Close") : (zh ? "菜单" : "Menu"));
                                   }),
                                   .variant = computed([s] { return s->menu.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                   .on_activated = [s] {
                                       s->menu.set(!s->menu.get());
                                       s->panel.set(false);
                                       s->drawing.set(false);
                                       s->checking.set(false);
                                   }}),
                                     narrow),
                           button({.id = "lang",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "English" : "中文"); }),
                                   .variant = ButtonVariant::secondary,
                                   .on_activated = [s] {
                                       s->mark(s->lang.peek() == "zh" ? "English" : "中文");
                                       s->switched_at = s->runtime ? s->runtime->now().count() : 0;
                                       s->lang.set(s->lang.get() == "zh" ? "en" : "zh");
                                       s->switches.set(s->switches.get() + 1);
                                       // The labels in the other language, the windows' textures drawn again.
                                       s->relayout(s->layout.view);
                                       for (const Place& p : all)
                                           if (auto n = s->scene().find(p.id)) s->scene().touch(*n);
                                       s->soon();
                                   }})});
        // The places down the side.
        std::vector<Element> nav;
        auto go = [&nav, s](std::string id, std::string_view en, std::string_view zh) {
            nav.push_back(retyped(button({.id = "to_" + id,
                                          .text = computed([s, en, zh] { return std::string(s->lang.get() == "zh" ? zh : en); }),
                                          // Where you are: outlined, not filled (filled is for an action to take).
                                          .variant = computed([s, id] { return s->place.get() == id ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                          .on_activated = [s, id] {
                                              s->menu.set(false);
                                              s->fly(id);
                                          }}),
                                  s));
        };
        // Two groups: what to read, and what runs.
        auto group = [&nav, s](std::string id, std::string_view en, std::string_view zh) {
            // Lined up with the buttons' text, a little space above it.
            nav.push_back(box({.id = std::move(id), .padding = Edges(Insets{12, 16, 2, 16})},
                              {retyped(text({.id = "label",
                                             .text = computed([s, en, zh] { return std::string(s->lang.get() == "zh" ? zh : en); }),
                                             .style = "caption",
                                             .color = "text_muted"}),
                                       s)}));
        };
        go("overview", "Overview", "全景");
        group("learn", "LEARN", "阅读");
        for (const Place& p : all | std::views::take(3)) go(std::string(p.id), p.en, p.zh);
        group("explore", "EXPLORE", "运行");
        for (const Place& p : all | std::views::drop(3)) go(std::string(p.id), p.en, p.zh);
        // Compact: the bar's switches here.
        {
            std::vector<Element> more;
            more.push_back(retyped(text({.id = "label", .text = computed([s] { return std::string(s->lang.get() == "zh" ? "查看" : "VIEW"); }),
                                         .style = "caption", .color = "text_muted"}),
                                   s));
            for (auto [id, name] : {std::pair{"pixels", "Pixels"}, {"tree", "Tree"}, {"agent", "Agent"}, {"test", "Test"}})
                more.push_back(lens_button(s, id, name, true));
            more.push_back(button({.id = "guide_toggle",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "让 agent 来" : "Ask an agent"); }),
                                   .variant = ButtonVariant::ghost,
                                   .on_activated = [s] {
                                       s->menu.set(false);
                                       s->panel.set(true);
                                   }}));
            more.push_back(button({.id = "drawing_toggle",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "绘制" : "Drawing"); }),
                                   .variant = ButtonVariant::ghost,
                                   .on_activated = [s] {
                                       s->menu.set(false);
                                       s->drawing.set(true);
                                   }}));
            nav.push_back(only_when(column({.id = "view_group", .padding = Edges(Insets{12, 0, 0, 0}), .gap = 4, .align = "start"}, std::move(more)),
                                    [s] { return s->compact.get(); }));
        }
        // The places: windows on one canvas, the camera flying between them.
        std::vector<Element> shown;
        Element atlas = canvas({.id = "atlas",
                                .background = Fill(Color::hex(0x05070b)),  // between the windows, darker than any page
                                .label = "openmui's places",
                                .promotion = s->promotion,
                                .scene = s->scene_name,
                                .start = std::format("preset:{}", s->place.peek())});
        atlas.on("activated", [s](const Value& e) {
            std::string key(e.get("key") ? e.get("key")->as_string() : "");
            if (key.ends_with("-label")) key.resize(key.size() - 6);
            if (std::ranges::any_of(all, [&](const Place& p) { return p.id == key; })) s->fly(key);
        });
        shown.push_back(std::move(atlas));
        // Over the place, as the lens says.
        shown.push_back(fades_in(lens({.id = "wireframe", .nodes = s->nodes}), [s] { return s->lens.get() == "tree"; }));
        for (const std::string_view which : {"agent", "test"}) {
            const std::string key(which);
            shown.push_back(fades_in(scroll({.id = key, .padding = Edges(24.0f, 32.0f), .background = "background"},
                                             {text({.id = key + "_text", .text = key == "agent" ? s->agent : s->test, .style = "code"})}),
                                      [s, key] { return s->lens.get() == key; }));
        }
        Element body = row({.id = "body", .min_height = Dim(0.0f), .align = "stretch", .grow = 1},
                           {only_when(column({.id = "nav",
                                              .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(188.0f); }),
                                              .padding = Edges(20.0f, 12.0f),
                                              .gap = 4,
                                              .align = "start",
                                              .background = "surface"},
                                             std::move(nav)),
                                      [s] { return !s->compact.get() || s->menu.get(); }),
                            // (compact: the menu or a panel, at the full width, leaves it none)
                            stack({.id = "view", .min_width = Dim(0.0f), .min_height = Dim(0.0f), .grow = 1}, std::move(shown)),
                            only_when(guide_panel(s), [s] { return s->panel.get(); }),
                            only_when(drawing_panel(s), [s] { return s->drawing.get(); }),
                            only_when(check_panel(s), [s] { return s->checking.get(); })});
        // The foot: the page's time (drag it, play it, play it backwards) and the tree's hash.
        auto say = [s](std::string_view en, std::string_view han) {
            return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
        };
        auto mode_is = [s](std::string_view m) { return s->time_mode.get() == m; };
        Element foot = row({.id = "foot", .padding = Edges(4.0f, 12.0f), .gap = 6, .align = "center", .background = "surface"},
                           {button({.id = "time_back",
                                    .text = say("Backwards", "倒放"),
                                    .variant = computed([mode_is] { return mode_is("backwards") ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                    .on_activated = [s] { s->time_mode.peek() == "backwards" ? s->pause() : s->backwards(); }}),
                            button({.id = "time_play",
                                    .disabled = computed([mode_is] { return mode_is("live"); }),
                                    .text = computed([s] {
                                        const bool zh = s->lang.get() == "zh";
                                        return std::string(s->time_mode.get() == "playing" ? (zh ? "暂停" : "Pause") : (zh ? "播放" : "Play"));
                                    }),
                                    .variant = ButtonVariant::ghost,
                                    .on_activated = [s] { s->time_mode.peek() == "playing" ? s->pause() : s->play(); }}),
                            button({.id = "time_live",
                                    .text = say("Live", "实时"),
                                    .variant = computed([mode_is] { return mode_is("live") ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                    .on_activated = [s] { s->live(); }}),
                            timebar({.id = "time",
                                     .end = s->time_end,
                                     .label = say("The page's time", "页面的时间"),
                                     .live = computed([mode_is] { return mode_is("live"); }),
                                     .marks = s->marks,
                                     .start = s->time_start,
                                     .time = s->playhead,
                                     .window = Site::time_window,
                                     .on_scrubbed = [s](double t) { s->scrub(t); },
                                     .grow = 1}),
                            text({.id = "clock",
                                  .text = computed([s] {
                                      if (s->time_mode.get() == "live") return std::string(s->lang.get() == "zh" ? "现在" : "now");
                                      std::string t = std::format("{:.1f} s", s->playhead.get() - s->time_end.get());
                                      if (t.starts_with('-')) t.replace(0, 1, "−");
                                      return t;
                                  }),
                                  .style = "caption"}),
                            only_when(row({.id = "proof"}, {text({.id = "hash", .text = s->hash, .style = "caption"})}), [s] { return !s->compact.get(); }),
                            only_when(button({.id = "ci_check",
                                    .text = say("Check against CI", "对照 CI"),
                                    .variant = computed([s] { return s->checking.get() ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                    .on_activated = [s] {
                                        const bool open = !s->checking.peek();
                                        if (open && !s->replay) s->want_frame(0);
                                        s->checking.set(open);
                                    }}),
                                      [s] { return !s->compact.get(); })});
        return column({.id = "site", .grow = 1, .background = "background"}, {std::move(bar), std::move(body), std::move(foot)});
    };
    return a;
}

namespace {

// Text as HTML: its markup characters escaped.
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

std::string static_html(std::string_view lang) {
    std::string out = std::format("<main id=\"omui-static\" lang=\"{}\">\n<nav>", lang == "zh" ? "zh-CN" : "en");
    for (const Place& p : all) out += std::format("<a href=\"#/{}{}\">{}</a> ", lang == "zh" ? "zh/" : "", p.id, lang == "zh" ? p.zh : p.en);
    out += "</nav>\n";
    for (const Place& p : all) {
        out += std::format("<section id=\"/{}{}\">\n", lang == "zh" ? "zh/" : "", p.id);
        const bool zh = lang == "zh";
        if (p.id == "home") out += hero_html(zh);
        if (const std::string_view doc = document(p.id, lang); !doc.empty()) {
            // Links to the site's own documents go to their sections, as they go to their places when the program runs.
            std::string html = md::to_html(md::parse(doc));
            for (const auto& [file, place] : {std::pair{"best-practices.md", "practices"}, std::pair{"features.md", "features"}})
                for (std::string from = std::format("href=\"{}", file), to = std::format("href=\"#/{}{}", lang == "zh" ? "zh/" : "", place);;) {
                    const auto at = html.find(from);
                    if (at == std::string::npos) break;
                    const auto end = html.find('"', at + 6);
                    html.replace(at, end - at, to);
                }
            out += html;
        } else if (p.id == "timeline")
            out += zh ? "<h1>时间线编辑器</h1>\n<p>一个可用的时间线编辑器：拖动、修剪、切开，每次编辑都能精确撤销。需要 JavaScript。</p>\n"
                      : "<h1>A timeline editor</h1>\n<p>A working timeline editor: drag, trim and split clips, every edit undone exactly. It needs JavaScript.</p>\n";
        else if (p.id == "wall")
            out += zh ? "<h1>文档墙（S1）</h1>\n<p>所有文档在一块无限画布上：远处是色块，近一些是离屏画好的纹理，再近就是活的文档。需要 JavaScript。</p>\n"
                      : "<h1>The document wall (S1)</h1>\n<p>Every document on one infinite canvas: far away a block, nearer a texture drawn offscreen, close up a live document. It needs JavaScript.</p>\n";
        else if (p.id == "moves") {
            // The move list for readers without the program (and search engines): every fighter's frame data, measured here as the page is.
            out += zh ? "<h1>出招表：帧数据，实测</h1>\n<p>每个数字都是游戏自己实测的：每个招式对着站立的、再对着防御的木桩，从刚好够得着的距离出一遍。逐帧演示需要 JavaScript。</p>\n"
                      : "<h1>Move list — frame data, measured</h1>\n<p>Every number is measured by the game itself: each move played against a dummy standing still, then guarding, "
                        "from where it reaches. Seeing a move frame by frame needs JavaScript.</p>\n";
            const fighting::Roster& r = fighting::roster();
            for (std::size_t i = 0; i < r.names.size(); ++i) {
                const gongfu::Fighter& f = r.fighters[i];
                out += std::format("<h2>{}</h2>\n<table>\n<tr><th>{}</th><th>{}</th><th>{}</th><th>{}</th><th>{}</th><th>{}</th><th>{}</th></tr>\n",
                                   escaped(fighter_name(r.names[i], f.def.name, zh)), zh ? "招式" : "Move", zh ? "发生" : "Startup", zh ? "持续" : "Active",
                                   zh ? "收招" : "Recovery", zh ? "命中" : "On hit", zh ? "被防" : "On block", zh ? "伤害" : "Damage");
                for (std::size_t m = 0; m < f.def.moves.size(); ++m) {
                    const gongfu::MoveDef& md = f.def.moves[m];
                    if (!md.asked) continue;
                    const auto d = gongfu::training::frame_data(r.system, f, static_cast<std::int32_t>(m));
                    const std::string kind = d.throw_ ? (zh ? "投" : "throw") : d.air ? (zh ? "空中" : "air") : "";
                    out += std::format("<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>\n", escaped(md.name), d.startup,
                                       d.active, d.recovery, kind.empty() ? advantage(d.on_hit, d.knockdown, zh) : kind,
                                       kind.empty() ? advantage(d.on_block, false, zh) : "—", d.damage);
                }
                out += "</table>\n";
            }
        } else if (p.id == "styles")
            out += zh ? "<h1>风格工坊（S2）</h1>\n<p>同一个真实的界面，放进 openmui 的每个风格包：玻璃、霓虹、复古、水墨、液态；当前的风格包可以边看边改。变的只有像素：每个风格包下语义树都一样。需要 JavaScript。</p>\n"
                      : "<h1>The style workshop (S2)</h1>\n<p>One real interface in each of openmui's style packs — glass, neon, retro, ink, liquid — and the pack shown edited as you go. "
                        "Only pixels change: the semantic tree is the same in every pack. It needs JavaScript.</p>\n";
        else if (p.id == "hello")
            out += std::format("<h1>{}</h1>\n<p>{}</p>\n<pre><code>{}</code></pre>\n", zh ? "示例 01：你好，立即模式" : "Example 01 — hello, immediate mode",
                               zh ? "一个完整的 openmui 程序。运行它需要 JavaScript；它的全部源码如下。" : "A whole openmui program. Running it needs JavaScript; here is all of its source.",
                               escaped(hello::hello_source));
        out += "</section>\n";
    }
    out += "</main>\n";
    return out;
}

std::optional<std::string> inject(std::string_view page, std::string_view fragment, std::string_view lang) {
    std::string out(page);
    const auto canvas = out.find("<canvas id=\"canvas\"");
    const auto head = out.find("</head>");
    const auto loader = out.rfind("<script src=\"");
    if (canvas == std::string::npos || head == std::string::npos || loader == std::string::npos || loader < canvas) return std::nullopt;
    if (out.contains("id=\"omui-static\"")) return std::nullopt;  // once
    // The program's own script last: before it, hide what was read without it once it runs.
    out.insert(loader,
               "<script>(function () { var f = Module.onRuntimeInitialized; Module.onRuntimeInitialized = function () {"
               " var s = document.getElementById('omui-static'); if (s) s.hidden = true; if (f) f(); }; })();</script>\n");
    out.insert(canvas, std::string(fragment));
    // The static page in the program's own fonts: the files the page already lists for it (omuiFonts).
    std::string faces;
    for (const auto& [stem, family] : {std::pair{"Inter", "Inter"}, std::pair{"JetBrainsMono", "JetBrains Mono"}, std::pair{"NotoSansSC", "Noto Sans SC"}}) {
        const std::string key = std::format("\"{}.ttf\", \"", stem);
        const auto at = out.find(key);
        if (at == std::string::npos) continue;
        const auto from = at + key.size(), to = out.find('"', from);
        faces += std::format("@font-face{{font-family:'{}';src:url({});font-display:swap}}", family, out.substr(from, to - from));
    }
    out.insert(head,
               "<style>" + faces +
                   "#omui-static{position:fixed;inset:0;z-index:1;overflow:auto;padding:24px 40px;background:#0b0f16;color:#d6dde8;"
                   "font:16px/1.6 Inter,'Noto Sans SC',system-ui,sans-serif;user-select:text}#omui-static[hidden]{display:none}"
                   "#omui-static nav{margin-bottom:24px}#omui-static a{color:#8ab4ff}#omui-static nav a{margin-right:16px}"
                   "#omui-static section{max-width:860px;margin-bottom:48px}#omui-static code{font:14px 'JetBrains Mono',monospace;background:#161c27;"
                   "padding:1px 4px;border-radius:3px}#omui-static pre{white-space:pre-wrap}#omui-static table{border-collapse:collapse}"
                   "#omui-static td,#omui-static th{border:1px solid #2a3344;padding:4px 8px}</style>\n"
                   "<meta name=\"description\" content=\"openmui: one semantic tree, many renderings — pixels, accessibility, agents and tests.\">\n");
    // A reader may zoom the page (WCAG 1.4.4): the packer's viewport forbids it.
    if (const auto at = out.find(", maximum-scale=1, user-scalable=no"); at != std::string::npos) out.erase(at, std::string_view(", maximum-scale=1, user-scalable=no").size());
    if (const auto html = out.find("<html lang=\""); html != std::string::npos) {
        const auto end = out.find('"', html + 12);
        out.replace(html + 12, end - (html + 12), lang == "zh" ? "zh-CN" : "en");
    }
    return out;
}

}  // namespace omui::site
