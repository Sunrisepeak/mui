module omui.build.plan;

import std;
import mlibs.build.plan;

namespace omui::build {

std::string generator() { return mlibs::build::tool("omui.gen", "omui-gen"); }

}  // namespace omui::build
