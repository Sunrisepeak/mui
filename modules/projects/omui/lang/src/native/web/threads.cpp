// The Web: the page's engine runs scripts on the page's one thread, which is the runtime's (no workers, so no
// SharedArrayBuffer and no cross-origin isolation asked of the page).
module omui.lang;

namespace omui::lang {

bool Host::threads_available() noexcept { return false; }

}  // namespace omui::lang
