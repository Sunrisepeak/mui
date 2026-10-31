// The application: notes you can add, star and delete; deleting them all is
// irreversible, so the verb behind it states its answer.
export module notes;

import std;
import openmui;

export namespace notes {

struct Note {
    std::string id;
    std::string text;
    bool        starred = false;
    bool operator==(const Note&) const = default;
};

struct Model {
    omui::Signal<std::vector<Note>> notes{std::vector<Note>{{"n1", "Buy milk", false}, {"n2", "Call Ada", true}}};
    omui::Signal<std::string>       draft{std::string{}};
    omui::Signal<bool>              starred_only{false};
    int                             next = 3;

    void add() {
        const std::string text = draft.peek();
        if (text.empty()) return;
        auto list = notes.peek();
        list.push_back({std::format("n{}", next++), text, false});
        notes.set(std::move(list));
        draft.set("");
    }
};

[[nodiscard]] inline omui::View view(Model& m) {
    using namespace omui;
    return column({.padding = 20, .gap = 12}, {
        text({.id = "title", .text = "Notes", .style = "title"}),
        row({.id = "compose", .gap = 8}, {
            text_box({.id = "draft", .label = "New note", .placeholder = "Write a note", .text = m.draft,
                      .on_submitted = [&m](std::string) { m.add(); }, .grow = 1}),
            button({.id = "add", .text = "Add", .variant = ButtonVariant::primary, .on_activated = [&m] { m.add(); }}),
        }),
        toggle({.id = "starred-only", .checked = m.starred_only, .label = "Starred only"}),
        column({.id = "list", .gap = 6}, {dynamic([&m] {
                   std::vector<Element> rows;
                   const auto& all = m.notes.get();
                   const bool only = m.starred_only.get();
                   untracked([&] {
                       for (const Note& n : all) {
                           if (only && !n.starred) continue;
                           Element r = row({.id = n.id, .gap = 8}, {
                               checkbox({.id = "star", .checked = n.starred, .label = n.text,
                                         .on_changed = [&m, id = n.id](bool v) {
                                             auto list = m.notes.peek();
                                             for (Note& x : list)
                                                 if (x.id == id) x.starred = v;
                                             m.notes.set(std::move(list));
                                         }}),
                           });
                           r.key = n.id;
                           rows.push_back(std::move(r));
                       }
                   });
                   return rows;
               })}),
        text({.id = "count", .text = derive([&m] { return std::format("{} notes", m.notes.get().size()); }), .style = "caption"}),
        button({.id = "delete-all", .text = "Delete all", .variant = ButtonVariant::danger, .verb = "notes.delete_all"}),
    });
}

[[nodiscard]] inline omui::AppSpec app(Model& m) {
    return {
        .id = "org.openmui.examples.notes",
        .title = "Notes",
        .size = {420, 420},
        .view = [&m] { return view(m); },
        .verbs = {{{"notes.delete_all", "Delete every note", omui::Effect::irreversible, "delete", {}},
                   [&m](const omui::Value&) -> omui::VerbResult {
                       const auto n = static_cast<std::int64_t>(m.notes.peek().size());
                       m.notes.set({});
                       return omui::Value{}.set("deleted", n);
                   }}},
    };
}

}  // namespace notes
