// The site (C2: positioning §2, the design language): bright and editorial —
// a bar with the mark and the places, one place at a time below it (the home,
// the components, the documents, getting started, a working timeline editor),
// scrolled as a page; over the place, as the lens says, the tree's wireframe,
// the JSON an agent reads or the assertions a test writes; at the foot, the
// lenses, the page's time and the tree's hash. The address follows the place
// (the route), and the browser's back and forward move it.
module omui.site;

import std;
import omui;
import mlibs.hash;
import omui.ui.markdown;
import omui.ui.timeline;
import xengine.timeline;
import omui.theme;
import omui.theme.styles;
import hello;
import mcpplibs.os;
import omui.semantic;
import mlibs.trace;
import omui.inspector;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui::site {

namespace {

constexpr Log site_log{"site"};

// Where the places are in the tree (the window mounts the site as "main").
constexpr std::string_view place_root = "main/body/view/";

const auto all = places();

// The widest a place's content is (the design language's 1200, less its margins).
constexpr float content_width = 1120;

std::string_view embedded(std::span<const std::pair<std::string_view, std::string_view>> table, std::string_view file) {
    for (const auto& [name, text] : table)
        if (name == file) return text;
    return {};
}

Theme site_theme() {
    if (auto r = themes::add_styles(); !r) site_log.warn("the style packs: {}", r.error().empty() ? std::string() : r.error().front().text());
    if (auto r = theme::library().add(embedded(files::theme_files, "site.theme.omui.toml"), "site.theme.omui.toml"); !r)
        site_log.warn("the site's theme: {}", r.error().empty() ? std::string() : r.error().front().text());
    if (auto t = theme::library().compose(std::vector<std::string>{"site"}); t) return std::move(*t);
    site_log.warn("the site's theme could not be composed");
    return Theme::light();
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
    BrandStudy          brand;
    DocsState           docs;
    std::shared_ptr<inspector::cellular::Session> reactions = std::make_shared<inspector::cellular::Session>();
    SiteLinks           links;
    Signal<bool>        tools = signal(false);
    std::map<std::string, double, std::less<>> scroll_positions;
    std::string         initial_route;
    Signal<std::string> missing_route = signal(std::string());
    Signal<std::string> place = signal(std::string("home")), lang = signal(std::string("en")), lens = signal(std::string("pixels"));
    bool foreground = true;
    Signal<Value>       nodes = signal(Value{});
    Signal<std::string> agent = signal(std::string()), test = signal(std::string()), hash = signal(std::string());

    Signal<bool>        panel = signal(false);     // the agent panel open
    // The person's own agent: the prompt given to it, the self-check it presses.
    Signal<std::string> prompt = signal(std::string());
    Signal<std::int64_t> hellos = signal(std::int64_t{0});
    Signal<bool>        settings = signal(false);  // the settings panel open
    // A narrow window (a phone): the places and the switches in a menu, a panel in the place's place.
    static constexpr float compact_below = 900;
    Signal<bool>        compact = signal(false), menu = signal(false);
    Signal<bool>        core_menu = signal(false), logo_unfolded = signal(true);
    bool                logo_introduced = false;
    std::uint64_t       logo_timer = 0;
    std::uint64_t       feature_timer = 0;
    Signal<float>       width = signal(1280.0f);  // the window's width, logical px
    Signal<float>       room = signal(1280.0f);   // actual available width, including changes inside one responsive band
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
    std::optional<xengine::timeline::Timeline> timeline_model;
    const xengine::timeline::Timeline* timeline_now(const std::string& text) {
        if (text != timeline_read_from || !timeline_model) {
            timeline_read_from = text;
            auto t = xengine::timeline::read_timeline(text, "site.timeline.omui.toml");
            timeline_model = t ? std::optional(std::move(*t)) : std::nullopt;
        }
        return timeline_model ? &*timeline_model : nullptr;
    }
    // Played: the playhead on a frame at a time (30 a second), to the timeline's end, then back to its start.
    void play_timeline() {
        (void)runtime->after(seconds(1.0 / 30), [this] {
            if (!timeline_playing.peek()) return;
            const xengine::timeline::Timeline* t = timeline_now(timeline.peek());
            const std::int64_t end = t ? t->rate.frame_at(t->duration()) : 270;
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
        const xengine::timeline::Timeline* t = timeline_now(timeline.get());
        if (!t) return 0;
        std::vector<xengine::timeline::Playing> now;
        xengine::timeline::playing(*t, t->rate.frame_time(frame), now);
        for (const auto& p : now)
            if (t->tracks[p.track].clips[p.clip].name == name) return p.level;
        return 0;
    }
    hello::State        hello;                 // example 01's, as the person left it
    Hero                hero;                  // the home page's opening: its checklist, the node its panels show
    HeroSite            hero_site;             // what the opening reads of the site
    Screens             screens;               // the home page's screens after it
    ScreensSite         screens_site;
    Gallery             gallery;               // the components page: the style, the code's language, the samples' values
    GallerySite         gallery_site;
    HomeLanding         landing;
    HomeLandingSite     landing_site;
    HomeScenes          scenes;
    Wall                wall;
    HomeScenesSite      scenes_site;
    MotionGallery       basic, creative;
    MotionGalleryContext basic_site, creative_site;
    FeatureExamples     style_examples, interface_examples;
    FeatureExamplesContext style_site, interface_site;
    AgentFeature        agent_feature_state;
    AgentFeatureContext agent_site;
    // The reference run (the foot's check against CI): played here, on a
    // headless runtime of its own, when asked; the frame shown, its hash as
    // this machine drew it, and CI's.
    Signal<bool>        checking = signal(false);
    Signal<double>      ref_frame = signal(0.0);
    Signal<std::string> ref_mine = signal(std::string()), ref_ci = signal(std::string());
    Signal<int>         ref_verdict = signal(0), ref_shown = signal(0);  // 2 loading, 1 same, −1 not, 0 unpinned
    std::unique_ptr<ReferenceReplay> replay;
    xrender::ImageRef    ref_image{};
    // A frame asked for (a handler's request, played by the frame stage
    // after paint: driving another runtime inside a handler would run in the
    // page's batch, its own effects held back until the batch ends).
    std::optional<std::int32_t> ref_wanted;
    std::int32_t ref_target = 0;
    bool ref_waiting = false;
    void want_frame(std::int32_t f) {
        ref_target = f;
        ref_wanted = f;
        if (runtime) runtime->request_frame();
    }
    void check_frame(std::int32_t f) {
        if (!runtime) return;
        mlibs::trace::Span span("reference frame", "site");
        if (mcpplibs::os::fs::read(mlibs::text::FontCollection::default_directory() / ".partial")) {
            ref_verdict.set(2);
            runtime->want_fonts();  // the reference frames are drawn in every face: the deferred ones asked for
            if (!ref_waiting) {
                ref_waiting = true;
                (void)runtime->after(seconds(0.1), [this] {
                    ref_waiting = false;
                    if (checking.peek()) want_frame(ref_target);
                });
            }
            return;
        }
        if (!replay) replay = std::make_unique<ReferenceReplay>();
        const std::uint64_t mine = replay->hash(f);
        if (ref_image.id != 0) runtime->images().remove(ref_image);
        ref_image = runtime->images().add(std::make_shared<const xrender::raster::Surface>(replay->pixels()));
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
    [[nodiscard]] static std::string route_of(std::string_view place, std::string_view lang, std::string_view component = {}, std::string_view example = {}) {
        return canonical_route(place, lang, component, example);
    }
    [[nodiscard]] std::string route_now() const {
        const auto& p = place.peek();
        const auto example = p == "motion" && basic.detail.peek() ? basic.selected.peek()
                           : p == "explore" && creative.detail.peek() ? creative.selected.peek()
                           : p == "agent" ? agent_feature_state.mode.peek() : std::string();
        return route_of(p, lang.peek(), gallery.selected.peek(), example);
    }
    void stop_previews(std::string_view next) {
        if (next != "reactions") reactions->pause();
        if (feature_timer && runtime) runtime->cancel(std::exchange(feature_timer, 0));
        if (next != "components") {
            if (gallery.preview_timer && runtime) runtime->cancel(std::exchange(gallery.preview_timer, 0));
            ++gallery.preview_generation;
        }
        if (next != "home") cancel_home_landing(landing, landing_site);
        if (next != "release") { ++hero.generation; hero.agent_running.set(false); }
        if (next != "motion") motion_gallery_cancel(basic, basic_site);
        if (next != "explore") motion_gallery_cancel(creative, creative_site);
        if (next != "agent") cancel_agent_feature(agent_feature_state);
        if (next != "styles") cancel_feature_examples(style_examples);
        if (next != "interface") cancel_feature_examples(interface_examples);
        core_menu.set(false);
    }
    void show_route() {
        if (!runtime) return;
        if (place.peek() != "not_found") { runtime->set_route(route_now()); return; }
        std::string_view path = missing_route.peek();
        if (path.starts_with('#')) path.remove_prefix(1);
        while (path.starts_with('/')) path.remove_prefix(1);
        if (path.starts_with("zh/") || path.starts_with("en/")) path.remove_prefix(3);
        std::string address = lang.peek() == "zh" ? "zh/" + std::string(path) : std::string(path);
        missing_route.set(address);
        runtime->set_route(address);
    }
    // The person's own agent (agent connect §5.1): a pairing code from the
    // host, and the prompt that gives it to their agent — copied, and shown
    // to read. Where the host cannot link one (a window, a test), the prompt
    // says how instead.
    void give_prompt() {
        if (!runtime) return;
        const std::string code = runtime->link_agent();
        const AgentLink& l = runtime->agent_link();
        const bool zh = lang.peek() == "zh";
        const std::string guide = l.guide.empty() ? std::string("agent.md") : l.guide;
        const int port = l.port ? l.port : 47110;
        std::string text;
        if (code.empty())
            text = zh ? "这里连不了本机的 agent：在浏览器里打开这一页，或者用 `omui agent` 操作桌面窗口里的它。"
                      : "Your agent links to this page in a browser; a window is operated with `omui agent`.";
        else if (zh)
            text = std::format("请连接并操作我浏览器里打开的 omui 应用。\n配对码：{}（10 分钟内有效），端口 {}。\n\n按 {} 的说明进行：\n"
                               "1. 获取 omui（校验 sha256）或按规范自己写本地桥，用配对码运行；\n2. 用 `omui page status --wait` 确认已经配对；\n"
                               "3. 完成说明里的自检任务；\n4. 然后按我接下来的要求操作页面，完成后告诉我结果。\n"
                               "只通过页面的语义接口操作；需要我确认的动作，等我在页面上确认。",
                               code, port, guide);
        else
            text = std::format("Please connect to the omui application open in my browser and operate it.\nPairing code: {} (valid for 10 minutes), port {}.\n\n"
                               "Follow {}:\n1. get omui (check its sha256), or write the local bridge from its specification, and run it with the code;\n"
                               "2. confirm the pairing with `omui page status --wait`;\n3. do the self-check it describes;\n"
                               "4. then operate the page as I ask next, and tell me the result.\n"
                               "Only operate through the page's semantic interface; for an action that needs my answer, wait for me to answer it on the page.",
                               code, port, guide);
        if (!code.empty()) runtime->set_clipboard_text(text);
        prompt.set(std::move(text));
    }
    // The effects tier the person chose (the settings, the home page's fast screen): "auto" is the device's.
    void choose_effects(std::string v) {
        effects.set(v);
        if (!runtime) return;
        Adaptation a = runtime->adaptation();
        const std::optional<mlibs::pipeline::Tier> chosen = v == "t1" ? std::optional(mlibs::pipeline::Tier::t1)
                                                     : v == "t2" ? std::optional(mlibs::pipeline::Tier::t2)
                                                     : v == "t3" ? std::optional(mlibs::pipeline::Tier::t3)
                                                     : v == "t4" ? std::optional(mlibs::pipeline::Tier::t4)
                                                                 : std::nullopt;
        a.effects = chosen.value_or(a.tier);
        a.reason = v == "auto" ? std::string("the device's") : std::string("the person's setting");
        runtime->set_adaptation(a);
    }
    // A route the person moved to (back, forward, a typed address): its place and its language.
    void follow_route(std::string_view route) {
        const auto parsed = parse_site_route(route);
        stop_previews(parsed ? parsed->place : "not_found");
        if (!parsed) {
            if (compact.peek()) menu.set(false), panel.set(false), settings.set(false), checking.set(false);
            missing_route.set(std::string(route));
            lang.set(route == "zh" || route.starts_with("zh/") ? std::string("zh") : std::string("en"));
            place.set(std::string("not_found"));
            soon();
            return;
        }
        if (parsed->language != lang.peek()) {
            lang.set(parsed->language);
            switches.set(switches.peek() + 1);
            switched_at = runtime ? runtime->now().count() : 0;
        }
        // A browser history move must not push a new entry, including an old alias.
        if (compact.peek()) menu.set(false), panel.set(false), settings.set(false), checking.set(false);
        gallery.selected.set(parsed->component);
        gallery.pane.set(""); gallery.states.set(false); gallery.variants.set(false); gallery.browser_open.set(false);
        if (parsed->place == "motion") { if (!parsed->example.empty()) basic.selected.set(parsed->example); basic.detail.set(!parsed->example.empty()); }
        if (parsed->place == "explore") { if (!parsed->example.empty()) creative.selected.set(parsed->example); creative.detail.set(!parsed->example.empty()); }
        if (parsed->place == "agent") agent_feature_state.mode.set(parsed->example.empty() ? "use" : parsed->example);
        gallery.dialog.set(false); gallery.popover.set(false); gallery.drawer.set(false); gallery.toast.set(false);
        place.set(parsed->place);
        soon();
    }

    // To a place: shown, its page from its top; the address follows.
    void go(std::string_view to) {
        if (!find_place(to)) return;
        stop_previews(to);
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
        if (to == "components") gallery.selected.set("");
        if (to == "motion") basic.detail.set(false);
        if (to == "explore") creative.detail.set(false);
        gallery.dialog.set(false); gallery.popover.set(false); gallery.drawer.set(false); gallery.toast.set(false);
        place.set(std::string(to));
        show_route();
        soon();
    }

    void open_component(std::string id) {
        if (!id.empty() && !find_component(id)) return;
        stop_previews("components");
        gallery.dialog.set(false); gallery.popover.set(false); gallery.drawer.set(false); gallery.toast.set(false);
        gallery.pane.set(""); gallery.states.set(false); gallery.copied.set(false);
        gallery.selected.set(std::move(id));
        place.set("components");
        if (compact.peek()) menu.set(false), panel.set(false), settings.set(false), checking.set(false);
        show_route(); soon();
    }

    void open_example(bool is_creative, std::string id) {
        if (!id.empty() && !find_motion_case(id, is_creative)) return;
        const std::string next = is_creative ? "explore" : "motion";
        stop_previews(next);
        auto& target = is_creative ? creative : basic;
        if (!id.empty()) target.selected.set(id);
        target.detail.set(!id.empty());
        place.set(next); menu.set(false); show_route(); soon();
    }
    void open_agent_mode(std::string mode) {
        if (mode != "use" && mode != "develop" && mode != "design") return;
        cancel_agent_feature(agent_feature_state);
        stop_previews("agent"); agent_feature_state.mode.set(mode);
        place.set("agent"); menu.set(false); show_route(); soon();
    }

    void apply_scope(std::string_view scope, std::string pack) {
        if (!runtime) return;
        if (pack == "plain") {
            runtime->crossfade(seconds(0.25), scope);
            runtime->set_scope(scope, runtime->theme());
        } else if (auto r = theme::apply_style(*runtime, pack, scope, true); !r) site_log.warn("the style {}: {}", pack, r.error());
    }

    void refresh_plain_scopes() {
        if (!runtime) return;
        for (const auto& [scope, plain] : {
                 std::pair{gallery_scope, gallery.style.peek() == "plain"},
                 std::pair{std::string_view("motion-preview"), basic.style.peek() == "plain"},
                 std::pair{std::string_view("creative-preview"), creative.style.peek() == "plain"},
                 std::pair{std::string_view("interface-feature"), true},
                 std::pair{std::string_view("style-feature"), style_examples.style_name.peek() == "plain"},
                 std::pair{std::string_view("agent-design"), agent_feature_state.style_name.peek() == "plain"}})
            if (plain) runtime->set_scope(scope, runtime->theme());
    }

    void tick_features() {
        if ((runtime->reduce_motion() || runtime->motion_level() != MotionLevel::full)) logo_unfolded.set(false);
        const auto& p = place.peek();
        if (p == "home") tick_home_landing(landing, landing_site);
        if (p == "components") tick_gallery(gallery, gallery_site);
        if (p == "motion") motion_gallery_tick(basic, basic_site);
        if (p == "explore") motion_gallery_tick(creative, creative_site);
        if (p == "agent") tick_agent_feature(agent_feature_state, agent_site);
        if (p != "styles" && p != "interface") return;
        auto& state = p == "styles" ? style_examples : interface_examples;
        auto& context = p == "styles" ? style_site : interface_site;
        observe_feature_examples(state, context);
        const auto preview = state.preview_nodes[p == "styles" ? 0 : 4];
        const auto id = runtime->valid(preview) ? runtime->parent(preview) : NodeId{};
        const auto box = runtime->valid(id) ? runtime->screen_bounds(id) : Rect{};
        const bool visible = runtime->valid(id) && !(compact.peek() && (panel.peek() || settings.peek() || checking.peek()))
                          && box.y + box.height > 80 && box.y < runtime->size().height - 44;
        const bool details = p == "styles" ? state.styles_details.peek() : state.interface_details.peek();
        if (!visible || state.paused.peek() || state.interacting.peek() || details || (runtime->reduce_motion() || runtime->motion_level() != MotionLevel::full)) {
            if (feature_timer) runtime->cancel(std::exchange(feature_timer, 0));
            return;
        }
        if (feature_timer) return;
        feature_timer = runtime->after(seconds(2.2), [this, p] {
            feature_timer = 0;
            if (place.peek() != p || (runtime->reduce_motion() || runtime->motion_level() != MotionLevel::full)) return;
            auto& state = p == "styles" ? style_examples : interface_examples;
            const auto& context = p == "styles" ? style_site : interface_site;
            if (!state.paused.peek() && !state.interacting.peek()) feature_examples_demo_step(state, context, p);
        });
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
        mlibs::trace::Span span("look", "site");
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
        // This is the visitor's changing tree. The explicit reference player
        // compares an isolated, fixed script with CI's complete frame hashes.
        hash.set(std::format("tree {:08x}", h >> 32));
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

// The public pipeline lets input cancel scheduled preview work before the
// runtime's timer stage. Otherwise an already-due step could change a task
// immediately before the person's pointer, text or window-blur event.
class PreviewInputGuard final : public mlibs::pipeline::Stage {
public:
    explicit PreviewInputGuard(Site& site) : site_(site) {}
    [[nodiscard]] std::vector<mlibs::pipeline::Port> reads() const override {
        return {mlibs::pipeline::port<FrameInput>(std::string(frame_resource::input))};
    }
    [[nodiscard]] std::vector<mlibs::pipeline::Port> writes() const override { return {}; }
    std::expected<void, std::string> run(mlibs::pipeline::Context& context) override {
        const auto* input = context.resources.get<FrameInput>(frame_resource::input);
        if (!input || input->events.empty() || !site_.runtime) return {};
        auto& rt = *site_.runtime;
        if (site_.place.peek() == "agent" && (runtime_ != &rt || !rt.valid(specimen_))) {
            runtime_ = &rt;
            specimen_ = rt.find(site_.agent_site.root + "/specimen");
        }
        for (const auto& event : input->events) {
            if (const auto* focus = std::get_if<FocusEvent>(&event)) {
                site_.foreground = focus->gained;
                if (!focus->gained) {cancel_agent_feature(site_.agent_feature_state);site_.reactions->pause();}
                continue;
            }
            if (site_.place.peek() != "agent" || !rt.valid(specimen_)) continue;
            bool takeover = false;
            if (const auto* pointer = std::get_if<PointerEvent>(&event)) {
                takeover = pointer->kind != PointerKind::leave && pointer->kind != PointerKind::cancel
                        && rt.screen_bounds(specimen_).contains(pointer->position);
            } else if (const auto* key = std::get_if<KeyEvent>(&event)) {
                // Button contracts activate on key-down. A later key-up must
                // not cancel the task the person just explicitly requested.
                takeover = key->kind == KeyKind::down;
            } else if (std::holds_alternative<TextEvent>(event)
                       || std::holds_alternative<PreeditEvent>(event) || std::holds_alternative<WheelEvent>(event)) {
                takeover = true;
            }
            if (takeover) takeover_agent_feature(site_.agent_feature_state);
        }
        return {};
    }
private:
    Site& site_;
    Runtime* runtime_ = nullptr;
    NodeId specimen_;
};

// A stage of the window's frame (D-66) after paint: the window's width
// (compact or not, the place's room), the route the person moved, the page's
// time; and when the tree changed (a resize, the fonts arriving, an agent's
// edit), the place read again once the frame is done.
class TreeWatch final : public mlibs::pipeline::Stage {
public:
    explicit TreeWatch(Site& site) : site_(site) {}
    [[nodiscard]] std::vector<mlibs::pipeline::Port> reads() const override { return {}; }
    [[nodiscard]] std::vector<mlibs::pipeline::Port> writes() const override { return {}; }
    std::expected<void, std::string> run(mlibs::pipeline::Context&) override {
        Runtime& rt = *site_.runtime;
        // A phone, or a window made narrow: compact (and back).
        const float w = rt.size().width;
        if (const bool narrow = w < Site::compact_below; narrow != site_.compact.peek()) {
            site_.compact.set(narrow);
            if (!narrow) site_.menu.set(false);
        }
        if (std::abs(w - site_.width.peek()) >= 1) site_.width.set(w);
        // The place's room: the view as laid out (a panel open beside it takes some); its band, when it changes, builds the page again.
        const NodeId view = rt.find("main/body/view");
        const float available = rt.valid(view) && rt.bounds(view).width > 0 ? rt.bounds(view).width : w;
        if (std::abs(available - site_.room.peek()) >= 1) site_.room.set(available);
        if (const int b = Site::band_of(available); b != site_.band.peek()) site_.band.set(b);
        // The person moved the route (back, forward): the place and the language it names.
        if (rt.route_revision() != route_seen_) {
            route_seen_ = rt.route_revision();
            if (rt.route() != site_.route_now()) {
                const std::string route = rt.route();
                (void)rt.after(seconds(0.0), [this, route] { site_.follow_route(route); });
            }
        }
        site_.tick_features();
        const NodeId motion = rt.find("main/body/view/release/page/motion/shown/shape");
        const int motion_band = site_.band.peek();
        if (motion != motion_seen_ || motion_band != motion_band_) {
            motion_seen_ = motion; motion_band_ = motion_band;
            if (rt.valid(motion)) (void)rt.after(seconds(2.0 / 60), [this, motion, motion_band] {
                auto& runtime = *site_.runtime;
                if (site_.place.peek() != "release" || site_.band.peek() != motion_band || runtime.find("main/body/view/release/page/motion/shown/shape") != motion) return;
                const double y = runtime.get(runtime.find("main/body/view/release"), "y").as_double();
                site_.screens.last_y = y;
                scrolled(site_.screens, site_.screens_site, "main/body/view/release", y);
            });
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
    NodeId        motion_seen_;
    int           motion_band_ = -1;
};

class ProgramMotionStage final : public mlibs::pipeline::Stage {
public:
    explicit ProgramMotionStage(Site& site):site_(site){}
    [[nodiscard]] std::vector<mlibs::pipeline::Port> reads() const override {
        return {mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree)),
                mlibs::pipeline::port<FrameLayout>(std::string(frame_resource::layout))};
    }
    [[nodiscard]] std::vector<mlibs::pipeline::Port> writes() const override {
        return {mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree))};
    }
    std::expected<void,std::string> run(mlibs::pipeline::Context&) override {
        if(site_.place.peek()=="motion")motion_gallery_update(site_.basic,site_.basic_site);
        if(site_.place.peek()=="explore")motion_gallery_update(site_.creative,site_.creative_site);
        return {};
    }
private:
    Site& site_;
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

std::string_view document(std::string_view place, std::string_view lang) {
    const bool zh = lang == "zh";
    if (place == "hello") return embedded(files::content_files, zh ? "start.zh.md" : "start.en.md");
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
    mlibs::hash::Fnv1a h;
    const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr;
    if (!nodes) return h.value();
    for (const Value& n : *nodes) {
        // Not the live readouts the page shows: this hash itself (the time screen's), and what the machine is (the fast screen's).
        if (const Value* id = n.get("id"); id && (id->as_string().ends_with("/time/proof/hash") || id->as_string().ends_with("/fast/shown/device"))) continue;
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
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    // The log's words in Chinese (its own are English).
    Value zh_phrases = Value{}
                           .set("idle", "你的 agent 还没有连接。")
                           .set("waiting", "等你的 agent 连接 — 配对码 {code}")
                           .set("paired", "{agent} 已连接")
                           .set("lost", "{agent} 的桥断开了，正在重新寻找")
                           .set("expired", "配对码过期了：再复制一次提示词，换一个新的")
                           .set("stopped", "连接已结束")
                           .set("empty", "还没有 agent 在这里操作过。")
                           .set("undo", "撤销")
                           .set("asked", "问了你")
                           .set("refused", "被拒绝");
    Element panel = scroll(
        {.id = "guide", .width = computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(380.0f); }), .gap = 10, .padding = Edges(20.0f, 16.0f), .background = "surface"},
        {text({.id = "guide_title", .text = say("Your agent, on this page", "你的 agent，操作这一页"), .style = "heading"}),
         // An agent's question for the person first: theirs to answer (S-10), above all else.
         only_when(column({.id = "asking", .padding = Edges(10.0f), .gap = 6, .background = "accent_subtle"},
                          {text({.id = "question",
                                 .text = computed([s, zh] {
                                     const std::string what = s->guide ? s->guide->asking.get() : std::string();
                                     return zh() ? std::format("agent 请求：{}。这是外部的动作，要先问你。", what)
                                                 : std::format("The agent asks: {}. It is external, so it asks you first.", what);
                                 }),
                                 .style = "body"}),
                           std::move(answer)}),
                   [s] { return s->guide && !s->guide->asking.get().empty(); }),
         text({.id = "guide_about",
               .text = say("Your own agent — Claude Code, Codex, any that runs commands — operates this page by its semantic tree, through a bridge on your machine. "
                           "openas decides each request; what is yours to answer stays yours.",
                           "你自己的 agent（Claude Code、Codex，任何能执行命令的 agent）经本机的一个桥，按语义树操作这一页。"
                           "每个请求由 openas 决定；该你回答的，只有你能回答。"),
               .style = "caption"}),
         button({.id = "copy_prompt",
                 .text = say("Copy the prompt for your agent", "复制给 agent 的提示词"),
                 .variant = ButtonVariant::primary,
                 .on_activated = [s] { s->give_prompt(); }}),
         only_when(text({.id = "prompt", .text = s->prompt, .style = "code", .selectable = true}), [s] { return !s->prompt.get().empty(); }),
         // Ending the link is the person's own (S-10): an agent pressing it is refused.
         only_when(mine(button({.id = "disconnect",
                                .text = say("Disconnect your agent", "断开你的 agent"),
                                .variant = ButtonVariant::ghost,
                                .on_activated = [s] {
                                    if (s->runtime) s->runtime->unlink_agent();
                                    s->prompt.set(std::string());
                                }})),
                   [s] { return !s->prompt.get().empty(); }),
         actor_log({.id = "log",
                    .phrases = computed([s, zh_phrases] { return s->lang.get() == "zh" ? zh_phrases : Value{}; }),
                    .width = Dim(mlibs::layout::pct(100)),
                    .label = "What agents did here"}),
         // The self-check agent.md gives an agent: press it, and the count says so.
         row({.id = "check", .gap = 8, .align = "center"},
             {button({.id = "hello", .text = say("Hello, agent", "你好，agent"), .variant = ButtonVariant::secondary, .on_activated = [s] { s->hellos.set(s->hellos.peek() + 1); }}),
              text({.id = "hellos",
                    .text = computed([s] {
                        const std::int64_t n = s->hellos.get();
                        return s->lang.get() == "zh" ? std::format("按过 {} 次", n) : std::format("pressed {} {}", n, n == 1 ? "time" : "times");
                    }),
                    .style = "caption"})}),
         // A scripted one, marked so: what a real agent does, shown without one.
         row({.id = "simulated", .gap = 8, .align = "center"},
             {text({.id = "sim_title", .text = say("A simulated agent", "模拟的 agent"), .style = "label"}),
              badge({.id = "sim_tag", .text = say("Simulated", "模拟")})}),
         text({.id = "sim_about",
               .text = say("Scripted, no model. Each step is a real openmui.tree@0.1 request on the bus, as your agent's are, and openas decides it.",
                           "按脚本行动，没有模型。每一步都是总线上真实的 openmui.tree@0.1 请求，和你的 agent 发的一样，由 openas 决定。"),
               .style = "caption"}),
         column({.id = "tasks", .gap = 4}, std::move(asks)),
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
         scroll({.id = "said", .height = Dim(220.0f), .padding = Edges(10.0f), .background = "background"},
                {text({.id = "transcript", .text = computed([s] { return s->guide ? s->guide->transcript.get() : std::string(); }), .style = "code"})})});
    // Beside the place it keeps its width (a phone: the whole of it).
    panel.prop("min_width", Prop<Dim>(computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(380.0f); })));
    panel.prop("shrink", Source{Value(0.0), {}, {}});
    return panel;
}

Element check_panel(const std::shared_ptr<Site>& s) {
    auto say = [s](std::string_view en, std::string_view han) {
        return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
    };
    return column({.id = "check", .width = computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(360.0f); }), .min_width = computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(360.0f); }), .padding = Edges(16.0f, 16.0f), .gap = 10, .shrink = 0, .background = "surface"},
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
                             if (v == 2) return std::string(zh ? "参考回放所需字体正在加载…" : "Waiting for the reference fonts…");
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
    // A choice with its name above it (the name the select is read by, drawn).
    auto labelled = [say](std::string id, std::string_view en, std::string_view han, SelectOptions o) {
        o.id = id;
        o.label = say(en, han);
        return column({.id = id + "_field", .gap = 6},
                      {text({.id = "name", .text = say(en, han), .style = "label", .color = "text_muted"}), select(std::move(o))});
    };
    return column({.id = "settings",
                   .width = computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(340.0f); }),
                   .min_width = computed([s] { return s->compact.get() ? Dim(mlibs::layout::pct(100)) : Dim(340.0f); }),
                   .padding = Edges(20.0f, 20.0f),
                   .gap = 14,
                   .shrink = 0,
                   .background = "surface",
                   .label = "Settings"},
                  {text({.id = "title", .text = say("Settings", "设置"), .style = "heading"}),
                   text({.id = "about",
                         .text = say("Yours, as far as this site lets you: a program on omui says which of the framework's choices its users make.",
                                     "这些是你的选择，以这个网站开放的为限：基于 omui 的程序自己决定把框架的哪些选择交给用户。"),
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
                                       s->refresh_plain_scopes();
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
                           .on_changed = [s](std::string v) { s->choose_effects(std::move(v)); }}),
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
    Element title = retyped(text({.id = "title", .text = say("Get started", "开始使用"), .style = "title"}), s);
    title.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    Element instructions = column({.id = "instructions", .max_width = Dim(720.0f), .gap = 16}, {
        std::move(title),
        markdown({.id = "start", .source = computed([s] { return std::string(document("hello", s->lang.get())); }),
                   .on_link_activated = [s](std::string href) { follow(*s, href); }, .max_width = Dim(720.0f)}),
        button({.id = "copy_commands", .text = say("Copy commands", "复制命令"), .on_activated = [s] {
            if (s->runtime) s->runtime->set_clipboard_text(quickstart_commands);
        }, .align_self = "start"})});
    Element intro = column({.id = "intro", .width = narrow ? Dim(mlibs::layout::pct(100)) : Dim(440.0f), .gap = 16},
                           {text({.id = "about",
                                  .text = say("Example 01, running here: type a name, press the button. Its interface is declared each frame in plain C++; an agent finds the field by its role, and renaming the button keeps its id.",
                                              "示例 01，就在这里运行：输入名字，按按钮。界面每一帧用普通的 C++ 声明；agent 按角色找到输入框，按钮改名后 id 不变。"),
                                  .style = "body"}),
                            box({.id = "run", .width = narrow ? Dim(mlibs::layout::pct(100)) : Dim(440.0f), .height = Dim(280.0f), .background = "surface",
                                 .border_color = "border", .border_width = 1, .radius = 14, .clip = true},
                                {hello::app(s->hello).view()})});
    // The source as written: lines kept whole (indentation is meaning), scrolled sideways when long.
    Element code = scroll({.id = "code", .horizontal = true, .vertical = narrow, .grow = 1, .padding = Edges(20.0f, 24.0f), .background = "surface_variant"},
                          {text({.id = "source", .text = std::string(hello::hello_source), .style = "code", .wrap = false})});
    Element example = narrow ? column({.id = "example", .gap = 24}, {std::move(intro), std::move(code)})
                             : row({.id = "example", .gap = 40, .align = "stretch"}, {std::move(intro), std::move(code)});
    return column({.id = "page", .gap = 32}, {std::move(instructions), std::move(example)});
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
                                        ctx.list.fill_round_rect({x - w * 0.05f, h * 0.18f, w * 0.1f, w * 0.1f}, CornerRadii::all(w * 0.05f), xrender::Paint::solid(c));
                                        xrender::Path body;
                                        body.move_to({x, h * 0.32f}).line_to({x - dir * w * 0.02f, h * 0.62f}).line_to({x - dir * w * 0.09f, h * 0.92f});
                                        body.move_to({x - dir * w * 0.02f, h * 0.62f}).line_to({x + dir * w * 0.07f, h * 0.92f});
                                        body.move_to({x, h * 0.4f}).line_to({x + dir * w * 0.16f, h * 0.36f});
                                        ctx.list.stroke_path(body, xrender::StrokeStyle{.width = w * 0.035f, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round},
                                                             xrender::Paint::solid(c));
                                    };
                                    figure(w * 0.32f, 1, Color::hex(0x60a5fa));
                                    figure(w * 0.68f, -1, Color::hex(0xf59e0b));
                                    ctx.list.fill_round_rect({w * 0.47f, h * 0.31f, w * 0.06f, w * 0.06f}, CornerRadii::all(w * 0.03f), xrender::Paint::solid(Color::hex(0xffffff, 0.85f)));
                                    ctx.list.fill_rect({0, h * 0.94f, w, 2}, Color::hex(0xffffff, 0.25f));
                                }});
    Element monitor = stack({.id = "monitor", .width = Dim(mw), .height = Dim(mw * 9 / 16), .background = Fill(Color::hex(0x05070b)), .radius = 14, .clip = true,
                             .label = std::string("Program monitor")},
                            {column({.id = "title_card", .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .gap = 8, .justify = "center", .align = "center",
                                     .opacity = level("title")},
                                    {brand_mark("mark", mw * 0.14f), text({.id = "name", .text = std::string("a short"), .style = "title", .color = Fill(Color::hex(0xe6edf7))})}),
                             column({.id = "fight_card", .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .justify = "center", .align = "center",
                                     .opacity = level("the fight")},
                                    {std::move(fighters)}),
                             column({.id = "time", .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(10.0f, 14.0f), .justify = "end"},
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
                                    .max_width = Dim(room >= 900 ? 640.0f : std::min(room, 720.0f))}),
                          s, Arrival::lines);
    doc.prop("min_width", Source(Value(0.0)));
    auto rail = [s, id](std::string_view lang) {
        std::vector<Element> entries;
        entries.push_back(text({.id = "label", .text = std::string(lang == "zh" ? "本页内容" : "On this page"), .style = "caption", .color = "text_muted"}));
        const mlibs::markdown::Document d = mlibs::markdown::parse(document(id, lang));
        std::size_t i = 0;
        mlibs::markdown::walk(d.root, [&](const mlibs::markdown::Block& b) {
            if (b.kind != mlibs::markdown::Kind::heading || b.level > 2) return;
            ButtonOptions o{.id = std::format("s{}", i++), .text = mlibs::markdown::plain_text(b), .variant = ButtonVariant::ghost};
            o.max_width = Dim(200.0f);
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
    if (room >= 900) {
        Element contents = column({.id = "contents", .width = Dim(200.0f), .gap = 2, .label = "Sections"}, {dynamic([s, rail] { return rail(s->lang.get()); })});
        contents.prop("shrink", Source(Value(0.0)));
        both.push_back(std::move(contents));
    }
    // Narrower, one column: the document as wide as there is room (at most its measure).
    return room >= 900 ? row({.id = "read", .min_width = Dim(0.0f), .gap = 32, .align = "start", .grow = 1}, std::move(both))
                        : column({.id = "read", .min_width = Dim(0.0f)}, std::move(both));
}

// A place: its page, scrolled, its content as wide as the design's measure, centred.
Element place_page(const std::shared_ptr<Site>& s, const std::string& id) {
    std::string scroll_key = id;
    if (id == "components") scroll_key = canonical_route(id, "en", s->gallery.selected.get());
    else if (id == "motion" || id == "explore") {
        const auto& gallery = id == "motion" ? s->basic : s->creative;
        scroll_key = canonical_route(id, "en", {}, gallery.detail.get() ? gallery.selected.get() : std::string());
    }
    const float view = s->room.get();
    const bool narrow = view < 700;
    const float margin = narrow ? 20 : 48;
    const float room = std::min(content_width, view - 2 * margin);
    std::vector<Element> content;
    if (id == "home") {
        content.push_back(home_landing(s->landing, s->landing_site, room));
        content.push_back(home_scenes(s->scenes, s->scenes_site, room));
    } else if (id == "release") {
        content.push_back(hero(s->hero, s->hero_site, true, room));
        // A screen a strength: your agent, time, motion, speed; then what it is, in words.
        content.push_back(agent_screen(s->screens_site, room));
        content.push_back(time_screen(s->screens_site, room));
        content.push_back(motion_screen(s->screens, s->screens_site, room));
        content.push_back(fast_screen(s->screens_site, room));
        content.push_back(home_destinations(s->links, room));
    } else if (id == "components") {
        // The wall first (every component at once, live up close), the library under it; a
        // component's detail opened, the page is that component's (back, the wall as it was left).
        if (room >= 900)
            content.push_back(dynamic([s, room] {
                if (!s->gallery.selected.get().empty()) return std::vector<Element>{};
                return std::vector<Element>{component_wall(s->wall, s->gallery, s->gallery_site, room, 600)};
            }));
        content.push_back(gallery(s->gallery, s->gallery_site, true, room));
    } else if (id == "motion") {
        content.push_back(motion_gallery(s->basic, s->basic_site, false, room));
    } else if (id == "styles") {
        content.push_back(styles_feature(s->style_examples, s->style_site, room));
    } else if (id == "interface") {
        content.push_back(interface_feature(s->interface_examples, s->interface_site, room));
    } else if (id == "agent") {
        content.push_back(agent_feature(s->agent_feature_state, s->agent_site, room));
    } else if (id == "hello") {
        content.push_back(hello_page(s, room));
    } else if (id == "timeline") {
        content.push_back(timeline_page(s, room));
    } else if (id == "reactions") {
        content.push_back(inspector::cellular::view(s->reactions, room, s->lang.get() == "zh"));
    } else if (id == "brand") {
        content.push_back(brand_study(s->brand, s->runtime, s->lang, room));
    } else if (id == "explore") {
        content.push_back(motion_gallery(s->creative, s->creative_site, true, room));
        content.push_back(explore_landing(s->links, room));
    } else if (id == "docs") {
        content.push_back(docs_landing(s->docs, s->links, room >= 1000 ? room - 196 : room));
    } else if (id == "not_found") {
        auto message = [s](std::string en, std::string zh) { return computed([s, en, zh] { return s->lang.get() == "zh" ? zh : en; }); };
        content.push_back(column({.id = "missing", .gap = 24}, {
            text({.id = "title", .text = message("This page isn't here.", "没有找到这一页。"), .style = "title"}).prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1}))),
            text({.id = "route", .text = s->missing_route, .style = "code", .origin = "user_content", .max_width = Dim(room)}),
            button({.id = "home", .text = message("Back to home", "回到首页"), .variant = ButtonVariant::primary, .on_activated = [s] { s->go("home"); }, .align_self = "start"})}));
    } else {
        content.push_back(document_page(s, id, room >= 1000 ? room - 196 : room));
    }
    Element body = id == "hello" || id == "timeline" ? std::move(content.front())
                                                     : column({.id = "page", .gap = 64}, std::move(content));
    if (is_document(id)) {
        const bool rail = room >= 1000 && id != "hello";
        body.children.insert(body.children.begin(), docs_directory(s->links, id, rail));
        body.prop("direction", Source(Value(rail ? "row" : "column")));
        body.prop("gap", Source(Value(32.0)));
    }
    // The design's measure, centred in the window (`…/<place>/page` is where its tree is read).
    body.prop("width", Source(Value("100%")));
    body.prop("max_width", Source(Value(static_cast<double>(content_width))));
    body.prop("align_self", Source(Value("center")));
    // Filling the view (a stack: the place under the lens's renderings).
    Element page = scroll({.id = id, .y = static_cast<float>(s->scroll_positions[scroll_key]), .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(narrow ? 24.0f : 56.0f, margin), .background = "background"},
                          {std::move(body)});
    // The home page's scroll is its motion screen's time.
    page.on("scrolled", [s, id, scroll_key](const Value& at) {
        const double y = at.get("y") ? at.get("y")->as_double() : 0;
        s->scroll_positions[scroll_key] = y;
        if (id == "release") scrolled(s->screens, s->screens_site, std::string(place_root) + "release", y);
    });
    return page;
}

}  // namespace

