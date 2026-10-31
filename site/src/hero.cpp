module openmui.site;

import std;
import openmui;

namespace omui::site {

namespace {

// The four renderings' colours, as the lens draws them: what a person reads,
// what an agent operates, what a test checks.
constexpr std::uint32_t reader_hue = 0x60a5fa, agent_hue = 0xf59e0b, test_hue = 0x34d399;

struct Item {
    std::string_view id, en, zh;
};
constexpr std::array<Item, 3> items{{
    {"tests", "Tests pass on every platform", "每个平台的测试都通过"},
    {"docs", "Docs reviewed", "文档已审阅"},
    {"notes", "Release notes written", "发布说明已写好"},
}};

bool has_state(const Value& node, std::string_view state) {
    if (const Value* st = node.get("state"); st && st->list())
        for (const Value& s : *st->list())
            if (s.as_string() == state) return true;
    return false;
}

std::string str(const Value& node, std::string_view key) {
    const Value* v = node.get(key);
    return v ? std::string(v->as_string()) : std::string();
}

// A node's id from the specimen on: what the panels show of it.
std::string short_id(const Value& node) {
    const std::string id = str(node, "id");
    const auto at = id.find("specimen/");
    return at == std::string::npos ? id : id.substr(at);
}

// The node of the snapshot the panels show, by its id's end.
const Value* find_node(const Value& nodes, std::string_view selected) {
    const Value::List* list = nodes.list();
    if (!list) return nullptr;
    const std::string tail = std::format("/{}", selected);
    for (const Value& n : *list)
        if (const std::string id = str(n, "id"); id.find("/specimen/") != std::string::npos && id.ends_with(tail)) return &n;
    return nullptr;
}

// What the panels show before there is a tree to read (the atlas's still): the button as it starts.
Value still_node(bool zh) {
    Value n;
    n.set("id", "specimen/ship").set("role", "button").set("name", zh ? "发布 0.1" : "Ship 0.1");
    n.set("state", Value(Value::List{Value("disabled")}));
    n.set("actions", Value(Value::List{Value{}.set("name", "activate").set("effect", "reversible")}));
    return n;
}

}  // namespace

std::string reader_line(const Value& node, bool zh) {
    const std::string role = str(node, "role");
    const std::string spoken = role == "checkbox" ? (zh ? "复选框" : "checkbox") : role == "button" ? (zh ? "按钮" : "button") : role;
    std::string line = zh ? std::format("“{}”，{}", str(node, "name"), spoken) : std::format("\"{}\", {}", str(node, "name"), spoken);
    if (role == "checkbox") line += has_state(node, "checked") ? (zh ? "，已选中" : ", checked") : (zh ? "，未选中" : ", not checked");
    if (has_state(node, "disabled")) line += zh ? "，不可用" : ", dimmed";
    return line;
}

std::string agent_line(const Value& node, std::string_view action) {
    std::string act(action);
    if (act.empty())
        if (const Value* a = node.get("actions"); a && a->list() && !a->list()->empty()) act = str(a->list()->front(), "name");
    return std::format("{{\"act\": \"{}\", \"action\": \"{}\"}}", short_id(node), act);
}

std::string test_line(const Value& node) {
    const std::string id = short_id(node);
    std::string line = std::format("check_eq(node(\"{}\").role, \"{}\");", id, str(node, "role"));
    if (str(node, "role") == "checkbox")
        line += std::format("\ncheck({}node(\"{}\").states.has(\"checked\"));", has_state(node, "checked") ? "" : "!", id);
    else line += std::format("\ncheck({}node(\"{}\").states.has(\"disabled\"));", has_state(node, "disabled") ? "" : "!", id);
    return line;
}

std::string hero_html(bool zh) {
    auto esc = [](std::string_view t) {
        std::string o;
        for (const char c : t) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
        return o;
    };
    const Value ship = still_node(zh);
    std::string out = std::format("<h1>{}</h1>\n<p>{}</p>\n<ul>\n", zh ? "一个界面，每一种呈现。" : "One interface. Every rendering.",
                                  zh ? "openmui 从一棵语义树画出界面：你看到的像素、读屏软件读出的、agent 操作的、测试断言的，是同一批节点。"
                                     : "openmui draws an interface from one semantic tree: the pixels you see, what a screen reader says, what an agent acts on and what a test asserts are the same nodes.");
    for (const Item& it : items) out += std::format("<li>☐ {}</li>\n", zh ? it.zh : it.en);
    out += std::format("</ul>\n<p><strong>{}</strong></p>\n<pre><code>{}\n{}\n{}</code></pre>\n", zh ? "发布 0.1（等三项都勾上）" : "Ship 0.1 (waits for all three)",
                       esc(reader_line(ship, zh)), esc(agent_line(ship, {})), esc(test_line(ship)));
    return out;
}

Element brand_mark(std::string id, float size) {
    return painter({.id = std::move(id),
                    .width = Dim(size),
                    .height = Dim(size),
                    .role = Role::none,
                    .paint = [size](PaintContext& ctx) {
                        auto& out = ctx.list;
                        const float s = size;
                        const Color stem = ctx.theme.color("accent");
                        const Point root{s * 0.17f, s * 0.5f};
                        constexpr std::array<float, 4> ys{0.14f, 0.38f, 0.62f, 0.86f};
                        render::StrokeStyle line{.width = std::max(1.2f, s * 0.05f), .cap = render::LineCap::round};
                        for (const float y : ys) {
                            render::Path p;
                            const Point end{s * 0.7f, s * y};
                            p.move_to(root).cubic_to({s * 0.42f, root.y}, {s * 0.42f, end.y}, end);
                            out.stroke_path(p, line, render::Paint::solid(stem.with_alpha(0.75f)));
                        }
                        out.fill_round_rect({root.x - s * 0.1f, root.y - s * 0.1f, s * 0.2f, s * 0.2f}, CornerRadii::all(s * 0.1f), render::Paint::solid(stem));
                        const float t = s * 0.16f, x = s * 0.74f;
                        // A pixel.
                        out.fill_rect({x, s * ys[0] - t / 2, t, t}, ctx.theme.color("text"));
                        // A focus ring.
                        out.stroke_round_rect({x, s * ys[1] - t / 2, t, t}, CornerRadii::all(t / 2), std::max(1.0f, s * 0.035f),
                                              render::Paint::solid(Color::hex(reader_hue)));
                        // Braces: what an agent reads.
                        render::StrokeStyle thin{.width = std::max(1.0f, s * 0.035f), .cap = render::LineCap::round, .join = render::LineJoin::round};
                        for (const float side : {-1.0f, 1.0f}) {
                            render::Path b;
                            const float cx = x + t / 2 + side * t * 0.38f, y = s * ys[2], h = t / 2, w = t * 0.18f;
                            b.move_to({cx + side * w, y - h}).line_to({cx, y - h * 0.6f}).line_to({cx, y - h * 0.15f}).line_to({cx - side * w, y})
                                .line_to({cx, y + h * 0.15f}).line_to({cx, y + h * 0.6f}).line_to({cx + side * w, y + h});
                            out.stroke_path(b, thin, render::Paint::solid(Color::hex(agent_hue)));
                        }
                        // A check: what a test asserts.
                        render::Path c;
                        const float y = s * ys[3];
                        c.move_to({x, y}).line_to({x + t * 0.38f, y + t * 0.36f}).line_to({x + t, y - t * 0.42f});
                        out.stroke_path(c, render::StrokeStyle{.width = std::max(1.2f, s * 0.045f), .cap = render::LineCap::round, .join = render::LineJoin::round},
                                        render::Paint::solid(Color::hex(test_hue)));
                    }});
}

Element hero(Hero& h, const HeroSite& site, bool live, float width) {
    const bool narrow = width < 700;  // a phone: one column, the lines between left out
    const bool zh_now = site.lang && site.lang->peek() == "zh";
    // A word in the page's language: following it when live, as it is in a still.
    auto say = [&](std::string_view en, std::string_view han) -> Prop<std::string> {
        if (!live || !site.lang) return std::string(zh_now ? han : en);
        Signal<std::string>* lang = site.lang;
        return computed([lang, en, han] { return std::string(lang->get() == "zh" ? han : en); });
    };
    Hero* hp = &h;
    const HeroSite* sp = &site;
    auto all_checked = [hp] { return hp->checks[0].get() && hp->checks[1].get() && hp->checks[2].get(); };

    // The checklist, as a person sees it.
    std::vector<Element> checks;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        CheckboxOptions o{.id = std::string(it.id), .label = say(it.en, it.zh)};
        if (live) {
            o.checked = hp->checks[i];  // bound both ways: a click or an agent's toggle writes it
            o.on_changed = [hp, i, id = std::string(it.id)](bool on) {
                hp->checks[i].set(on);
                hp->selected.set(id);
                if (!hp->agent_running.peek()) hp->exchange.set(std::string());  // a person's change (or its undo): the agent's request is past
            };
        }
        checks.push_back(checkbox(std::move(o)));
    }
    ButtonOptions ship{.id = "ship", .text = say("Ship 0.1", "发布 0.1"), .variant = ButtonVariant::primary};
    if (live) {
        ship.disabled = computed([hp, all_checked] { return !all_checked() || hp->shipped.get(); });
        ship.on_activated = [hp, sp] {
            hp->shipped.set(true);
            hp->selected.set("ship");
            // Shipping is taken back as the checks are: undo unships it (and the panels follow), redo ships it again.
            if (sp->runtime && *sp->runtime)
                (*sp->runtime)->taken_back_by(
                    [hp] {
                        hp->shipped.set(false);
                        hp->selected.set("ship");
                        hp->exchange.set(std::string());
                    },
                    [hp] {
                        hp->shipped.set(true);
                        hp->selected.set("ship");
                    });
        };
    } else ship.disabled = true;
    Element card = column({.id = "specimen", .width = narrow ? Dim(layout::pct(100)) : Dim(330.0f), .padding = Edges(20.0f), .gap = 12, .background = "surface",
                           .border_color = "border", .border_width = 1, .radius = 14},
                          {text({.id = "release", .text = say("Release 0.1", "发布 0.1"), .style = "heading"}),
                           text({.id = "as_seen", .text = say("A checklist, as a person sees it", "一张清单，人看到的样子"), .style = "caption"}),
                           column({.id = "items", .gap = 10}, std::move(checks)), button(std::move(ship)),
                           text({.id = "shipped",
                                 .text = live ? Prop<std::string>(computed([hp, lang = site.lang] {
                                     if (!hp->shipped.get()) return std::string(" ");
                                     return std::string(lang && lang->get() == "zh" ? "已发布。每一步都可以撤销。" : "Shipped. Every step can be taken back.");
                                 }))
                                              : Prop<std::string>(std::string(" ")),
                                 .style = "caption"})});

