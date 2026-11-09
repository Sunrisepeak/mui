// mpal:native — a system's notification centre reached through its object
// model (mcpplibs.os's windows::com, macos::objc): declared on every target,
// defined in that system's own unit (src/windows, src/macos, selected by the
// manifest), and called only in a branch `if constexpr (os::is_windows)` or
// `os::is_macos` keeps — elsewhere the branch is discarded, no use, no link.
// An ok image is no native build of either: there notifications are the
// session bus's (Linux) or unsupported.
module mpal:native;

import std;
import :error;
import :notify;

namespace mpal::native {

[[nodiscard]] std::expected<std::shared_ptr<Centre>, Error> open();
[[nodiscard]] std::expected<std::uint32_t, Error> show(Centre& centre, const Notification& notification);
[[nodiscard]] std::expected<void, Error> close(Centre& centre, std::uint32_t id);
[[nodiscard]] std::expected<std::optional<Answer>, Error> next(Centre& centre, std::chrono::milliseconds wait);

}  // namespace mpal::native
