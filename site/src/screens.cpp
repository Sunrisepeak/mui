module openmui.site;

import std;
import openmui;

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

std::string_view tier_name(pipeline::Tier t, bool zh) {
    switch (static_cast<int>(t)) {
        case 0: return zh ? "T0 · 微控制器" : "T0 · a microcontroller";
        case 1: return zh ? "T1 · 嵌入式" : "T1 · embedded";
        case 2: return zh ? "T2 · 网页" : "T2 · the Web";
        case 3: return zh ? "T3 · 电脑" : "T3 · a computer";
        default: return zh ? "T4 · 独立显卡" : "T4 · a discrete GPU";
    }
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
                                   .width = Dim(layout::pct(100)),
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
                         {text({.id = "said", .text = say(site, "This page's semantic tree, as your browser computed it, and as CI pinned it:",
                                                          "这一页的语义树，你的浏览器算出的，和 CI 固定的："),
                                .style = "caption", .color = "text_muted"}),
                          text({.id = "hash", .text = computed([h = site.hash] { return h ? h->get() : std::string(); }), .style = "code"}),
                          row({.id = "do", .gap = 10, .wrap = "wrap"},
                              {button({.id = "replay", .text = say(site, "Replay the last seconds", "回放刚才"), .on_activated = site.replay}),
                               button({.id = "check", .text = say(site, "Check the pixels against CI", "对照 CI 的像素"), .variant = ButtonVariant::ghost,
                                       .on_activated = site.open_check})})},
                         std::min(room, 720.0f));
    return screen(site, "time", {"Deterministic", "确定性"}, {"Every moment, drawn again.", "每一刻，都能再画一次。"},
                  {"Animations are functions of time: drag the page's time at the foot of the window, play it backwards, and it is drawn exactly as it was. "
                   "The tree your browser computes is the one CI pins on Linux, macOS and Windows.",
                   "动画是时间的函数：拖动窗口底部的页面时间、倒着放，页面就按当时的样子精确地重画。你的浏览器算出的语义树，就是 CI 在 Linux、macOS、Windows 上固定的那一棵。"},
                  std::move(proof));
}

Element motion_screen(Screens& screens, const ScreensSite& site, float room) {
    // One box, two drawings: a list of three, or three cards. Nothing here says how one becomes the other.
    const float w = std::min(room, 560.0f);
    auto shaped = screens.shaped;
    auto piece = [shaped, w](std::string id, std::string colour, int i) {
        Element p = box({.id = std::move(id),
                         .width = computed([shaped, w] { return shaped.get() ? Dim((w - 48) / 3) : Dim(w * 0.72f); }),
                         .height = computed([shaped] { return shaped.get() ? Dim(150.0f) : Dim(36.0f); }),
                         .background = computed([shaped, colour, i] { return Fill(shaped.get() ? colour : i == 0 ? std::string("accent") : std::string("surface_variant")); })});
        p.prop("radius", derive([shaped] { return shaped.get() ? 20.0 : 8.0; }));
        return p;
    };
    Element shape = column({.id = "shape", .width = Dim(w), .height = Dim(190.0f), .padding = Edges(12.0f), .gap = 12, .background = "surface", .border_color = "border",
                            .border_width = 1, .radius = 16},
                           {piece("a", "#6366f1", 0), piece("b", "#22c55e", 1), piece("c", "#f59e0b", 2)});
    shape.prop("direction", derive([shaped] { return std::string(shaped.get() ? "row" : "column"); }));
    shape.prop("motion", Source(Value("morph")));  // whatever its drawing becomes, it moves there
    return screen(site, "motion", {"Universal motion", "通用动画"}, {"One motion for every change.", "一种动画，适用于每一种变化。"},
                  {"Whatever changed moves from its old drawing to its new one — a control, a list, a page. Scroll: this list becomes three cards, "
                   "and the scroll is the motion's time.",
                   "变了的东西，从旧的样子移动到新的样子——控件、列表、整页都一样。往下滚：这个列表变成三张卡片，滚动就是这段动画的时间轴。"},
                  column({.id = "shown", .gap = 10}, {std::move(shape),
                                                      text({.id = "note", .text = say(site, "Its code says only the two drawings.", "代码里只有这两种样子。"),
                                                            .style = "caption", .color = "text_muted"})}));
}

Element fast_screen(const ScreensSite& site, float room) {
    Element facts = text({.id = "device",
                          .text = computed([rt = site.runtime, lang = site.lang, effects = site.effects] {
                              (void)(effects ? effects->get() : std::string());
                              const bool zh = lang && lang->get() == "zh";
                              if (!rt || !*rt) return std::string();
                              const Adaptation& a = (*rt)->adaptation();
                              const pipeline::Tier drawn = std::min(a.tier, a.effects);  // never above what the device is
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
    return screen(site, "fast", {"Performance tiers", "性能分档"}, {"Fast on every device.", "在每一种设备上都快。"},
                  {"openmui measures what the device can afford and draws the effects that fit: the same page on a phone, a laptop or a workstation, "
                   "every frame on time. Draw as another tier, and flip the switches.",
                   "openmui 测出设备负担得起什么，就画什么效果：同一页在手机、笔记本、工作站上，每一帧都准时。换一个档位画，再拨一拨开关。"},
                  column({.id = "shown", .gap = 16}, {std::move(facts), std::move(tiers), std::move(switches)}));
}

void scrolled(Screens& screens, const ScreensSite& site, std::string_view place, double y) {
    if (!site.runtime || !*site.runtime) return;
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
    auto hold = std::make_shared<std::function<void(int)>>();
    *hold = [&screens, &rt, shape, hold, seek](int tries) {
        if (!rt.valid(shape)) return;
        if (!rt.hold_motion(shape)) {
            if (tries > 0) (void)rt.after(seconds(1.0 / 60), [hold, tries] { (*hold)(tries - 1); });
            return;
        }
        rt.pause_motion(shape);
        screens.held = true;
        seek();
    };
    (void)rt.after(seconds(0), [hold] { (*hold)(8); });
}

}  // namespace omui::site
