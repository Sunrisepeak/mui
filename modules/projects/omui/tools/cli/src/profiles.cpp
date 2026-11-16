// omui's profiles (D-207): the general and workspace rules (the tool kernel's), and what omui declares as data for
// an application and an extension package (omui.profile.xdef.ts, carried here as its snapshot, :omui_profile).
module omui.cli;

import std;
import mlibs.project;

namespace omui::cli {

std::vector<mlibs::project::Profile> profiles() {
    std::vector<mlibs::project::Profile> out{mlibs::project::general(), mlibs::project::workspace()};
    if (auto declared = mlibs::project::profiles_from(omui_profile, "omui.profile.xdef.json")) std::ranges::move(*declared, std::back_inserter(out));
    return out;
}

}  // namespace omui::cli
