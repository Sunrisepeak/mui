// The wall's model (live text, streaming), its HUD and the application.
module wall;

import std;
import openmui;
import openmui.ui.canvas;
import openmui.ui.canvas_kit;
import openmui.ui.markdown;

namespace wall {

namespace {

constexpr std::array<std::string_view, 24> tokens{"the",  "wall", "streams", "text", "as",    "a",     "model", "writes", "it",   "token", "by",   "token",
                                                  "and",  "only", "the",     "tail", "is",    "parsed", "again", "while",  "every", "frame", "stays", "smooth"};

// A stream's n-th token: words, sentences, now and then a new paragraph.
std::string token(std::string_view key, std::uint64_t n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : key) h = (h ^ c) * 0x100000001b3ull;
    std::string t(tokens[(h + n * 7) % tokens.size()]);
    if (n % 60 == 0) return "\n\n" + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])))) + t.substr(1);
    if (n % 12 == 11) return " " + t + ".";
    return " " + t;
}

}  // namespace

Model::~Model() {
    // Nothing a kind or a timer of this model holds outlives it.
    alive_.reset();
    omui::world::drop_scene(scene);
}

const Document* Model::document(std::string_view key) const {
    for (const Document& d : docs)
        if (d.key == key) return &d;
    return nullptr;
}

omui::Signal<std::string> Model::live(std::string_view key) {
    auto it = live_.find(std::string(key));
    if (it == live_.end()) {
        const Document* d = document(key);
        it = live_.emplace(std::string(key), omui::signal(d ? d->source : std::string{})).first;
    }
    return it->second;
}

std::string Model::text(std::string_view key) const {
    if (auto it = live_.find(std::string(key)); it != live_.end()) return it->second.peek();
    const Document* d = document(key);
    return d ? d->source : std::string{};
}

omui::Canvas* Model::canvas() const { return runtime ? runtime->component<omui::Canvas>(runtime->find("main/wall")) : nullptr; }

void Model::stream(std::size_t windows, double rate) {
    stop_stream();
    if (!runtime || windows == 0) return;
    omui::world::Scene& s = omui::world::scene(scene);
    // The windows nearest the view's centre: what a person watches.
    std::vector<std::pair<double, std::string>> near;
    s.each_unordered([&](omui::world::NodeRef n) {
        if (s.kind_name(n) != "window") return;
        const auto r = s.rect(n);
        const double dx = r.x + r.width / 2 - x.peek(), dy = r.y + r.height / 2 - y.peek();
        near.emplace_back(dx * dx + dy * dy, std::string(s.key(n)));
    });
    std::ranges::sort(near);
    for (std::size_t i = 0; i < std::min(windows, near.size()); ++i) streams_.push_back({near[i].second});
    rate_ = rate;
    last_ = runtime->now();
    schedule();
}

void Model::stop_stream() {
    if (runtime && timer_) runtime->cancel(timer_);
    timer_ = 0;
    // What was written stays; the textures show all of it.
    if (runtime)
        if (auto* s = omui::world::find_scene(scene))
            for (const Stream& st : streams_)
                if (auto n = s->find(st.key)) s->touch(*n);
    streams_.clear();
}

void Model::schedule() {
    timer_ = runtime->after(omui::seconds(0.05), [this, token = std::weak_ptr<int>(alive_)] {
        if (token.expired()) return;
        timer_ = 0;
        tick();
        if (!streams_.empty()) schedule();
    });
}

// Twenty times a second: each stream's tokens due since the last tick. A
// live window gets them at once (its document appends, parsing only the
// tail); a window drawn as a texture is made stale at most four times a
// second — its texture is drawn again on the pool, the old one meanwhile.
void Model::tick() {
    const omui::seconds now = runtime->now();
    const double dt = (now - last_).count();
    last_ = now;
    omui::world::Scene* s = omui::world::find_scene(scene);
    for (Stream& st : streams_) {
        st.owed += rate_ * dt;
        const auto n = static_cast<std::uint64_t>(st.owed);
        if (n == 0) continue;
        st.owed -= static_cast<double>(n);
        std::string more;
        for (std::uint64_t i = 0; i < n; ++i) more += token(st.key, st.next++);
        tokens_ += n;
        auto text = live(st.key);
        text.set(text.peek() + more);
        if (s && now.count() - st.touched >= 0.25)
            if (auto node = s->find(st.key)) {
                s->touch(*node);
                st.touched = now.count();
            }
    }
}

void register_all() {
    omui::register_canvas_component();
    omui::register_canvas_kit();
    omui::register_markdown_component();
    for (auto [text, file] : {std::pair{group_drag_behavior, std::string_view("group-drag.behavior.omui.toml")},
                              std::pair{views_behavior, std::string_view("views.behavior.omui.toml")}}) {
        // Checked when the program is built (the tests load them); a broken one is a bug, said once.
        if (auto loaded = omui::world::load_behaviors(text, file); !loaded)
            for (const auto& p : loaded.error()) omui::warn("wall: {}", p.text());
    }
}

