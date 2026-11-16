// omui-devtools platforms: the user documents' platform pages are the table's (docs/release.platforms.xdef.json), in
// English and Chinese; a status outside the four is refused.
import std;
import mcpplibs.mtest;
import omui.devtools.platforms;

using namespace mcpplibs::mtest;
namespace platforms = omui::devtools::platforms;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / ".." / ".." / ".."; }

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

UTest cases{"devtools.platforms", [] {
    "the committed pages are the table's, in both languages"_test = [] {
        const std::string table = read(root() / "docs" / "release.platforms.xdef.json");
        for (const std::string_view language : {"en", "zh"}) {
            const auto written = platforms::page(table, language);
            expect(fatal(written.has_value())) << (written ? "" : written.error());
            expect(read(root() / "docs" / std::string(language) / "platforms.md") == *written)
                << std::format("docs/{}/platforms.md is not current: run omui-devtools platforms", language);
        }
    };

    "a cell's status is one of four, and what it rests on is said beside the table"_test = [] {
        const std::string table = R"({"platforms":[{"id":"a","name":"A","tier":1},{"id":"b","name":"B","tier":2}],
            "capability":[{"name":"Drawing","label":"stable","cells":{"a":{"status":"real","evidence":"a run"},"b":{"status":"unverified","needs":"a device"}}}]})";
        const auto written = platforms::page(table, "en");
        expect(fatal(written.has_value()));
        expect(written->contains("| Drawing | stable | real | unverified |")) << *written;
        expect(written->contains("- **Drawing**, B: missing: a device")) << *written;
        const auto chinese = platforms::page(table, "zh");
        expect(chinese && chinese->contains("| Drawing | 稳定 | 实机 | 未验证 |")) << (chinese ? *chinese : chinese.error());
        const auto refused = platforms::page(R"({"platforms":[{"id":"a"}],"capability":[{"name":"X","cells":{"a":{"status":"passed"}}}]})", "en");
        expect(!refused && refused.error().contains("passed")) << "a status outside the four";
    };
}};
