module omui.site;

import std;
import omui;
import omui.ui.liquid_glass;
import hanim.engine;

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
    // ⑧ Glass that flows: two drops of liquid glass over a lively backdrop, sprung together and apart — they merge like drops.
    {
        Signal<bool> together = state.together;
        const float drop = std::round(std::min(inner, stage_height) * 0.42f);
        const auto sprung = [&](std::string id, float toward) {
            Element glass = liquid_glass({.id = std::move(id), .radius = drop / 2, .width = Dim(drop), .height = Dim(drop)});
            glass.prop("translate_x", derive([together, toward] { return together.get() ? toward : 0.0f; }));
            glass.animate("translate_x", Transition::spring_with());  // the drops' place on a spring
            return glass;
        };
        std::vector<Element> backdrop;
        const std::array<std::pair<std::string_view, std::uint32_t>, 3> blobs{{{"blob_a", 0x6366f1}, {"blob_b", 0xf59e0b}, {"blob_c", 0x14b8a6}}};
        for (std::size_t index = 0; index < blobs.size(); ++index) {
            const float side = std::round(stage_height * 0.42f);
            Element blob = box({.id = std::string(blobs[index].first), .width = Dim(side), .height = Dim(side), .background = Fill(Color::hex(blobs[index].second)),
                                .radius = side / 2});
            blob.prop("position", Source(Value("absolute")))
                .prop("left", Source(Value(static_cast<double>(inner * (0.12f + 0.3f * static_cast<float>(index))))))
                .prop("top", Source(Value(static_cast<double>(stage_height * (index % 2 == 0 ? 0.12f : 0.46f)))));
            backdrop.push_back(std::move(blob));
        }
        backdrop.push_back(column({.id = "word", .width = Dim(inner), .height = Dim(stage_height), .justify = "center", .align = "center"},
                                  {text({.id = "omui", .text = "omui", .style = "display", .size = std::round(stage_height * 0.32f)})}));
        backdrop.push_back(column({.id = "drops", .width = Dim(inner), .height = Dim(stage_height), .justify = "center", .align = "center"},
                                  {glass_group({.id = "glass", .gap = 40.0, .label = "Two drops of glass", .smoothness = 26.0, .thickness = 16.0},
                                               {sprung("left", drop * 0.62f), sprung("right", -drop * 0.62f)})}));
        works.push_back(work(site, "glass", card, {"Glass that flows", "会流动的玻璃"},
                             {"Two drops of liquid glass: bring them together and they merge, the backdrop bending through them.", "两滴液态玻璃：靠在一起就融成一滴，背后的画面在里面弯折。"},
                             stack({.id = "stage", .width = Dim(inner), .height = Dim(stage_height), .clip = true}, std::move(backdrop)),
                             row({.id = "controls", .width = Dim(inner), .gap = 10, .align = "center"},
                                 {segmented({.id = "drops",
                                             .label = words(site, "The drops", "两滴玻璃"),
                                             .labels = computed([lang = site.lang] {
                                                 return lang && lang->get() == "zh" ? std::vector<std::string>{"分开", "靠拢"} : std::vector<std::string>{"Apart", "Together"};
                                             }),
                                             .options = std::vector<std::string>{"apart", "together"},
                                             .value = Source{std::nullopt, [together] { return Value(std::string(together.get() ? "together" : "apart")); },
                                                             [together](const Value& value) { together.set(value.as_string() == "together"); }}})})));
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
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    return column({.id = "works", .width = Dim(width), .gap = 16},
                  {std::move(heading),
                   text({.id = "line", .text = words(site, "Each a capability you can take in your hands.", "每一个都是能拿在手里的能力。"), .style = "body", .color = "text_muted"}),
                   row({.id = "grid", .width = Dim(width), .gap = 20, .align = "start", .wrap = "wrap"}, std::move(works))});
}


