// Structured design operations as source edits (omui.lang.ts:edit, W14): the
// builder call at an element's origin found past strings, templates, comments
// and regular expressions; a property set, added or removed, a child inserted,
// a call removed — each result byte for byte, laid out as its neighbours are.
import std;
import mcpplibs.mtest;
import omui.lang.ts;

using namespace mcpplibs::mtest;
namespace ts = omui::lang::ts;

namespace {

constexpr std::string_view page = R"ts(import { column, row, text, button, mount } from "openmui";
const greet = (who: string) => `Hello, ${who.replace(/[)}]/g, "")}!`;  // a template, a regex: "(" is not code
mount("main", column({ gap: 8, padding: 12 }, [
  text({ id: "title", text: "Settings" }),
  row({}, [button({ id: "save", text: "Save" }), button({ id: "cancel", text: "Cancel" })]),
  text({
    id: "note",
    text: greet("you"),
  }),
]));
)ts";

}  // namespace

UTest cases{"lang.ts.edit", [] {
    "the call at an origin: at its callee or its `(`, with its properties and its children"_test = [] {
        auto at_callee = ts::builder_call_at(page, 4, 3);
        expect(at_callee.has_value()) << (at_callee ? "" : at_callee.error());
        if (!at_callee) return;
        expect(eq(at_callee->callee, std::string("text")));
        expect(at_callee->properties.has_value() && !at_callee->children.has_value());
        expect(eq(std::string(page.substr(at_callee->start, at_callee->end - at_callee->start)), std::string(R"(text({ id: "title", text: "Settings" }))")));
        auto at_paren = ts::builder_call_at(page, 4, 7);
        expect(at_paren.has_value() && at_paren->start == at_callee->start);
        auto column = ts::builder_call_at(page, 3, 15);
        expect(column.has_value() && column->callee == "column" && column->properties && column->children);
        expect(!ts::builder_call_at(page, 2, 1).has_value()) << "`const` is not a call";
        expect(!ts::builder_call_at(page, 40, 1).has_value()) << "no such line";
    };

    "a property: its value replaced where it is written; added after the last, on its line or on a line of its own"_test = [] {
        auto replaced = ts::set_property(page, 4, 3, "text", "\"Preferences\"");
        expect(replaced.has_value() && replaced->contains(R"(text({ id: "title", text: "Preferences" }),)")) << (replaced ? *replaced : replaced.error());
        auto added = ts::set_property(page, 4, 3, "tone", "\"muted\"");
        expect(added.has_value() && added->contains(R"(text({ id: "title", text: "Settings", tone: "muted" }),)")) << (added ? *added : added.error());
        auto lines = ts::set_property(page, 6, 3, "tone", "\"muted\"");
        expect(lines.has_value() && lines->contains("    text: greet(\"you\"),\n    tone: \"muted\",\n  }),")) << (lines ? *lines : lines.error());
        auto empty = ts::set_property(page, 5, 3, "gap", "4");
        expect(empty.has_value() && empty->contains("row({ gap: 4 }, [")) << (empty ? *empty : empty.error());
        auto quoted = ts::set_property(page, 4, 3, "aria-label", "\"t\"");
        expect(quoted.has_value() && quoted->contains(R"("aria-label": "t")")) << (quoted ? *quoted : quoted.error());
        // Everything else byte for byte.
        expect(replaced && eq(replaced->size(), page.size() + std::string_view("Preferences").size() - std::string_view("Settings").size()));
    };

    "a property removed with its comma; a call with no properties gets its object"_test = [] {
        auto inline_removed = ts::remove_property(page, 4, 3, "id");
        expect(inline_removed.has_value() && inline_removed->contains(R"(text({ text: "Settings" }),)")) << (inline_removed ? *inline_removed : inline_removed.error());
        auto last_removed = ts::remove_property(page, 4, 3, "text");
        expect(last_removed.has_value() && last_removed->contains(R"(text({ id: "title" }),)")) << (last_removed ? *last_removed : last_removed.error());
        auto line_removed = ts::remove_property(page, 6, 3, "id");
        expect(line_removed.has_value() && line_removed->contains("  text({\n    text: greet(\"you\"),\n  }),")) << (line_removed ? *line_removed : line_removed.error());
        expect(!ts::remove_property(page, 4, 3, "width").has_value()) << "not written";
        constexpr std::string_view bare = "mount(\"main\", text());\n";
        auto given = ts::set_property(bare, 1, 15, "text", "\"Hi\"");
        expect(given.has_value() && *given == "mount(\"main\", text({ text: \"Hi\" }));\n") << (given ? *given : given.error());
    };

    "a child inserted: at the end or at an index, laid out as its siblings"_test = [] {
        auto at_end = ts::insert_child(page, 3, 15, "text({ text: \"Help\" })");
        expect(at_end.has_value() && at_end->contains("  }),\n  text({ text: \"Help\" }),\n]));")) << (at_end ? *at_end : at_end.error());
        auto first = ts::insert_child(page, 3, 15, "text({ text: \"Top\" })", 0);
        expect(first.has_value() && first->contains("[\n  text({ text: \"Top\" }),\n  text({ id: \"title\"")) << (first ? *first : first.error());
        auto in_row = ts::insert_child(page, 5, 3, "button({ text: \"Apply\" })", 1);
        expect(in_row.has_value() && in_row->contains(R"([button({ id: "save", text: "Save" }), button({ text: "Apply" }), button({ id: "cancel")"))
            << (in_row ? *in_row : in_row.error());
        auto none_yet = ts::insert_child(page, 4, 3, "text({ text: \"x\" })");
        expect(none_yet.has_value() && none_yet->contains(R"(text({ id: "title", text: "Settings" }, [text({ text: "x" })]),)"))
            << (none_yet ? *none_yet : none_yet.error());
    };

    "a call removed from its list: its line when it had one, its comma otherwise; outside a list it is refused"_test = [] {
        auto line = ts::remove_call(page, 4, 3);
        expect(line.has_value() && line->contains("[\n  row({}, [")) << (line ? *line : line.error());
        auto inline_first = ts::remove_call(page, 5, 12);
        expect(inline_first.has_value() && inline_first->contains(R"(row({}, [button({ id: "cancel", text: "Cancel" })]),)"))
            << (inline_first ? *inline_first : inline_first.error());
        auto inline_last = ts::remove_call(page, 5, 50);
        expect(inline_last.has_value() && inline_last->contains(R"(row({}, [button({ id: "save", text: "Save" })]),)"))
            << (inline_last ? *inline_last : inline_last.error());
        auto last_of_lines = ts::remove_call(page, 6, 3);
        expect(last_of_lines.has_value() && last_of_lines->contains("text: \"Cancel\" })]),\n]));")) << (last_of_lines ? *last_of_lines : last_of_lines.error());
        expect(!ts::remove_call(page, 3, 15).has_value()) << "mount's argument is not a list's";
    };
}};
