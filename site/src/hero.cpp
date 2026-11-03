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
    const std::string tail = selected == "ship" ? std::string("/actions/ship") : "/items/" + std::string(selected) + "/done";
    for (const Value& n : *list)
        if (const std::string id = str(n, "id"); id.find("/specimen/") != std::string::npos && id.ends_with(tail)) return &n;
    return nullptr;
}

// What the panels show before there is a tree to read (and in a still): the button as it starts.
Value still_node(bool zh) {
    Value n;
    n.set("id", "specimen/actions/ship").set("role", "button").set("name", zh ? "记录发布" : "Record release");
    n.set("state", Value(Value::List{Value("disabled")}));
    n.set("actions", Value(Value::List{Value{}.set("name", "activate").set("effect", "reversible")}));
    return n;
}

}  // namespace

Value release_tasks() {
    Value::List tasks;
    for (const auto& item : items) tasks.push_back(Value{}.set("id", std::string(item.id)).set("en", std::string(item.en)).set("zh", std::string(item.zh)).set("done", false));
    return Value(std::move(tasks));
}
std::string task_title(const Value& task, bool zh) {
    if (const Value* title = task.get("title"); title && !title->as_string().empty()) return std::string(title->as_string());
    const auto* text = task.get(zh ? "zh" : "en"); return text ? std::string(text->as_string()) : std::string();
}
bool release_ready(const Value& tasks) {
    return tasks.list() && !tasks.list()->empty() && std::ranges::all_of(*tasks.list(), [](const auto& task) { const auto* done = task.get("done"); return done && done->as_bool(); });
}
namespace {
const Value* task_named(const Value& tasks, std::string_view id) {
    if (tasks.list()) for (const auto& task : *tasks.list()) if (str(task, "id") == id) return &task;
    return nullptr;
}
void focus_work_control(HeroSite site, std::string base, std::string control, Actor actor, int tries = 4) {
    if (!site.runtime || !*site.runtime || !site.root || site.root() != base) return;
    auto& runtime = **site.runtime; const auto target = runtime.find(base + "/hero/demo/specimen/" + control);
    if (!runtime.valid(target)) {
        if (tries > 0) (void)runtime.after(seconds(1.0 / 60), [site, base, control, actor, tries] { focus_work_control(site, base, control, actor, tries - 1); });
        return;
    }
    runtime.focus(target); Invocation call; call.actor = std::move(actor); (void)runtime.act(target, "scroll_into_view", call);
}
void focus_work_control(const HeroSite& site, std::string control) {
    if (!site.runtime || !*site.runtime || !site.root) return;
    auto& runtime = **site.runtime; const auto actor = runtime.acting().value_or(Actor{}); const std::string base = site.root();
    (void)runtime.after(seconds(0), [site, base, control, actor] { focus_work_control(site, base, control, actor); });
}
std::string markdown_tasks(const Value& tasks, bool zh) {
    std::string out = zh ? "# 发布清单\n\n" : "# Release checklist\n\n";
    if (tasks.list()) for (const auto& task : *tasks.list()) out += std::format("- [{}] {}\n", task.get("done") && task.get("done")->as_bool() ? "x" : " ", task_title(task, zh));
    return out;
}
void change_tasks(Hero& hero, const HeroSite& site, Value after, bool shipped = false) {
    const Value before = hero.tasks.peek(); const bool was_shipped = hero.shipped.peek();
    if (before == after && was_shipped == shipped) return;
    hero.tasks.set(after); hero.shipped.set(shipped);
    auto restore = [p = &hero](Value tasks, bool sent) { p->tasks.set(std::move(tasks)); p->shipped.set(sent); p->editing.set(""); p->validation.set(""); p->selected.set("ship"); p->exchange.set(""); };
    if (site.runtime && *site.runtime) (*site.runtime)->taken_back_by([restore, before, was_shipped] { restore(before, was_shipped); }, [restore, after, shipped] { restore(after, shipped); });
}
void activate_work_action(const HeroSite& site, std::string_view action) {
    if (!site.runtime || !*site.runtime || !site.root) return;
    Runtime& rt = **site.runtime;
    (void)rt.act(rt.find(site.root() + "/hero/demo/specimen/" + std::string(action)), "activate");
}
std::string clean_title(std::string text) {
    const auto start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return {};
    return text.substr(start, text.find_last_not_of(" \t\n\r") - start + 1);
}
void save_task(Hero& hero, const HeroSite& site, bool rename) {
    if (hero.shipped.peek() || hero.agent_running.peek()) return;
    const std::string title = clean_title(rename ? hero.edit_text.peek() : hero.draft.peek());
    if (title.empty()) { hero.validation.set(site.lang && site.lang->peek() == "zh" ? "请写一个任务名称。" : "Give this task a name."); return; }
    Value tasks = hero.tasks.peek();
    const std::string edited = hero.editing.peek();
    if (rename) {
        if (tasks.list()) for (auto& task : *tasks.list()) if (str(task, "id") == hero.editing.peek()) task.set("title", title);
    } else {
        if (!tasks.list() || tasks.list()->size() >= 24) { hero.validation.set(site.lang && site.lang->peek() == "zh" ? "这张小工作台最多容纳 24 项任务。" : "This small board holds up to 24 tasks."); return; }
        tasks.list()->push_back(Value{}.set("id", "task-" + std::to_string(hero.next_task++)).set("title", title).set("done", false));
    }
    change_tasks(hero, site, std::move(tasks));
    hero.draft.set(""); hero.editing.set(""); hero.validation.set("");
    focus_work_control(site, rename ? "items/" + edited + "/edit" : "add/title");
}
} // namespace

