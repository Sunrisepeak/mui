// omui-devtools docs: the user documents' maturity and experimental pages are the manifests' labels and the experimental
// READMEs' sections, in English and Chinese (plan §6.3); a README's section is read item by item, to its next heading.
import std;
import mcpplibs.mtest;
import omui.devtools.docs;

using namespace mcpplibs::mtest;
namespace docs = omui::devtools::docs;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / ".." / ".." / ".."; }

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

UTest cases{"devtools.docs", [] {
    "the committed pages are the manifests' and the READMEs', in both languages"_test = [] {
        const auto inventory = docs::read(root());
        expect(fatal(inventory.has_value())) << (inventory ? "" : inventory.error());
        expect(inventory->packages.size() > 100u) << "every member with a label";
        expect(!inventory->experimental.empty()) << "gongfu, Glade and others are experimental in 0.1";
        for (const std::string_view language : {"en", "zh"}) {
            expect(read(root() / "docs" / std::string(language) / "maturity.md") == docs::maturity_page(*inventory, language))
                << std::format("docs/{}/maturity.md is not current: run omui-devtools docs", language);
            expect(read(root() / "docs" / std::string(language) / "experimental.md") == docs::experimental_page(*inventory, language))
                << std::format("docs/{}/experimental.md is not current: run omui-devtools docs", language);
        }
    };

    "a package's project: the directory under projects, mlibs below it, the top directory otherwise"_test = [] {
        expect(eq(docs::project_of("modules/projects/xengine/kernel/field"), std::string("xengine")));
        expect(eq(docs::project_of("modules/base/json"), std::string("mlibs")));
        expect(eq(docs::project_of("showcase/glade/headless"), std::string("showcase")));
    };

    "an Experimental section: its items in order, a continued line joined, ended by the next heading as high"_test = [] {
        const auto section = docs::section_of("# thing\n\nWhat it is.\n\n## Experimental (release 0.1)\n\nIt ships.\n\n- **Target**: the best.\n"
                                              "- **Gap**: far\n  from it.\n- **Graduation to preview**: a second consumer.\n\n## Use\n\n- **Not**: this.\n");
        expect(fatal(section.has_value()));
        expect(eq(section->title, std::string("thing")));
        expect(fatal(eq(section->items.size(), std::size_t{3})));
        expect(eq(section->items[1].second, std::string("far from it.")));
        expect(eq(section->items[2].first, std::string("Graduation to preview")));
        expect(!docs::section_of("# thing\n\n## Use\n").has_value()) << "no section, none";
        const docs::Inventory inventory{.packages = {{"modules/projects/thing", "thing", "experimental"}},
                                        .experimental = {{"modules/projects/thing/README.md", "thing", section->items, {"thing"}}}};
        const std::string english = docs::experimental_page(inventory, "en"), chinese = docs::experimental_page(inventory, "zh");
        expect(english.contains("- **Target**: the best.")) << english;
        expect(chinese.contains("- **目标**：the best.")) << chinese;
        expect(docs::maturity_page(inventory, "en").contains("| thing | 0 | 0 | 1 |")) << docs::maturity_page(inventory, "en");
    };
}};
