// mlibs.project: a project read from its manifests; the general rules and the workspace's, each finding with its
// place and its fix; a profile's rules only over the packages that say it; the repository's examples keep the
// general rules.
import std;
import mcpplibs.mtest;
import mlibs.project;

using namespace mcpplibs::mtest;
namespace project = mlibs::project;

namespace {

void write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::filesystem::path repository() { return (std::filesystem::path(std::source_location::current().file_name()).parent_path() / ".." / ".." / ".." / "..").lexically_normal(); }

bool has(const std::vector<project::Finding>& findings, std::string_view rule, std::string_view where) {
    return std::ranges::any_of(findings, [&](const auto& finding) { return finding.rule == rule && finding.where == where; });
}

}  // namespace

UTest cases{"mlibs.project", [] {
    "a workspace read: its members, their names, profiles and dependencies"_test = [] {
        const auto root = temp_dir() / "space";
        write(root / "mcpp.toml", "[workspace]\nmembers = [\"libs/*\", \"app\"]\n");
        write(root / "libs" / "core" / "mcpp.toml", "[package]\nnamespace = \"demo\"\nname = \"core\"\n[dependencies]\nmcpplibs.os = \"0.4.4\"\n");
        write(root / "app" / "mcpp.toml",
              "[package]\nnamespace = \"demo\"\nname = \"app\"\n[package.metadata.openmui]\nprofile = \"app\"\n[dependencies]\ndemo.core = { path = \"../libs/core\" }\n");
        const auto read = project::read(root);
        expect(read.has_value()) << (read ? "" : read.error());
        if (!read) return;
        expect(read->workspace && read->members == std::vector<std::string>{"libs/core", "app"});
        const auto& app = read->packages.back();
        expect(app.name == "demo.app" && app.profile == "app" && app.path_dependencies == std::vector<std::string>{"libs/core"});
        expect(read->packages.front().dependencies == std::vector<std::string>{"mcpplibs.os"});
    };

    "the general rules: one project file, the name its path and its module, tests with mtest, data named as data"_test = [] {
        const auto root = temp_dir() / "bad";
        write(root / "mcpp.toml", "[workspace]\nmembers = [\"lib\"]\n");
        write(root / "lib" / "mcpp.toml", "[package]\nnamespace = \"demo\"\nname = \"other\"\n");
        write(root / "lib" / "CMakeLists.txt", "project(x)\n");
        write(root / "lib" / "src" / "lib.cppm", "export module demo.lib;\n");
        write(root / "lib" / "src" / "lib_test.cpp", "int main() {}\n");
        write(root / "lib" / "tests" / "g_test.cpp", "#include <gtest/gtest.h>\n");
        write(root / "lib" / "brand.ts", "import { defineTheme } from \"omui/define\";\nexport default defineTheme({});\n");
        write(root / "stray" / "mcpp.toml", "[package]\nname = \"stray\"\n");
        const auto read = project::read(root);
        const std::array profiles{project::general(), project::workspace()};
        const auto findings = project::check(*read, profiles);
        expect(has(findings, "general.manifest", "lib/CMakeLists.txt"));
        expect(has(findings, "general.path-name", "lib/mcpp.toml"));
        expect(has(findings, "general.module-name", "lib/mcpp.toml"));
        expect(has(findings, "general.tests", "lib/src/lib_test.cpp") && has(findings, "general.tests", "lib/tests/g_test.cpp"));
        expect(has(findings, "general.data-names", "lib/brand.ts"));
        expect(has(findings, "workspace.members", "stray/mcpp.toml"));
        expect(std::ranges::all_of(findings, [](const auto& finding) { return !finding.fix.empty(); })) << "every finding says what to do";
    };

    "a profile's rules look only at the packages that say it"_test = [] {
        const auto root = temp_dir() / "profiled";
        write(root / "mcpp.toml", "[workspace]\nmembers = [\"a\", \"b\"]\n");
        write(root / "a" / "mcpp.toml", "[package]\nname = \"a\"\n[package.metadata.openmui]\nprofile = \"app\"\n");
        write(root / "b" / "mcpp.toml", "[package]\nname = \"b\"\n");
        const project::Profile app{"app", "test", {{"app.seen", "test", [](const project::Project&, const project::Package& package, std::vector<project::Finding>& out) {
                                                       out.push_back({"app.seen", package.relative, "seen", "-"});
                                                   }}}};
        const auto findings = project::check(*project::read(root), std::array{app});
        expect(findings.size() == 1 && findings.front().where == "a");
    };

    "the repository's examples and showcases keep the general rules"_test = [] {
        std::error_code error;
        std::size_t examples = 0;
        std::vector<std::filesystem::path> corpus;
        for (const auto* place : {"examples", "showcase"})
            for (const auto& entry : std::filesystem::directory_iterator(repository() / place, error)) corpus.push_back(entry.path());
        for (const auto& entry : corpus | std::views::transform([](const auto& path) { return std::filesystem::directory_entry(path); })) {
            if (!std::filesystem::exists(entry.path() / "mcpp.toml")) continue;
            ++examples;
            const auto read = project::read(entry.path());
            expect(read.has_value()) << (read ? "" : read.error());
            if (!read) continue;
            for (const auto& finding : project::check(*read, std::array{project::general()}))
                expect(false) << std::format("{}: {}: {} — {}", entry.path().filename().string(), finding.where, finding.text, finding.rule);
        }
        expect(examples >= 18);
    };
}};
