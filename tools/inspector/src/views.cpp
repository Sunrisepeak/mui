// The Inspector's interface: a header with the application, the clock bar,
// the tree beside the panels (properties, frames, pipeline).
module openmui.inspector;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import openmui;

namespace omui::inspector {

namespace {

std::string number(const Value* v, std::string_view unit = "", int precision = 2) {
    if (!v || !v->is_number()) return "—";
    return std::format("{:.{}f}{}", v->as_double(), precision, unit);
}

std::string text_of(const Value& v) {
    if (v.type() == Value::Type::text) return std::string(v.as_string());
    return v.json();
}

// A label and its value on one row.
Element field(std::string label, std::string value) {
    return row({.gap = 8}, {
                               text({.text = std::move(label), .style = "caption", .width = 110}),
                               text({.text = std::move(value), .style = "code", .grow = 1}),
                           });
}

Element heading(std::string title) { return text({.text = std::move(title), .style = "label"}); }

}  // namespace

View Inspector::view() {
    Inspector* self = this;
    auto clock_text = derive([self] {
        const Value& c = self->clock.get();
        if (!c.get("now")) return std::string("clock: —");
        const bool paused = c.get("paused") && c.get("paused")->as_bool();
        return std::format("t = {} s{}{}", number(c.get("now"), "", 3), paused ? "  · paused" : "",
                           c.get("rate") && c.get("rate")->as_double() != 1 ? std::format("  · ×{}", number(c.get("rate"), "", 2)) : "");
    });
    auto pause_text = derive([self] {
        const Value& c = self->clock.get();
        return c.get("paused") && c.get("paused")->as_bool() ? std::string("Resume") : std::string("Pause");
    });

    Element header = row({.id = "header", .padding = Edges(8.0f, 12.0f), .gap = 12, .align = "center", .background = "surface"},
                         {
                             text({.text = "openmui Inspector", .style = "title"}),
                             omui::select({.id = "app", .label = "Application",
                                     .options = derive([self] { return self->applications.get(); }),
                                     .value = derive([self] { return self->app.get(); }),
                                     .on_changed = [self](std::string a) { self->choose(std::move(a)); },
                                     .width = 320}),
                             button({.id = "refresh", .text = "Refresh", .variant = ButtonVariant::secondary,
                                     .on_activated = [self] { self->refresh(); }}),
                             spacer(),
                             text({.id = "nodes", .text = derive([self] { return std::format("{} nodes", self->tree_size.get()); }),
                                   .style = "caption"}),
                         });

    Element clock_bar = row({.id = "clock", .padding = Edges(6.0f, 12.0f), .gap = 8, .align = "center"},
                            {
                                button({.id = "pause", .text = pause_text, .variant = ButtonVariant::primary,
                                        .on_activated = [self] { self->toggle_pause(); }}),
                                button({.id = "back", .text = "−1 frame", .variant = ButtonVariant::ghost,
                                        .on_activated = [self] { self->step(-1.0 / 60.0); }}),
                                button({.id = "forward", .text = "+1 frame", .variant = ButtonVariant::ghost,
                                        .on_activated = [self] { self->step(1.0 / 60.0); }}),
                                omui::select({.id = "rate", .label = "Speed",
                                        .labels = std::vector<std::string>{"×0.1", "×0.25", "×0.5", "×1", "×2"},
                                        .options = std::vector<std::string>{"0.1", "0.25", "0.5", "1", "2"},
                                        .value = "1",
                                        .on_changed = [self](std::string r) {
                                            double v = 1;
                                            (void)std::from_chars(r.data(), r.data() + r.size(), v);
                                            self->set_rate(v);
                                        },
                                        .width = 110}),
                                button({.id = "release", .text = "Live", .variant = ButtonVariant::ghost,
                                        .on_activated = [self] { self->release_time(); }}),
                                text({.id = "time", .text = clock_text, .style = "code"}),
                                spacer(),
                                toggle({.id = "bounds", .label = "Bounds", .on_changed = [self](bool on) { self->set_overlay("bounds", on); }}),
                                toggle({.id = "repaints", .label = "Repaints", .on_changed = [self](bool on) { self->set_overlay("repaints", on); }}),
                                toggle({.id = "hud", .label = "HUD", .on_changed = [self](bool on) { self->set_overlay("hud", on); }}),
                            });

    // The tree: a virtualised list of the semantic nodes, indented by depth.
    Element tree = list_view({.id = "tree", .model = std::string(tree_model), .selectable = ListSelectable::single,
                              .selected = derive([self] {
                                  const std::string& id = self->selected.get();
                                  return id.empty() ? std::vector<std::string>{} : std::vector<std::string>{self->key_of_node(id)};
                              }),
                              .on_changed = [self](std::vector<std::string> keys) {
                                  if (!keys.empty()) self->select(self->node_of_key(keys.front()));
                              },
                              .width = Dim(layout::Length::percent(38)), .grow = 0},
                             [](const Row& r, std::size_t) {
                                 return text({.text = Model::label_of(r), .style = "code"});
                             });

    Element properties = dynamic([self] {
        const Value& d = self->details.get();
        std::vector<Element> out;
        if (self->selected.get().empty()) {
            out.push_back(text({.text = "Choose a node in the tree.", .style = "caption"}));
            return out;
        }
        if (const Value* e = d.get("error")) {
            out.push_back(text({.text = text_of(*e), .style = "caption"}));
            return out;
        }
        out.push_back(heading(self->selected.get()));
        for (std::string_view k : {"type", "contract", "source", "function"})
            if (const Value* v = d.get(k)) out.push_back(field(std::string(k), text_of(*v)));
        if (const Value* b = d.get("bounds"); b && b->list() && b->list()->size() == 4) {
            const auto& l = *b->list();
            out.push_back(field("bounds", std::format("{} , {}   {} × {}", number(&l[0], "", 1), number(&l[1], "", 1), number(&l[2], "", 1),
                                                      number(&l[3], "", 1))));
        }
        for (std::string_view group : {"props", "state", "layout"})
            if (const Value* m = d.get(group); m && m->map() && !m->map()->empty()) {
                out.push_back(heading(std::string(group)));
                for (const auto& [k, v] : *m->map()) out.push_back(field(k, text_of(v)));
            }
        for (std::string_view group : {"bound", "handlers", "transitions"})
            if (const Value* l = d.get(group); l && l->list() && !l->list()->empty()) {
                std::string joined;
                for (const Value& x : *l->list()) joined += (joined.empty() ? "" : ", ") + text_of(x);
                out.push_back(field(std::string(group), joined));
            }
        return out;
    });

    // Frame times against the 60 Hz budget: one bar per frame, oldest first.
    Element graph = painter({.id = "graph", .height = 140, .label = "Frame times",
                             .paint = [self](PaintContext& ctx) {
                                 const Value* h = self->stats.peek().get("history");
                                 const Rect area = ctx.content;
                                 ctx.list.fill_rect(area, ctx.theme.color("surface_variant"));
                                 constexpr double budget = 1000.0 / 60.0;
                                 const double top = budget * 2;
                                 const float y_budget = area.bottom() - static_cast<float>(budget / top) * area.height;
                                 ctx.list.fill_rect({area.x, y_budget, area.width, 1}, ctx.theme.color("danger"));
                                 if (!h || !h->list() || h->list()->empty()) return;
                                 const auto& frames = *h->list();
                                 const float w = area.width / static_cast<float>(std::max<std::size_t>(frames.size(), 120));
                                 for (std::size_t i = 0; i < frames.size(); ++i) {
                                     const double ms = frames[i].get("total") ? frames[i].get("total")->as_double() : 0;
                                     const float bh = static_cast<float>(std::min(ms, top) / top) * area.height;
                                     const Color c = ms > budget ? ctx.theme.color("danger") : ctx.theme.color("accent");
                                     ctx.list.fill_rect({area.x + static_cast<float>(i) * w, area.bottom() - bh, std::max(1.0f, w - 1), bh}, c);
                                 }
                             }});
    Element frames = column({.gap = 8}, {
                                 graph,
                                 text({.id = "frame-summary",
                                       .text = derive([self] {
                                           const Value& s = self->stats.get();
                                           return std::format("mean {}  ·  p95 {}  ·  max {}  ·  {} frames", number(s.get("mean_ms"), " ms"),
                                                              number(s.get("p95_ms"), " ms"), number(s.get("max_ms"), " ms"),
                                                              s.get("frames") ? s.get("frames")->as_int() : 0);
                                       }),
                                       .style = "code"}),
                                 dynamic([self] {
                                     std::vector<Element> out;
                                     if (const Value* p = self->stats.get().get("phases_mean_ms"); p && p->map())
                                         for (const auto& [k, v] : *p->map()) out.push_back(field(k, number(&v, " ms", 3)));
                                     return out;
                                 }),
                             });

    Element stages = dynamic([self] {
        std::vector<Element> out;
        const Value& p = self->pipeline.get();
        if (const Value* n = p.get("name")) out.push_back(heading(std::format("pipeline {}", text_of(*n))));
        const Value* list = p.get("stages");
        if (!list || !list->list()) return out;
        double most = 0.001;
        for (const Value& st : *list->list()) most = std::max(most, st.get("ms") ? st.get("ms")->as_double() : 0);
        for (const Value& st : *list->list()) {
            const double ms = st.get("ms") ? st.get("ms")->as_double() : 0;
            out.push_back(row({.gap = 8, .align = "center"},
                              {
                                  text({.text = st.get("name") ? text_of(*st.get("name")) : "", .style = "label", .width = 100}),
                                  // A fixed track, so the columns after it line up.
                                  box({.width = 168, .height = 8, .background = "surface_variant", .radius = 4},
                                      {box({.width = static_cast<float>(4 + 164 * ms / most), .height = 8, .background = "accent", .radius = 4})}),
                                  text({.text = std::format("{:.3f} ms", ms), .style = "code", .width = 90}),
                                  text({.text = st.get("plugin") ? text_of(*st.get("plugin")) : "", .style = "caption", .grow = 1}),
                              }));
        }
        return out;
    });

    Element panels = column({.id = "panels", .padding = Edges(Insets{0, 0, 0, 12}), .gap = 8, .grow = 1},
                            {
                                tabs({.id = "tabs", .selected = tab, .on_changed = [self](std::string t) { self->tab.set(std::move(t)); }},
                                     {
                                         omui::tab({.id = "properties", .text = "Properties"}),
                                         omui::tab({.id = "frames", .text = "Frames"}),
                                         omui::tab({.id = "pipeline", .text = "Pipeline"}),
                                     }),
                                 scroll({.id = "panel", .grow = 1, .gap = 4},
                                        {
                                            // The chosen tab's panel, rebuilt when the tab changes.
                                            dynamic([self, frames, properties, stages] {
                                                const std::string t = self->tab.get();
                                                return std::vector<Element>{t == "frames" ? frames : t == "pipeline" ? stages : properties};
                                            }),
                                        }),
                            });

    return column({.id = "inspector", .grow = 1, .background = "background"},
                  {
                      header,
                      clock_bar,
                      row({.id = "body", .padding = 12, .align = "stretch", .grow = 1}, {tree, panels}),
                      box({.padding = Edges(Insets{4, 12, 8, 12})},
                          {text({.id = "status", .text = derive([self] { return self->status.get(); }), .style = "caption"})}),
                  });
}

AppSpec app(Options options) {
    AppSpec spec;
    spec.id = options.self;
    spec.title = "openmui Inspector";
    spec.size = {1280, 800};
    auto holder = std::make_shared<std::unique_ptr<Inspector>>();
    spec.setup = [holder, options](Runtime& rt) { *holder = std::make_unique<Inspector>(rt, options); };
    spec.view = [holder] { return (*holder)->view(); };
    return spec;
}

}  // namespace omui::inspector
