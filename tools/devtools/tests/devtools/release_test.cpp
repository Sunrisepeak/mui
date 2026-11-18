// omui-devtools release (RC3.1): the templates' closure is omui's SDK and what it reaches, never
// the tools or the works; a descriptor's place in the index (mcpp publish writes it); a template's published versions point at a checkout only where they name a
// member.
import std;
import mcpplibs.mtest;
import omui.devtools.release;

using namespace mcpplibs::mtest;
namespace release = omui::devtools::release;

namespace {

std::filesystem::path root() {
    return (std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() /
            ".." / ".." / "..")
        .lexically_normal();
}

}  // namespace

static UTest cases{
    "devtools.release", [] {
        "the templates reach the SDK, its build rules and what they reach; no tool, showcase or example"_test =
            [] {
                const auto made = release::closure(root());
                expect(made.has_value()) << (made ? "" : made.error());
                if (!made)
                    return;
                const auto has = [&](std::string_view name) {
                    return std::ranges::any_of(made->packages,
                                               [&](const release::Package& package) {
                                                   return package.name == name;
                                               });
                };
                expect(has("omui.sdk") && has("omui.build") && has("omui.core") &&
                       has("mlibs.math"))
                    << "the SDK and its base";
                expect(std::ranges::contains(made->roots, std::string("omui.sdk")));
                expect(!std::ranges::any_of(made->packages, [](const release::Package& package) {
                    return package.path.starts_with("tools/") ||
                           package.path.starts_with("showcase/") ||
                           package.path.starts_with("examples/");
                }));
                expect(std::ranges::contains(made->external, std::string("nlohmann.json@3.12.0")))
                    << "the native template's own";
            };

        "a descriptor's place in mcpp-index: by the full name's initial (the descriptor is mcpp publish's)"_test = [] {
            const release::Package package{.name = "omui.core", .path = "modules/projects/omui/core", .version = "0.1.0"};
            expect(release::descriptor_path(package) == "pkgs/o/omui.core.lua");
        };

        "a template's versions point at the checkout where they name a member, and nowhere else"_test =
            [] {
                const std::map<std::string, std::string> members{{"omui.theme",
                                                                  "modules/projects/omui/theme"},
                                                                 {"omui.build", "mcpp/build"}};
                const std::string manifest =
                    "[package]\nversion = \"0.1.0\"\n\n[dependencies]\nomui.theme = \"0.1.0\"  # "
                    "the default\nnlohmann.json = \"3.12.0\"\n\n"
                    "[build-dependencies]\nomui.build = { version = \"0.1.0\", host-module = true "
                    "}\n";
                const auto pointed = release::pointed_at(manifest, members, "/src");
                expect(pointed.contains(
                    "omui.theme = { path = \"/src/modules/projects/omui/theme\" }  # the default"))
                    << pointed;
                expect(pointed.contains(
                    "omui.build = { path = \"/src/mcpp/build\", host-module = true }"))
                    << pointed;
                expect(pointed.contains("nlohmann.json = \"3.12.0\"") &&
                       pointed.contains("[package]\nversion = \"0.1.0\""))
                    << pointed;
            };

        "an index without a dependency's version names it missing"_test = [] {
            const auto index = temp_dir() / "index";
            std::filesystem::create_directories(index / "pkgs" / "n");
            std::ofstream(index / "pkgs" / "n" / "nlohmann.json.lua")
                << "package = {\n    namespace = \"nlohmann\",\n    name = \"json\",\n    xpm = { "
                   "linux = { [\"3.12.0\"] = {} } },\n}\n";
            const std::vector<std::string> wanted{"nlohmann.json@3.12.0", "nlohmann.json@9.9.9",
                                                  "mcpplibs.os@0.4.4"};
            const auto missing = release::missing_from(index, wanted);
            expect(missing == std::vector<std::string>{"nlohmann.json@9.9.9", "mcpplibs.os@0.4.4"});
        };

    }};
