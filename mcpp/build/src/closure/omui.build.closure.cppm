// omui.build.closure — what a TypeScript application carries, derived from what it wrote (B7b; the closure design,
// D-251): the base components its scripts and its design's replay take by name from `openmui` become the registry
// its program calls — the player's `closure` entry (`main = { from = "omui.entry.player/closure" }`) — so the
// components it never names are not in it. omui-gen derives it (omui.gen.closure) as a source of the build; every
// component stays where the designer is linked (development) or the application's generation is open to components.
//
//     // build.mcpp
//     omui::build::closure({.files = {"app", "design/main.replay.xdef.json"}});
//
// closure.json, beside the build's outputs, says what was taken and where.
export module omui.build.closure;

import std;
import mcpp;
export import omui.build.plan;

export namespace omui::build {

struct closure_options {
    // The application's scripts and its design's replay, relative to the package (a directory: the scripts in it).
    std::vector<std::string> files = {"app"};
    std::string              config = "omui.config.ts";  // its generation open to components keeps every one
    std::string              out_dir = std::string(mcpp::out_dir());
};

// Hands the derivation to the build: the registry unit (omui_application_components) the application's program
// compiles, and closure.json. Only the application being built derives it (a dependency's build does nothing).
bool closure(closure_options opt = {});

}  // namespace omui::build