    // The same node, three more ways: read from the page's semantic tree.
    auto node_now = [hp, sp, zh_now, live]() -> Value {
        if (live && sp->nodes) {
            const Value nodes = sp->nodes->get();
            if (const Value* n = find_node(nodes, hp->selected.get())) return *n;
        }
        return still_node(sp->lang ? sp->lang->peek() == "zh" : zh_now);
    };
    auto panel = [&](std::string id, std::uint32_t hue, Prop<std::string> title, Prop<std::string> line) {
        return row({.id = std::move(id), .height = Dim(96.0f), .gap = 0, .align = "stretch", .background = "surface", .border_color = "border", .border_width = 1, .radius = 12, .clip = true},
                   {box({.width = Dim(4.0f), .background = Fill(Color::hex(hue))}),
                    column({.id = "body", .padding = Edges(12.0f, 16.0f), .gap = 6, .grow = 1},
                           {text({.id = "title", .text = std::move(title), .style = "caption"}), text({.id = "line", .text = std::move(line), .style = "code"})})});
    };
    auto from_tree = [&](std::function<std::string(const Value&, bool)> f) -> Prop<std::string> {
        if (!live) return f(node_now(), zh_now);
        return computed([node_now, f, lang = site.lang] { return f(node_now(), lang && lang->get() == "zh"); });
    };
    Element panels = column({.id = "renderings", .gap = 10, .grow = 1},
                            {panel("reader", reader_hue, say("What a screen reader says", "读屏软件读出的"), from_tree([](const Value& n, bool zh) { return reader_line(n, zh); })),
                             panel("agent", agent_hue, say("What an agent sends", "agent 发出的"),
                                   from_tree([hp, live](const Value& n, bool) {
                                       const std::string done = live ? hp->exchange.get() : std::string();
                                       return done.empty() ? agent_line(n, {}) : done;
                                   })),
                             panel("test", test_hue, say("What a test asserts", "测试断言的"), from_tree([](const Value& n, bool) { return test_line(n); }))});

