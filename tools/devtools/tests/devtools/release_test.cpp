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

        "release try's stand-in: the template package as its own manifest, a payload's place, the home's default index"_test = [] {
            constexpr std::string_view published = "package = {\n    spec = \"1\",\n    namespace = \"omui\",\n    name = \"sdk\",\n"
                                                   "    xpm = {\n        linux = {},\n    },\n\n    mcpp = {\n        schema = \"0.1\",\n"
                                                   "        deps = {\n            [\"omui.core\"] = \"0.1.0\",\n        },\n        manifest = \"mcpp.toml\",\n    },\n}\n";
            const std::string payload = release::as_own_manifest(published);
            expect(payload.contains("    mcpp = \"*/mcpp.toml\",") && !payload.contains("schema") && payload.contains("xpm = {") &&
                   payload.ends_with("}\n"))
                << payload;
            expect(release::as_own_manifest(payload) == payload) << "already the manifest's: kept";
            expect(release::identity_of(published) == std::pair<std::string, std::string>{"omui", "sdk"});
            expect(release::identity_of("package = {\n    namespace = \"omui.entry\",\n    name = \"player\",\n}") ==
                   std::pair<std::string, std::string>{"omui.entry", "player"});
            expect(release::store_path("omui.entry", "player", "0.1.0") == std::filesystem::path("xpkgs/omui.entry-x-player/0.1.0"));
            expect(release::store_path("", "mpal", "0.1.0") == std::filesystem::path("xpkgs/mcpplibs-x-mpal/0.1.0")) << "no namespace: mcpp's default one";
            const std::string config = "[xlings]\nbinary = \"bundled\"\n\n[index.repos.\"mcpplibs\"]\nurl      = \"https://github.com/mcpplibs/mcpp-index.git\"\n"
                                       "artifact = { GLOBAL = \"x\", CN = \"y\" }\n# a comment\n\n[cache]\nsearch_ttl_seconds = 3600\n";
            const std::string pointed = release::with_default_index(config, "/work/index");
            expect(pointed.contains("[index.repos.\"mcpplibs\"]\nurl    = \"/work/index\"\nsource = \"git\"\n") && !pointed.contains("artifact") &&
                   pointed.contains("[xlings]\nbinary") && pointed.contains("[cache]\nsearch_ttl_seconds = 3600"))
                << pointed;
            expect(release::with_default_index("[xlings]\n", "/work/index").empty()) << "no default index table: nothing";
        };

        "release try pins what publish leaves as paths (mcpp#962): feature-deps, a key with the package's own name"_test = [] {
            const std::map<std::string, std::string> versions{{"modules/projects/hanim/core", "0.1.0"}, {"modules/base/agent", "0.2.0"},
                                                              {"modules/projects/hanim", "0.1.0"}};
            const auto pinned = release::pinned("[package]\nname = \"hanim\"\n\n[dependencies]\nhanim = { core = { path = \"core\" } }\n\n"
                                                "[dev-dependencies]\nmlibs = { json = { path = \"../../base/json\" } }\n\n"
                                                "[feature-deps.agent]\nmlibs = { agent = { path = \"../../base/agent\" } }\n",
                                                "modules/projects/hanim", versions);
            expect(pinned.has_value()) << (pinned ? std::string() : pinned.error());
            if (pinned) {
                expect(pinned->contains("core = '0.1.0'") || pinned->contains("core = \"0.1.0\"")) << *pinned;
                expect(pinned->contains("agent = '0.2.0'") || pinned->contains("agent = \"0.2.0\"")) << *pinned;
                expect(pinned->contains("../../base/json")) << "dev-dependencies left as they are";
            }
            const auto featured = release::pinned("[feature-deps.hanim]\nhanim = { default-features = false, features = [\"engine\"], path = \"../../hanim\" }\n",
                                                  "modules/projects/omui/view", {{"modules/projects/hanim", "0.1.0"}});
            expect(featured && featured->contains("version") && featured->contains("0.1.0") && featured->contains("engine") && !featured->contains("path"))
                << (featured ? *featured : featured.error());
            constexpr std::string_view clean = "[dependencies]\nomui.core = \"0.1.0\"\n";
            expect(release::pinned(clean, "x", {}) == std::expected<std::string, std::string>(std::string(clean))) << "nothing to pin: unchanged";
            expect(!release::pinned("[dependencies]\nx = { path = \"../nowhere\" }\n", "a/b", {})) << "a path that is no member";
        };

        "release try reads a new project's manifest as strict TOML does (mcpp#964: a header after dotted keys)"_test = [] {
            expect(release::toml_problem("[dependencies]\nomui.entry.player = \"0.1.0\"\nomui.sdk = \"0.1.0\"\n").empty());
            const std::string problem = release::toml_problem("[dependencies]\nomui.entry.player = \"0.1.0\"\n\n[dependencies.omui]\nsdk = \"0.1.0\"\n");
            expect(problem.starts_with("4:")) << problem;
        };

        "a vendored package's xlings dependencies are what its ELF files name (red 142)"_test = [] {
            namespace fs = std::filesystem;
            const fs::path package = fs::temp_directory_path() / "omui-release-wants";
            fs::remove_all(package);
            fs::create_directories(package / "bin");
            fs::create_directories(package / "lib");
            // A program: its symbol versions and the libraries it loads by name; the Vulkan loader is carried beside it.
            std::ofstream(package / "bin/app", std::ios::binary) << std::string("\x7f" "ELF", 4) << '\0' << "GLIBC_2.2.5" << '\0'
                                                                  << "GLIBC_2.38" << '\0' << "GLIBCXX_3.4.35" << '\0' << "libgcc_s.so.1" << '\0'
                                                                  << "libX11.so.6" << '\0' << "libvulkan.so.1";
            std::ofstream(package / "lib/libvulkan.so.1", std::ios::binary) << std::string("\x7f" "ELF", 4);
            std::ofstream(package / "README.md") << "libXi.so.6 GLIBC_2.99: not an ELF file, not read";
            const release::Wants wants = release::wants_of(package);
            expect(wants.glibc == "2.38") << wants.glibc;
            const std::vector<std::string> expected{"xim:gcc-runtime@>=16.1", "xim:libX11@>=1.8"};
            expect(wants.packages == expected) << "GLIBCXX_3.4.35 is GCC 16's; the carried loader is not wanted";
            expect(wants.graphics) << "X11: a window";
            fs::remove_all(package);

            const std::string lua = release::xlings_descriptor({.name = "omui",
                                                                .description = "the tool",
                                                                .about = "a\nb",
                                                                .programs = {"omui", "omui-player"},
                                                                .categories = {"gui"},
                                                                .keywords = {"omui"},
                                                                .versions = {{.version = "0.1.0", .url = "https://x/0.1.0.tar.gz", .sha256 = "aa"},
                                                                             {.version = "0.2.0", .url = "https://x/0.2.0.tar.gz", .sha256 = "bb"}},
                                                                .wants = wants,
                                                                .starts = {"xim:mcpp@>=2026.11.18.1"}});
            expect(lua.starts_with("-- a\n-- b\npackage = {")) << lua;
            expect(lua.contains("runtime = { \"xim:glibc@>=2.38\", \"xim:gcc-runtime@>=16.1\", \"xim:libX11@>=1.8\", \"xim:graphics@>=0.1.5\", "
                                "\"xim:mcpp@>=2026.11.18.1\" }"))
                << "glibc first: the one loader; then the libraries, the graphics stack, the programs it starts";
            expect(lua.contains("[\"latest\"] = { ref = \"0.2.0\" }")) << "the last version is latest";
            expect(lua.contains("xvm.add(\"omui-player\", { bindir = bindir, envs = envs })") && lua.contains("graphics.consumer_envs()"))
                << "a window's programs get the drivers' variables";
            expect(!lua.contains("elfpatch")) << "xlings patches it from the loader among its dependencies";
        };

    }};
