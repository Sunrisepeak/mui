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
    Element heading = text({.id = "heading", .text = words(site, "Motion you can hold", "拿在手里的动效"), .style = "title"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{2})));
    return column({.id = "works", .width = Dim(width), .gap = 16},
                  {std::move(heading),
                   text({.id = "line", .text = words(site, "Each a capability you can take in your hands.", "每一个都是能拿在手里的能力。"), .style = "body", .color = "text_muted"}),
                   row({.id = "grid", .width = Dim(width), .gap = 20, .align = "start", .wrap = "wrap"}, std::move(works))});
}

}  // namespace omui::site