Prop<bool> task_binding(Hero& hero, std::string id) {
    Hero* hp = &hero;
    return Source{std::nullopt, [hp, id] { const Value tasks = hp->tasks.get(); const auto* task = task_named(tasks, id); return Value(task && task->get("done") && task->get("done")->as_bool()); },
        [hp, id](const Value& value) { Value tasks = hp->tasks.peek(); if (tasks.list()) for (auto& task : *tasks.list()) if (str(task, "id") == id) task.set("done", value.as_bool()); hp->tasks.set(std::move(tasks)); }};
}

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
    std::string out = std::format("<h1>{}</h1><p>{}</p><h2>{}</h2><ul>", zh ? "一个界面，每一种呈现。" : "One interface. Every rendering.",
        zh ? "omui 是实现 openmui 开放规范的 UI 框架。从一棵语义树，得到像素、无障碍、agent 和测试。" : "omui is a UI framework implementing the openmui specification. Pixels, accessibility, agents and tests come from one semantic tree.",
        zh ? "你的下一次发布" : "Your next release");
    for (const auto& task : items) out += std::format("<li>☐ {}</li>", zh ? task.zh : task.en);
    out += zh ? "</ul><p>运行工作台后，可以添加、修改、勾选和移除任务；每一步都可以撤销。</p><details><summary>同一个对象，不同的呈现</summary><p>选中任务后，可检查读屏描述、agent 动作和测试断言。模拟 agent 演示会明确标注。</p></details>"
              : "</ul><p>The live workspace lets you add, edit, check and remove tasks. Every change can be undone.</p><details><summary>The same object, different views</summary><p>Inspect a selected task as a screen reader, an agent or a test. The scripted agent is labelled as simulated.</p></details>";
    return out;
}

