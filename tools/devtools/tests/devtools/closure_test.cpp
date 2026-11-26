// omui-devtools closure: each project's closure checklist read from the tree (plan §5.1, gate 3) — a README's definition,
// its feature table, its Known limitations and its exceptions read as they are written; the committed pages current.
import std;
import mcpplibs.mtest;
import omui.devtools.closure;

using namespace mcpplibs::mtest;
namespace closure = omui::devtools::closure;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / ".." / ".." / ".."; }

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

static UTest cases{"devtools.closure", [] {
    "the committed pages are the tree's checklist, in both languages; every project counted"_test = [] {
        const auto report = closure::check(root());
        expect(fatal(report.has_value())) << (report ? "" : report.error());
        expect(eq(report->projects.size(), std::size_t{7})) << "mlibs, mrender, hanim, omui, meng, gongfu, xtools";
        for (const auto& project : report->projects) {
            const auto spared = std::ranges::count(project.items, closure::Mark::not_required, &closure::Cell::mark);
            // An experimental project owes the four starred items only; the others owe all ten.
            expect(eq(static_cast<std::size_t>(spared), project.label == "experimental" ? closure::item_count - 4 : std::size_t{0})) << project.name;
            for (const auto& cell : project.items) expect(!cell.note.empty()) << std::format("{}: every cell says why", project.name);
        }
        for (const std::string_view language : {"en", "zh"})
            expect(read(root() / "docs" / std::string(language) / "closure.md") == closure::page(*report, language))
                << std::format("docs/{}/closure.md is not current: run omui-devtools closure", language);
    };

    "the definition: the first line under the title, one sentence"_test = [] {
        expect(eq(closure::definition("# thing\n\nA thing that does one job, e.g. this.\n\nMore.\n").value_or(""), std::string("A thing that does one job, e.g. this.")));
        expect(!closure::definition("# thing\n\nIt does this. And that.\n").has_value()) << "two sentences";
        expect(!closure::definition("# thing\n\n| a | b |\n").has_value()) << "a table, not a sentence";
    };

    "the feature table: each row a label and an evidence link"_test = [] {
        const auto owed = closure::features_owed("# t\n\n| Feature | Label | Evidence |\n|---|---|---|\n| one | stable | [test](a.cpp) |\n| two | preview | none |\n| three | | [x](b) |\n");
        expect(fatal(owed.has_value()));
        expect(fatal(eq(owed->size(), std::size_t{2})));
        expect(eq((*owed)[0], std::string("two (no evidence link)")));
        expect(eq((*owed)[1], std::string("three (no label)")));
        expect(!closure::features_owed("# t\n\n| Feature | Kind |\n|---|---|\n| one | x |\n").has_value()) << "no Label column, no table";
    };

    "Known limitations: each item names the level it is solved at; exceptions by number"_test = [] {
        const auto owed = closure::limitations_owed("# t\n\n## Known limitations\n\n- **No Web audio** (foundation): the device.\n- **Slow** sometimes.\n\n## Next\n\n- **x** (part): y\n");
        expect(fatal(owed.has_value()));
        expect(fatal(eq(owed->size(), std::size_t{1}))) << "the second item only; the next section not read";
        expect(owed->front().starts_with("**Slow**"));
        expect(!closure::limitations_owed("# t\n").has_value());
        const auto empty = closure::limitations_owed("# t\n\n## Known limitations\n\nNone worth saying.\n");
        expect(empty.has_value() && empty->size() == 1u) << "a section that lists nothing owes";
        const auto fenced = closure::limitations_owed("# t\n\n## Known limitations\n\n```sh\n# a comment\n```\n\n- **Slow** (part): x.\n- **Bare** item\n");
        expect(fenced.has_value() && fenced->size() == 1u) << "a `#` line inside a fence is code: the items after it are read";
        const auto exempt = closure::exceptions("# t\n\n## Closure exceptions\n\n- **7**: nothing runs per frame.\n- **x**: not a number\n- **99999999999999999999**: too long\n");
        expect(eq(exempt.size(), std::size_t{1})) << "a number too long is not read (nor thrown on)";
        expect(exempt.contains(7) && exempt.at(7) == "nothing runs per frame.");
    };
}};