namespace {

// A number as code writes it: to a thousandth, no trailing zeros.
std::string short_number(double value) {
    const double rounded = std::round(value * 1000) / 1000;
    return std::format("{}", rounded == 0 ? 0.0 : rounded);
}

// A recipe's parameter and period as tuned (its own where not).
double tuned_number(const hanim::anim::RecipeInfo& info, const WorkTuning& tuning) { return tuning.tuned ? tuning.amount : info.number; }
hanim::Vec2 tuned_pair(const hanim::anim::RecipeInfo& info, const WorkTuning& tuning) { return tuning.tuned ? hanim::Vec2{tuning.amount, tuning.amount_y} : info.vector; }
double tuned_period(const hanim::anim::RecipeInfo& info, const WorkTuning& tuning) { return tuning.period > 0 ? tuning.period : info.period; }

// How far the editor lets a parameter go: about its default, both ways where it signs a direction.
struct Range {
    double low, high, step;
};
Range parameter_range(const hanim::anim::RecipeInfo& info, double value) {
    switch (info.param) {
        case hanim::anim::RecipeParam::times: return {1, 6, 1};
        case hanim::anim::RecipeParam::angle: {
            const double reach = std::max(std::abs(value) * 2, 0.5) * 180 / std::numbers::pi;
            return {-std::round(reach), std::round(reach), 1};
        }
        default: break;
    }
    // A scale or factor (near 1): from half to half again; a distance: from nothing to three times.
    if (value > 0.5 && value < 1.5) return value < 1 ? Range{0.5, 1, 0.01} : Range{1, 1.5, 0.01};
    const double reach = std::max(std::abs(value) * 3, 8.0);
    return {info.param == hanim::anim::RecipeParam::pair ? -reach : 0, reach, 1};
}

}  // namespace

std::string recipe_cpp(std::string_view name, const WorkTuning& tuning) {
    const auto* info = hanim::anim::find_recipe(name);
    if (!info) return {};
    std::string args = "card";
    switch (info->param) {
        case hanim::anim::RecipeParam::none: break;
        case hanim::anim::RecipeParam::number:
        case hanim::anim::RecipeParam::angle: args += ", " + short_number(tuned_number(*info, tuning)); break;
        case hanim::anim::RecipeParam::times: args += std::format(", {}", static_cast<int>(std::round(tuned_number(*info, tuning)))); break;
        case hanim::anim::RecipeParam::pair: {
            const hanim::Vec2 pair = tuned_pair(*info, tuning);
            args += std::format(", {{{}, {}}}", short_number(pair.x), short_number(pair.y));
            break;
        }
    }
    if (info->period > 0) args += std::format(", seconds({})", short_number(tuned_period(*info, tuning)));
    if (tuning.duration > 0) args += std::format(", {{.duration = seconds({})}}", short_number(tuning.duration));
    return std::format("hanim::anim::{}({})", name, args);
}

std::string recipe_json(std::string_view name, const WorkTuning& tuning) {
    const auto* info = hanim::anim::find_recipe(name);
    if (!info) return {};
    std::string fields = std::format(R"({{"do": "{}", "object": "card")", name);
    switch (info->param) {
        case hanim::anim::RecipeParam::none: break;
        case hanim::anim::RecipeParam::number: fields += std::format(R"(, "{}": {})", info->param_name, short_number(tuned_number(*info, tuning))); break;
        // A document writes an angle in degrees.
        case hanim::anim::RecipeParam::angle: fields += std::format(R"(, "{}": {:.1f})", info->param_name, tuned_number(*info, tuning) * 180 / std::numbers::pi); break;
        case hanim::anim::RecipeParam::times: fields += std::format(R"(, "{}": {})", info->param_name, static_cast<int>(std::round(tuned_number(*info, tuning)))); break;
        case hanim::anim::RecipeParam::pair: {
            const hanim::Vec2 pair = tuned_pair(*info, tuning);
            fields += std::format(R"(, "{}": [{}, {}])", info->param_name, short_number(pair.x), short_number(pair.y));
            break;
        }
    }
    if (tuning.duration > 0) fields += std::format(R"(, "duration": {})", short_number(tuning.duration));
    if (info->period > 0) fields += std::format(R"(, "period": {})", short_number(tuned_period(*info, tuning)));
    return fields + "}";
}

namespace {

// A card of the library played (from its start when it is at its end).
LiveWallPlay recipe_play(Runtime** runtime) {
    return {.play = [runtime](const std::string& card) {
                if (runtime && *runtime) (void)(*runtime)->act((*runtime)->find(card + "/stage"), "play");
            },
            .few = 4,
            .every = 1.6};
}

}  // namespace