    // The lines between them: from the node last touched to each rendering.
    Element links = painter({.id = "links",
                             .width = Dim(64.0f),
                             .height = Dim(3 * 96.0f + 2 * 10),
                             .role = Role::none,
                             .paint = [hp, sp, live](PaintContext& ctx) {
                                 if (!live || !sp->runtime || !*sp->runtime || !sp->root) return;
                                 Runtime& rt = **sp->runtime;
                                 const Rect self = ctx.services.bounds(ctx.node);
                                 const std::string base = sp->root() + "/hero/";
                                 const NodeId from = rt.find(base + "demo/specimen/" + (hp->selected.get() == "ship" ? std::string("ship") : "items/" + hp->selected.get()));
                                 if (!from) return;
                                 const Rect f = rt.bounds(from);
                                 const float y0 = std::clamp(f.y + f.height / 2 - self.y, 4.0f, self.height - 4);
                                 constexpr std::array<std::uint32_t, 3> hues{reader_hue, agent_hue, test_hue};
                                 for (int i = 0; i < 3; ++i) {
                                     const float y1 = 48 + static_cast<float>(i) * (96 + 10);
                                     render::Path p;
                                     p.move_to({0, y0}).cubic_to({32, y0}, {32, y1}, {ctx.size.width, y1});
                                     const Color c = Color::hex(hues[static_cast<std::size_t>(i)]);
                                     ctx.list.stroke_path(p, render::StrokeStyle{.width = 1.5f, .cap = render::LineCap::round}, render::Paint::solid(c.with_alpha(0.8f)));
                                     ctx.list.fill_round_rect({ctx.size.width - 4, y1 - 3, 6, 6}, CornerRadii::all(3), render::Paint::solid(c));
                                 }
                                 ctx.list.fill_round_rect({-3, y0 - 3, 6, 6}, CornerRadii::all(3), render::Paint::solid(ctx.theme.color("text")));
                             }});