AppSpec app(std::string_view place, std::string_view lang, std::string_view incoming_route) {
    AppSpec a;
    a.id = "org.openmui.site";
    a.title = "omui — one tree, many renderings";
    a.size = {1280, 800};
    a.description = "omui's own website: a UI framework implementing the openmui specification. Its components, identity and tools are live, and your agent can operate them.";
    // What agent.md asks of an agent once it is linked (the panel is open: the person copied the prompt there).
    a.agent_check = {"main/body/guide/check/hello", "main/body/guide/check/hellos", "pressed 1 time"};
    // Mui, the agent's body on the page (the site plan §9.2.1).
    a.agent_skill =
        "Mui (`main/body/view/mui`, a pixel cat) is your body on the page. When you act on a node it goes there, its eyes lit, and points; "
        "while a request of yours waits for the person (exit code 2) it holds up a card — the answer stays the person's. You can also ask it: "
        "`move_to {node}`, `point_at {node}`, `look_at {node}`, `say {text}` (one line), `sleep`, `wake`. Everything else on the page you "
        "operate yourself, except what asks the person on purpose (publishing, deleting everything, opening another site).";
    // The site's own theme (its tokens: the design language), over the framework's default themes and the style packs
    // (the components page shows them in a scope of its own).
    a.theme = site_theme();
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
    inspector::cellular::add_verbs(a, s->reactions);
    for(const auto& desc:motion_control_verbs())a.verbs.push_back({desc,[s,name=desc.name](const Value& args)->VerbResult {
        const auto place=s->place.peek();
        if(place=="motion")return motion_control(s->basic,s->basic_site,name,args);
        if(place=="explore")return motion_control(s->creative,s->creative_site,name,args);
        return std::unexpected("Open the animation or creative gallery first");
    }});
    if (const auto parsed = parse_site_route(place, lang)) {
        s->place.set(parsed->place);
        s->lang.set(parsed->language);
        s->gallery.selected.set(parsed->component);
        if (parsed->place == "motion") { s->basic.selected.set(parsed->example); s->basic.detail.set(!parsed->example.empty()); }
        if (parsed->place == "explore") { s->creative.selected.set(parsed->example); s->creative.detail.set(!parsed->example.empty()); }
        if (parsed->place == "agent") s->agent_feature_state.mode.set(parsed->example.empty() ? "use" : parsed->example);
    } else {
        s->place.set(std::string("not_found"));
        s->missing_route.set(std::string(incoming_route.empty() ? place : incoming_route));
        s->lang.set(lang == "zh" || place.starts_with("zh/") ? std::string("zh") : std::string("en"));
    }
    s->initial_route = incoming_route;
    s->links = {.language = &s->lang, .go = [p = s.get()](std::string id) { p->go(id); },
                .open_url = [p = s.get()](std::string href) { if (p->runtime) (void)p->runtime->open_url(href); },
                .document = [](std::string_view id, std::string_view language) { return document(id, language); }};
    a.setup = [s](Runtime& rt) {
        s->runtime = &rt;
        s->reactions->runtime = &rt;
        register_type(component_type<Lens>());
        register_base_components();
        register_type(component_type<Timebar>());
        register_type(component_type<FrameMeter>());
        register_type(component_type<Mascot>());
        register_type(component_type<DropCard>());
        fill_gallery_models(rt);  // the components page's tree view and data grid
        s->screens_site = {.runtime = &s->runtime, .tasks = &s->hero,
                           .lang = &s->lang,
                           .hash = &s->hash,
                           .effects = &s->effects,
                           .copy_prompt = [p = s.get()] {
                               p->settings.set(false), p->checking.set(false);
                               p->panel.set(true);
                               p->give_prompt();
                           },
                           .open_agent = [p = s.get()] { p->settings.set(false), p->checking.set(false), p->panel.set(true); },
                           .open_check = [p = s.get()] {
                               if (!p->replay) p->want_frame(p->ref_target);
                               p->panel.set(false), p->settings.set(false);
                               p->checking.set(true);
                           },
                           .replay = [p = s.get()] { p->tools.set(true); p->backwards(); },
                           .set_effects = [p = s.get()](std::string v) { p->choose_effects(std::move(v)); }};
        s->hero_site = {.runtime = &s->runtime,
                        .lang = &s->lang,
                        .nodes = &s->nodes,
                        .root = [] { return std::string(place_root) + "release/page"; },
                        .go = [p = s.get()](std::string id) { p->go(id); },
                        .tree_lens = [p = s.get()] {
                            if (p->lens.peek() != "tree") p->mark(p->lang.peek() == "zh" ? "镜头 Tree" : "Lens Tree");
                            p->tools.set(true);
                            p->lens.set("tree");
                            p->soon();
                        }};
        // The components page in a theme scope of its own: plain (the site's theme), or a style pack.
        rt.set_scope(gallery_scope, rt.theme());
        s->gallery_site = {.lang = &s->lang, .tree = &s->nodes,
                           .open = [p = s.get()](std::string id) { p->open_component(std::move(id)); },
                           .clipboard = [p = s.get()](std::string text) { if (p->runtime) p->runtime->set_clipboard_text(std::move(text)); },
                           .style = [p = s.get()](std::string pack) {
                               if (!p->runtime) return;
                               if (pack == "plain") {
                                   p->runtime->crossfade(seconds(0.35), gallery_scope);
                                   p->runtime->set_scope(gallery_scope, p->runtime->theme());
                               } else if (auto r = theme::apply_style(*p->runtime, pack, gallery_scope, true); !r) {
                                   site_log.warn("the style {}: {}", pack, r.error());
                               }
                               p->soon();
                           }, .runtime = &s->runtime};
        for (const auto scope : {"motion-preview", "creative-preview", "style-feature", "interface-feature", "agent-design"}) rt.set_scope(scope, rt.theme());
        s->landing_site = {.runtime = &s->runtime, .lang = &s->lang, .go = [p = s.get()](std::string id) { p->go(id); }};
        s->scenes_site = {.runtime = &s->runtime, .lang = &s->lang, .lens = &s->lens, .tools = &s->tools};
        s->basic_site = {.runtime = &s->runtime, .lang = &s->lang,
                         .open_detail = [p = s.get()](std::string id) { p->open_example(false, std::move(id)); },
                         .style = [p = s.get()](std::string pack) { p->apply_scope("motion-preview", std::move(pack)); },
                         .root = "main/body/view/motion/page/motion_gallery", .scope = "motion-preview", .foreground=&s->foreground};
        s->creative_site = {.runtime = &s->runtime, .lang = &s->lang,
                            .open_detail = [p = s.get()](std::string id) { p->open_example(true, std::move(id)); },
                            .style = [p = s.get()](std::string pack) { p->apply_scope("creative-preview", std::move(pack)); },
                            .root = "main/body/view/explore/page/motion_gallery", .scope = "creative-preview", .foreground=&s->foreground};
        s->style_site = {.runtime = &s->runtime, .lang = &s->lang,
                         .style = [p = s.get()](std::string pack) { p->apply_scope("style-feature", std::move(pack)); },
                         .go = [p = s.get()](std::string id) { p->go(id); },
                         .root = "main/body/view/styles/page/styles_feature", .scope = "style-feature"};
        s->interface_site = {.runtime = &s->runtime, .lang = &s->lang,
                             .style = [p = s.get()](std::string pack) { p->apply_scope("interface-feature", std::move(pack)); },
                             .go = [p = s.get()](std::string id) { p->go(id); },
                             .root = "main/body/view/interface/page/interface_feature", .scope = "interface-feature"};
        s->agent_site = {.runtime = &s->runtime, .lang = &s->lang,
                         .connect = [p = s.get()] { p->panel.set(true); p->give_prompt(); },
                         .style = [p = s.get()](std::string pack) { p->apply_scope("agent-design", std::move(pack)); },
                         .open_mode = [p = s.get()](std::string mode) { p->open_agent_mode(std::move(mode)); },
                         .preview_blocked = [p = s.get()] {
                             return !p->foreground || (p->compact.peek() && (p->panel.peek() || p->settings.peek() || p->checking.peek() || p->menu.peek()));
                         }};
        s->compact.set(rt.size().width < Site::compact_below);
        s->width.set(rt.size().width);
        s->room.set(rt.size().width);
        s->band.set(Site::band_of(rt.size().width));
        if ((rt.reduce_motion() || rt.motion_level() != MotionLevel::full)) s->logo_unfolded.set(false);
        else s->logo_timer = rt.after(seconds(0.15), [p = s.get()] { p->logo_timer = 0; p->logo_unfolded.set(false); });
        s->guide = std::make_unique<Guide>(rt, [site = s.get()] { return site->place.get(); });
        {
            const auto it = std::ranges::find_if(all, [&](const Place& p) { return p.id == s->place.peek(); });
            const bool zh = s->lang.peek() == "zh";
            if (it != all.end()) s->mark(std::string(zh ? it->zh : it->en));
        }
        (void)rt.stage_plugins().add({.name = "openmui.site.preview_input_guard", .kind = "stage", .description = "person input cancels pending task previews before timers",
                                      .cost = mlibs::pipeline::Cost{.cpu_us = 50}},
                               [site = s.get()](const mlibs::pipeline::Config&) -> std::expected<std::unique_ptr<mlibs::pipeline::Stage>, std::string> {
                                   return std::make_unique<PreviewInputGuard>(*site);
                               });
        auto guarded = rt.frame_pipeline().description();
        guarded.stages.insert(guarded.stages.begin(), {"preview_input_guard", "openmui.site.preview_input_guard", {}, {}});
        for (auto& stage : guarded.stages) if (stage.name == "timers") stage.after.push_back("preview_input_guard");
        (void)rt.stage_plugins().add({.name="omui.site.program_motion",.kind="stage",
            .description="public Hanim programs publish selected UI poses before bounds and paint",
            .trace_category="omui.hanim.playback",.cost=mlibs::pipeline::Cost{.cpu_us=50}},
            [site=s.get()](const mlibs::pipeline::Config&)->std::expected<std::unique_ptr<mlibs::pipeline::Stage>,std::string>{
                return std::make_unique<ProgramMotionStage>(*site);
            });
        const auto bounds=std::ranges::find_if(guarded.stages,[](const auto& stage){return stage.name=="bounds";});
        if(bounds!=guarded.stages.end()) {
            bounds->after.push_back("program_motion");
            guarded.stages.insert(bounds,{"program_motion","omui.site.program_motion",{"layout"},{}});
        }
        const std::array<mlibs::pipeline::Port, 2> frame_inputs{
            mlibs::pipeline::port<FrameInput>(std::string(frame_resource::input)), mlibs::pipeline::port<FrameTree>(std::string(frame_resource::tree))};
        if (auto ready = mlibs::pipeline::Pipeline::build(guarded, rt.stage_plugins(), frame_inputs); ready)
            rt.frame_pipeline() = std::move(*ready);
        else site_log.warn("the preview input guard is not in the frame: {}", mlibs::pipeline::to_text(ready.error()));
        (void)rt.stage_plugins().add({.name = "openmui.site.tree_watch", .kind = "stage", .description = "the site's place read again when its tree changed",
                                      .cost = mlibs::pipeline::Cost{.cpu_us = 200}},
                               [site = s.get()](const mlibs::pipeline::Config&) -> std::expected<std::unique_ptr<mlibs::pipeline::Stage>, std::string> {
                                   return std::make_unique<TreeWatch>(*site);
                               });
        if (auto in = rt.frame_pipeline().insert({"tree_watch", "openmui.site.tree_watch", {"paint"}, {}}, rt.stage_plugins()); !in)
            site_log.warn("the tree watch is not in the frame: {}", mlibs::pipeline::to_text(in.error()));
        if (!s->initial_route.empty()) rt.set_route(s->initial_route);
        else s->show_route();
        s->soon();
    };
    a.view = [s] {
        auto say = [s](std::string_view en, std::string_view han) {
            return computed([s, en = std::string(en), han = std::string(han)] { return s->lang.get() == "zh" ? han : en; });
        };
        auto wide = [s] { return !s->compact.get(); };
        Element bar = dynamic([s] {
            const bool compact = s->compact.get(), menu = s->menu.get();
            auto label = [s](std::string_view en, std::string_view zh) {
                return computed([s, en = std::string(en), zh = std::string(zh)] { return s->lang.get() == "zh" ? zh : en; });
            };
            std::vector<Element> links;
            std::vector<Element> core_links;
            for (const Place& p : all) {
                if (!p.navigation) continue;
                const std::string id(p.id);
                auto selected = [s, id] {
                    const Place* current = find_place(s->place.get());
                    return current && (current->id == id || current->section == id);
                };
                Element b = button({.id = "to_" + id, .text = label(p.en, p.zh),
                                    .variant = computed([selected] { return selected() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                                    .on_activated = [s, id] { s->go(id); }});
                b.prop("selected", derive(selected));
                // Navigation uses the same typed default recipe as other
                // buttons. Large letter-scatter effects belong in a scene.
                if (p.section == "core") core_links.push_back(std::move(b));
                else links.push_back(std::move(b));
            }
            auto core_open = button({.id = "open", .text = label("Core features", "核心特色"),
                .variant = computed([s] { const auto* p = find_place(s->place.get()); return s->core_menu.get() || (p && p->section == "core") ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                .on_activated = [s] { s->core_menu.set(!s->core_menu.peek()); }});
            core_open.prop("expanded", derive([s] { return s->core_menu.get(); }));
            PopoverOptions core_options;
            core_options.id = "menu"; core_options.label = label("Core features", "核心特色");
            core_options.open = s->core_menu; core_options.side = PopoverSide::below;
            core_options.width = Dim(std::min(300.0f, s->width.get() - 48));
            auto core_panel = popover(std::move(core_options), std::move(core_links));
            core_panel.prop("direction", Source(Value("column"))).prop("gap", Source(Value(4)));
            links.insert(links.begin(), row({.id = "core", .gap = 0, .align = "center"}, {std::move(core_open), std::move(core_panel)}));
            Element nav = row({.id = "nav", .gap = 4, .align = "center", .wrap = "wrap", .label = "Places"}, std::move(links));
            nav.prop("role", Source(Value("navigation")));
            if (compact) nav.prop("width", Source(Value("100%")));
            Element start = button({.id = "start", .text = label("Get started", "开始使用"), .variant = ButtonVariant::primary,
                                    .on_activated = [s] { s->go("hello"); }});
            Element settings = button({.id = "settings_toggle", .text = label("Settings", "设置"), .variant = ButtonVariant::ghost,
                                        .on_activated = [s] {
                                            s->settings.set(!s->settings.peek());
                                            if (s->settings.peek()) s->panel.set(false), s->checking.set(false), s->menu.set(false);
                                        }});
            std::vector<Element> items;
            items.push_back(animated_brand_mark("logo", compact ? 24.0f : 32.0f, s->logo_unfolded));
            Element home = button({.id = "mark", .text = "omui", .variant = ButtonVariant::ghost,
                                   .on_activated = [s] { s->go("home"); }, .description = "omui — home"});
            home.prop("selected", derive([s] { return s->place.get() == "home"; }));
            home.on("pointer_enter", [s](const Value&) { if (s->runtime && !s->runtime->reduce_motion() && s->runtime->motion_level() == MotionLevel::full) s->logo_unfolded.set(true); });
            home.on("pointer_leave", [s](const Value&) { s->logo_unfolded.set(false); });
            home.on("focus", [s](const Value&) { if (s->runtime && !s->runtime->reduce_motion() && s->runtime->motion_level() == MotionLevel::full) s->logo_unfolded.set(true); });
            home.on("blur", [s](const Value&) { s->logo_unfolded.set(false); });
            items.push_back(std::move(home));
            if (!compact) items.push_back(std::move(nav));
            items.push_back(box({.id = "fill", .grow = 1}));
            if (compact) items.push_back(button({.id = "menu_toggle", .text = label("Menu", "菜单"),
                                                .variant = menu ? ButtonVariant::secondary : ButtonVariant::ghost,
                                                .on_activated = [s] { s->menu.set(!s->menu.peek()); }}));
            if (!compact) {
                items.push_back(only_when(frame_meter({.id = "frames", .label = label("Frame rate", "帧率")}), [s] { return s->tools.get() && s->width.get() >= 1400; }));
                items.push_back(std::move(settings));
            }
            items.push_back(button({.id = "guide_toggle", .text = label("Connect", "连接"),
                                     .variant = computed([s] { return s->panel.get() ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                     .on_activated = [s] {
                                         s->panel.set(!s->panel.peek());
                                         if (s->panel.peek()) s->settings.set(false), s->checking.set(false), s->menu.set(false);
                                     }}));
            items.push_back(button({.id = "lang", .text = computed([s] { return std::string(s->lang.get() == "zh" ? "English" : "中文"); }),
                                     .variant = ButtonVariant::ghost, .on_activated = [s] {
                                         s->mark(s->lang.peek() == "zh" ? "English" : "中文");
                                         s->switched_at = s->runtime ? s->runtime->now().count() : 0;
                                         s->lang.set(s->lang.get() == "zh" ? "en" : "zh");
                                         s->switches.set(s->switches.get() + 1);
                                         s->show_route(); s->soon();
                                     }}));
            if (!compact) items.push_back(std::move(start));
            if (compact && menu) {
                items.push_back(std::move(nav));
                items.push_back(std::move(start));
                items.push_back(std::move(settings));
            }
            if (compact) for (auto& item : items)
                if (item.id == "mark" || item.id == "menu_toggle" || item.id == "guide_toggle" || item.id == "lang")
                    item.prop("padding_x", Source(Value(4.0)));
            return std::vector<Element>{row({.id = "bar", .padding = Edges(12.0f, compact ? 16.0f : 24.0f), .gap = compact ? 4.0f : 8.0f, .align = "center", .wrap = "wrap", .background = "bar"}, std::move(items))};
        });
        // The place, one at a time; built again when another is shown or the window crosses a width band.
        std::vector<Element> shown;
        shown.push_back(dynamic([s] {
            (void)s->band.get();
            return std::vector<Element>{place_page(s, s->place.get())};
        }));
        // Over the place, as the lens says.
        shown.push_back(fades_in(lens({.id = "wireframe", .nodes = s->nodes, .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100))}),
                                 [s] { return s->lens.get() == "tree"; }));
        for (const std::string_view which : {"agent", "test"}) {
            const std::string key(which);
            shown.push_back(fades_in(scroll({.id = "lens_" + key, .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100)), .padding = Edges(32.0f, 48.0f), .background = "background"},
                                             {text({.id = key + "_text", .text = key == "agent" ? s->agent : s->test, .style = "code"})}),
                                      [s, key] { return s->lens.get() == key; }));
        }
        // Mui, over the place (not over the documents: they stay plain, §9.4), as the pixels show it.
        // The first time, it comes alive from the home's mark (D3): its pixels gather on the O, it drops to the page.
        shown.push_back(only_when(mascot({.id = "mui", .arrive = "main/body/view/home/page/landing/scene/visual/mark", .arrive_after = 1.0, .width = Dim(mlibs::layout::pct(100)), .height = Dim(mlibs::layout::pct(100))}),
                                  [s] { return !is_document(s->place.get()) && s->lens.get() == "pixels"; }));
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
        Element foot = row({.id = "foot", .padding = Edges(6.0f, 16.0f), .gap = 6, .align = "center", .wrap = "wrap", .background = "bar"}, {
            button({.id = "tools_toggle", .text = say("Inspect", "检查"),
                     .variant = computed([s] { return s->tools.get() ? ButtonVariant::secondary : ButtonVariant::ghost; }),
                     .on_activated = [s] {
                         s->tools.set(!s->tools.peek());
                         if (!s->tools.peek()) { s->lens.set(std::string("pixels")); s->checking.set(false); s->live(); }
                     }}),
            dynamic([s, mode_is] {
                if (!s->tools.get()) return std::vector<Element>{text({.id = "signature", .text = std::string("Built with omui"), .style = "caption", .color = "text_muted"})};
                return untracked([s, mode_is] {
                auto wide = [s] { return !s->compact.get(); };
                auto say = [s](std::string_view en, std::string_view zh) {
                    return computed([s, en = std::string(en), zh = std::string(zh)] { return s->lang.get() == "zh" ? zh : en; });
                };
                return std::vector<Element>{lens_button(s, "pixels", "Pixels"), lens_button(s, "tree", "Tree"), lens_button(s, "agent", "Agent"), lens_button(s, "test", "Test"),
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
                                                  if (open && !s->replay) s->want_frame(s->ref_target);
                                                  s->checking.set(open);
                                                  if (open) s->panel.set(false), s->settings.set(false);
                                              }}),
                                      wide)};
                });
            })});
        Element site = column({.id = "site", .grow = 1, .background = "background"}, {std::move(bar), std::move(body), std::move(foot)});
        // Esc puts away what is open over the place: a panel.
        site.on("key_down", [s](const Value& e) {
            if (!e.get("key") || e.get("key")->as_string() != "Escape") return;
            for (Signal<bool>* open : {&s->menu, &s->core_menu, &s->panel, &s->settings, &s->checking})
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
    const Theme theme = site_theme();
    const bool first_chinese = lang == "zh";
    std::string out = std::format("<main id=\"omui-static\" lang=\"{}\">\n<a class=\"skip\" href=\"#/{}home\">{}</a>\n",
                                  first_chinese ? "zh-CN" : "en", first_chinese ? "zh/" : "", first_chinese ? "跳到正文" : "Skip to content");
    auto href = [](std::string_view place, bool zh) {
        const std::string route = place == "home" ? (zh ? "zh/home" : "home") : canonical_route(place, zh ? "zh" : "en");
        return "#/" + route;
    };
    const std::array<bool, 2> languages{first_chinese, !first_chinese};
    for (const bool chinese : languages) {
        out += std::format("<nav aria-label=\"{}\" data-locale=\"{}\"{}>", chinese ? "页面" : "Places", chinese ? "zh" : "en", chinese == first_chinese ? " data-default" : "");
        out += std::format("<a class=\"brand\" href=\"{}\" data-omui-target=\"main/bar/mark\" aria-label=\"omui\">{}<b>omui</b></a> ", href("home", chinese), brand_svg(theme.color("accent")));
        out += std::format("<details class=\"core-navigation\"><summary data-omui-target=\"main/bar/nav/core/open\">{}</summary>", chinese ? "核心特色" : "Core features");
        for (const Place& p : all)
            if (p.navigation && p.section == "core") out += std::format("<a href=\"{}\" data-omui-target=\"main/bar/nav/core/open\">{}</a> ", href(p.id, chinese), chinese ? p.zh : p.en);
        out += "</details>";
        for (const Place& p : all)
            if (p.navigation && p.section != "core") out += std::format("<a href=\"{}\" data-omui-target=\"main/bar/nav/to_{}\">{}</a> ", href(p.id, chinese), p.id, chinese ? p.zh : p.en);
        out += std::format("<a href=\"{}\" data-omui-target=\"main/bar/start\">{}</a></nav>\n", href("hello", chinese), chinese ? "开始使用" : "Get started");
    }
    out += "<div class=\"routes\">\n";
    for (const bool chinese : languages) {
        const std::string language = chinese ? "zh" : "en";
        for (const Place& p : all) {
            out += std::format("<section id=\"{}\" lang=\"{}\" data-place=\"{}\"{}>\n", href(p.id, chinese).substr(1), chinese ? "zh-CN" : "en", p.id,
                               p.id == "home" && chinese == first_chinese ? " data-default" : "");
            const std::string alias = std::format("/{}{}", chinese ? "zh/" : "", p.id);
            if (alias != href(p.id, chinese).substr(1)) out += std::format("<span class=\"route-alias\" id=\"{}\"></span>", alias);
            if (p.id == "home") {
                out += std::format("<span class=\"route-alias\" id=\"{}\"></span>", chinese ? "/zh" : "/");
                out += "<div class=\"landing-static\"><div>" + home_landing_html(chinese) + "</div><div class=\"landing-mark\">" + brand_svg(theme.color("accent")) + "</div></div>";
            } else if (p.id == "release") out += hero_html(chinese) + destinations_html(chinese);
            else if (p.id == "motion") out += motion_gallery_html(chinese, false);
            else if (p.id == "styles") out += chinese ? "<h1>同一语义，不同材质。</h1><p>对照同一组真实控件，切换玻璃、墨纸、霓虹、复古与液态风格。交互演示需要 JavaScript；参数与源码按需展开。</p>" : "<h1>Same meaning. A new material.</h1><p>Compare the same live controls in glass, ink, neon, retro and liquid styles. Interactive previews need JavaScript; parameters and source open on request.</p>";
            else if (p.id == "interface") out += chinese ? "<h1>一套接口，多种入口。</h1><p>修改真实工作台并读回同一对象的语义；查看详情后对照 C++、TypeScript、Python、Lua 的接口表达式。</p>" : "<h1>One interface. Every way in.</h1><p>Edit a live workspace and read the same object's semantics. Open details to compare C++, TypeScript, Python and Lua API expressions.</p>";
            else if (p.id == "agent") out += chinese ? "<h1>让 agent 直接理解界面。</h1><p>使用、开发、设计：通过真实语义操作、动作回执和可撤销记录理解 Agent 原生。内置脚本明确标注为演示，连接入口用于你自己的 agent。</p>" : "<h1>An interface your agent can understand.</h1><p>Use, develop and design with real semantic actions, receipts and undoable records. Scripted previews are marked; connect your own agent to operate this page.</p>";
            else if (p.id == "brand") out += brand_html(chinese, theme.color("accent"));
            else if (p.id == "reactions") out += chinese
                ? "<h1>给火种，留一条路。</h1><p>反应实验室：编辑路径与屏障，逐步观察整数规则的演变，回退并保存工程。交互需要 JavaScript，也可通过 Inspector 的 CLI 与 Agent 操作。</p>"
                : "<h1>Make room for a spark.</h1><p>Reaction lab: edit paths and barriers, inspect integer rules step by step, rewind and save a project. Interaction needs JavaScript; the Inspector also provides CLI and Agent operations.</p>";
            else if (p.id == "docs") out += docs_html(chinese);
            else if (p.id == "explore") out += motion_gallery_html(chinese, true);
            if (const std::string_view doc = document(p.id, language); !doc.empty()) {
                if (p.id == "hello") out += std::format("<h1>{}</h1>", chinese ? "开始使用" : "Get started");
                std::string html = mlibs::markdown::to_html(mlibs::markdown::parse(doc));
                for (const auto& [file, place] : {std::pair{"best-practices.md", "practices"}, std::pair{"features.md", "features"}})
                    for (std::string from = std::format("href=\"{}", file), to = std::format("href=\"{}", href(place, chinese));;) {
                        const auto at = html.find(from);
                        if (at == std::string::npos) break;
                        const auto end = html.find('"', at + 6);
                        html.replace(at, end - at, to);
                    }
                out += html;
                if (p.id == "hello") out += std::format("<pre><code>{}</code></pre>", escaped(hello::hello_source));
            } else if (p.id == "components") {
                out += gallery_html(chinese);
            } else if (p.id == "timeline")
                out += chinese ? "<h1>时间线编辑器</h1>\n<p>拖动、修剪、切开，每次编辑都能撤销。运行编辑器需要 JavaScript。</p>\n"
                               : "<h1>A timeline editor</h1>\n<p>Drag, trim and split clips, then undo. Running the editor needs JavaScript.</p>\n";
            out += "</section>\n";
        }
        for (const auto& component : component_catalog) {
            const std::string route = canonical_route("components", language, component.id);
            out += std::format("<section id=\"/{}\" lang=\"{}\" data-place=\"components\" data-live-root=\"main/body/view/components/page/gallery/detail/{}\">", route, chinese ? "zh-CN" : "en", component.id);
            const std::string alias = std::string(chinese ? "zh/" : "") + "components/" + std::string(component.id);
            if (alias != route) out += std::format("<span class=\"route-alias\" id=\"/{}\"></span>", alias);
            out += gallery_html(chinese, component.id) + "</section>\n";
        }
        for (const bool creative : {false, true}) for (const auto& item : creative ? creative_motions() : basic_motions()) {
            const auto place = creative ? "explore" : "motion";
            const std::string route = canonical_route(place, language, {}, item.id);
            out += std::format("<section id=\"/{}\" lang=\"{}\" data-place=\"{}\" data-live-root=\"main/body/view/{}/page/motion_gallery/stage/scene\" data-live-name=\"{}\">", route, chinese ? "zh-CN" : "en", place, place, escaped(chinese ? item.zh : item.en));
            out += motion_gallery_html(chinese, creative, item.id) + "</section>\n";
        }
        for (const auto mode : {"use", "develop", "design"}) {
            out += std::format("<section id=\"/{}\" lang=\"{}\" data-place=\"agent\" data-live-root=\"main/body/view/agent/page/agent_feature\"><h1>{}</h1><p>{}</p></section>",
                canonical_route("agent", language, {}, mode), chinese ? "zh-CN" : "en", chinese ? "Agent 原生" : "Agent native", mode);
        }
        out += std::format("<section id=\"/{}not_found\" lang=\"{}\" data-place=\"not_found\"><h1>{}</h1><code data-missing-route></code><p><a href=\"{}\">{}</a></p></section>",
                           chinese ? "zh/" : "", chinese ? "zh-CN" : "en", chinese ? "没有找到这一页。" : "This page isn't here.", href("home", chinese), chinese ? "回到首页" : "Back to home");
    }
    return out + "</div></main>\n";
}

std::optional<std::string> inject(std::string_view page, std::string_view fragment, std::string_view lang, std::string_view opening_assets) {
    std::string out(page);
    const auto canvas = out.find("<canvas id=\"canvas\"");
    const auto head = out.find("</head>");
    const auto loader = out.rfind("<script src=\"");
    if (canvas == std::string::npos || head == std::string::npos || loader == std::string::npos || loader < canvas) return std::nullopt;
    if (out.contains("id=\"omui-static\"")) return std::nullopt;  // once
    // The static view is handed over only after presented pixels and the live mirror agree on the route.
    std::string opening_data;
    if (!opening_assets.empty()) {
        const auto parsed = parse_json(opening_assets);
        if (!parsed || !parsed->get("version") || parsed->get("version")->as_int() != 1 ||
            !parsed->get("previews") || !parsed->get("previews")->list()) return std::nullopt;
        // JSON is inert data. Escape HTML's script terminator even if a
        // future catalogue adds text originating outside the build.
        for (const char c : opening_assets) {
            if (c == '<') opening_data += "\\u003c";
            else if (c == '>') opening_data += "\\u003e";
            else if (c == '&') opening_data += "\\u0026";
            else opening_data += c;
        }
        opening_data = "<script id=\"omui-first-frames\" type=\"application/json\">" + opening_data + "</script>\n";
    }
    out.insert(loader, opening_data + "<script>" + std::string(files::static_bootstrap) + "</script>\n");
    out.insert(canvas, std::string(fragment));
    // The static page in the program's own fonts: the files the page already lists for it (omuiFonts).
    std::string faces;
    Value::List static_fonts;
    Value font_entries;
    constexpr std::string_view font_declaration = "var omuiFonts = ";
    if (const auto at = out.find(font_declaration); at != std::string::npos) {
        const auto from = at + font_declaration.size(), to = out.find(';', from);
        if (to != std::string::npos) if (auto entries = parse_json(std::string_view(out).substr(from, to - from))) font_entries = std::move(*entries);
    }
    for (const auto& [stem, family] : {std::pair{"Inter", "Inter"}, std::pair{"JetBrainsMono", "JetBrains Mono"}, std::pair{"NotoSansSC", "Noto Sans SC"}}) {
        std::string source;
        if (const auto* entries = font_entries.list()) for (const auto& entry : *entries) {
            const auto* row = entry.list();
            if (!row || row->size() < 2 || (*row)[0].as_string() != std::format("{}.ttf", stem)) continue;
            const std::size_t initial = family == std::string_view("Inter") && row->size() > 3 ? 3 : 1;
            source = (*row)[initial].as_string();
        }
        if (source.empty()) continue;
        static_fonts.push_back(Value{}.set("name", std::format("{}.ttf", stem)).set("family", std::string(family))
            .set("weight", family == std::string_view("Inter") ? "100 900" : "400")
            .set("when", family == std::string_view("Inter") ? "body" : family == std::string_view("JetBrains Mono") ? "code" : "chinese"));
        // The Chinese font only for a Chinese page: an English one fetching it for a word or two (中文) would fetch it before the
        // program, which takes it just after its first frame; the system's font draws those.
        if (family == std::string_view("Noto Sans SC") && lang != "zh") continue;
        faces += std::format("@font-face{{font-family:'{}';src:url({});font-display:swap{}}}", family, source,
                             family == std::string_view("Inter") ? ";font-weight:100 900" : "");
    }
    // In the site's own colours (site.theme.omui.toml's light tokens).
    out.insert(head,
               "<script id=\"omui-static-fonts\" type=\"application/json\">" + Value(std::move(static_fonts)).json() + "</script>"
                   "<noscript><style>" + faces + "</style></noscript><style>"
                   "#omui-static{position:fixed;inset:0;z-index:1;overflow:auto;padding:32px 48px;background:#fbfbfa;color:#16161a;"
                   "font:16px/1.6 Inter,'Noto Sans SC',system-ui,sans-serif;user-select:text}#omui-static[hidden]{display:none}"
                   "#omui-static nav{display:flex;flex-wrap:wrap;align-items:center;gap:24px;margin-bottom:32px}#omui-static a{color:#4f46e5}#omui-static nav a{font-size:14px;text-decoration:none}"
                   "#omui-static .skip{position:absolute;left:8px;top:-60px;padding:8px 14px;background:#16161a;color:#fbfbfa}#omui-static .skip:focus{top:8px}"
                   "#omui-static section{max-width:860px;margin-bottom:64px;scroll-margin-top:100vh}#omui-static code{font:14px 'JetBrains Mono',monospace;background:#f3f3f1;"
                   "padding:1px 4px;border-radius:3px}#omui-static pre{white-space:pre-wrap}#omui-static table{border-collapse:collapse}"
                   "#omui-static td,#omui-static th{border:1px solid #e6e6e3;padding:4px 8px}"
                   "#omui-static .brand{display:inline-flex;align-items:center;gap:8px}#omui-static .brand svg{width:32px;height:32px}"
                   "#omui-static .core-navigation{display:inline-block;position:relative;vertical-align:middle}"
                   "#omui-static .core-navigation[open]>a{display:block}#omui-static .core-navigation summary{cursor:pointer;color:#4f46e5}"
                   "#omui-static .static-links{display:flex;gap:24px;align-items:center}"
                   "#omui-static .static-menu{margin-left:auto;border:0;background:transparent;padding:8px;color:#4f46e5;font:inherit;cursor:pointer}"
                   "#omui-static nav[data-compact=true] .static-links{flex-basis:100%;flex-direction:column;align-items:stretch;gap:12px}"
                   "#omui-static section[data-place=home]{max-width:1120px;margin:auto}#omui-static .landing-static{display:grid;grid-template-columns:1fr 1fr;gap:64px;align-items:center;min-height:560px}"
                   "#omui-static .landing-static h1{font-size:48px;line-height:1.3;font-weight:600;letter-spacing:-1px;margin:0 0 24px}"
                   "#omui-static .landing-eyebrow{font-size:14px;color:#4f46e5;margin:0 0 24px}#omui-static .landing-lead{color:#65656d;margin:0 0 24px}"
                   "#omui-static .landing-actions{display:flex;gap:12px;align-items:center;margin:0}#omui-static .landing-actions a{text-decoration:none;padding:8px 12px;border-radius:10px}"
                   "#omui-static .landing-actions a:first-child{background:#4f46e5;color:#fff}"
                   "#omui-static .landing-mark{text-align:center}#omui-static .landing-mark svg{width:248px;height:248px}"
                   "@media(max-width:900px){#omui-static .landing-static{display:flex;flex-direction:column;gap:28px;align-items:stretch;min-height:0}#omui-static .landing-static h1{font-size:38px}}"
                   "#omui-static .omui-opening{max-width:100%;margin:28px auto;pointer-events:none}"
                   "#omui-static .omui-opening-picture{position:relative;line-height:0;overflow:hidden;border-radius:16px}"
                   "#omui-static .omui-opening-picture>img{display:block;width:100%;height:auto}"
                   "#omui-static .omui-opening-picture>svg{position:absolute;inset:0;width:100%;height:100%;fill:none;stroke:#4f46e5;stroke-width:1;overflow:hidden}"
                   "#omui-static .omui-opening-picture rect{vector-effect:non-scaling-stroke;transition:stroke-dashoffset .12s linear}"
                   "#omui-static .omui-opening[data-phase=reading] img{opacity:.3}"
                   "#omui-static .omui-opening[data-phase=initializing] svg{opacity:.18}"
                   "#omui-static .omui-opening figcaption{margin-top:10px;color:#65656d;font-size:12px;line-height:1.5;text-align:center}"
                   "#omui-static .omui-loading-note{margin:24px 0 0;color:#65656d;font-size:13px}"
                   "#omui-static .omui-loading-note button{margin-left:12px;padding:6px 12px;border:1px solid #dedee4;border-radius:8px;background:#fff;color:#4f46e5;cursor:pointer}"
                   "@media(prefers-reduced-motion:reduce){#omui-static .omui-opening-picture>svg{display:none}#omui-static .omui-opening[data-phase] img{opacity:1}#omui-static .omui-opening-picture rect{transition:none}}"
                   "#omui-static [hidden]{display:none!important}#omui-static>.routes>section{display:none}"
                   "#omui-static>.routes>section[data-default]{display:block}"
                   "#omui-static:has(.routes :target)>.routes>section{display:none}"
                   "#omui-static:has(.routes :target)>.routes>section:target,#omui-static:has(.routes :target)>.routes>section:has(.route-alias:target){display:block}"
                   "#omui-static>nav[data-locale]{display:none}#omui-static>nav[data-default]{display:flex}"
                   "#omui-static:has(.routes :target)>nav[data-locale]{display:none}"
                   "#omui-static:has(section[lang=zh-CN]:target,section[lang=zh-CN] .route-alias:target)>nav[data-locale=zh]{display:flex}"
                   "#omui-static:has(section[lang=en]:target,section[lang=en] .route-alias:target)>nav[data-locale=en]{display:flex}"
                   "@media(max-width:700px){#omui-static{padding:24px 20px}#omui-static nav{display:flex;flex-wrap:wrap}}"
                   "</style>\n<meta name=\"description\" content=\"omui: a UI framework implementing the openmui specification. One semantic tree, many renderings.\">\n");
    // A reader may zoom the page (WCAG 1.4.4): the packer's viewport forbids it.
    if (const auto at = out.find(", maximum-scale=1, user-scalable=no"); at != std::string::npos) out.erase(at, std::string_view(", maximum-scale=1, user-scalable=no").size());
    if (const auto html = out.find("<html lang=\""); html != std::string::npos) {
        const auto end = out.find('"', html + 12);
        out.replace(html + 12, end - (html + 12), lang == "zh" ? "zh-CN" : "en");
    }
    // The tab's mark is the same vector original; no second hand-maintained icon.
    const std::string svg = brand_svg(site_theme().color("accent"));
    std::string uri;
    for (const unsigned char c : svg) uri += std::format("%{:02X}", c);
    out.insert(out.find("</head>"), std::format("<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,{}\">\n", uri));
    if (const auto title = out.find("<title>"); title != std::string::npos)
        if (const auto end = out.find("</title>", title); end != std::string::npos)
            out.replace(title + 7, end - title - 7, "omui — one tree, many renderings");
    return out;
}

}  // namespace omui::site