void recipe_library_tick(Works& state, const WorksSite& site) {
    if (site.runtime && *site.runtime) live_wall_tick(state.recipe_live, **site.runtime, recipe_play(site.runtime));
}

void tune_recipe(Works& state, std::string_view name) {
    const auto* info = hanim::anim::find_recipe(name);
    if (!info) return;
    batch([&] {
        state.tune_name.set(std::string(name));
        state.tune_amount.set(info->param == hanim::anim::RecipeParam::pair ? info->vector.x : info->number);
        state.tune_amount_y.set(info->vector.y);
        state.tune_duration.set(0.0);
        state.tune_period.set(info->period);
        state.tune_playing.set(false);
        // At rest it shows its card: an entrance at its end (it came in), the rest at their start.
        state.tune_at.set(info->kind == hanim::anim::RecipeKind::entrance ? 1e6 : 0.0);
    });
}

// The library's editor: one recipe on a larger stage, its parameter, duration (a loop's period)
// on sliders — each change made again and played — and its call and step as tuned, to copy:
// the step shown is the step that plays.
Element recipe_editor(Works& state, const WorksSite& site, float width) {
    Works* const model = &state;
    Runtime** runtime = site.runtime;
    const bool wide = width >= 760;
    const float stage_width = wide ? std::floor(width * 0.42f) : width - 32;
    return column({.id = "tune", .width = Dim(width), .padding = Edges(16.0f), .gap = 12, .background = "surface_variant", .radius = 16},
                  {dynamic([model, runtime, lang = site.lang, width, wide, stage_width] {
                      Works& state = *model;
                      const std::string name = state.tune_name.get();
                      const auto* info = hanim::anim::find_recipe(name);
                      if (!info) return std::vector<Element>{};
                      const bool zh = lang && lang->peek() == "zh";
                      const auto tuning = [model] {
                          const Works& state = *model;
                          return WorkTuning{.tuned = true, .amount = state.tune_amount.get(), .amount_y = state.tune_amount_y.get(),
                                            .duration = state.tune_duration.get(), .period = state.tune_period.get()};
                      };
                      // Each change seen at once: played again from its start.
                      const auto replay = [model] {
                          model->tune_at.set(0.0);
                          model->tune_playing.set(true);
                      };
                      std::vector<Element> sliders;
                      // A row a value: its name, its slider, the value it is at.
                      const float room = wide ? width - stage_width - 64 : width - 32;
                      const auto add = [&](std::string id, std::string label, Range range, std::function<double()> shown, Prop<double> value) {
                          const int places = range.step >= 1 ? 0 : range.step >= 0.1 ? 1 : 2;
                          sliders.push_back(row({.id = id + "_row", .width = Dim(room), .gap = 10, .align = "center"},
                                                {text({.id = "name", .text = label, .style = "caption", .color = "text_muted", .width = Dim(wide ? 96.0f : 72.0f)}),
                                                 slider({.id = id, .label = label, .max = range.high, .min = range.low, .step = range.step, .value = std::move(value),
                                                         .on_committed = [replay](double) { replay(); }, .min_width = Dim(0.0f), .grow = 1}),
                                                 text({.id = "value", .text = computed([shown, places] { return std::format("{:.{}f}", shown(), places); }), .style = "code",
                                                       .width = Dim(52.0f)})}));
                      };
                      const std::string parameter(info->param_name);
                      switch (info->param) {
                          case hanim::anim::RecipeParam::none: break;
                          case hanim::anim::RecipeParam::angle: {
                              // Degrees on the slider, radians in the recipe.
                              const Range range = parameter_range(*info, info->number);
                              add("amount", parameter, range, [model] { return std::round(model->tune_amount.get() * 180 / std::numbers::pi); },
                                  Source{std::nullopt, [model] { return Value(std::round(model->tune_amount.get() * 180 / std::numbers::pi)); },
                                         [model](const Value& value) { model->tune_amount.set(value.as_double() * std::numbers::pi / 180); }});
                              break;
                          }
                          case hanim::anim::RecipeParam::pair: {
                              const Range range = parameter_range(*info, std::max(std::abs(info->vector.x), std::abs(info->vector.y)));
                              add("amount", parameter + " x", range, [model] { return model->tune_amount.get(); }, state.tune_amount);
                              add("amount_y", parameter + " y", range, [model] { return model->tune_amount_y.get(); }, state.tune_amount_y);
                              break;
                          }
                          default: add("amount", parameter, parameter_range(*info, info->number), [model] { return model->tune_amount.get(); }, state.tune_amount); break;
                      }
                      if (info->period > 0) add("period", zh ? "周期（秒）" : "period (s)", {0.3, 6, 0.1}, [model] { return model->tune_period.get(); }, state.tune_period);
                      else
                          // Its own duration until set: what the stage says it is.
                          add("duration", zh ? "时长（秒）" : "duration (s)", {0.1, 2, 0.05}, [model] { return model->tune_duration.get() > 0 ? model->tune_duration.get() : model->tune_length.get(); },
                              Source{std::nullopt, [model] { return Value(model->tune_duration.get() > 0 ? model->tune_duration.get() : model->tune_length.get()); },
                                     [model](const Value& value) { model->tune_duration.set(value.as_double()); }});
                      const auto copy = [runtime](std::string text) {
                          if (runtime && *runtime) (*runtime)->set_clipboard_text(std::move(text));
                      };
                      Element stage = program_stage({.id = "stage",
                                                     .amount = state.tune_amount,
                                                     .amount_y = state.tune_amount_y,
                                                     .at = state.tune_at,
                                                     .duration = state.tune_duration,
                                                     .label = name,
                                                     .length = state.tune_length,
                                                     .period = state.tune_period,
                                                     .playing = state.tune_playing,
                                                     .tuned = true,
                                                     .work = "recipe:" + name,
                                                     .width = Dim(stage_width),
                                                     .height = Dim(std::round(stage_width * 0.56f))});
                      std::vector<Element> controls{text({.id = "name", .text = name, .style = "heading"})};
                      for (auto& one : sliders) controls.push_back(std::move(one));
                      controls.push_back(row({.id = "actions", .gap = 8, .align = "center", .wrap = "wrap"},
                                             {button({.id = "play", .text = zh ? "播放" : "Play", .variant = ButtonVariant::secondary, .on_activated = replay}),
                                              button({.id = "reset", .text = zh ? "还原" : "Reset", .variant = ButtonVariant::ghost,
                                                      .on_activated = [model, name] { tune_recipe(*model, name); }}),
                                              button({.id = "cpp", .text = zh ? "复制 C++" : "Copy C++", .variant = ButtonVariant::ghost,
                                                      .on_activated = [copy, name, tuning] { copy(recipe_cpp(name, tuning())); }}),
                                              button({.id = "json", .text = zh ? "复制 JSON" : "Copy JSON", .variant = ButtonVariant::ghost,
                                                      .on_activated = [copy, name, tuning] { copy(recipe_json(name, tuning())); }})}));
                      // The call as it is tuned, read as it changes.
                      controls.push_back(text({.id = "call", .text = computed([name, tuning] { return recipe_cpp(name, tuning()); }), .style = "code", .color = "text_muted",
                                               .max_width = Dim(wide ? width - stage_width - 64 : width - 32)}));
                      Element side = column({.id = "controls", .gap = 10}, std::move(controls));
                      return std::vector<Element>{wide ? row({.id = "editor", .gap = 24, .align = "start"}, {std::move(stage), std::move(side)})
                                                       : column({.id = "editor", .gap = 16}, {std::move(stage), std::move(side)})};
                  })});
}

