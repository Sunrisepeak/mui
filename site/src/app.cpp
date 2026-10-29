// The site: a bar with the lens and the language, the places down the side,
// the place in the middle (a document, or the live timeline editor) and over
// it, as the lens says, the tree's wireframe, the JSON an agent reads or the
// assertions a test writes; at the foot, the tree's hash.
module openmui.site;

import std;
import openmui;
import openmui.ui.markdown;
import openmui.ui.timeline;
import openmui.ui.canvas;
import openmui.semantic;
import openmui.trace;

namespace omui::site {

namespace {

constexpr Log site_log{"site"};

// Where the places are in the tree (the window mounts the site as "main";
// the places are the atlas canvas's live windows).
constexpr std::string_view atlas_path = "main/body/view/atlas";
constexpr std::string_view place_root = "main/body/view/atlas/";

constexpr std::array<Place, 4> all{{{"home", "Home", "首页"}, {"features", "Features", "特性"}, {"practices", "Practices", "实践"}, {"timeline", "Timeline", "时间线"}}};

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

    Signal<bool>        panel = signal(false);  // the agent panel open
    Signal<int>         switches = signal(0);   // the language switched this many times
    double              switched_at = -1;       // when it last did (the runtime's seconds)
    Signal<std::string> timeline = signal(std::string(sample));  // the timeline as last edited
    std::unique_ptr<Guide> guide;

    // The atlas: its scene (by a name of this site's own) and its layout.
    std::string         scene_name;
    AtlasLayout         layout;
    std::function<void()> kinds;  // registers the atlas's kinds again (their tiers follow the layout)

