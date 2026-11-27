// On the Web the component reads Markdown source only with a parser a program installed (registering the component by
// name does): a page that shows documents as HTML alone links no Markdown parser.
module omui.ui.markdown;

import mlibs.markdown;
import :parser;

namespace omui {

mlibs::markdown::Parser fallback_parser() noexcept { return {}; }

}  // namespace omui
