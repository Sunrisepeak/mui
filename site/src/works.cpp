module omui.site;

import std;
import omui;

namespace omui::site {
namespace {

auto words(const WorksSite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}

// A work: its stage, its name, one line, its controls.
Element work(const WorksSite& site, std::string id, float width, std::pair<std::string_view, std::string_view> name, std::pair<std::string_view, std::string_view> line,
             Element stage, Element controls) {
    return column({.id = std::move(id), .width = Dim(width), .padding = Edges(16.0f), .gap = 10, .background = "surface", .border_color = "border",
                   .border_width = 1, .radius = 16},
                  {std::move(stage), text({.id = "name", .text = words(site, name.first, name.second), .style = "label"}),
                   text({.id = "line", .text = words(site, line.first, line.second), .style = "caption", .color = "text_muted", .max_width = Dim(width - 32)}),
                   std::move(controls)});
}

// A recipe's time in your hands: play (pause), and its time to drag.
Element transport(const WorksSite& site, Signal<double> at, Signal<double> length, Signal<bool> playing, float width) {
    return row({.id = "controls", .width = Dim(width), .gap = 10, .align = "center"},
               {button({.id = "play",
                        .text = computed([lang = site.lang, playing] {
                            const bool zh = lang && lang->get() == "zh";
                            return std::string(playing.get() ? (zh ? "暂停" : "Pause") : (zh ? "播放" : "Play"));
                        }),
                        .variant = ButtonVariant::secondary,
                        .on_activated = [playing] { playing.set(!playing.peek()); }}),
                slider({.id = "time", .label = "Time", .max = length, .step = 0.01, .value = at, .grow = 1})});
}

// The same three items, a list or cards: one container, each item keeping its
// identity (its id) and only its place and size changing — so it travels
// there (universal motion), never cut and drawn anew.
Element list_and_cards(const WorksSite& site, Signal<bool> cards, float width, float height) {
    return dynamic([lang = site.lang, cards, width, height] {
        const bool zh = lang && lang->get() == "zh";
        const bool as_cards = cards.get();
        const std::array<std::pair<std::string_view, std::string_view>, 3> items{{{"Passport", "证件"}, {"Tickets", "车票"}, {"Keys", "钥匙"}}};
        const float card = std::floor((width - 2 * 12) / 3), card_height = std::min(height - 16, 120.0f), row_height = 52;
        const float list_top = std::max(0.0f, (height - 3 * row_height - 2 * 10) / 2), cards_top = std::max(0.0f, (height - card_height) / 2);
        std::vector<Element> children;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const float x = as_cards ? static_cast<float>(index) * (card + 12) : 0;
            const float y = as_cards ? cards_top : list_top + static_cast<float>(index) * (row_height + 10);
            Element item = column({.id = std::format("item{}", index), .width = Dim(as_cards ? card : width), .height = Dim(as_cards ? card_height : row_height),
                                   .padding = Edges(12.0f), .gap = 4, .justify = "center", .background = "surface_variant", .radius = as_cards ? 16.0f : 10.0f},
                                  {text({.id = "name", .text = std::string(zh ? items[index].second : items[index].first), .style = "label"}),
                                   text({.id = "state", .text = std::string(zh ? "已收好" : "Packed"), .style = "caption", .color = "text_muted"})});
            item.prop("position", Source(Value("absolute"))).prop("left", Source(Value(static_cast<double>(x)))).prop("top", Source(Value(static_cast<double>(y))));
            item.prop("motion", Source(Value("morph")));  // a change of place and size: travelled
            children.push_back(std::move(item));
        }
        return std::vector<Element>{box({.id = "items", .width = Dim(width), .height = Dim(height)}, std::move(children))};
    });
}

}  // namespace