Element recipe_library(Works& state, const WorksSite& site, float width) {
    const int columns = width >= 1000 ? 4 : width >= 640 ? 3 : 2;
    const float thumb = std::floor((width - static_cast<float>(columns - 1) * 14.0f) / static_cast<float>(columns));
    Runtime** runtime = site.runtime;
    const std::string root = site.root.substr(0, site.root.rfind('/')) + "/recipes";
    Element heading = text({.id = "heading", .text = words(site, "The recipe library", "配方库"), .style = "title"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{2})));
    Signal<std::string> kind = state.recipe_kind;
    Element filter = segmented({.id = "kind",
                                .label = words(site, "Kind", "种类"),
                                .labels = computed([lang = site.lang] {
                                    return lang && lang->get() == "zh" ? std::vector<std::string>{"全部", "进场", "离场", "提示", "循环"}
                                                                       : std::vector<std::string>{"All", "Entrances", "Exits", "Attention", "Loops"};
                                }),
                                .options = std::vector<std::string>{"all", "entrance", "exit", "attention", "loop"},
                                .value = kind});
    Works* const state_pointer = &state;
    Element grid = dynamic([kind, runtime, root, thumb, lang = site.lang, state = state_pointer] {
        state->recipe_live.cards.clear();
        const std::string shown = kind.get();
        const auto kind_name = [](hanim::anim::RecipeKind k) {
            switch (k) {
                case hanim::anim::RecipeKind::entrance: return "entrance";
                case hanim::anim::RecipeKind::exit: return "exit";
                case hanim::anim::RecipeKind::attention: return "attention";
                case hanim::anim::RecipeKind::loop: return "loop";
            }
            return "";
        };
        std::vector<Element> cards;
        for (const auto& info : hanim::anim::recipes()) {
            if (shown != "all" && shown != kind_name(info.kind)) continue;
            const std::string name(info.name);
            const std::string card_path = std::format("{}/grid/{}", root, name), stage_path = card_path + "/stage";
            state->recipe_live.cards.push_back(card_path);
            const auto hover = [state, runtime, card_path](bool on) {
                if (runtime && *runtime) live_wall_hover(state->recipe_live, **runtime, recipe_play(runtime), card_path, on);
            };
            const auto copy = [runtime](std::string text) {
                if (runtime && *runtime) (*runtime)->set_clipboard_text(text);
            };
            cards.push_back(column({.id = name, .width = Dim(thumb), .padding = Edges(10.0f), .gap = 6, .background = "surface", .border_color = "border",
                                    .border_width = 1, .radius = 12,
                                    .on = {{"pointer_enter", [hover](const Value&) { hover(true); }}, {"pointer_leave", [hover](const Value&) { hover(false); }}}},
                                   // At rest it shows its card: an entrance at its end (it came in), the rest at their start.
                                   {program_stage({.id = "stage", .at = info.kind == hanim::anim::RecipeKind::entrance ? 1e6 : 0.0, .label = name, .work = "recipe:" + name,
                                                   .width = Dim(thumb - 20), .height = Dim(std::round((thumb - 20) * 0.56f))}),
                                    row({.id = "head", .gap = 6, .align = "center"},
                                        {text({.id = "name", .text = name, .style = "code"}), spacer(),
                                         button({.id = "play", .text = lang && lang->peek() == "zh" ? "播放" : "Play", .variant = ButtonVariant::ghost,
                                                 .on_activated = [runtime, stage_path] {
                                                     if (runtime && *runtime) (void)(*runtime)->act((*runtime)->find(stage_path), "play");
                                                 }})}),
                                    row({.id = "copy", .gap = 6},
                                        {button({.id = "cpp", .text = "C++", .variant = ButtonVariant::ghost, .on_activated = [copy, name] { copy(recipe_cpp(name)); }}),
                                         button({.id = "json", .text = "JSON", .variant = ButtonVariant::ghost, .on_activated = [copy, name] { copy(recipe_json(name)); }}),
                                         // Into the editor (above the grid), which comes into view.
                                         button({.id = "tune", .text = lang && lang->peek() == "zh" ? "调整" : "Tune", .variant = ButtonVariant::ghost,
                                                 .on_activated = [state, runtime, root, name] {
                                                     tune_recipe(*state, name);
                                                     if (runtime && *runtime) (void)(*runtime)->act((*runtime)->find(root + "/tune"), "scroll_into_view");
                                                 }})})}));
        }
        return std::vector<Element>{row({.id = "grid", .gap = 14, .align = "start", .wrap = "wrap"}, std::move(cards))};
    });
    return column({.id = "recipes", .width = Dim(width), .gap = 14},
                  {std::move(heading),
                   text({.id = "line", .text = words(site, "hanim's library by name: play each on a card, tune it, copy its call or its step.", "hanim 的配方，按名字排开：在卡片上播放，调整参数，复制它的调用或文档步骤。"),
                         .style = "body", .color = "text_muted"}),
                   recipe_editor(state, site, width),
                   row({.id = "tools", .width = Dim(width), .gap = 12, .align = "center", .wrap = "wrap"}, {std::move(filter), spacer(), live_wall_pause(state.recipe_live, site.lang)}),
                   std::move(grid)});
}

}  // namespace omui::site
