// The TypeScript language on the Web declares nothing: a page type-checks nothing (`omui check` and the player's
// `--types` are the desktop's), so a Web program carries no declarations — the binding API, `openmui/define`,
// `omui/define` (src/checker, off the Web).
module omui.lang.js;

import std;
import omui.player;

namespace omui::lang::js {

decltype(player::Language::declare) declaring() { return {}; }

}  // namespace omui::lang::js
