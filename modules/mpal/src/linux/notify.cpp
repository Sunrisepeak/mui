// Notifications on Linux: org.freedesktop.Notifications on the session bus.
module mpal;

import std;
import mcpplibs.os;

namespace mpal {

std::expected<std::uint32_t, Error> notify(const Notification& notification) {
    auto bus = dbus::Bus::session();
    if (!bus) return std::unexpected(bus.error());
    return notify_on(*bus, notification);
}

}  // namespace mpal