    // An agent does it: the same actions, as an agent, one after another.
    ButtonOptions run{.id = "let_agent", .text = say("Let an agent ship it", "让 agent 来发布"), .variant = ButtonVariant::secondary};
    if (live) {
        run.disabled = computed([hp] { return hp->agent_running.get(); });
        run.on_activated = [hp, sp] {
            if (!sp->runtime || !*sp->runtime || hp->agent_running.peek()) return;
            if (hp->shipped.peek()) {  // again from the start
                for (auto& c : hp->checks) c.set(false);
                hp->shipped.set(false);
            }
            hp->agent_running.set(true);
            hp->exchange.set(std::string());
            // The steps: each unchecked item toggled, then the button activated.
            auto steps = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
            for (std::size_t i = 0; i < items.size(); ++i)
                if (!hp->checks[i].peek()) steps->push_back({"items/" + std::string(items[i].id), std::string(items[i].id)});
            steps->push_back({"ship", "ship"});
            auto next = std::make_shared<std::function<void(std::size_t)>>();
            *next = [hp, sp, steps, next](std::size_t k) {
                Runtime& rt = **sp->runtime;
                if (k >= steps->size()) {
                    hp->agent_running.set(false);
                    return;
                }
                const auto& [path, id] = (*steps)[k];
                hp->selected.set(id);
                const std::string action = id == "ship" ? "activate" : "toggle";
                (void)rt.after(seconds(0.45), [hp, sp, steps, next, k, path, id, action] {
                    Runtime& r = **sp->runtime;
                    Invocation call;
                    call.actor = {"agent", "site-guide", {}};
                    const Envelope e = r.act(r.find(sp->root() + "/hero/demo/specimen/" + path), action, call);
                    const bool ok = e.exit_code() == std::optional<std::int64_t>(0);
                    hp->exchange.set(std::format("{{\"act\": \"specimen/{}\", \"action\": \"{}\"}}\n→ {}", path, action, ok ? "done, reversible" : "refused"));
                    (void)r.after(seconds(0.8), [next, k] { (*next)(k + 1); });
                });
            };
            (*next)(0);
        };
    } else run.disabled = true;

