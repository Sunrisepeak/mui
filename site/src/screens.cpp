module omui.site;

import std;
import omui;

namespace omui::site {

namespace {

auto say(const ScreensSite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}

// A screen: what it is about (small, in the accent), its claim, a line or two on it, and what shows it.
Element screen(const ScreensSite& site, std::string id, std::pair<std::string_view, std::string_view> eyebrow, std::pair<std::string_view, std::string_view> claim,
               std::pair<std::string_view, std::string_view> lead, Element shown) {
    return column({.id = std::move(id), .gap = 16},
                  {text({.id = "eyebrow", .text = say(site, eyebrow.first, eyebrow.second), .style = "label", .color = "accent"}),
                   text({.id = "claim", .text = say(site, claim.first, claim.second), .style = "title", .max_width = Dim(820.0f)}),
                   text({.id = "lead", .text = say(site, lead.first, lead.second), .style = "body", .size = 20.0f, .max_width = Dim(720.0f)}), std::move(shown)});
}

Element card(std::string id, std::vector<Element> children, float width) {
    return column({.id = std::move(id), .width = Dim(width), .padding = Edges(20.0f), .gap = 12, .background = "surface", .border_color = "border", .border_width = 1,
                   .radius = 12},
                  std::move(children));
}

std::string_view tier_name(mlibs::pipeline::Tier t, bool zh) {
    switch (static_cast<int>(t)) {
        case 0: return zh ? "T0 · 微控制器" : "T0 · a microcontroller";
        case 1: return zh ? "T1 · 嵌入式" : "T1 · embedded";
        case 2: return zh ? "T2 · 网页" : "T2 · the Web";
        case 3: return zh ? "T3 · 电脑" : "T3 · a computer";
        default: return zh ? "T4 · 独立显卡" : "T4 · a discrete GPU";
    }
}

// A bounded retry owned by the runtime's timers. A function capturing its
// own shared_ptr keeps itself (and its captures) alive after the last retry.
void hold_scroll_motion(Screens& screens, Runtime& rt, NodeId shape, int tries) {
    if (!screens.follow.peek() || !rt.valid(shape)) return;
    if (!rt.hold_motion(shape)) {
        if (tries > 0)
            (void)rt.after(seconds(1.0 / 60), [&screens, &rt, shape, tries] { hold_scroll_motion(screens, rt, shape, tries - 1); });
        return;
    }
    rt.pause_motion(shape);
    screens.held = true;
    if (const auto t = rt.motion_time(shape)) rt.seek_motion(shape, screens.at * t->first);
}

}  // namespace

Element agent_screen(const ScreensSite& site, float room) {
    const bool wide = room >= 900;
    Element actions = column({.id = "actions", .gap = 10, .align = "start"},
                             {button({.id = "copy", .text = say(site, "Copy the prompt for your agent", "复制给 agent 的提示词"), .variant = ButtonVariant::primary,
                                      .on_activated = site.copy_prompt}),
                              button({.id = "watch", .text = say(site, "Watch a simulated agent", "看一个模拟的 agent"), .on_activated = site.open_agent}),
                              text({.id = "how",
                                    .text = say(site, "No chat box, no model, no key here: your agent is the one you already use.",
                                                "这里没有对话框、模型和密钥：你的 agent 就是你已经在用的那个。"),
                                    .style = "caption",
                                    .color = "text_muted",
                                    .max_width = Dim(320.0f)})});
    Value zh = Value{}.set("empty", "还没有 agent 在这里操作过。").set("idle", "你的 agent 还没有连接。").set("undo", "撤销");
    Element log = card("record",
                       {text({.id = "title", .text = say(site, "What agents did on this page", "agent 在这一页做过的事"), .style = "label"}),
                        actor_log({.id = "log",
                                   .phrases = computed([lang = site.lang, zh] { return lang && lang->get() == "zh" ? zh : Value{}; }),
                                   .width = Dim(mlibs::layout::pct(100)),
                                   .label = "What agents did on this page"})},
                       wide ? 520.0f : room);
    return screen(site, "agent", {"Agent native", "agent 原生"}, {"Your agent works here, as you do.", "你的 agent 在这里工作，和你一样。"},
                  {"It links to this page through a bridge on your machine and operates the nodes you see — no cursor, no screenshots. openas decides "
                   "each request; each action is listed here, for you to undo.",
                   "它经你本机的一个桥连到这一页，直接操作你看到的这些节点——没有光标，没有截图。每个请求由 openas 决定；它做的每一步都列在这里，你可以撤销。"},
                  wide ? row({.id = "shown", .gap = 32, .align = "start"}, {std::move(actions), std::move(log)})
                       : column({.id = "shown", .gap = 20}, {std::move(actions), std::move(log)}));
}

Element time_screen(const ScreensSite& site, float room) {
    Element proof = card("proof",
                         {text({.id = "said", .text = say(site, "Seek visual time, or compare an independent reference run against CI.",
                                                          "寻址视觉时间，或用独立的参考回放对照 CI。"),
                                .style = "caption", .color = "text_muted"}),
                          row({.id = "do", .gap = 10, .wrap = "wrap"},
                              {button({.id = "replay", .text = say(site, "Replay the last seconds", "回放刚才"), .on_activated = site.replay}),
                               button({.id = "check", .text = say(site, "Check the pixels against CI", "对照 CI 的像素"), .variant = ButtonVariant::ghost,
                                       .on_activated = site.open_check})})},
                         std::min(room, 720.0f));
    return screen(site, "time", {"Deterministic", "确定性"}, {"Every moment, drawn again.", "每一刻，都能再画一次。"},
                  {"Seek animations without changing your task data. Use Undo in the release workspace to restore edits; the pixel comparison runs in its own reference instance.",
                   "寻址动画不会改变任务数据。要恢复编辑，可以使用发布工作台的撤销；像素对照在独立的参考实例中运行。"},
                  std::move(proof));
}

Element motion_screen(Screens& screens, const ScreensSite& site, float room) {
    const float w = std::min(room, 760.0f); const int columns = w >= 600 ? 3 : 2;
    constexpr float edge = 12, border = 1, gutter = 12;
    const float inner = w - 2 * (edge + border);
    if (screens.follow.peek()) {
        screens.held = false;
        screens.shaped.set(false);  // the new view starts from its list before the restored scroll seeks it
    }
    auto shaped = screens.shaped; auto* state = &screens;
    Element shape = column({.id = "shape", .width = Dim(w), .padding = Edges(edge), .gap = gutter, .background = "surface", .border_color = "border", .border_width = border, .radius = 16, .clip = true}, {
        dynamic([site, shaped, inner, columns, gutter] {
            const Value tasks = site.tasks ? site.tasks->tasks.get() : release_tasks();
            std::vector<Element> cards;
            if (tasks.list()) for (const auto& task : *tasks.list()) {
                const auto* key = task.get("id"); if (!key) continue; const std::string id(key->as_string());
                const bool done = task.get("done") && task.get("done")->as_bool();
                cards.push_back(column({.id = id,
                    .width = computed([shaped, inner, columns, gutter] { return shaped.get() ? Dim(std::floor((inner - gutter * static_cast<float>(columns - 1)) / static_cast<float>(columns))) : Dim(inner); }),
                    .min_height = computed([shaped] { return shaped.get() ? Dim(120) : Dim(48); }), .padding = Edges(12), .gap = 8, .background = done ? "accent_subtle" : "surface_variant", .radius = 10}, {
                    text({.id = "title", .text = computed([task, lang = site.lang] { return task_title(task, lang && lang->get() == "zh"); }), .style = "body", .origin = task.get("title") ? "user_content" : "app"}),
                    text({.id = "state", .text = done ? say(site, "Complete", "已完成") : say(site, "Next up", "待完成"), .style = "caption", .color = "text_muted"})}));
            }
            if (cards.empty()) cards.push_back(text({.id = "empty", .text = say(site, "Add a task in the release workspace above.", "先在上方工作台添加一项任务。"), .style = "body"}));
            return cards;
        })});
    shape.prop("direction", derive([shaped] { return std::string(shaped.get() ? "row" : "column"); }));
    shape.prop("wrap", derive([shaped] { return std::string(shaped.get() ? "wrap" : "nowrap"); }));
    shape.prop("motion", Source(Value("morph")));
    auto release = [state, site] {
        if (site.runtime && *site.runtime) { auto& rt = **site.runtime; rt.release_motion(rt.find("main/body/view/release/page/motion/shown/shape")); }
        state->held = false;
    };
    return screen(site, "motion", {"Universal motion", "通用动画"}, {"The same tasks. A different view.", "同一批任务，换一种视图。"},
        {"The release tasks you edited above keep their names and completion state. Switch between a list and cards, interrupt the change, or let scrolling seek through it.",
         "刚才编辑的发布任务，名称与完成状态都保留。切换列表与卡片，随时打断变化，也可以用滚动来寻址。"},
        column({.id = "shown", .width = Dim(w), .gap = 16, .clip = true}, {
            row({.id = "controls", .gap = 12, .align = "center", .wrap = "wrap"}, {
                button({.id = "change", .text = computed([shaped, lang = site.lang] { const bool zh = lang && lang->get() == "zh"; return std::string(shaped.get() ? (zh ? "切换列表" : "Show list") : (zh ? "切换卡片" : "Show cards")); }),
                    .on_activated = [state, release] { release(); state->follow.set(false); state->shaped.set(!state->shaped.peek()); }}),
                checkbox({.id = "follow", .checked = state->follow, .label = say(site, "Follow scrolling", "跟随滚动"),
                    .on_changed = [state, site, release](bool on) { release(); if (on) {
                        state->shaped.set(false);
                        if (site.runtime && *site.runtime) (void)(*site.runtime)->after(seconds(2.0 / 60), [state, site] { if (state->follow.peek() && site.runtime && *site.runtime) {
                            auto& rt = **site.runtime; const double y = rt.get(rt.find("main/body/view/release"), "y").as_double();
                            state->last_y = y; scrolled(*state, site, "main/body/view/release", y);
                        } });
                    } }})}),
            // The explanation precedes the moving viewport; its target
            // layout cannot overlap cards still travelling from the list.
            text({.id = "note", .text = say(site, "The objects keep their ids. The framework moves between their layouts.", "对象保留原来的 ID，布局之间的变化由框架完成。"), .style = "caption", .color = "text_muted"}),
            std::move(shape)}));
}

Element fast_screen(const ScreensSite& site, float room) {
    Element facts = text({.id = "device",
                          .text = computed([rt = site.runtime, lang = site.lang, effects = site.effects] {
                              (void)(effects ? effects->get() : std::string());
                              const bool zh = lang && lang->get() == "zh";
                              if (!rt || !*rt) return std::string();
                              const Adaptation& a = (*rt)->adaptation();
                              const mlibs::pipeline::Tier drawn = std::min(a.tier, a.effects);  // never above what the device is
                              const std::string by((*rt)->renderer());
                              return zh ? std::format("这台设备：{} · 由 {} 绘制 · 效果按 {}", tier_name(a.tier, true), by == "cpu" ? "CPU" : by, tier_name(drawn, true))
                                        : std::format("This device: {} · drawn by {} · effects as {}", tier_name(a.tier, false), by == "cpu" ? "the CPU" : by,
                                                      tier_name(drawn, false));
                          }),
                          .style = "code"});
    Element tiers = segmented({.id = "tier",
                               .label = "Draw as",
                               .labels = computed([lang = site.lang] {
                                   return lang && lang->get() == "zh" ? std::vector<std::string>{"按设备", "T1", "T2", "T3", "T4"}
                                                                      : std::vector<std::string>{"As it can", "T1", "T2", "T3", "T4"};
                               }),
                               .options = std::vector<std::string>{"auto", "t1", "t2", "t3", "t4"},
                               .value = site.effects ? Prop<std::string>(*site.effects) : Prop<std::string>(std::string("auto")),
                               .on_changed = site.set_effects,
                               .width = Dim(std::min(room, 420.0f))});
    // Three switches, three motions: each where its tier affords it, else the one it falls back to.
    auto flip = [](std::string id, std::string motion, std::string label) {
        Element t = toggle({.id = std::move(id), .label = std::move(label)});
        t.prop("motion", Source(Value(std::move(motion))));
        return t;
    };
    Element switches = row({.id = "switches", .gap = 28, .wrap = "wrap"},
                           {flip("liquid", "liquid", "Liquid · T3"), flip("ink", "ink", "Ink · T2"), flip("morph", "morph", "Morph · T1")});
    return screen(site, "fast", {"Performance tiers", "性能分档"}, {"One interface. A fitting budget.", "同一个界面，合适的预算。"},
                  {"Compare effect tiers on this device. The readout names the renderer and effective tier; choosing a higher tier stays within the device's reported capability.",
                   "在当前设备上比较效果档位。下方显示实际渲染器和生效档位；选择更高档位时，仍受设备报告的能力限制。"},
                  column({.id = "shown", .gap = 16}, {std::move(facts), std::move(tiers), std::move(switches)}));
}

void scrolled(Screens& screens, const ScreensSite& site, std::string_view place, double y) {
    if (!site.runtime || !*site.runtime || !screens.follow.peek()) return;
    Runtime& rt = **site.runtime;
    const NodeId view = rt.find(place), shape = rt.find(std::format("{}/page/motion/shown/shape", place));
    if (!rt.valid(view) || !rt.valid(shape)) return;
    // Where the motion screen will be once the page is at y (its bounds are as drawn at the last offset):
    // nothing while it is below the fold, all of it a quarter of the way up.
    const Rect v = rt.screen_bounds(view), b = rt.screen_bounds(shape);
    const double top = b.y + screens.last_y - y;
    screens.last_y = y;
    screens.at = std::clamp((v.bottom() - top) / (0.75 * v.height), 0.0, 1.0);
    auto seek = [&screens, &rt, shape] {
        if (const auto t = rt.motion_time(shape)) rt.seek_motion(shape, screens.at * t->first);
    };
    if (screens.held) return seek();
    if (screens.shaped.peek() || screens.at <= 0) return;
    // The change, then its morph held to the scroll: the frame after it starts the morph, so the hold is tried until it takes.
    screens.shaped.set(true);
    (void)rt.after(seconds(0), [&screens, &rt, shape] { hold_scroll_motion(screens, rt, shape, 8); });
}

}  // namespace omui::site
