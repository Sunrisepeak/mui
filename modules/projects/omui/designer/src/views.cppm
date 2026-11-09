// The overlays the player lays over an application while it is designed
// (D-149, D-151): a preview half seen, a board of variants, the timeline of
// the design's history. Each is a view of its model with the handlers it is
// given — nothing here changes the design; the player's verbs do.
module omui.designer:views;

import std;
import omui.core;
import omui.view;

namespace omui::designer {

// The design overlays' colours: a dark glass over any application.
inline constexpr std::string_view glass = "#0f172ae8", ink = "#f8fafc", muted = "#94a3b8", accent = "#38bdf8", paper = "#f8fafc";

inline std::string utf8_of(const std::filesystem::path& file) {
    const std::u8string text = file.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// A small labelled button of the overlays (its id is what an agent presses).
inline Element chip(std::string id, std::string label, std::string_view fill, std::string_view text_colour, Handler pressed) {
    return box({.id = std::move(id),
                .padding = Edges(4.0f, 10.0f),
                .background = Fill(std::string(fill)),
                .radius = 6,
                .label = label,
                .on = {{"press", std::move(pressed)}}},
               {text({.text = label, .style = "label", .color = Fill(std::string(text_colour))})});
}

// The preview: its picture over the application, half seen, with how to take it or let it go.
inline Element preview_view(Size window, const std::filesystem::path& picture, Handler accept, Handler reject) {
    return stack({.width = window.width, .height = window.height, .label = "A preview of a design change"},
                 {image({.id = "ghost", .source = utf8_of(picture), .alt = "the design as it would be", .fit = "fill", .width = window.width,
                         .height = window.height, .opacity = 0.55f}),
                  row({.id = "choice", .padding = Edges(6.0f, 10.0f), .gap = 8, .align = "center", .background = Fill(std::string(glass)), .radius = 8},
                      {text({.text = "Preview", .style = "label", .color = Fill(std::string(ink))}),
                       chip("accept", "Accept", "#22c55e", "#052e16", std::move(accept)),
                       chip("reject", "Reject", "#e2e8f0", "#0f172a", std::move(reject))})});
}

// A variant on the board.
struct Card {
    std::string           name;
    std::filesystem::path picture;
};

// The board: each variant's picture on a card, side by side, to pick one.
inline Element board_view(Size window, std::string_view brief, const std::vector<Card>& cards, const std::function<void(const std::string&)>& pick) {
    const float columns = static_cast<float>(std::max<std::size_t>(cards.size(), 1));
    const float card_width = std::max(80.0f, (window.width - 24.0f - 12.0f * (columns - 1)) / columns);
    const float picture_height = (card_width - 12.0f) * window.height / std::max(window.width, 1.0f);
    std::vector<Element> shown;
    for (const Card& card : cards) {
        const std::string chosen = card.name;
        shown.push_back(column({.id = std::format("variant-{}", card.name),
                                .width = card_width,
                                .padding = 6,
                                .gap = 6,
                                .background = Fill(std::string(paper)),
                                .radius = 10,
                                .shadow = "md",
                                .label = std::format("Variant {}", card.name),
                                .on = {{"press", [pick, chosen](const Value&) { pick(chosen); }}}},
                               {image({.source = utf8_of(card.picture), .alt = std::format("variant {}", card.name), .fit = "contain",
                                       .width = card_width - 12.0f, .height = picture_height}),
                                text({.text = card.name, .style = "label", .color = "#0f172a"})}));
    }
    return column({.width = window.width, .height = window.height, .padding = 12, .gap = 10, .justify = "center", .background = "#0f172ac0",
                   .label = std::format("Variants: {}", brief)},
                  {text({.id = "brief", .text = std::string(brief), .style = "title", .color = Fill(std::string(ink))}),
                   row({.id = "cards", .gap = 12, .align = "start"}, std::move(shown))});
}

// The timeline (D-151 §2.14): the design's history and its time in one
// picture — a lane for each branch, its commits placed by when they were
// made (one column each), each with its picture when it has one.
struct TimelineCommit {
    std::string                          id, message, actor;
    std::size_t                          column = 0;  // its place in time among all commits
    std::optional<std::filesystem::path> picture;
    bool                                 shown = false;     // what the application shows now
    bool                                 selected = false;  // one of two being compared
};
struct TimelineLane {
    std::string                 branch;
    bool                        head = false;  // designed on
    std::vector<TimelineCommit> commits;       // by column
};
struct Timeline {
    std::vector<TimelineLane> lanes;
    std::size_t               columns = 0;
    std::string               note;  // what the last press came to, or how to use it
    bool                      comparing = false;
};
struct TimelineHandlers {
    std::function<void(const std::string&)> open;    // a commit pressed: shown
    std::function<void(const std::string&)> select;  // a commit pressed while comparing
    std::function<void(int)>                step;    // back or forward in time
    std::function<void()>                   compare, close;
};

inline Element timeline_view(Size window, const Timeline& model, const TimelineHandlers& handlers) {
    constexpr float lane_label = 96, cell = 74, thumb_width = 64, thumb_height = 40;
    const float panel_height = std::clamp(window.height * 0.36f, 150.0f, 320.0f);
    // The latest columns that fit.
    const auto fits = static_cast<std::size_t>(std::max(1.0f, (window.width - lane_label - 40.0f) / cell));
    const std::size_t first = model.columns > fits ? model.columns - fits : 0;
    std::vector<Element> lanes;
    for (const TimelineLane& lane : model.lanes) {
        std::vector<Element> cells;
        std::size_t next = first;
        for (const TimelineCommit& commit : lane.commits) {
            if (commit.column < first) continue;
            for (; next < commit.column; ++next) cells.push_back(box({.width = cell, .height = thumb_height + 18}));
            const std::string id = commit.id;
            const Handler pressed = model.comparing ? Handler([handlers, id](const Value&) { handlers.select(id); })
                                                    : Handler([handlers, id](const Value&) { handlers.open(id); });
            const std::string_view edge = commit.selected ? "#f59e0b" : commit.shown ? accent : "#334155";
            Element picture = commit.picture ? image({.source = utf8_of(*commit.picture), .alt = commit.message, .fit = "cover", .width = thumb_width,
                                                      .height = thumb_height, .radius = 4})
                                             : box({.width = thumb_width, .height = thumb_height, .background = "#1e293b", .radius = 4});
            cells.push_back(column({.id = std::format("commit-{}", id.substr(0, 12)),
                                    .width = cell - 6,
                                    .padding = 3,
                                    .gap = 2,
                                    .align = "center",
                                    .border_color = Fill(std::string(edge)),
                                    .border_width = commit.shown || commit.selected ? 2.0f : 1.0f,
                                    .radius = 6,
                                    .label = commit.message,
                                    .description = std::format("{} by {}", id.substr(0, 8), commit.actor),
                                    .on = {{"press", pressed}}},
                                   {std::move(picture),
                                    text({.text = commit.message, .style = "caption", .color = Fill(std::string(muted)), .wrap = false, .max_lines = 1})}));
            next = commit.column + 1;
        }
        lanes.push_back(row({.id = std::format("lane-{}", lane.branch), .gap = 6, .align = "center"},
                            {box({.width = lane_label},
                                 {text({.text = lane.head ? std::format("● {}", lane.branch) : lane.branch, .style = "label",
                                        .color = Fill(std::string(lane.head ? accent : ink)), .wrap = false, .max_lines = 1})}),
                             row({.id = "commits", .gap = 6}, std::move(cells))}));
    }
    Element panel = column({.id = "panel",
                            .width = window.width,
                            .height = panel_height,
                            .padding = 10,
                            .gap = 8,
                            .background = Fill(std::string(glass)),
                            .label = "The design's history"},
                           {row({.id = "bar", .gap = 8, .align = "center"},
                                {text({.text = "Design history", .style = "label", .color = Fill(std::string(ink))}), spacer(),
                                 chip("back", "◀", "#1e293b", std::string(ink), [handlers](const Value&) { handlers.step(-1); }),
                                 chip("forward", "▶", "#1e293b", std::string(ink), [handlers](const Value&) { handlers.step(1); }),
                                 chip("compare", model.comparing ? "Comparing" : "Compare", model.comparing ? "#f59e0b" : "#1e293b",
                                      model.comparing ? "#1c1917" : std::string(ink), [handlers](const Value&) { handlers.compare(); }),
                                 chip("close", "Close", "#1e293b", std::string(ink), [handlers](const Value&) { handlers.close(); })}),
                            scroll({.id = "lanes", .grow = 1}, {column({.id = "branches", .gap = 6}, std::move(lanes))}),
                            text({.id = "note", .text = model.note, .style = "caption", .color = Fill(std::string(muted)), .wrap = false, .max_lines = 2})});
    return column({.width = window.width, .height = window.height, .justify = "end"}, {std::move(panel)});
}

}  // namespace omui::designer
