// The site (C2: positioning §2, the design language): bright and editorial —
// a bar with the mark and the places, one place at a time below it (the home,
// the components, the documents, getting started, a working timeline editor),
// scrolled as a page; over the place, as the lens says, the tree's wireframe,
// the JSON an agent reads or the assertions a test writes; at the foot, the
// lenses, the page's time and the tree's hash. The address follows the place
// (the route), and the browser's back and forward move it.
module openmui.site;

import std;
import openmui;
import openmui.hash;
import openmui.ui.markdown;
import openmui.ui.timeline;
import openmui.timeline;
import openmui.theme;
import openmui.theme.styles;
import hello;
import openmui.semantic;
import openmui.trace;

namespace omui::site {

namespace {

constexpr Log site_log{"site"};

// Where the places are in the tree (the window mounts the site as "main").
constexpr std::string_view place_root = "main/body/view/";

constexpr std::array<Place, 6> all{{{"home", "Home", "首页"},
                                     {"components", "Components", "组件"},
                                     {"features", "Features", "特性"},
                                     {"practices", "Practices", "实践"},
                                     {"hello", "Get started", "开始使用"},
                                     {"timeline", "Timeline editor", "时间线编辑器"}}};

// The widest a place's content is (the design language's 1200, less its margins).
constexpr float content_width = 1120;

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
struct Site {
    Runtime*            runtime = nullptr;
    Signal<std::string> place = signal(std::string("home")), lang = signal(std::string("en")), lens = signal(std::string("pixels"));
    Signal<Value>       nodes = signal(Value{});
    Signal<std::string> agent = signal(std::string()), test = signal(std::string()), hash = signal(std::string());

    Signal<bool>        panel = signal(false);     // the agent panel open
    Signal<bool>        settings = signal(false);  // the settings panel open
    // A narrow window (a phone): the places and the switches in a menu, a panel in the place's place.
    static constexpr float compact_below = 900;
    Signal<bool>        compact = signal(false), menu = signal(false);
    Signal<float>       width = signal(1280.0f);  // the window's width, logical px
    float               room = 1280;              // the place's: the view's width (the window's, less an open panel's)
    // Its band (a place's page is built again when it changes): a phone, a tablet, a laptop, a wide screen.
    Signal<int>         band = signal(2);
    [[nodiscard]] static int band_of(float w) noexcept { return w < 700 ? 0 : w < 1100 ? 1 : w < 1400 ? 2 : 3; }

    // The settings (the person's, where the site exposes them: D-94's three layers).
    Signal<std::string> renderer_choice = signal(std::string("automatic")), settings_note = signal(std::string());
    Signal<std::string> motion_level = signal(std::string("full")), motion_choice = signal(std::string("default")),
                        effects = signal(std::string("auto"));
    Signal<bool>        dark = signal(false);

    Signal<int>         switches = signal(0);   // the language switched this many times
    double              switched_at = -1;       // when it last did (the runtime's seconds)
    Signal<std::string> timeline = signal(std::string(sample));  // the timeline as last edited
    Signal<std::int32_t> timeline_frame = signal(45);            // its playhead (the title on screen)
    Signal<bool>        timeline_playing = signal(false);
    // The timeline read again only when its text changed: what the monitor shows at the playhead.
    std::string                      timeline_read_from;
    std::optional<omui::timeline::Timeline> timeline_model;
    const omui::timeline::Timeline* timeline_now(const std::string& text) {
        if (text != timeline_read_from || !timeline_model) {
            timeline_read_from = text;
            auto t = omui::timeline::read_timeline(text, "site.timeline.omui.toml");
            timeline_model = t ? std::optional(std::move(*t)) : std::nullopt;
        }
        return timeline_model ? &*timeline_model : nullptr;
    }
    // Played: the playhead on a frame at a time (30 a second), to the timeline's end, then back to its start.
    void play_timeline() {
        (void)runtime->after(seconds(1.0 / 30), [this] {
            if (!timeline_playing.peek()) return;
            const omui::timeline::Timeline* t = timeline_now(timeline.peek());
            const std::int64_t end = t ? omui::timeline::frame_at(t->rate, t->duration()) : 270;
            const std::int32_t next = timeline_frame.peek() + 1;
            if (next > end) {
                timeline_frame.set(0);
                timeline_playing.set(false);
                return;
            }
            timeline_frame.set(next);
            play_timeline();
        });
    }
    // The level (0–1, its fades) the picture clip named `name` plays at the frame shown; 0 when it does not.
    float clip_level(std::string_view name, std::int32_t frame) {
        const omui::timeline::Timeline* t = timeline_now(timeline.get());
        if (!t) return 0;
        std::vector<omui::timeline::Playing> now;
        omui::timeline::playing(*t, omui::timeline::frame_time(t->rate, frame), now);
        for (const auto& p : now)
            if (t->tracks[p.track].clips[p.clip].name == name) return p.level;
        return 0;
    }
    hello::State        hello;                 // example 01's, as the person left it
    Hero                hero;                  // the home page's opening: its checklist, the node its panels show
    HeroSite            hero_site;             // what the opening reads of the site
    Gallery             gallery;               // the components page: the style, the code's language, the samples' values
    GallerySite         gallery_site;
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
    std::unique_ptr<Guide> guide;

    Site() = default;
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;

    // Where the tree is read: the place's page.
    [[nodiscard]] std::string root() const { return std::string(place_root) + place.get() + "/page"; }

