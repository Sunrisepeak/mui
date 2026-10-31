// A package's data files became partitions at build time: each holds its
// file's text byte for byte, and the tag's table lists them by name.
import std;
import acme.data;
import openmui.testing;

using namespace omui::testing;

namespace {

std::string file(std::string_view name) {
    const auto path = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / name;
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"plugins.embed"};

    s.test("each file a partition holding its text, `-` and `.` in a stem as `_`", [] {
        check(acme::data::first_note == file("data/first.txt"));
        check(acme::data::second_part_two_note == file("data/second-part.two.txt"), "a dash, a dot and UTF-8");
    });

    s.test("the tag's table: every file of the suffix by name, no other", [] {
        std::vector<std::string> names;
        for (const auto& [name, text] : acme::data::note_files) {
            names.emplace_back(name);
            check(text == file("data/" + std::string(name)), std::string(name));
        }
        check(names == std::vector<std::string>{"first.txt", "second-part.two.txt"}, "ignored.md is not a note");
    });

    s.test("files left out: no partition a file, and an empty table the same code reads", [] {
        check(acme::data::left_files.empty(), "nothing embedded");
        std::size_t n = 0;
        for (const auto& [name, text] : acme::data::left_files) n += name.size() + text.size();
        check(n == 0);
    });

    s.test("themes() and behaviors(): embed() for their directories", [] {
        check(acme::data::dusk_theme == file("themes/dusk.theme.omui.toml"));
        check(acme::data::pan_zoom_behavior == file("behaviors/pan-zoom.behavior.omui.toml"));
    });

    return s.run(argc, argv);
}