namespace {

// The HUD (immediate mode): what the last frames cost, what the canvas drew
// by tier, the textures, the streams. Built again four times a second.
void hud(omui::Ui& ui, Model& model) {
    using namespace omui;
    ui.rebuild_after(seconds(0.25));
    if (!model.hud.get()) return;
    const Canvas* c = model.canvas();
    if (!c || !model.runtime) return;
    const auto history = model.runtime->frame_history();
    std::vector<double> ms;
    for (const auto& f : history) ms.push_back(f.timings.total);
    std::vector<double> sorted = ms;
    std::ranges::sort(sorted);
    const double p50 = sorted.empty() ? 0 : sorted[sorted.size() / 2], p99 = sorted.empty() ? 0 : sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
    const auto& st = c->stats();
    const auto tx = c->texture_stats();
    const std::size_t drawn = st.textured;
    ui.column({.id = "hud", .width = 340, .padding = Edges(10.0f, 12.0f), .gap = 4, .background = Fill("surface"), .border_color = Fill("border"),
               .border_width = 1, .radius = 10, .shadow = "md"},
              [&] {
                  ui.text(std::format("frame {:.2f} ms · p99 {:.2f} ms", p50, p99), {.id = "frame", .style = "label"});
                  // The last frames, against a 120 Hz frame's 8.3 ms.
                  ui.add(painter({.id = "frames",
                                  .width = 316,
                                  .height = 44,
                                  .label = "Frame times",
                                  .paint = [ms](PaintContext& p) {
                                      const float w = p.size.width, h = p.size.height;
                                      p.list.fill_rect({0, 0, w, h}, p.theme.color("surface_variant"));
                                      const float budget = h - static_cast<float>(8.33 / 16.67) * h;
                                      p.list.fill_rect({0, budget, w, 1}, p.theme.color("warning"));
                                      const std::size_t n = std::min<std::size_t>(ms.size(), 158);
                                      for (std::size_t i = 0; i < n; ++i) {
                                          const double v = ms[ms.size() - n + i];
                                          const float bh = std::min(h, static_cast<float>(v / 16.67) * h);
                                          p.list.fill_rect({static_cast<float>(i) * 2, h - bh, 1.5f, bh}, p.theme.color(v > 8.33 ? "danger" : "accent"));
                                      }
                                  }}));
                  ui.text(std::format("{} on screen · {:.3f} ms to query, {:.3f} ms to draw", st.visible, st.query_ms, st.paint_ms),
                          {.id = "visible", .style = "caption"});
                  ui.text(std::format("T0 {} · texture {} ({} stretched) · live {}", st.custom, drawn, st.stretched, st.promoted),
                          {.id = "tiers", .style = "caption"});
                  ui.text(std::format("textures {} · {:.1f} MiB · {} drawing · {} in, {} out", tx.textures, static_cast<double>(tx.bytes) / (1 << 20), tx.jobs,
                                      tx.entered, tx.evicted),
                          {.id = "textures", .style = "caption"});
                  ui.text(std::format("sharp {:.0f}% · zoom {:.3g}% · {} documents", drawn ? 100.0 * static_cast<double>(drawn - st.stretched) / static_cast<double>(drawn) : 100.0,
                                      model.zoom.peek() * 100, model.docs.size()),
                          {.id = "sharp", .style = "caption"});
                  if (model.streaming())
                      ui.text(std::format("streaming into {} windows · {} tokens", model.streaming(), model.tokens()), {.id = "streams", .style = "caption"});
                  ui.text("drag a label or a title · 1: one to one · 2: all · H", {.id = "keys", .style = "caption", .color = Fill("text_muted")});
              });
}

}  // namespace

omui::AppSpec app(Model& model) {
    omui::AppSpec a;
    a.id = "org.openmui.wall";
    a.title = model.title;
    a.size = {1440, 900};
    a.setup = [&model](omui::Runtime& rt) {
        model.runtime = &rt;
        register_all();
        build(model, omui::world::scene(model.scene));
    };
    a.verbs.push_back({{"wall.stream", "Writes into the windows nearest the view's centre at a language model's pace.", omui::Effect::reversible, {},
                        {{"windows", "i64"}, {"rate", "f64"}}},
                       [&model](const omui::Value& args) -> omui::VerbResult {
                           const auto n = args.get("windows") ? args.get("windows")->as_int(0) : 0;
                           const double rate = args.get("rate") ? args.get("rate")->as_double(50) : 50;
                           if (n < 0 || n > 1000 || !(rate > 0)) return std::unexpected(std::string("windows is 0 to 1000 and rate above 0"));
                           model.stream(static_cast<std::size_t>(n), rate);
                           return omui::Value(static_cast<std::int64_t>(model.streaming()));
                       }});
    a.verbs.push_back({{"wall.stop", "Stops streaming; what was written stays.", omui::Effect::reversible, {}, {}},
                       [&model](const omui::Value&) -> omui::VerbResult {
                           model.stop_stream();
                           return omui::Value{};
                       }});
    a.view = [&model] {
        using namespace omui;
        // The HUD over the canvas, its own size at the corner (not stretched over the wall, where it would take its input).
        return stack({.id = "app", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {canvas({.id = "wall",
                              .behaviors = std::vector<std::string>{"select", "drag", "snap", "minimap", "group-drag", "views"},
                              .label = model.title,
                              .max_zoom = 8,
                              .min_zoom = 0.004,
                              .scene = model.scene,
                              .start = "fit",
                              .x = model.x,
                              .y = model.y,
                              .zoom = model.zoom,
                              .width = Dim(layout::pct(100)),
                              .height = Dim(layout::pct(100)),
                              .on = {{"hud.toggle", [&model](const Value&) { model.hud.set(!model.hud.peek()); }}}}),
                      immediate([&model](Ui& ui) { hud(ui, model); }, {.id = "hud-layer", .margin = Edges(12)})});
    };
    return a;
}

}  // namespace wall
