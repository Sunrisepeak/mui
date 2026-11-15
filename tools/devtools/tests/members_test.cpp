// omui-devtools llvm's choice of members: the workspace's list, what each
// depends on by path, which member a changed file belongs to, and a change's
// members with their direct consumers.
import std;
import mcpplibs.mtest;
import omui.devtools.members;

using namespace mcpplibs::mtest;
namespace ws = omui::devtools::members;

namespace {

constexpr std::string_view root = R"(# the workspace
[workspace]
members = [
    "modules/base/math",   # the bottom
    "modules/base/geom",
    "modules/projects/xrender/mesh",
    "modules/projects/xrender/mesh/gpu",
    "showcase/quay/play",
]

[workspace.profile.ci]
opt = 0
)";

std::vector<ws::Member> workspace() {
    const auto paths = ws::listed(root);
    return {
        {paths[0], {}},
        {paths[1], ws::uses_of(paths[1], "[dependencies]\nmlibs.math = { path = \"../math\" }\n", paths)},
        {paths[2], ws::uses_of(paths[2], "[dependencies]\nmlibs.geom = { path = \"../../../base/geom\" }\n[dev-dependencies]\nmlibs.math = { path = \"../../../base/math\" }\n", paths)},
        {paths[3], ws::uses_of(paths[3], "[dependencies]\nxrender.mesh = { path = \"..\" }\nother = { path = \"../../../../elsewhere\" }\n", paths)},
        {paths[4], ws::uses_of(paths[4], "[dependencies]\n\"xrender.mesh.gpu\" = { path = \"../../../modules/projects/xrender/mesh/gpu\" }\n", paths)},
    };
}

}  // namespace

UTest cases{"devtools.members", [] {

    "the workspace's members as the root manifest lists them, comments aside"_test = [] {
        const auto paths = ws::listed(root);
        expect(paths == std::vector<std::string>{"modules/base/math", "modules/base/geom", "modules/projects/xrender/mesh",
                                                 "modules/projects/xrender/mesh/gpu", "showcase/quay/play"})
            << paths.size();
        expect(ws::listed("[workspace]\nmembers = [\"a\", \"b/c\"]\n") == std::vector<std::string>{"a", "b/c"}) << "on one line";
    };

    "what a member depends on by path, of every kind, resolved against its directory; a path outside the workspace is not a member"_test = [] {
        const auto all = workspace();
        expect(all[2].uses == std::vector<std::string>{"modules/base/geom", "modules/base/math"});
        expect(all[3].uses == std::vector<std::string>{"modules/projects/xrender/mesh"}) << "`..` is the parent member";
        expect(all[4].uses == std::vector<std::string>{"modules/projects/xrender/mesh/gpu"});
    };

    "a changed file belongs to the deepest member it lies under"_test = [] {
        const auto all = workspace();
        expect(ws::owner_of("modules/projects/xrender/mesh/src/trace.cpp", all) == "modules/projects/xrender/mesh");
        expect(ws::owner_of("modules/projects/xrender/mesh/gpu/src/pass.cpp", all) == "modules/projects/xrender/mesh/gpu");
        expect(ws::owner_of("modules/projects/xrender/meshes/x.cpp", all).empty()) << "a name that only starts like a member's";
        expect(ws::owner_of("AGENTS.md", all).empty());
    };

    "a change's members and their direct consumers, in the workspace's order; consumers' consumers are not"_test = [] {
        const auto all = workspace();
        const std::vector<std::string> changed{"modules/projects/xrender/mesh"};
        expect(ws::with_consumers(changed, all) == std::vector<std::string>{"modules/projects/xrender/mesh", "modules/projects/xrender/mesh/gpu"});
        const std::vector<std::string> bottom{"modules/base/math"};
        expect(ws::with_consumers(bottom, all) == std::vector<std::string>{"modules/base/math", "modules/base/geom", "modules/projects/xrender/mesh"});
    };
}};