    // The agent's turn: beside its words, or above them when narrow.
    std::vector<Element> agent_parts{button(std::move(run)),
                                     text({.id = "how",
                                           .text = say("Each step is a real action on these nodes, as an agent: the same checks and the same undo as your clicks.",
                                                       "每一步都是 agent 对这些节点的真实动作：和你的点击一样的检查、一样的撤销。"),
                                           .style = "caption"})};
    Element agent_row = narrow ? column({.id = "agent_row", .gap = 8, .align = "start"}, std::move(agent_parts))
                               : row({.id = "agent_row", .gap = 12, .align = "center"}, std::move(agent_parts));
    Element demo = narrow ? column({.id = "demo", .gap = 12, .align = "stretch"}, {std::move(card), std::move(panels)})
                          : row({.id = "demo", .gap = 0, .align = "start"}, {std::move(card), std::move(links), std::move(panels)});
    return column({.id = "hero", .padding = Edges(Insets{4, 0, 32, 0}), .gap = narrow ? 16.0f : 22.0f},
                  {row({.id = "brand", .gap = narrow ? 12.0f : 18.0f, .align = "center"},
                       {brand_mark("mark", narrow ? 40.0f : 56.0f),
                        column({.min_width = Dim(0.0f), .gap = 6, .grow = 1, .shrink = 1},
                               {text({.id = "claim",
                                      .text = say("One interface. Every rendering.", "一个界面，每一种呈现。"),
                                      .style = narrow ? "title" : "display"})
                                    .prop("role", Source{Value("heading"), {}, {}}),  // the page's first heading, to a screen reader too
                                text({.id = "thesis",
                                      .text = say("openmui draws an interface from one semantic tree: the pixels you see, what a screen reader says, what an agent acts on and what a test asserts are the same nodes.",
                                                  "openmui 从一棵语义树画出界面：你看到的像素、读屏软件读出的、agent 操作的、测试断言的，是同一批节点。"),
                                      .style = "body",
                                      .max_width = Dim(680.0f)})})}),
                   row({.id = "cta", .gap = 10, .align = "center"},
                       {button({.id = "to_features",
                                .text = say("What sets it apart", "它的不同之处"),
                                .variant = ButtonVariant::primary,
                                .on_activated = [sp] { if (sp->go) sp->go("features"); }}),
                        button({.id = "to_tree",
                                .text = say("See this page's tree", "看这一页的语义树"),
                                .variant = ButtonVariant::secondary,
                                .on_activated = [sp] { if (sp->tree_lens) sp->tree_lens(); }})}),
                   std::move(demo),
                   std::move(agent_row)});
}

}  // namespace omui::site
