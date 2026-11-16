// omui's profiles (D-207): what an application and an extension package keep beyond the general rules, over the
// tool kernel's project model (mlibs.project). `omui doctor` runs them with the general and workspace rules.
module omui.cli;

import std;
import mlibs.project;

namespace omui::cli {

namespace {

bool has_file(const mlibs::project::Package& package, std::string_view prefix, std::string_view suffix) {
    return std::ranges::any_of(package.files, [&](const std::string& file) { return file.starts_with(prefix) && file.ends_with(suffix); });
}

std::string where(const mlibs::project::Package& package) { return package.relative.empty() ? std::string("mcpp.toml") : package.relative + "/mcpp.toml"; }

}  // namespace

std::vector<mlibs::project::Profile> profiles() {
    using mlibs::project::Finding, mlibs::project::Package, mlibs::project::Project;
    mlibs::project::Profile app{"app", "an omui application: who it is, how it starts, and a test that runs it without a window", {}};
    app.rules.push_back({"app.identity", "an application says its id and title", [](const Project&, const Package& package, std::vector<Finding>& out) {
        for (const std::string_view key : {"id", "title"})
            if (!package.metadata.contains(std::string(key)))
                out.push_back({"app.identity", where(package), std::format("[package.metadata.openmui] has no `{}`", key),
                               key == "id" ? "id = \"org.example.app\": the platforms' identity (a package's, an installer's)" : "title = \"…\": what a window and a launcher show"});
    }});
    app.rules.push_back({"app.entry", "an application has a program, or a script the player runs", [](const Project&, const Package& package, std::vector<Finding>& out) {
        const bool script = package.metadata.contains("scripts") || has_file(package, "", "omui.config.ts");
        const bool program = has_file(package, "src/", "main.cpp") || std::ranges::contains(package.dependencies, std::string("omui.entry.player"));
        if (!script && !program)
            out.push_back({"app.entry", where(package), "no program (src/main.cpp, or the player) and no script to run",
                           "omui new makes one: an app-cpp's src/main.cpp, or an app-ts's app/main.ts with the player"});
    }});
    app.rules.push_back({"app.headless-test", "an application has a test that runs it without a window", [](const Project&, const Package& package, std::vector<Finding>& out) {
        if (!has_file(package, "tests/", ".cpp"))
            out.push_back({"app.headless-test", where(package), "no test in tests/",
                           "a test that drives it headless, as an agent does (omui.host.headless; the templates have one)"});
    }});
    mlibs::project::Profile extension{"extension", "an omui extension package: what it declares, as data the build reads", {}};
    extension.rules.push_back({"extension.declared", "an extension declares what it adds", [](const Project&, const Package& package, std::vector<Finding>& out) {
        if (!has_file(package, "contract/", ".contract.xdef.ts") && !has_file(package, "native/", ".native.xdef.ts") && !has_file(package, "themes/", ".theme.xdef.ts"))
            out.push_back({"extension.declared", where(package), "no contract/*.contract.xdef.ts, native/*.native.xdef.ts or themes/*.theme.xdef.ts",
                           "an extension is its declarations: omui add component|native|theme NAME makes one"});
    }});
    return {mlibs::project::general(), mlibs::project::workspace(), std::move(app), std::move(extension)};
}

}  // namespace omui::cli