    // The route a place has in a language: its address's hash ("features", "zh/features"; the English home, none).
    [[nodiscard]] static std::string route_of(std::string_view place, std::string_view lang) {
        const std::string p = place == "home" ? std::string() : std::string(place);
        if (lang != "zh") return p;
        return p.empty() ? std::string("zh") : "zh/" + p;
    }
    void show_route() {
        if (runtime) runtime->set_route(route_of(place.peek(), lang.peek()));
    }
    // A route the person moved to (back, forward, a typed address): its place and its language.
    void follow_route(std::string_view route) {
        std::string_view r = route;
        std::string l = "en";
        if (r == "zh" || r.starts_with("zh/")) l = "zh", r = r.size() > 2 ? r.substr(3) : std::string_view();
        const std::string p = r.empty() ? std::string("home") : std::string(r);
        if (!std::ranges::any_of(all, [&](const Place& x) { return x.id == p; })) return;
        if (l != lang.peek()) {
            lang.set(l);
            switches.set(switches.peek() + 1);
            switched_at = runtime ? runtime->now().count() : 0;
        }
        go(p);
    }
    // To a place: shown, its page from its top; the address follows.
    void go(std::string_view to) {
        // On a phone a panel or the menu covers the place: going somewhere puts them away, so it is seen.
        if (compact.peek()) {
            menu.set(false);
            panel.set(false);
            settings.set(false);
            checking.set(false);
        }
        if (to != place.peek()) {
            const auto it = std::ranges::find_if(all, [&](const Place& p) { return p.id == to; });
            const bool zh = lang.peek() == "zh";
            if (it != all.end()) mark(std::string(zh ? it->zh : it->en));
        }
        place.set(std::string(to));
        show_route();
        soon();
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
        // The strip as it is drawn where it was grabbed (it does not move under the pointer); a time outside it, its edge.
        t = std::clamp(t, time_start.peek(), std::max(time_start.peek(), time_end.peek()));
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
        const std::string at = root();
        const Value snapshot = runtime->find(at) ? tree.snapshot({.root = at}).to_value() : Value{};
        nodes.set(snapshot.get("nodes") ? *snapshot.get("nodes") : Value{});
        // Its ids shown from the place (`…/page/doc/b0`).
        std::string place_path = at;
        if (place_path.ends_with("/page")) place_path.resize(place_path.size() - 5);
        agent.set(agent_text(snapshot, place_path));
        test.set(test_text(snapshot, place_path));
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
// from the tree) otherwise, its state kept; shown as `shown` (a stack stays one).
Element only_when(Element e, std::function<bool()> cond, std::string_view shown = "flex") {
    e.prop("display", derive([cond = std::move(cond), shown = std::string(shown)] { return cond() ? shown : std::string("none"); }));
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
        // Only right after a switch: a place shown later shows its text at rest.
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
            s.go(place);
            return;
        }
    if (s.runtime) (void)s.runtime->open_url(href);
}

// A stage of the window's frame (D-66) after paint: the window's width
// (compact or not, the place's room), the route the person moved, the page's
// time; and when the tree changed (a resize, the fonts arriving, an agent's
// edit), the place read again once the frame is done.
class TreeWatch final : public pipeline::Stage {
public:
    explicit TreeWatch(Site& site) : site_(site) {}
    [[nodiscard]] std::vector<pipeline::Port> reads() const override { return {}; }
    [[nodiscard]] std::vector<pipeline::Port> writes() const override { return {}; }
    std::expected<void, std::string> run(pipeline::Context&) override {
        Runtime& rt = *site_.runtime;
        // A phone, or a window made narrow: compact (and back).
        const float w = rt.size().width;
        if (const bool narrow = w < Site::compact_below; narrow != site_.compact.peek()) {
            site_.compact.set(narrow);
            if (!narrow) site_.menu.set(false);
        }
        if (std::abs(w - site_.width.peek()) >= 1) site_.width.set(w);
        // The place's room: the view as laid out (a panel open beside it takes some); its band, when it changes, builds the page again.
        if (const NodeId v = rt.find("main/body/view"); rt.valid(v) && rt.bounds(v).width > 0) site_.room = rt.bounds(v).width;
        else site_.room = w;
        if (const int b = Site::band_of(site_.room); b != site_.band.peek()) site_.band.set(b);
        // The person moved the route (back, forward): the place and the language it names.
        if (rt.route_revision() != route_seen_) {
            route_seen_ = rt.route_revision();
            if (rt.route() != Site::route_of(site_.place.peek(), site_.lang.peek())) {
                const std::string route = rt.route();
                (void)rt.after(seconds(0.0), [this, route] { site_.follow_route(route); });
            }
        }
        site_.tick_time();
        if (const auto f = std::exchange(site_.ref_wanted, std::nullopt)) site_.check_frame(*f);
        if (rt.semantic_revision() != site_.seen && !queued_) {
            queued_ = true;
            (void)rt.after(seconds(0.0), [this] {
                queued_ = false;
                site_.look();
            });
        }
        return {};
    }

private:
    Site&         site_;
    bool          queued_ = false;
    std::uint64_t route_seen_ = 0;
};

// A lens's switch: highlighted when it is the lens.
Element lens_button(const std::shared_ptr<Site>& s, std::string_view id, std::string_view name) {
    const std::string key(id), label(name);
    Element b = button({.id = "lens_" + key,
                        .text = std::string(label),
                        .variant = computed([s, key] { return s->lens.get() == key ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                        .on_activated = [s, key, label] {
                            if (s->lens.peek() != key) s->mark(std::format("{} {}", s->lang.peek() == "zh" ? "镜头" : "Lens", label));
                            s->lens.set(key);
                            s->menu.set(false);
                            s->soon();
                        }});
    b.prop("selected", derive([s, key] { return s->lens.get() == key; }));
    return b;
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

std::string agent_text(const Value& snapshot, std::string_view root) {
    std::string out;
    const std::string under = root.empty() ? std::string() : std::format("\"{}/", root);
    if (!root.empty())
        out += std::format("// openmui.tree@0.1's snapshot of {}, a node a line (its ids are paths: \"…/\" is that place).\n", root);
    if (const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr)
        for (const Value& n : *nodes) {
            std::string line = n.json();
            for (std::size_t at = 0; !under.empty() && (at = line.find(under, at)) != std::string::npos;) {
                line.replace(at, under.size(), "\"…/");
                at += 5;
            }
            out += line + "\n";
        }
    return out;
}

std::string test_text(const Value& snapshot, std::string_view root) {
    std::string out = "// What a test of this page asserts, read from the same tree:\n";
    if (!root.empty()) out += std::format("auto node = [&](std::string_view path) {{ return *tree.node(std::format(\"{}/{{}}\", path)); }};\n", root);
    const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr;
    if (!nodes) return out;
    std::size_t lines = 0;
    for (const Value& n : *nodes) {
        std::string id = n.get("id") ? std::string(n.get("id")->as_string()) : std::string();
        if (!root.empty()) {
            if (!id.starts_with(std::string(root) + "/")) continue;  // the place itself, and what is not under it
            id.erase(0, root.size() + 1);
        }
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
        {.id = "guide", .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(380.0f); }), .min_width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(380.0f); }), .padding = Edges(20.0f, 16.0f), .gap = 10, .shrink = 0, .background = "surface"},
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

Element check_panel(const std::shared_ptr<Site>& s) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    return column({.id = "check", .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(360.0f); }), .min_width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(360.0f); }), .padding = Edges(16.0f, 16.0f), .gap = 10, .shrink = 0, .background = "surface"},
                  {text({.id = "title", .text = say("The reference run", "参考回放"), .style = "heading"}),
                   text({.id = "about",
                         .text = say("A few seconds of this site, played here offscreen at CI's size (1280 × 800): its home, the features, the language switched, a lens. "
                                     "This machine draws each frame and hashes its pixels; CI pinned the same frames' hashes.",
                                     "这个网站的几秒钟，在这里离屏按 CI 的尺寸（1280 × 800）重放：首页、特性、切换语言、切换镜头。"
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


// The settings: how the page moves and draws, the person's where the site
// exposes it (D-94's three layers: the device, the developer, the person) —
// itself a demonstration of the framework's.
Element settings_panel(const std::shared_ptr<Site>& s) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    auto labels = [s](std::vector<std::string> en, std::vector<std::string> han) {
        return computed([s, en = std::move(en), han = std::move(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    auto effects_tier = [](std::string_view v) -> std::optional<pipeline::Tier> {
        if (v == "t1") return pipeline::Tier::t1;
        if (v == "t2") return pipeline::Tier::t2;
        if (v == "t3") return pipeline::Tier::t3;
        if (v == "t4") return pipeline::Tier::t4;
        return std::nullopt;
    };
    // A choice with its name above it (the name the select is read by, drawn).
    auto labelled = [say](std::string id, std::string_view en, std::string_view han, SelectOptions o) {
        o.id = id;
        o.label = say(en, han);
        return column({.id = id + "_field", .gap = 6},
                      {text({.id = "name", .text = say(en, han), .style = "label", .color = "text_muted"}), select(std::move(o))});
    };
    return column({.id = "settings",
                   .width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(340.0f); }),
                   .min_width = computed([s] { return s->compact.get() ? Dim(layout::pct(100)) : Dim(340.0f); }),
                   .padding = Edges(20.0f, 20.0f),
                   .gap = 14,
                   .shrink = 0,
                   .background = "surface",
                   .label = "Settings"},
                  {text({.id = "title", .text = say("Settings", "设置"), .style = "heading"}),
                   text({.id = "about",
                         .text = say("Yours, as far as this site lets you: a program on openmui says which of the framework's choices its users make.",
                                     "这些是你的选择，以这个网站开放的为限：基于 openmui 的程序自己决定把框架的哪些选择交给用户。"),
                         .style = "caption",
                         .color = "text_muted"}),
                   labelled("theme", "Theme", "主题", {.labels = labels({"Light", "Dark"}, {"浅色", "深色"}),
                           .options = std::vector<std::string>{"light", "dark"},
                           .value = computed([s] { return std::string(s->dark.get() ? "dark" : "light"); }),
                           .on_changed = [s](std::string v) {
                               s->dark.set(v == "dark");
                               if (s->runtime)
                                   if (auto t = theme::library().compose(std::vector<std::string>{v == "dark" ? "site_dark" : "site"}); t) {
                                       s->runtime->crossfade(seconds(0.3));
                                       s->runtime->set_theme(std::move(*t));
                                   }
                           }}),
                   labelled("motion", "Motion", "动效", {.labels = labels({"Full", "Reduced (short fades)", "Off"}, {"完整", "精简（短暂的淡化）", "关闭"}),
                           .options = std::vector<std::string>{"full", "reduced", "off"},
                           .value = s->motion_level,
                           .on_changed = [s](std::string v) {
                               s->motion_level.set(v);
                               if (s->runtime) s->runtime->set_motion_level(v == "off" ? MotionLevel::off : v == "reduced" ? MotionLevel::reduced : MotionLevel::full);
                           }}),
                   labelled("motion_style", "How things move", "动的方式", {.labels = labels({"As each control says", "Simple", "Liquid", "Ink"}, {"按各控件的设定", "简洁", "液态", "水墨"}),
                           .options = std::vector<std::string>{"default", "simple", "liquid", "ink"},
                           .value = s->motion_choice,
                           .on_changed = [s](std::string v) {
                               s->motion_choice.set(v);
                               if (s->runtime) s->runtime->set_motion_choice(v == "default" ? std::string() : v);
                           }}),
                   labelled("effects", "Effects", "效果", {.labels = labels({"As this device can (automatic)", "Embedded (T1)", "The Web (T2)", "A computer (T3)", "A discrete GPU (T4)"},
                                            {"按这台设备的能力（自动）", "嵌入式（T1）", "网页（T2）", "电脑（T3）", "独立显卡（T4）"}),
                           .options = std::vector<std::string>{"auto", "t1", "t2", "t3", "t4"},
                           .value = s->effects,
                           .on_changed = [s, effects_tier](std::string v) {
                               s->effects.set(v);
                               if (!s->runtime) return;
                               Adaptation a = s->runtime->adaptation();
                               a.effects = effects_tier(v).value_or(a.tier);
                               a.reason = v == "auto" ? std::string("the device's") : std::string("the person's setting");
                               s->runtime->set_adaptation(a);
                           }}),
                   labelled("renderer", "Who draws", "谁来画", {.labels = labels({"Automatic (a GPU when there is one)", "The GPU", "The CPU (the reference)"},
                                            {"自动（有 GPU 就用 GPU）", "GPU", "CPU（参考实现）"}),
                           .options = std::vector<std::string>{"automatic", "gpu", "cpu"},
                           .value = s->renderer_choice,
                           .on_changed = [s](std::string v) {
                               s->renderer_choice.set(v);
                               if (auto r = choose_renderer(v); !r) s->settings_note.set(r.error());
                               else s->settings_note.set(std::string());
                           }}),
                   text({.id = "now",
                         .text = computed([s] {
                             const std::string by(s->runtime ? s->runtime->renderer() : std::string_view("cpu"));
                             const std::string name = by == "cpu" ? "the CPU" : by == "webgpu" ? "WebGPU" : by == "webgl" ? "WebGL 2" : by;
                             return s->lang.get() == "zh" ? std::format("现在由 {} 绘制。", by == "cpu" ? "CPU" : name) : std::format("Drawn by {} now.", name);
                         }),
                         .style = "caption"}),
                   text({.id = "note", .text = s->settings_note, .style = "caption", .color = "text_muted"})});
}

// Getting started: example 01, the shortest openmui program, running beside its own source.
Element hello_page(const std::shared_ptr<Site>& s, float room) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    const bool narrow = room < 900;
    Element intro = column({.id = "intro", .width = narrow ? Dim(layout::pct(100)) : Dim(440.0f), .gap = 16},
                           {retyped(text({.id = "title", .text = say("Get started", "开始使用"), .style = "title"}), s),
                            text({.id = "about",
                                  .text = say("Example 01, a whole openmui program, running here: type a name, press the button. Its interface is declared each "
                                              "frame in plain C++; an agent finds the field by its role, and renaming the button keeps its id.",
                                              "示例 01，一个完整的 openmui 程序，就在这里运行：输入名字，按按钮。界面每一帧用普通的 C++ 声明；agent 按角色找到输入框，"
                                              "按钮改名后 id 不变。"),
                                  .style = "body",
                                  .size = 20.0f}),
                            box({.id = "run", .width = narrow ? Dim(layout::pct(100)) : Dim(440.0f), .height = Dim(280.0f), .background = "surface",
                                 .border_color = "border", .border_width = 1, .radius = 14, .clip = true},
                                {hello::app(s->hello).view()})});
    // The source as written: lines kept whole (indentation is meaning), scrolled sideways when long.
    Element code = scroll({.id = "code", .horizontal = true, .vertical = narrow, .grow = 1, .padding = Edges(20.0f, 24.0f), .background = "surface_variant"},
                          {text({.id = "source", .text = std::string(hello::hello_source), .style = "code", .wrap = false})});
    if (narrow) return column({.id = "page", .gap = 24}, {std::move(intro), std::move(code)});
    return row({.id = "page", .gap = 40, .align = "stretch"}, {std::move(intro), std::move(code)});
}

// The timeline editor: a working one, its program monitor above it.
Element timeline_page(const std::shared_ptr<Site>& s, float room) {
    auto words = [s](std::string_view en, std::string_view han) -> Prop<std::string> {
        return computed([s, en, han] { return std::string(s->lang.get() == "zh" ? han : en); });
    };
    Element editor = timeline_editor({.id = "editor", .document = s->timeline.peek(), .frame = Prop<std::int32_t>(s->timeline_frame), .zoom = 60});
    // Its own history (S-12); an edit is kept for the next time it is shown, and changes the tree a lens shows.
    editor.prop("undo_scope", Source{Value(true), {}, {}});
    editor.on("edited", [s](const Value& e) {
        if (const Value* d = e.get("document")) s->timeline.set(std::string(d->as_string()));
        s->soon();
    });
    // The program monitor: what the timeline plays at its playhead — the title card fading as its clip does,
    // the fight's two figures, the music's level — and the time; Play runs the playhead at the timeline's rate.
    auto level = [s](std::string name) -> Prop<float> { return computed([s, name] { return s->clip_level(name, s->timeline_frame.get()); }); };
    const bool slim = room < 760;
    const float mw = std::min(380.0f * 16 / 9, room - (slim ? 0.0f : 160.0f));
    Element fighters = painter({.id = "figures",
                                .width = Dim(mw * 0.5f),
                                .height = Dim(mw * 9 / 16 * 0.6f),
                                .role = Role::none,
                                .paint = [](PaintContext& ctx) {
                                    // Two figures squared up on a floor line, and the light of a blow between them.
                                    const float w = ctx.size.width, h = ctx.size.height;
                                    auto figure = [&](float x, float dir, Color c) {
                                        ctx.list.fill_round_rect({x - w * 0.05f, h * 0.18f, w * 0.1f, w * 0.1f}, CornerRadii::all(w * 0.05f), render::Paint::solid(c));
                                        render::Path body;
                                        body.move_to({x, h * 0.32f}).line_to({x - dir * w * 0.02f, h * 0.62f}).line_to({x - dir * w * 0.09f, h * 0.92f});
                                        body.move_to({x - dir * w * 0.02f, h * 0.62f}).line_to({x + dir * w * 0.07f, h * 0.92f});
                                        body.move_to({x, h * 0.4f}).line_to({x + dir * w * 0.16f, h * 0.36f});
                                        ctx.list.stroke_path(body, render::StrokeStyle{.width = w * 0.035f, .cap = render::LineCap::round, .join = render::LineJoin::round},
                                                             render::Paint::solid(c));
                                    };
                                    figure(w * 0.32f, 1, Color::hex(0x60a5fa));
                                    figure(w * 0.68f, -1, Color::hex(0xf59e0b));
                                    ctx.list.fill_round_rect({w * 0.47f, h * 0.31f, w * 0.06f, w * 0.06f}, CornerRadii::all(w * 0.03f), render::Paint::solid(Color::hex(0xffffff, 0.85f)));
                                    ctx.list.fill_rect({0, h * 0.94f, w, 2}, Color::hex(0xffffff, 0.25f));
                                }});
    Element monitor = stack({.id = "monitor", .width = Dim(mw), .height = Dim(mw * 9 / 16), .background = Fill(Color::hex(0x05070b)), .radius = 14, .clip = true,
                             .label = std::string("Program monitor")},
                            {column({.id = "title_card", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .gap = 8, .justify = "center", .align = "center",
                                     .opacity = level("title")},
                                    {brand_mark("mark", mw * 0.14f), text({.id = "name", .text = std::string("a short"), .style = "title", .color = Fill(Color::hex(0xe6edf7))})}),
                             column({.id = "fight_card", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .justify = "center", .align = "center",
                                     .opacity = level("the fight")},
                                    {std::move(fighters)}),
                             column({.id = "time", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .padding = Edges(10.0f, 14.0f), .justify = "end"},
                                    {row({.gap = 10, .align = "center"},
                                         {text({.id = "timecode",
                                                .text = computed([s] {
                                                    const std::int32_t f = s->timeline_frame.get();
                                                    return std::format("{:02}:{:02}", f / 30, f % 30);
                                                }),
                                                .style = "code",
                                                .color = Fill(Color::hex(0xe6edf7, 0.8f))}),
                                          box({.id = "music", .width = Dim(mw * 0.25f), .height = Dim(4.0f), .background = Fill(Color::hex(0x10b981)), .radius = 2,
                                               .opacity = level("main theme")})})})});
    ButtonOptions play{.id = "play", .text = words("Play", "播放"), .variant = ButtonVariant::secondary};
    play.text = computed([s] { return std::string(s->timeline_playing.get() ? (s->lang.get() == "zh" ? "暂停" : "Pause") : (s->lang.get() == "zh" ? "播放" : "Play")); });
    play.on_activated = [s] {
        const bool on = !s->timeline_playing.peek();
        s->timeline_playing.set(on);
        if (on && s->runtime) s->play_timeline();
    };
    return column({.id = "page", .gap = 16},
                  {retyped(text({.id = "title", .text = words("A timeline editor", "时间线编辑器"), .style = "title"}), s),
                   text({.id = "how",
                         .text = words("Drag a clip, trim its edge, press S to split it at the playhead, Ctrl+Z to undo. Every edit has its exact inverse; switch the lens to see the same timeline as an agent reads it.",
                                       "拖动片段、拖边修剪、按 S 在播放头处切开，Ctrl+Z 撤销。每次编辑都有它精确的反向；切换镜头，看 agent 读到的同一份时间线。"),
                         .style = "body",
                         .size = 20.0f,
                         .max_width = Dim(760.0f)}),
                   slim ? column({.id = "program", .gap = 10, .align = "start"}, {std::move(monitor), button(std::move(play))})
                        : row({.id = "program", .gap = 16, .align = "end"}, {std::move(monitor), button(std::move(play))}),
                   std::move(editor)});
}

// A document: its text, and where a wide window has room its sections down its right, each taking the reader to it.
Element document_page(const std::shared_ptr<Site>& s, const std::string& id, float room) {
    Element doc = retyped(markdown({.id = "doc",
                                    .origin = MarkdownOrigin::app,
                                    .source = computed([s, id] { return std::string(document(id, s->lang.get())); }),
                                    .on_link_activated = [s](std::string href) { follow(*s, href); },
                                    .max_width = Dim(720.0f)}),
                          s, Arrival::lines);
    if (id == "home") return std::move(doc);
    auto rail = [s, id](std::string_view lang) {
        std::vector<Element> entries;
        entries.push_back(text({.id = "label", .text = std::string(lang == "zh" ? "本页内容" : "On this page"), .style = "caption", .color = "text_muted"}));
        const md::Document d = md::parse(document(id, lang));
        std::size_t i = 0;
        md::walk(d.root, [&](const md::Block& b) {
            if (b.kind != md::Kind::heading || b.level > 2) return;
            ButtonOptions o{.id = std::format("s{}", i++), .text = md::plain_text(b), .variant = ButtonVariant::ghost};
            o.max_width = Dim(260.0f);
            o.align_self = "start";
            o.on_activated = [s, id, anchor = b.anchor] {
                if (!s->runtime) return;
                Invocation call;
                call.args = Value{}.set("anchor", anchor);
                (void)s->runtime->act(s->runtime->find(std::string(place_root) + id + "/page/read/doc"), "scroll_to_anchor", call);
            };
            entries.push_back(button(std::move(o)));
        });
        return entries;
    };
    std::vector<Element> both;
    both.push_back(std::move(doc));
    if (room >= 1100) {
        Element contents = column({.id = "contents", .width = Dim(280.0f), .gap = 2, .label = "Sections"}, {dynamic([s, rail] { return rail(s->lang.get()); })});
        contents.prop("shrink", Source(Value(0.0)));
        both.push_back(std::move(contents));
    }
    // Narrower, one column: the document as wide as there is room (at most its measure).
    return room >= 1100 ? row({.id = "read", .min_width = Dim(0.0f), .gap = 64, .align = "start"}, std::move(both))
                        : column({.id = "read", .min_width = Dim(0.0f)}, std::move(both));
}

// A place: its page, scrolled, its content as wide as the design's measure, centred.
Element place_page(const std::shared_ptr<Site>& s, const std::string& id) {
    const float view = s->room;
    const bool narrow = view < 700;
    const float margin = narrow ? 20 : 48;
    const float room = std::min(content_width, view - 2 * margin);
    std::vector<Element> content;
    if (id == "home") {
        content.push_back(hero(s->hero, s->hero_site, true, room));
        content.push_back(document_page(s, id, room));
    } else if (id == "components") {
        content.push_back(gallery(s->gallery, s->gallery_site, true, room));
    } else if (id == "hello") {
        content.push_back(hello_page(s, room));
    } else if (id == "timeline") {
        content.push_back(timeline_page(s, room));
    } else {
        content.push_back(document_page(s, id, room));
    }
    Element body = id == "hello" || id == "timeline" ? std::move(content.front())
                                                     : column({.id = "page", .gap = 64}, std::move(content));
    // The design's measure, centred in the window (`…/<place>/page` is where its tree is read).
    body.prop("width", Source(Value("100%")));
    body.prop("max_width", Source(Value(static_cast<double>(content_width))));
    body.prop("align_self", Source(Value("center")));
    // Filling the view (a stack: the place under the lens's renderings).
    return scroll({.id = id, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .padding = Edges(narrow ? 24.0f : 56.0f, margin), .background = "background"},
                  {std::move(body)});
}

}  // namespace

AppSpec app(std::string_view place, std::string_view lang) {
    AppSpec a;
    a.id = "org.openmui.site";
    a.title = "openmui — one tree, many renderings";
    a.size = {1280, 800};
    // The site's own theme (its tokens: the design language), over the framework's default themes and the style packs
    // (the components page shows them in a scope of its own).
    if (auto r = themes::add_styles(); !r) site_log.warn("the style packs: {}", r.error().empty() ? std::string() : r.error().front().text());
    if (auto r = theme::library().add(embedded(files::theme_files, "site.theme.omui.toml"), "site.theme.omui.toml"); !r)
        site_log.warn("the site's theme: {}", r.error().empty() ? std::string() : r.error().front().text());
    if (auto t = theme::library().compose(std::vector<std::string>{"site"}); t) a.theme = std::move(*t);
    else {
        site_log.warn("the site's theme: {}", t.error());
        a.theme = Theme::light();
    }
    // The page's time is a live readout of the clock: it is drawn where the time is, never eased there
    // (universal motion: a morph is for a change, not for a clock ticking).
    a.theme.motion_rules.push_back({"timebar", "update", MotionSpec{.kind = MotionSpec::Kind::none}});
    // Drawn by the machine's GPU where it has one (WebGPU, else WebGL 2; a
    // software adapter counts as none), else by the CPU. The CPU's pixels are
    // the reference, the same bytes on every machine — the check against CI
    // draws its frames with the CPU whoever draws the page — and a GPU's are
    // within tolerances of them.
    a.renderer = "automatic";
    auto s = std::make_shared<Site>();
    if (std::ranges::any_of(all, [&](const Place& p) { return p.id == place; })) s->place.set(std::string(place));
    s->lang.set(lang == "zh" ? "zh" : "en");
    a.setup = [s](Runtime& rt) {
        s->runtime = &rt;
        register_type(component_type<Lens>());
        register_base_components();
        register_type(component_type<Timebar>());
        register_type(component_type<FrameMeter>());
        s->hero_site = {.runtime = &s->runtime,
                        .lang = &s->lang,
                        .nodes = &s->nodes,
                        .root = [] { return std::string(place_root) + "home/page"; },
                        .go = [p = s.get()](std::string id) { p->go(id); },
                        .tree_lens = [p = s.get()] {
                            if (p->lens.peek() != "tree") p->mark(p->lang.peek() == "zh" ? "镜头 Tree" : "Lens Tree");
                            p->lens.set("tree");
                            p->soon();
                        }};
        // The components page in a theme scope of its own: plain (the site's theme), or a style pack.
        rt.set_scope(gallery_scope, rt.theme());
        s->gallery_site = {.lang = &s->lang, .style = [p = s.get()](std::string pack) {
                               if (!p->runtime) return;
                               if (pack == "plain") {
                                   p->runtime->crossfade(seconds(0.35), gallery_scope);
                                   p->runtime->set_scope(gallery_scope, p->runtime->theme());
                               } else if (auto r = theme::apply_style(*p->runtime, pack, gallery_scope, true); !r) {
                                   site_log.warn("the style {}: {}", pack, r.error());
                               }
                               p->soon();
                           }};
        s->compact.set(rt.size().width < Site::compact_below);
        s->width.set(rt.size().width);
        s->room = rt.size().width;
        s->band.set(Site::band_of(rt.size().width));
        s->guide = std::make_unique<Guide>(rt, [site = s.get()] { return site->place.get(); });
        {
            const auto it = std::ranges::find_if(all, [&](const Place& p) { return p.id == s->place.peek(); });
            const bool zh = s->lang.peek() == "zh";
            if (it != all.end()) s->mark(std::string(zh ? it->zh : it->en));
        }
        (void)rt.stage_plugins().add({.name = "openmui.site.tree_watch", .kind = "stage", .description = "the site's place read again when its tree changed",
                                      .cost = pipeline::Cost{.cpu_us = 200}},
                               [site = s.get()](const pipeline::Config&) -> std::expected<std::unique_ptr<pipeline::Stage>, std::string> {
                                   return std::make_unique<TreeWatch>(*site);
                               });
        if (auto in = rt.frame_pipeline().insert({"tree_watch", "openmui.site.tree_watch", {"paint"}, {}}, rt.stage_plugins()); !in)
            site_log.warn("the tree watch is not in the frame: {}", pipeline::to_text(in.error()));
        s->show_route();
        s->soon();
    };
    a.view = [s] {
        auto say = [s](std::string_view en, std::string_view han) {
            return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
        };
        auto wide = [s] { return !s->compact.get(); };
        // The places: where you are, said to every reader (selected) and seen (outlined).
        std::vector<Element> places;
        for (const Place& p : all) {
            const std::string id(p.id);
            Element b = button({.id = "to_" + id,
                                .text = computed([s, en = p.en, zh = p.zh] { return std::string(s->lang.get() == "zh" ? zh : en); }),
                                .variant = computed([s, id] { return s->place.get() == id ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                .on_activated = [s, id] { s->go(id); }});
            b.prop("selected", derive([s, id] { return s->place.get() == id; }));
            places.push_back(retyped(std::move(b), s));
        }
        Element nav = row({.id = "nav", .gap = 2, .align = "center", .wrap = "wrap", .label = "Places"}, std::move(places));
        nav.prop("role", Source(Value("navigation")));  // a landmark
        // Narrow: a line of its own, its places wrapping within it.
        nav.prop("width", derive([s] { return s->compact.get() ? Value(static_cast<double>(s->width.get() - 48)) : Value{}; }));
        // The bar: the mark, the places, the switches. Narrow, it wraps: the places on a line of their own.
        Element bar = row({.id = "bar", .padding = Edges(12.0f, 24.0f), .gap = 8, .align = "center", .wrap = "wrap", .background = "bar"},
                          {brand_mark("logo", 26), text({.id = "mark", .text = "openmui", .style = "heading"}), box({.id = "space", .width = Dim(16.0f)}),
                           std::move(nav), only_when(box({.id = "fill", .grow = 1}), wide),
                           only_when(frame_meter({.id = "frames", .label = say("Frame rate", "帧率")}), [s] { return s->width.get() >= 1400; }),
                           button({.id = "settings_toggle",
                                   .text = say("Settings", "设置"),
                                   .variant = computed([s] { return s->settings.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                   .on_activated = [s] {
                                       s->settings.set(!s->settings.peek());
                                       if (s->settings.peek()) s->panel.set(false), s->checking.set(false);
                                   }}),
                           button({.id = "guide_toggle",
                                   .text = say("Ask an agent", "让 agent 来"),
                                   .variant = computed([s] { return s->panel.get() ? ButtonVariant::primary : ButtonVariant::secondary; }),
                                   .on_activated = [s] {
                                       s->panel.set(!s->panel.peek());
                                       if (s->panel.peek()) s->settings.set(false), s->checking.set(false);
                                   }}),
                           button({.id = "lang",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "English" : "中文"); }),
                                   .variant = ButtonVariant::ghost,
                                   .on_activated = [s] {
                                       s->mark(s->lang.peek() == "zh" ? "English" : "中文");
                                       s->switched_at = s->runtime ? s->runtime->now().count() : 0;
                                       s->lang.set(s->lang.get() == "zh" ? "en" : "zh");
                                       s->switches.set(s->switches.get() + 1);
                                       s->show_route();
                                       s->soon();
                                   }})});
        // The place, one at a time; built again when another is shown or the window crosses a width band.
        std::vector<Element> shown;
        shown.push_back(dynamic([s] {
            (void)s->band.get();
            return std::vector<Element>{place_page(s, s->place.get())};
        }));
        // Over the place, as the lens says.
        shown.push_back(fades_in(lens({.id = "wireframe", .nodes = s->nodes, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}),
                                 [s] { return s->lens.get() == "tree"; }));
        for (const std::string_view which : {"agent", "test"}) {
            const std::string key(which);
            shown.push_back(fades_in(scroll({.id = key, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .padding = Edges(32.0f, 48.0f), .background = "background"},
                                             {text({.id = key + "_text", .text = key == "agent" ? s->agent : s->test, .style = "code"})}),
                                      [s, key] { return s->lens.get() == key; }));
        }
        Element view = stack({.id = "view", .min_width = Dim(0.0f), .min_height = Dim(0.0f), .grow = 1, .label = "The place"}, std::move(shown));
        view.prop("role", Source(Value("main")));  // a landmark: where a screen reader goes to read
        // A phone: a panel takes the place's room while it is open.
        auto some_panel = [s] { return s->panel.get() || s->settings.get() || s->checking.get(); };
        Element body = row({.id = "body", .min_height = Dim(0.0f), .align = "stretch", .grow = 1},
                           {only_when(std::move(view), [s, some_panel] { return !s->compact.get() || !some_panel(); }, "stack"),
                            only_when(guide_panel(s), [s] { return s->panel.get(); }), only_when(settings_panel(s), [s] { return s->settings.get(); }),
                            only_when(check_panel(s), [s] { return s->checking.get(); })});
        // The foot: the lenses, the page's time (drag it, play it, play it backwards) and the tree's hash.
        auto mode_is = [s](std::string_view m) { return s->time_mode.get() == m; };
        Element foot = row({.id = "foot", .padding = Edges(6.0f, 16.0f), .gap = 6, .align = "center", .background = "bar"},
                           {lens_button(s, "pixels", "Pixels"), lens_button(s, "tree", "Tree"), lens_button(s, "agent", "Agent"), lens_button(s, "test", "Test"),
                            only_when(box({.id = "between", .width = Dim(1.0f), .height = Dim(20.0f), .margin = Edges(Insets{0, 6, 0, 6}), .background = "border"}), wide),
                            // A phone: the lenses, Live and the strip (the playhead dragged); the rest of the time's controls on a wider screen.
                            only_when(button({.id = "time_back",
                                    .text = say("Backwards", "倒放"),
                                    .variant = computed([mode_is] { return mode_is("backwards") ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                    .on_activated = [s] { s->time_mode.peek() == "backwards" ? s->pause() : s->backwards(); }}), wide),
                            only_when(button({.id = "time_play",
                                    .disabled = computed([mode_is] { return mode_is("live"); }),
                                    .text = computed([s] {
                                        const bool zh = s->lang.get() == "zh";
                                        return std::string(s->time_mode.get() == "playing" ? (zh ? "暂停" : "Pause") : (zh ? "播放" : "Play"));
                                    }),
                                    .variant = ButtonVariant::ghost,
                                    .on_activated = [s] { s->time_mode.peek() == "playing" ? s->pause() : s->play(); }}), wide),
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
                            only_when(text({.id = "clock",
                                  .text = computed([s] {
                                      if (s->time_mode.get() == "live") return std::string(s->lang.get() == "zh" ? "现在" : "now");
                                      std::string t = std::format("{:.1f} s", s->playhead.get() - s->time_end.get());
                                      if (t.starts_with('-')) t.replace(0, 1, "−");
                                      return t;
                                  }),
                                  .style = "caption"}), wide),
                            // Monospace: a new hash keeps its width, so nothing beside it moves.
                            only_when(row({.id = "proof"}, {text({.id = "hash", .text = s->hash, .style = "code"})}), [s] { return s->band.get() >= 2; }),
                            only_when(button({.id = "ci_check",
                                              .text = say("Check against CI", "对照 CI"),
                                              .variant = computed([s] { return s->checking.get() ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                              .on_activated = [s] {
                                                  const bool open = !s->checking.peek();
                                                  if (open && !s->replay) s->want_frame(0);
                                                  s->checking.set(open);
                                                  if (open) s->panel.set(false), s->settings.set(false);
                                              }}),
                                      wide)});
        Element site = column({.id = "site", .grow = 1, .background = "background"}, {std::move(bar), std::move(body), std::move(foot)});
        // Esc puts away what is open over the place: a panel.
        site.on("key_down", [s](const Value& e) {
            if (!e.get("key") || e.get("key")->as_string() != "Escape") return;
            for (Signal<bool>* open : {&s->panel, &s->settings, &s->checking})
                if (open->peek()) open->set(false);
        });
        return site;
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
    const bool chinese = lang == "zh";
    // A skip link first (WCAG 2.4.1): past the places to the first one's text.
    std::string out = std::format("<main id=\"omui-static\" lang=\"{}\">\n<a class=\"skip\" href=\"#/{}home\">{}</a>\n<nav aria-label=\"{}\">",
                                  chinese ? "zh-CN" : "en", chinese ? "zh/" : "", chinese ? "跳到正文" : "Skip to content", chinese ? "页面" : "Places");
    // Each place's section by its whole route (#/home too: the static page's own anchors).
    auto href = [&](std::string_view place) { return std::format("#/{}{}", chinese ? "zh/" : "", place); };
    for (const Place& p : all) out += std::format("<a href=\"{}\">{}</a> ", href(p.id), chinese ? p.zh : p.en);
    out += "</nav>\n";
    for (const Place& p : all) {
        out += std::format("<section id=\"{}\">\n", href(p.id).substr(1));
        if (p.id == "home") out += hero_html(chinese);
        if (const std::string_view doc = document(p.id, lang); !doc.empty()) {
            // Links to the site's own documents go to their sections, as they go to their places when the program runs.
            std::string html = md::to_html(md::parse(doc));
            for (const auto& [file, place] : {std::pair{"best-practices.md", "practices"}, std::pair{"features.md", "features"}})
                for (std::string from = std::format("href=\"{}", file), to = std::format("href=\"{}", href(place));;) {
                    const auto at = html.find(from);
                    if (at == std::string::npos) break;
                    const auto end = html.find('"', at + 6);
                    html.replace(at, end - at, to);
                }
            out += html;
        } else if (p.id == "components") {
            // Every component, what it is to every reader: its contract's words.
            out += chinese ? "<h1>组件</h1>\n<p>openmui.ui.base 的每个组件。可以操作的组件、四种语言的代码和风格包需要 JavaScript。</p>\n<ul>\n"
                           : "<h1>Components</h1>\n<p>Every component of openmui.ui.base. Operating them, their code in four languages and the style packs need JavaScript.</p>\n<ul>\n";
            for (const std::string_view name : {"button", "toggle", "checkbox", "radio", "slider", "number_input", "text_box", "select", "tabs", "breadcrumb", "pagination",
                                                "progress", "badge", "avatar", "card", "accordion", "skeleton", "tooltip", "popover", "toast", "dialog"})
                if (const ComponentType* t = find_type(name); t && t->contract)
                    out += std::format("<li><b>{}</b> ({}) — {}</li>\n", name, spec::name(t->contract->role), escaped(t->contract->doc));
            out += "</ul>\n";
        } else if (p.id == "timeline")
            out += chinese ? "<h1>时间线编辑器</h1>\n<p>一个可用的时间线编辑器：拖动、修剪、切开，每次编辑都能精确撤销。需要 JavaScript。</p>\n"
                           : "<h1>A timeline editor</h1>\n<p>A working timeline editor: drag, trim and split clips, every edit undone exactly. It needs JavaScript.</p>\n";
        else if (p.id == "hello")
            out += std::format("<h1>{}</h1>\n<p>{}</p>\n<pre><code>{}</code></pre>\n", chinese ? "开始使用" : "Get started",
                               chinese ? "示例 01，一个完整的 openmui 程序。运行它需要 JavaScript；它的全部源码如下。"
                                       : "Example 01, a whole openmui program. Running it needs JavaScript; here is all of its source.",
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
    // In the site's own colours (site.theme.omui.toml's light tokens).
    out.insert(head,
               "<style>" + faces +
                   "#omui-static{position:fixed;inset:0;z-index:1;overflow:auto;padding:32px 48px;background:#fbfbfa;color:#16161a;"
                   "font:16px/1.6 Inter,'Noto Sans SC',system-ui,sans-serif;user-select:text}#omui-static[hidden]{display:none}"
                   "#omui-static nav{margin-bottom:32px}#omui-static a{color:#4f46e5}#omui-static nav a{margin-right:16px}"
                   "#omui-static .skip{position:absolute;left:8px;top:-60px;padding:8px 14px;background:#16161a;color:#fbfbfa}#omui-static .skip:focus{top:8px}"
                   "#omui-static section{max-width:860px;margin-bottom:64px}#omui-static code{font:14px 'JetBrains Mono',monospace;background:#f3f3f1;"
                   "padding:1px 4px;border-radius:3px}#omui-static pre{white-space:pre-wrap}#omui-static table{border-collapse:collapse}"
                   "#omui-static td,#omui-static th{border:1px solid #e6e6e3;padding:4px 8px}</style>\n"
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
