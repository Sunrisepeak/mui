// A script on a thread of its own: every platform with threads.
module omui.lang;

namespace omui::lang {

bool Host::threads_available() noexcept { return true; }

}  // namespace omui::lang
