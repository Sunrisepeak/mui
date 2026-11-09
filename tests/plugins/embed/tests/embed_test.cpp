// A package's data files became partitions at build time: each holds its
// file's text byte for byte, and the tag's table lists them by name.
import std;
import acme.data;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

std::string file(std::string_view name) {
    const auto path = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / name;
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

UTest cases{"plugins.embed", [] {

    "each file a partition holding its text, `-` and `.` in a stem as `_`"_test = [] {
        expect(acme::data::first_note == file("data/first.txt"));
        expect(acme::data::second_part_two_note == file("data/second-part.two.txt")) << "a dash, a dot and UTF-8";
    };

    "the tag's table: every file of the suffix by name, no other"_test = [] {
        std::vector<std::string> names;
        for (const auto& [name, text] : acme::data::note_files) {
            names.emplace_back(name);
            expect(text == file("data/" + std::string(name))) << std::string(name);
        }
        expect(names == std::vector<std::string>{"first.txt", "second-part.two.txt"}) << "ignored.md is not a note";
    };

    "files left out: no partition a file, and an empty table the same code reads"_test = [] {
        expect(acme::data::left_files.empty()) << "nothing embedded";
        std::size_t n = 0;
        for (const auto& [name, text] : acme::data::left_files) n += name.size() + text.size();
        expect(n == 0);
    };

    "themes() and behaviors(): embed() for their directories"_test = [] {
        expect(acme::data::dusk_theme == file("themes/dusk.theme.omui.toml"));
        expect(acme::data::pan_zoom_behavior == file("behaviors/pan-zoom.behavior.omui.toml"));
    };
}};
