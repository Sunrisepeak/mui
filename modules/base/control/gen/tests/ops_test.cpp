// mlibs.control.gen: every operation catalog of the repository reads clean and its committed outputs are what
// it makes now; a position's form, a catalog's tasks and examples, and a malformed catalog line by line.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import mlibs.control.gen;

using namespace mcpplibs::mtest;
namespace gen = mlibs::control::gen;

namespace {

// The repository: the nearest directory above this file with the workspace's `.xlings.json` and `modules/`.
std::filesystem::path repository() {
    auto at = std::filesystem::path(std::source_location::current().file_name()).parent_path();
    while (at.has_parent_path() && at != at.parent_path() && !(std::filesystem::exists(at / ".xlings.json") && std::filesystem::exists(at / "modules")))
        at = at.parent_path();
    return at;
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

UTest cases{"mlibs.control.gen", [] {
    "every operation catalog reads clean and its committed outputs are current (§10.3)"_test = [] {
        const auto repo = repository();
        std::size_t catalogs = 0;
        std::vector<std::filesystem::path> files;
        for (const auto* place : {"modules", "showcase"})
            for (const auto& entry : std::filesystem::recursive_directory_iterator(repo / place)) files.push_back(entry.path());
        for (const auto& path : files) {
            const std::filesystem::directory_entry entry(path);
            const auto name = mcpplibs::os::fs::utf8(entry.path().filename());
            if (!entry.is_regular_file() || !name.ends_with(".ops.omui.ts") || mcpplibs::os::fs::generic_utf8(entry.path()).contains("/target/"))
                continue;
            ++catalogs;
            auto catalog = gen::read_catalog_file(entry.path());
            expect(catalog.has_value()) << (catalog ? std::string() : catalog.error().front());
            if (!catalog) continue;
            // Its outputs where the catalog says, beside it.
            const auto out = entry.path().parent_path() / catalog->generated;
            const auto stale = [&](const std::filesystem::path& file, const std::string& now) {
                expect(read(file) == now) << std::format("{} is out of date: run ops-gen {} --out {}", mcpplibs::os::fs::generic_utf8(file), mcpplibs::os::fs::generic_utf8(entry.path()), mcpplibs::os::fs::generic_utf8(out));
            };
            if (!catalog->module.empty()) stale(out / (catalog->partition + ".cppm"), gen::emit_catalog_cpp(*catalog, name));
            stale(out / (catalog->product + ".ops.json"), gen::emit_catalog_json(*catalog));
            stale(out / (catalog->product + ".ops.md"), gen::emit_catalog_markdown(*catalog));
        }
        expect(catalogs >= 1) << "hanim's catalog at least";
    };

    "a position says the form it is written in: in the listing as x-space, an unknown form refused"_test = [] {
        const auto text = [](std::string_view space) {
            return std::format(R"({{"ops": {{
  "product": "x",
  "module": "x.control",
  "namespace": "x::control",
  "operations": [
    {{"name": "x.look", "effect": "read", "summary": "looks", "result": "json", "params": [
      {{"name": "at", "type": "json", "doc": "[x, y]", "space": "{}"}},
      {{"name": "depth", "type": "integer"}}]}}
  ]
}}}})", space);
        };
        const auto catalog = gen::read_catalog(text("lattice-2d"), "x.ops.omui.json");
        expect(fatal(catalog.has_value()));
        const std::string listing = gen::emit_catalog_json(*catalog);
        expect(listing.contains(R"("at": {"description": "[x, y]", "x-space": "lattice-2d"})")) << listing;
        expect(listing.contains(R"("depth": {"type": "integer"})")) << "a parameter that is no position says none";
        expect(gen::emit_catalog_markdown(*catalog).contains("`at`: json in lattice-2d"));
        const auto unknown = gen::read_catalog(text("somewhere"), "x.ops.omui.json");
        expect(!unknown && std::ranges::any_of(unknown.error(), [](const std::string& problem) { return problem.contains("space `somewhere` (plane, lattice-2d)"); }));
    };

    "a catalog's tasks and examples are requests as they are sent: checked against its operations, written to the table, the listing and the reference (D-186)"_test = [] {
        const auto catalog = gen::read_catalog(R"({"ops": {
  "product": "x", "module": "x.control", "namespace": "x::control", "doc": "A thing to look at and move.",
  "operations": [
    {"name": "x.look", "effect": "read", "summary": "looks", "result": "json", "params": [{"name": "at", "type": "json"}],
     "examples": [{"at": [1, 2]}]},
    {"name": "x.move", "effect": "reversible", "summary": "moves", "undo": "x.move back", "then": [{"why": "see where it is", "request": {"op": "x.look", "at": [0, 0]}}], "params": [
      {"name": "by", "type": "number"}, {"name": "note", "type": "string", "optional": true}]},
    {"name": "x.rest", "effect": "read", "summary": "rests", "keywords": ["sleep", "idle"]}
  ],
  "tasks": [
    {"name": "look-then-move", "summary": "Look at a place, move, look again", "steps": [
      {"op": "x.look", "at": [1, 2]}, {"op": "x.move", "by": 0.5, "note": "a \"small\" step"}, {"op": "x.rest"}]}
  ]
}})", "x.ops.omui.json");
        expect(fatal(catalog.has_value())) << (catalog ? std::string() : catalog.error().front());
        expect(catalog->operations[0].examples == std::vector<std::string>{R"({"op":"x.look","at":[1,2]})"});
        expect(catalog->operations[1].examples == std::vector<std::string>{R"({"op":"x.move","by":0.5,"note":"a \"small\" step"})"})
            << "an operation that shows none of its own shows a task's step: written once";
        expect(catalog->operations[2].examples == std::vector<std::string>{R"({"op":"x.rest"})"});
        const std::string table = gen::emit_catalog_cpp(*catalog, "x.ops.omui.json");
        for (const std::string_view wanted : {"inline constexpr std::array<std::string_view, 1> examples_x_look{{", "\"{\\\"op\\\":\\\"x.look\\\",\\\"at\\\":[1,2]}\",",
                                              "false, examples_x_move, ", "inline constexpr std::array<std::string_view, 3> steps_look_then_move{{",
                                              "inline constexpr std::array<mlibs::control::Task, 1> guide_tasks{{",
                                              "{\"look-then-move\", \"Look at a place, move, look again\", steps_look_then_move},",
                                              "inline constexpr mlibs::control::Guide guide{\"A thing to look at and move.\", guide_tasks};"})
            expect(table.contains(wanted)) << wanted << "\n" << table;
        expect(table.contains("examples_x_rest, \"sleep idle\"},")) << "words an intent may use for it: what find reads";
        expect(table.contains("inline constexpr std::array<mlibs::control::Way, 1> then_x_move{{") && table.contains("examples_x_move, \"\", then_x_move},")) << table;
        const std::string listing = gen::emit_catalog_json(*catalog);
        expect(listing.contains(R"("then": [{"why": "see where it is", "request": {"op":"x.look","at":[0,0]}}],)")) << listing;
        expect(listing.contains(R"("keywords": ["sleep", "idle"],)"));
        expect(listing.contains(R"("examples": [{"op":"x.look","at":[1,2]}],)") && listing.contains(R"("tasks": [)") && listing.contains(R"({"name": "look-then-move", "summary": "Look at a place, move, look again", "steps": [)"))
            << listing;
        expect(gen::emit_catalog_markdown(*catalog).contains("## Tasks to begin with") && gen::emit_catalog_markdown(*catalog).contains("**look-then-move** — Look at a place, move, look again"));

        // What a catalog shows is accepted as shown: a step that is no operation, an argument that is no parameter, one it needs left out.
        const auto broken = gen::read_catalog(R"({"ops": {
  "product": "x",
  "operations": [
    {"name": "x.look", "effect": "read", "summary": "looks", "params": [{"name": "at", "type": "json"}], "examples": [{"near": 1}]}
  ],
  "tasks": [
    {"name": "Look Around", "summary": "", "steps": [{"op": "x.fly"}]},
    {"name": "nothing", "summary": "does nothing", "steps": []}
  ]
}})", "x.ops.omui.json");
        expect(!broken);
        if (broken) return;
        const auto says = [&](std::string_view text) { return std::ranges::any_of(broken.error(), [&](const std::string& problem) { return problem.contains(text); }); };
        expect(says("an example: `x.look` has no parameter `near`") && says("an example: `x.look` needs `at`"));
        expect(says("a task's name is lowercase words joined by `-`") && says("`x.fly` is not one of its operations") && says("task `nothing` has the requests that do it"));
    };

    "a malformed catalog is reported line by line"_test = [] {
        const auto broken = gen::read_catalog(R"({"ops": {
  "product": "x",
  "module": "x.control",
  "namespace": "x::control",
  "operations": [
    {"name": "Bad Name", "effect": "maybe", "summary": "something"},
    {"name": "x.change", "effect": "reversible", "summary": "changes"},
    {"name": "x.run", "effect": "read", "delegated": true, "summary": "runs what it names"},
    {"name": "x.peek", "effect": "read", "undoable": true, "summary": "reads"}
  ]
}})", "x.ops.omui.json");
        expect(!broken) << "refused";
        if (broken) return;
        const auto& problems = broken.error();
        const auto says = [&](std::string_view text) { return std::ranges::any_of(problems, [&](const std::string& p) { return p.find(text) != std::string::npos; }); };
        expect(says("x.ops.omui.json:6") && says("dotted lowercase")) << "the bad name at its line";
        expect(says("not read, reversible or irreversible")) << "the bad effect";
        expect(says("say how it is undone")) << "a change without its undo";
        expect(says("the most that can be")) << "a delegated operation's bound is above read";
        expect(says("only a reversible operation is in the history")) << "a read is not undoable";
    };
}};