    [[nodiscard]] Canvas* atlas() const { return runtime ? runtime->component<Canvas>(runtime->find(atlas_path)) : nullptr; }
    [[nodiscard]] world::Scene& scene() const { return world::scene(scene_name); }
    // Where the tree is read: the place's window while it is live, else the whole atlas.
    [[nodiscard]] std::string root() const {
        const std::string place_path = std::string(place_root) + place.get() + "/page";
        return runtime && runtime->find(place_path) ? place_path : std::string(atlas_path);
    }
    // The camera flies to a place (or the overview), as the person's own request of the canvas.
    void fly(std::string_view to) {
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
        hash.set(pinned ? std::format("tree {:016x} · CI {:016x} {}", h, pinned_home_hash, h == pinned_home_hash ? "✓ the same tree" : "✗ not the pinned tree")
                        : std::format("tree {:016x}", h));
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
Element lens_button(const std::shared_ptr<Site>& s, std::string_view id, std::string_view label) {
    const std::string key(id);
    return button({.id = "lens_" + key,
                   .text = std::string(label),
                   .variant = computed([s, key] { return s->lens.get() == key ? ButtonVariant::primary : ButtonVariant::ghost; }),
                   .on_activated = [s, key] {
                       s->lens.set(key);
                       s->soon();
                   }});
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
            std::string text(name->as_string());
            if (text.size() > 60) text = text.substr(0, 57) + "...";
            out += std::format("check_eq(node(\"{}\").name, {});\n", id, Value(text).json());
        }
        if (++lines >= 60) break;
    }
    return out;
}

std::uint64_t tree_hash(const Value& snapshot) {
    std::uint64_t h = 1469598103934665603ull;
    const Value::List* nodes = snapshot.get("nodes") ? snapshot.get("nodes")->list() : nullptr;
    if (!nodes) return h;
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
        for (const char c : kept.json()) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    return h;
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
        {.id = "guide", .width = Dim(380.0f), .padding = Edges(20.0f, 16.0f), .gap = 10, .background = "surface"},
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
    std::map<std::string, typeset::Paragraph, std::less<>> shaped;
    bool                                                   dark = false;
    const typeset::Paragraph& get(world::DrawContext& d, std::string_view text, float size, Color colour) {
        if (d.theme.dark != dark) {
            shaped.clear();
            dark = d.theme.dark;
        }
        auto it = shaped.find(text);
        if (it == shaped.end()) {
            typeset::ParagraphStyle ps;
            ps.style.size = size;
            ps.style.weight = 650;
            ps.style.color = colour;
            ps.style.families = d.theme.text_style("title").families;
            ps.wrap = false;
            typeset::Paragraph p(*d.fonts, std::string(text), ps);
            p.layout(std::numeric_limits<float>::infinity());
            it = shaped.emplace(std::string(text), std::move(p)).first;
        }
        return it->second;
    }
};

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
    Element doc = live ? retyped(markdown({.id = "doc",
                                           .origin = MarkdownOrigin::app,
                                           .source = computed([s, id] { return std::string(document(id, s->lang.get())); }),
                                           .on_link_activated = [s](std::string href) { follow(*s, href); },
                                           .max_width = Dim(860.0f)}),
                                 s, Arrival::lines)
                       : markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = std::string(document(id, zh ? "zh" : "en")), .max_width = Dim(860.0f)});
    return scroll({.id = "page", .width = Dim(v.width), .height = Dim(v.height), .padding = Edges(32.0f, 40.0f), .background = "background"}, {std::move(doc)});
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
                                    .promote = [s](const world::Scene& sc, world::NodeRef n) { return place_window(s, std::string(sc.key(n)), true); },
                                    .view = [s](const world::Scene& sc, world::NodeRef n) { return place_window(s, std::string(sc.key(n)), false); },
                                    .role = Role::article});
    scene.kind("label", world::Kind{.tiers = {{0, Tier::Draw::custom}},
                                    .draw =
                                        [labels](world::DrawContext& d) {
                                            if (!d.fonts || d.screen.height < 6) return;
                                            const float size = std::clamp(d.screen.height * 0.42f, 13.0f, 26.0f);
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
    auto s = std::make_shared<Site>();
    if (place == "overview" || std::ranges::any_of(all, [&](const Place& p) { return p.id == place; })) s->place.set(std::string(place));
    s->lang.set(lang == "zh" ? "zh" : "en");
    a.setup = [s](Runtime& rt) {
        s->runtime = &rt;
        register_type(component_type<Lens>());
        // The atlas: a scene of this site's own, laid out for the window's size until the canvas has its own.
        static std::atomic<int> sites{0};
        s->scene_name = std::format("openmui.site.{}", sites++);
        s->kinds = [site = std::weak_ptr<Site>(s)] {
            if (auto p = site.lock()) atlas_kinds(p);
        };
        s->relayout({rt.size().width - 188, rt.size().height - 56 - 28});
        s->guide = std::make_unique<Guide>(rt, [site = s.get()] { return site->place.get(); });
        rt.stage_plugins().add({.name = "openmui.site.tree_watch", .kind = "stage", .description = "the site's place read again when its tree changed"},
                               [site = s.get()](const pipeline::Config&) -> std::expected<std::unique_ptr<pipeline::Stage>, std::string> {
                                   return std::make_unique<TreeWatch>(*site);
                               });
        if (auto in = rt.frame_pipeline().insert({"tree_watch", "openmui.site.tree_watch", {"paint"}, {}}, rt.stage_plugins()); !in)
            site_log.warn("the tree watch is not in the frame: {}", pipeline::to_text(in.error()));
        s->soon();
    };
    a.view = [s] {
        // The bar: the name, the lens, the language.
        Element bar = row({.id = "bar", .padding = Edges(10.0f, 16.0f), .gap = 6, .align = "center", .background = "surface"},
                          {text({.id = "mark", .text = "openmui", .style = "title"}),
                           retyped(text({.id = "motto", .text = computed([s] { return std::string(s->lang.get() == "zh" ? "一棵树，多种呈现" : "one tree, many renderings"); }),
                                         .style = "caption", .grow = 1}),
                                   s),
                           lens_button(s, "pixels", "Pixels"), lens_button(s, "tree", "Tree"), lens_button(s, "agent", "Agent"), lens_button(s, "test", "Test"),
                           button({.id = "guide_toggle",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "让 agent 来" : "Ask an agent"); }),
                                   .variant = computed([s] { return s->panel.get() ? ButtonVariant::primary : ButtonVariant::secondary; }),
                                   .on_activated = [s] { s->panel.set(!s->panel.get()); }}),
                           button({.id = "lang",
                                   .text = computed([s] { return std::string(s->lang.get() == "zh" ? "English" : "中文"); }),
                                   .variant = ButtonVariant::secondary,
                                   .on_activated = [s] {
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
                                          .variant = computed([s, id] { return s->place.get() == id ? ButtonVariant::primary : ButtonVariant::ghost; }),
                                          .on_activated = [s, id] { s->fly(id); }}),
                                  s));
        };
        go("overview", "Overview", "全景");
        for (const Place& p : all) go(std::string(p.id), p.en, p.zh);
        // The places: windows on one canvas, the camera flying between them.
        std::vector<Element> shown;
        Element atlas = canvas({.id = "atlas",
                                .background = Fill(Color::hex(0x05070b)),  // between the windows, darker than any page
                                .label = "openmui's places",
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
                           {column({.id = "nav", .width = Dim(188.0f), .padding = Edges(20.0f, 12.0f), .gap = 4, .background = "surface"}, std::move(nav)),
                            stack({.id = "view", .min_width = Dim(0.0f), .min_height = Dim(0.0f), .grow = 1}, std::move(shown)),
                            only_when(guide_panel(s), [s] { return s->panel.get(); })});
        Element foot = row({.id = "foot", .padding = Edges(6.0f, 16.0f), .background = "surface"}, {text({.id = "hash", .text = s->hash, .style = "caption"})});
        return column({.id = "site", .grow = 1, .background = "background"}, {std::move(bar), std::move(body), std::move(foot)});
    };
    return a;
}

std::string static_html(std::string_view lang) {
    std::string out = std::format("<main id=\"omui-static\" lang=\"{}\">\n<nav>", lang == "zh" ? "zh-CN" : "en");
    for (const Place& p : all) out += std::format("<a href=\"#/{}{}\">{}</a> ", lang == "zh" ? "zh/" : "", p.id, lang == "zh" ? p.zh : p.en);
    out += "</nav>\n";
    for (const Place& p : all) {
        out += std::format("<section id=\"/{}{}\">\n", lang == "zh" ? "zh/" : "", p.id);
        if (const std::string_view doc = document(p.id, lang); !doc.empty()) out += md::to_html(md::parse(doc));
        else out += lang == "zh" ? "<h1>时间线编辑器</h1>\n<p>一个可用的时间线编辑器：拖动、修剪、切开，每次编辑都能精确撤销。需要 JavaScript。</p>\n"
                                 : "<h1>A timeline editor</h1>\n<p>A working timeline editor: drag, trim and split clips, every edit undone exactly. It needs JavaScript.</p>\n";
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
    if (const auto html = out.find("<html lang=\""); html != std::string::npos) {
        const auto end = out.find('"', html + 12);
        out.replace(html + 12, end - (html + 12), lang == "zh" ? "zh-CN" : "en");
    }
    return out;
}

}  // namespace omui::site
