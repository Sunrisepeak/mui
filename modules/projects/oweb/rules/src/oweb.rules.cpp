module oweb.rules;

import std;
import mcpp;

using namespace mlibs::build;

namespace oweb::rules {

namespace {

// A path from UTF-8 text (the engine's, an option's): never through the process's code page.
std::filesystem::path path_of(std::string_view text) { return std::filesystem::path(std::u8string(text.begin(), text.end())); }

constexpr std::array<std::string_view, 4> made_from{"pages", "public", "theme", "layouts"};

}  // namespace

std::optional<edge> plan_site(const site_options& options) {
    const std::string command = tool("oweb", "oweb");
    if (command.empty()) {
        std::println(std::cerr, "oweb.rules: site() runs the command oweb, built for the build — the site's package names it under [build-dependencies]: oweb = {{ version = \"…\", tools = [\"oweb\"] }}");
        return std::nullopt;
    }
    // Whole addresses: an edge's command runs in the build's own directory, not the package's.
    const std::filesystem::path package = path_of(mcpp::manifest_dir());
    const auto from_package = [&](std::string_view stated) {
        const std::filesystem::path path = path_of(stated);
        return (path.is_absolute() ? path : package / path).lexically_normal();
    };
    const std::filesystem::path site = options.site.empty() ? package : from_package(options.site);
    const std::filesystem::path out = from_package(options.out);
    edge planned;
    planned.id = "oweb-site";
    planned.description = "the site";
    planned.command = {command, "site.build", "--site", generic_utf8(site), "--out", generic_utf8(out)};
    if (!options.engine.empty()) {
        planned.command.push_back("--engine");
        planned.command.push_back(generic_utf8(from_package(options.engine)));
    }
    // What the site is made from: a page added, changed or taken away builds it again.
    std::error_code error;
    for (const std::string_view stated : {"oweb.config.ts", "omui.config.ts"})
        if (const std::filesystem::path file = site / stated; std::filesystem::is_regular_file(file, error)) planned.inputs.push_back(generic_utf8(file));
    for (const std::string_view directory : made_from)
        for (std::string& file : files_under(site / directory)) planned.inputs.push_back(std::move(file));
    planned.outputs = {generic_utf8(out / "site.routes.xdef.json")};
    return planned;
}

bool site(site_options options) {
    const auto planned = plan_site(options);
    if (!planned) return false;
    // A page added or taken away changes the edge itself (its inputs): the build program runs again when the set
    // of the site's files changes. The patterns are from the package's directory, as the site is.
    const std::string under = options.site.empty() || options.site == "." ? std::string() : std::format("{}/", options.site);
    for (const std::string_view directory : made_from) mcpp::rerun_if_changed_glob(std::format("{}{}/**/*", under, directory).c_str());
    mcpp::action made;
    made.id = planned->id.c_str();
    made.role = mcpp::roles::artifact;
    made.description = planned->description.c_str();
    for (const std::string& argument : planned->command) made.arg(argument.c_str());
    for (const std::string& input : planned->inputs) made.input(input.c_str());
    for (const std::string& output : planned->outputs) made.output(output.c_str());
    made.submit();
    return true;
}

}  // namespace oweb::rules
