// omui.build.plan — what omui's build plugins share: the foundation's (mlibs.build.plan: a planned edge, handing
// edges to the build, a tool built for the build, a path's text, the files a plugin reads), re-exported, and omui's
// generator. Each plugin is a module of its own, and a package's build program imports only the ones it uses
// (mcpp/README.md); each re-exports this module.
export module omui.build.plan;

import std;
import mcpp;
export import mlibs.build.plan;

export namespace omui::build {

// omui-gen as built for this build (empty: it was not — the plugin's package depends on omui.gen with
// tools = ["omui-gen"]).
[[nodiscard]] std::string generator();

}  // namespace omui::build