Element hero(Hero& h, const HeroSite& site, bool live, float width) {
    const bool narrow = width < 700;  // a phone: one column, the lines between left out
    // Beside a panel, or a window too narrow for the card, the lines and the renderings side by side:
    // the renderings under the card, at the width there is (their lines of code are not cut).
    const bool stacked = narrow || width < 880;
    const bool zh_now = site.lang && site.lang->peek() == "zh";
    // A word in the page's language: following it when live, as it is in a still.
    auto say = [live, lang = site.lang, zh_now](std::string_view en, std::string_view han) -> Prop<std::string> {
        if (!live || !lang) return std::string(zh_now ? han : en);
        return computed([lang, en = std::string(en), han = std::string(han)] { return std::string(lang->get() == "zh" ? han : en); });
    };
    Hero* hp = &h;
    const HeroSite* sp = &site;
    auto all_checked = [hp] { return release_ready(hp->tasks.get()); };

    // A task board shared by the opening and the motion scene below it.
    const float board_width = stacked ? width : width * 0.56f;
    constexpr float board_padding = 20, board_border = 1;
    const float inside = std::max(120.0f, board_width - 2 * (board_padding + board_border));
    auto unavailable = computed([hp] { return hp->shipped.get() || hp->agent_running.get(); });
    Element tasks = dynamic([hp, sp, live, inside] {
        const Value list = live ? hp->tasks.get() : hp->tasks.peek();
        std::vector<Element> rows;
        if (list.list()) for (const auto& task : *list.list()) {
            const std::string id = str(task, "id");
            auto label = computed([hp, sp, id] { const Value tasks = hp->tasks.get(); const auto* task = task_named(tasks, id); return task ? task_title(*task, sp->lang && sp->lang->get() == "zh") : std::string(); });
            CheckboxOptions o{.id = "done", .disabled = computed([hp] { return hp->shipped.get(); }), .label = label};
            o.checked = task_binding(*hp, id);
            o.on_changed = [hp, id](bool) { hp->selected.set(id); if (!hp->agent_running.peek()) hp->exchange.set(""); };
            Element check = checkbox(std::move(o)); check.prop("width", Source(Value(static_cast<double>(std::max(80.0f, inside - 68)))));
            check.prop("origin", Source(Value(task.get("title") ? "user_content" : "app")));
            Element edit = button({.id = "edit", .disabled = computed([hp] { return hp->shipped.get() || hp->agent_running.get(); }),
                .text = computed([sp] { return std::string(sp->lang && sp->lang->get() == "zh" ? "编辑" : "Edit"); }), .variant = ButtonVariant::ghost,
                .on_activated = [hp, sp, id] { hp->editing.set(id); hp->selected.set(id); const Value tasks = hp->tasks.peek(); if (const auto* task = task_named(tasks, id)) hp->edit_text.set(task_title(*task, sp->lang && sp->lang->peek() == "zh")); hp->validation.set(""); focus_work_control(*sp, "editor/title"); }});
            edit.prop("origin", Source(Value(task.get("title") ? "user_content" : "app")));
            edit.prop("undo_scope", Source(Value(true))).prop("min_width", Source(Value(60.0))).prop("shrink", Source(Value(0.0)));
            edit.prop("label", derive([hp, sp, id] { const Value tasks = hp->tasks.get(); const auto* task = task_named(tasks, id); const bool zh = sp->lang && sp->lang->get() == "zh"; return std::string(zh ? "编辑任务：" : "Edit task: ") + (task ? task_title(*task, zh) : id); }));
            rows.push_back(row({.id = id, .width = Dim(inside), .gap = 8, .align = "center"}, {std::move(check), std::move(edit)}));
        }
        if (rows.empty()) rows.push_back(text({.id = "empty", .text = computed([sp] { return std::string(sp->lang && sp->lang->get() == "zh" ? "先添加一项任务，再准备发布。" : "Add a task to begin your next release."); }), .style = "body", .color = "text_muted"}));
        return std::vector<Element>{column({.id = "items", .gap = 14}, std::move(rows))};
    });
    ButtonOptions ship{.id = "ship", .text = say("Record release", "记录发布"), .variant = ButtonVariant::primary};
    ship.on_activated = [hp, sp] { change_tasks(*hp, *sp, hp->tasks.peek(), true); hp->selected.set("ship"); };
    // The simulated agent must be able to finish its own run.
    ship.disabled = computed([hp, all_checked] { return !all_checked() || hp->shipped.get(); });
    Element add = row({.id = "add", .gap = 8, .align = "center"}, {
        text_box({.id = "title", .disabled = unavailable, .label = say("New task", "新任务"), .max_length = 96, .placeholder = say("Add a release task…", "添加一项发布任务…"), .text = hp->draft,
                  .on_submitted = [sp](std::string) { activate_work_action(*sp, "add/submit"); }, .width = Dim(std::max(80.0f, inside - 76))}).prop("undo_scope", Source(Value(true))),
        button({.id = "submit", .disabled = unavailable, .text = say("Add", "添加"), .on_activated = [hp, sp] { save_task(*hp, *sp, false); }, .min_width = Dim(68), .shrink = 0})});
    Element editor = dynamic([hp, sp, inside] {
        if (hp->editing.get().empty()) return std::vector<Element>{};
        auto label = [sp](std::string en, std::string zh) { return computed([sp, en, zh] { return sp->lang && sp->lang->get() == "zh" ? zh : en; }); };
        return std::vector<Element>{column({.id = "editor", .padding = Edges(12), .gap = 12, .background = "surface_variant", .radius = 10}, {
            text_box({.id = "title", .label = label("Task name", "任务名称"), .max_length = 96, .text = hp->edit_text, .on_submitted = [sp](std::string) { activate_work_action(*sp, "editor/actions/save"); }, .width = Dim(inside - 24)}).prop("undo_scope", Source(Value(true))),
            row({.id = "actions", .gap = 8, .wrap = "wrap"}, {
                button({.id = "save", .text = label("Save task", "保存任务"), .on_activated = [hp, sp] { save_task(*hp, *sp, true); }}),
                button({.id = "cancel", .text = label("Cancel", "取消"), .variant = ButtonVariant::ghost, .on_activated = [hp, sp] { const auto id = hp->editing.peek(); hp->editing.set(""); hp->validation.set(""); focus_work_control(*sp, "items/" + id + "/edit"); }}).prop("undo_scope", Source(Value(true))),
                button({.id = "remove", .text = label("Remove task", "移除任务"), .variant = ButtonVariant::danger, .on_activated = [hp, sp] {
                    if (hp->agent_running.peek() || hp->shipped.peek()) return;
                    Value tasks = hp->tasks.peek(); const std::string id = hp->editing.peek(); if (tasks.list()) std::erase_if(*tasks.list(), [&](const Value& t) { return str(t, "id") == id; });
                    change_tasks(*hp, *sp, std::move(tasks)); hp->editing.set(""); hp->selected.set("ship"); focus_work_control(*sp, "add/title");
                }})})})};
    });
    std::vector<Element> history;
    for (const bool redo : {false, true}) {
        Element b = button({.id = redo ? "redo" : "undo", .disabled = computed([hp, sp, redo] {
                (void)hp->tasks.get(); (void)hp->shipped.get(); (void)hp->draft.get(); (void)hp->edit_text.get(); if (sp->nodes) (void)sp->nodes->get();
                if (!sp->runtime || !*sp->runtime || !sp->root) return true;
                const auto h = (*sp->runtime)->history((*sp->runtime)->find(sp->root() + "/hero/demo/specimen"));
                return redo ? h.redo.empty() : h.undo.empty() || h.undo.back().barrier;
            }), .text = redo ? say("Redo", "重做") : say("Undo", "撤销"), .variant = ButtonVariant::ghost, .on_activated = [hp, sp, redo] { if (!sp->runtime || !*sp->runtime || !sp->root) return; auto& rt = **sp->runtime; const auto scope = rt.find(sp->root() + "/hero/demo/specimen"); const auto result = redo ? rt.redo(scope) : rt.undo(scope); if (result.exit_code() == std::optional<std::int64_t>(0)) hp->validation.set(""); }});
        history.push_back(std::move(b));
    }
    Element copy = button({.id = "copy", .text = computed([hp, sp] {
        const bool zh = sp->lang && sp->lang->get() == "zh";
        const std::string copied = hp->copied_tasks.get();
        const bool same = !copied.empty() && copied == markdown_tasks(hp->tasks.get(), zh);
        return std::string(same ? (zh ? "清单已复制" : "Checklist copied") : (zh ? "复制清单" : "Copy checklist"));
    }), .variant = ButtonVariant::ghost, .on_activated = [hp, sp] {
        if (!sp->runtime || !*sp->runtime) return;
        std::string text = markdown_tasks(hp->tasks.peek(), sp->lang && sp->lang->peek() == "zh");
        (*sp->runtime)->set_clipboard_text(text); hp->copied_tasks.set(std::move(text));
    }});
    copy.prop("undo_scope", Source(Value(true))); history.push_back(std::move(copy));
    Element card = column({.id = "specimen", .width = Dim(board_width), .padding = Edges(board_padding), .gap = 16, .background = "surface", .border_color = "border", .border_width = board_border, .radius = 20}, {
        row({.id = "heading", .gap = 12, .align = "center", .wrap = "wrap"}, {text({.id = "release", .text = say("Your next release", "你的下一次发布"), .style = "heading"}),
            badge({.id = "status", .text = computed([hp, sp] { const bool zh = sp->lang && sp->lang->get() == "zh"; return std::string(hp->shipped.get() ? (zh ? "已记录" : "Recorded") : (zh ? "草稿" : "Draft")); }), .tone = BadgeTone::accent})}),
        std::move(tasks), std::move(add), std::move(editor),
        dynamic([hp] { if (hp->validation.get().empty()) return std::vector<Element>{}; Element message = text({.id = "validation", .text = hp->validation, .style = "caption", .color = "text_muted"}); message.prop("role", Source(Value("alert"))); return std::vector<Element>{std::move(message)}; }),
        row({.id = "actions", .gap = 8, .align = "center", .wrap = "wrap"}, {
            button(std::move(ship)), button({.id = "reset", .disabled = computed([hp] { return hp->agent_running.get(); }), .text = say("Reset board", "重置工作台"), .variant = ButtonVariant::ghost,
                .on_activated = [hp, sp] { change_tasks(*hp, *sp, release_tasks()); hp->draft.set(""); hp->editing.set(""); hp->validation.set(""); hp->selected.set("ship"); hp->exchange.set(""); }})}),
        row({.id = "history", .gap = 8, .wrap = "wrap"}, std::move(history)),
        text({.id = "shipped", .text = computed([hp, sp] {
            const bool zh = sp->lang && sp->lang->get() == "zh"; if (hp->shipped.get()) return std::string(zh ? "已记录发布。可以撤销，或重置开始下一轮。" : "Shipped locally. Undo it, or reset for the next release.");
            const Value tasks = hp->tasks.get(); std::size_t complete = 0, total = tasks.list() ? tasks.list()->size() : 0;
            if (tasks.list()) for (const auto& task : *tasks.list()) if (const auto* done = task.get("done"); done && done->as_bool()) ++complete;
            return zh ? std::format("{} / {} 项已完成 · 数据保留在本次访问中", complete, total) : std::format("{} / {} done · kept for this visit", complete, total);
        }), .style = "caption", .color = "text_muted"})});
    card.prop("undo_scope", Source(Value(true)));

    // The same node, three more ways: read from the page's semantic tree.
    auto node_now = [hp, sp, zh_now, live]() -> Value {
        if (live && sp->nodes) {
            const Value nodes = sp->nodes->get();
            if (const Value* n = find_node(nodes, hp->selected.get())) return *n;
        }
        return still_node(sp->lang ? sp->lang->peek() == "zh" : zh_now);
    };
    auto panel = [&](std::string id, std::uint32_t hue, Prop<std::string> title, Prop<std::string> line) {
        Element line_text = text({.id = "line", .text = std::move(line), .style = "code", .max_width = Dim(std::max(40.0f, (stacked ? width : width - board_width - 64) - 38))});
        if (id == "reader") line_text.prop("origin", derive([node_now] { return str(node_now(), "origin"); }));
        // Technical views grow with the selected object's text.
        return row({.id = std::move(id), .min_width = Dim(0), .min_height = Dim(96.0f), .gap = 0, .align = "stretch", .background = "surface",
                    .border_color = "border", .border_width = 1, .radius = 12, .clip = true},
                   {box({.width = Dim(4.0f), .background = Fill(Color::hex(hue))}),
                    column({.id = "body", .min_width = Dim(0), .padding = Edges(12.0f, 16.0f), .gap = 6, .grow = 1},
                           {text({.id = "title", .text = std::move(title), .style = "caption"}), std::move(line_text)})});
    };
    auto from_tree = [&](std::function<std::string(const Value&, bool)> f) -> Prop<std::string> {
        if (!live) return f(node_now(), zh_now);
        return computed([node_now, f, lang = site.lang] { return f(node_now(), lang && lang->get() == "zh"); });
    };
    Element panels = column({.id = "renderings", .width = Dim(stacked ? width : width - board_width - 64), .min_width = Dim(0), .gap = 10},
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
                             .role = Role::none,
                             .paint = [hp, sp, live](PaintContext& ctx) {
                                 if (!live || !sp->runtime || !*sp->runtime || !sp->root) return;
                                 Runtime& rt = **sp->runtime;
                                 const Rect self = ctx.services.bounds(ctx.node);
                                 const std::string base = sp->root() + "/hero/";
                                 const NodeId from = rt.find(base + "demo/specimen/" + (hp->selected.get() == "ship" ? std::string("actions/ship") : "items/" + hp->selected.get() + "/done"));
                                 if (!from) return;
                                 const Rect f = rt.bounds(from);
                                 const float y0 = std::clamp(f.y + f.height / 2 - self.y, 4.0f, self.height - 4);
                                 constexpr std::array<std::uint32_t, 3> hues{reader_hue, agent_hue, test_hue};
                                 constexpr std::array<std::string_view, 3> names{"reader", "agent", "test"};
                                 for (int i = 0; i < 3; ++i) {
                                     const auto target = rt.find(base + "demo/renderings/" + std::string(names[static_cast<std::size_t>(i)]));
                                     if (!rt.valid(target)) continue;
                                     const auto bounds = rt.bounds(target);
                                     const float y1 = bounds.y + bounds.height / 2 - self.y;
                                     render::Path p;
                                     p.move_to({0, y0}).cubic_to({32, y0}, {32, y1}, {ctx.size.width, y1});
                                     const Color c = Color::hex(hues[static_cast<std::size_t>(i)]);
                                     ctx.list.stroke_path(p, render::StrokeStyle{.width = 1.5f, .cap = render::LineCap::round}, render::Paint::solid(c.with_alpha(0.8f)));
                                     ctx.list.fill_round_rect({ctx.size.width - 4, y1 - 3, 6, 6}, CornerRadii::all(3), render::Paint::solid(c));
                                 }
                                 ctx.list.fill_round_rect({-3, y0 - 3, 6, 6}, CornerRadii::all(3), render::Paint::solid(ctx.theme.color("text")));
                             }});

    ButtonOptions run{.id = "let_agent", .text = computed([hp, sp] { const bool zh = sp->lang && sp->lang->get() == "zh"; return std::string(hp->agent_running.get() ? (zh ? "停止模拟 agent" : "Stop the simulation") : (zh ? "播放模拟 agent" : "Play a simulated agent")); }), .variant = ButtonVariant::secondary};
    run.disabled = computed([hp] { const Value tasks = hp->tasks.get(); return !hp->agent_running.get() && (hp->shipped.get() || !tasks.list() || tasks.list()->empty()); });
    run.on_activated = [hp, sp] {
        if (!sp->runtime || !*sp->runtime) return;
        if (hp->agent_running.peek()) { ++hp->generation; hp->agent_running.set(false); hp->exchange.set(sp->lang && sp->lang->peek() == "zh" ? "模拟已停止。" : "Simulation stopped."); return; }
        if (hp->shipped.peek()) return;
        hp->editing.set(""); hp->agent_running.set(true); hp->exchange.set(""); const auto generation = ++hp->generation;
        auto steps = std::make_shared<std::vector<std::string>>(); const Value tasks = hp->tasks.peek();
        if (tasks.list()) for (const auto& task : *tasks.list()) if (!task.get("done") || !task.get("done")->as_bool()) steps->push_back(str(task, "id"));
        steps->push_back("ship");
        auto next = std::make_shared<std::function<void(std::size_t)>>();
        *next = [hp, sp, steps, generation, self = std::weak_ptr(next)](std::size_t k) {
            const auto next = self.lock(); if (!next || generation != hp->generation) return;
            if (k >= steps->size()) { hp->agent_running.set(false); return; }
            Runtime& rt = **sp->runtime;
            (void)rt.after(seconds(0.45), [hp, sp, steps, generation, next, k] {
                if (generation != hp->generation) return;
                Runtime& rt = **sp->runtime;
                if (!rt.valid(rt.find(sp->root() + "/hero/demo/specimen"))) { hp->agent_running.set(false); return; }
                const std::string id = (*steps)[k], path = id == "ship" ? "actions/ship" : "items/" + id + "/done";
                hp->selected.set(id); const std::string action = id == "ship" ? "activate" : "toggle";
                const Value tasks = hp->tasks.peek(); const auto* task = task_named(tasks, id);
                bool ok = task && task->get("done") && task->get("done")->as_bool();
                if (!ok) { Invocation call; call.actor = {"agent", "site-guide", {}}; ok = rt.act(rt.find(sp->root() + "/hero/demo/specimen/" + path), action, call).exit_code() == std::optional<std::int64_t>(0); }
                hp->exchange.set(std::format("{{\"act\": \"specimen/{}\", \"action\": \"{}\"}}\n→ {}", path, action, ok ? "done, reversible" : "refused"));
                (void)rt.after(seconds(0.8), [next, k] { (*next)(k + 1); });
            });
        };
        (*next)(0);
    };
    // The agent's turn: beside its words, or above them when narrow.
    std::vector<Element> agent_parts{button(std::move(run)),
                                     text({.id = "how",
                                           .text = say("A scripted demonstration on your current tasks. It uses real semantic actions; your own agent connects below.",
                                                       "在当前任务上运行的脚本演示，使用真实语义动作。你自己的 agent 可以从下方连接。"),
                                           .style = "caption"})};
    Element agent_row = narrow ? column({.id = "agent_row", .gap = 8, .align = "start"}, std::move(agent_parts))
                               : row({.id = "agent_row", .gap = 12, .align = "center"}, std::move(agent_parts));
    auto inspect_button = [hp, sp] { return button({.id = "inspect", .text = computed([hp, sp] { const bool zh = sp->lang && sp->lang->get() == "zh"; return std::string(hp->inspect.get() ? (zh ? "关闭技术视角" : "Close technical views") : (zh ? "检查选中的对象" : "Inspect the selected object")); }), .variant = ButtonVariant::ghost, .on_activated = [hp] { hp->inspect.set(!hp->inspect.peek()); }}); };
    Element explanation = column({.id = "context", .padding = Edges(24), .gap = 18, .grow = 1}, {
        brand_mark("identity", stacked ? 64.0f : 96.0f),
        text({.id = "title", .text = say("Make something.\nIt is already agent ready.", "开始做一件事。\n它已经能被 agent 操作。"), .style = "heading"}),
        text({.id = "lead", .text = say("Add a task, make a change, take it back. The screen, accessibility and semantic actions stay together.", "添加任务、修改内容，再撤销它。画面、无障碍与语义动作始终对应。"), .style = "body", .max_width = Dim(stacked ? width : width - board_width - 112)}),
        inspect_button()});
    Element context = dynamic([hp, panels, explanation, inspect_button] {
        if (!hp->inspect.get()) return std::vector<Element>{explanation};
        Element showing = panels;
        showing.children.push_back(inspect_button());
        return std::vector<Element>{std::move(showing)};
    });
    // The connections appear with the technical views they explain.
    links.prop("align_self", Source(Value("stretch")));
    links.prop("visible", derive([hp] { return hp->inspect.get(); }));
    Element demo = stacked ? column({.id = "demo", .gap = 20, .align = "stretch"}, {std::move(card), std::move(context)})
                          : row({.id = "demo", .gap = 0, .align = "start"}, {std::move(card), std::move(links), std::move(context)});
    return column({.id = "hero", .padding = Edges(Insets{4, 0, 32, 0}), .gap = narrow ? 16.0f : 22.0f},
                  {row({.id = "brand", .gap = narrow ? 12.0f : 18.0f, .align = "center"},
                       {brand_mark("mark", narrow ? 40.0f : 56.0f),
                        column({.min_width = Dim(0.0f), .gap = 6, .grow = 1, .shrink = 1},
                               {text({.id = "claim",
                                      .text = narrow ? say("One interface.\nEvery rendering.", "一个界面，\n每一种呈现。") : say("One interface. Every rendering.", "一个界面，每一种呈现。"),
                                      .style = narrow ? "title" : "display"})
                                    .prop("label", say("One interface. Every rendering.", "一个界面，每一种呈现。"))
                                    .prop("role", Source{Value("heading"), {}, {}})  // wrapping does not change its accessible name
                                    .prop("level", Source{Value(std::int64_t{1}), {}, {}}),
                                text({.id = "thesis",
                                      .text = say("omui is a UI framework implementing the openmui specification. Pixels, accessibility, agents and tests come from one semantic tree.",
                                                  "omui 是实现 openmui 规范的 UI 框架。一棵语义树，连接像素、无障碍、agent 与测试。"),
                                      .style = "body",
                                      .max_width = Dim(680.0f)})})}),
                   row({.id = "cta", .gap = 10, .align = "center", .wrap = "wrap"},
                       {button({.id = "to_start",
                                .text = say("Build with it", "用它来做"),
                                .variant = ButtonVariant::primary,
                                .on_activated = [sp] { if (sp->go) sp->go("hello"); }}),
                        button({.id = "to_explore",
                                .text = say("Explore what is possible", "探索可以做什么"),
                                .variant = ButtonVariant::secondary,
                                .on_activated = [sp] { if (sp->go) sp->go("explore"); }})}),
                   std::move(demo),
                   std::move(agent_row)});
}

}  // namespace omui::site
