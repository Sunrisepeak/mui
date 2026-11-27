// Off the Web the component reads Markdown source with md4c whether or not a program installed it: a desktop program
// links it as it always did.
module omui.ui.markdown;

import mlibs.markdown;
import :parser;

namespace omui {

mlibs::markdown::Parser fallback_parser() noexcept { return mlibs::markdown::md4c(); }

}  // namespace omui