Element signature_works(Works& state, const WorksSite& site, float width) {
    const int columns = width >= 1000 ? 2 : 1;
    const float card = std::floor((width - (columns - 1) * 20.0f) / static_cast<float>(columns));
    const float inner = card - 32, stage_height = std::round(inner * 0.56f);
    const auto stage = [&](std::string work, std::string label, Signal<double> at, Signal<double> length, Signal<bool> playing) {
        return program_stage({.id = "stage", .at = at, .label = std::move(label), .length = length, .playing = playing, .work = std::move(work),
                              .width = Dim(inner), .height = Dim(stage_height)});
    };
    std::vector<Element> works{
        work(site, "mark", card, {"The mark writes itself", "标志自己写出来"},
             {"Each stroke drawn along its length. Drag its time: any moment, the same each time.", "每一笔沿着长度画出。拖动时间：任何一刻，每次都一样。"},
             stage("mark-writing", "The mark, written", state.mark_at, state.mark_length, state.mark_playing),
             transport(site, state.mark_at, state.mark_length, state.mark_playing, inner)),
        work(site, "gather", card, {"Pixels gather into Mui", "像素拼回成 Mui"},
             {"Every pixel a vector square, flying home. Scrub it backwards and they fly apart.", "每个像素都是矢量方块，各自飞回原位。往回拖，它们又散开。"},
             stage("mui-gather", "Mui's pixels, gathering", state.gather_at, state.gather_length, state.gather_playing),
             transport(site, state.gather_at, state.gather_length, state.gather_playing, inner)),
    };
    // ③ A soft landing, rewound (the soft card: drop it, then drag its time back).
    {
        Runtime** runtime = site.runtime;
        const std::string card_path = site.root + "/grid/soft/stage";
        works.push_back(work(site, "soft", card, {"A soft landing, rewound", "软着陆，再倒回去"},
                             {"A card of jelly lands and gives. Drag its time back: every step the same each time.", "果冻做的卡片落地、变形。把时间往回拖：每一步每次都一样。"},
                             drop_card({.id = "stage", .at = state.card_at, .span = 3.0, .width = Dim(inner), .height = Dim(stage_height)}),
                             row({.id = "controls", .width = Dim(inner), .gap = 10, .align = "center"},
                                 {button({.id = "drop", .text = words(site, "Drop", "扔下去"), .variant = ButtonVariant::secondary,
                                          .on_activated = [runtime, card_path] {
                                              if (runtime && *runtime) (void)(*runtime)->act((*runtime)->find(card_path), "drop");
                                          }}),
                                  slider({.id = "time", .label = "Time", .max = 3.0, .step = 0.01, .value = state.card_at, .grow = 1})})));
    }
    // ④ A line changing language in place: the letters both share stay, the rest give way.
    {
        Signal<bool> other = state.other;
        Element line = text({.id = "words",
                             .text = computed([lang = site.lang, other] {
                                 const bool zh = (lang && lang->get() == "zh") != other.get();
                                 return std::string(zh ? "omui 绘制每一个字形" : "omui draws every glyph");
                             }),
                             .style = "heading",
                             .align = "center",
                             .size = 30.0f});
        line.prop("motion", Source(Value("morph")));
        works.push_back(work(site, "letters", card, {"Only what changed moves", "只动变了的部分"},
                             {"Switch the language: the letters both share stay put; the rest give way.", "切换语言：共有的字母不动，其余的让位。"},
                             column({.id = "stage", .width = Dim(inner), .height = Dim(stage_height), .justify = "center", .align = "center"}, {std::move(line)}),
                             row({.id = "controls", .width = Dim(inner), .gap = 10, .align = "center"},
                                 {button({.id = "switch",
                                          .text = computed([lang = site.lang, other] {
                                              const bool zh = (lang && lang->get() == "zh") != other.get();
                                              return std::string(zh ? "English" : "中文");
                                          }),
                                          .variant = ButtonVariant::secondary,
                                          .on_activated = [other] { other.set(!other.peek()); }})})));
    }
    // ⑥ A spring you can throw: dragged and let go, it springs home with the speed it had.
    {
        Runtime** runtime = site.runtime;
        const std::string chip_path = site.root + "/grid/spring/stage";
        works.push_back(work(site, "spring", card, {"A spring you can throw", "能甩的弹簧"},
                             {"Drag it, fling it, let go: it springs home with the speed it had. Catch it mid-flight.", "拖着它甩出去再松手：它带着当时的速度弹回家。半路还能接住。"},
                             throw_chip({.id = "stage", .label = words(site, "Throw me", "甩我"), .width = Dim(inner), .height = Dim(stage_height)}),
                             row({.id = "controls", .width = Dim(inner), .gap = 10, .align = "center"},
                                 {button({.id = "fling", .text = words(site, "Fling it", "甩一下"), .variant = ButtonVariant::secondary,
                                          .on_activated = [runtime, chip_path] {
                                              if (!runtime || !*runtime) return;
                                              Invocation call;
                                              call.args = Value{}.set("vx", 1400.0).set("vy", -700.0);
                                              (void)(*runtime)->act((*runtime)->find(chip_path), "fling", call);
                                          }})})));
    }
    // ⑦ An agent changes a motion: it reads the cards' choreography and asks for them later and bouncier.
    {
        Runtime** runtime = site.runtime;
        const std::string stage_path = site.root + "/grid/agent/stage";
        Signal<double> delay = state.rise_delay;
        const auto retime = [runtime, stage_path](Actor actor, double to_delay, double to_bounce) {
            if (!runtime || !*runtime) return;
            Invocation call;
            call.actor = std::move(actor);
            call.args = Value{}.set("delay", to_delay).set("bounce", to_bounce);
            (void)(*runtime)->act((*runtime)->find(stage_path), "retime", call);
        };
        works.push_back(work(site, "agent", card, {"An agent changes a motion", "Agent 当场改动画"},
                             {"It reads the cards' timing — the same plan you see — and asks for 0.3 s later, a little bounce.", "它读到卡片的时间编排（和你看到的是同一份），要求晚 0.3 秒、弹一点。"},
                             program_stage({.id = "stage", .at = state.rise_at, .bounce = state.rise_bounce, .delay = state.rise_delay, .label = "Three cards, rising",
                                            .length = state.rise_length, .plan = state.rise_plan, .playing = state.rise_playing, .work = "cards-rise",
                                            .width = Dim(inner), .height = Dim(stage_height)}),
                             column({.id = "controls", .width = Dim(inner), .gap = 8},
                                    {row({.id = "do", .gap = 10, .align = "center", .wrap = "wrap"},
                                         {button({.id = "ask", .text = words(site, "Ask the agent: 0.3 s later, a little bounce", "让 agent 改：晚 0.3 秒、弹一点"),
                                                  .variant = ButtonVariant::primary,
                                                  .on_activated = [retime, delay] { retime(Actor{"agent", "works-demo", {}}, delay.peek() + 0.3, 0.4); }}),
                                          button({.id = "reset", .text = words(site, "Reset the timing", "恢复原来的时间"), .variant = ButtonVariant::ghost,
                                                  .on_activated = [retime] { retime(Actor{}, 0, 0); }})}),
                                     text({.id = "plan",
                                           .text = computed([lang = site.lang, plan = state.rise_plan, delay = state.rise_delay, bounce = state.rise_bounce] {
                                               const bool zh = lang && lang->get() == "zh";
                                               return std::format("{} {:.1f} s · {} {:.1f} · {}", zh ? "延迟" : "delay", delay.get(), zh ? "回弹" : "bounce", bounce.get(), plan.get());
                                           }),
                                           .style = "code",
                                           .color = "text_muted"})})));
    }
    // ⑤ The same items, a list and cards.
    {
        Signal<bool> cards = state.cards;
        works.push_back(work(site, "layout", card, {"A list becomes cards", "列表变成卡片"},
                             {"The same items keep who they are: each travels to its new place.", "同一批事项保持身份：各自走到新位置。"},
                             column({.id = "stage", .width = Dim(inner), .height = Dim(stage_height), .justify = "center"}, {list_and_cards(site, cards, inner, stage_height)}),
                             row({.id = "controls", .width = Dim(inner), .gap = 10, .align = "center"},
                                 {segmented({.id = "as",
                                             .label = words(site, "Show as", "显示为"),
                                             .labels = computed([lang = site.lang] {
                                                 return lang && lang->get() == "zh" ? std::vector<std::string>{"列表", "卡片"} : std::vector<std::string>{"List", "Cards"};
                                             }),
                                             .options = std::vector<std::string>{"list", "cards"},
                                             .value = Source{std::nullopt, [cards] { return Value(std::string(cards.get() ? "cards" : "list")); },
                                                             [cards](const Value& value) { cards.set(value.as_string() == "cards"); }}})})));
    }
    Element heading = text({.id = "heading", .text = words(site, "Motion you can hold", "拿在手里的动效"), .style = "title"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{2})));
    return column({.id = "works", .width = Dim(width), .gap = 16},
                  {std::move(heading),
                   text({.id = "line", .text = words(site, "Each a capability you can take in your hands.", "每一个都是能拿在手里的能力。"), .style = "body", .color = "text_muted"}),
                   row({.id = "grid", .width = Dim(width), .gap = 20, .align = "start", .wrap = "wrap"}, std::move(works))});
}

}  // namespace omui::site
