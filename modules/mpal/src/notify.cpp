// Notifications (mpal:notify): each system's backend, chosen with
// mcpplibs.os's platform questions — at compile time for a native build, when
// loaded for an ok image (openkal), which runs on every system — and the
// freedesktop call itself, which is protocol, not a platform's headers.
module mpal;

import std;
import mcpplibs.os;

namespace mpal {

namespace os = mcpplibs::os;

std::expected<std::uint32_t, Error> notify(const Notification& notification) {
    // Linux (a native build, or an image loaded there): the session bus.
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            if (!bus) return std::unexpected(bus.error());
            return notify_on(*bus, notification);
        }
    return failure(Error::Code::unsupported, std::format("notifications on {} are not written yet (mpal README)", os::name(os::host())));
}

std::expected<std::uint32_t, Error> notify_on(dbus::Bus& bus, const Notification& notification) {
    using dbus::Value;
    std::vector<Value> actions;
    for (const auto& [key, label] : notification.actions) {
        actions.push_back(Value::of(key));
        actions.push_back(Value::of(label));
    }
    // Notify(app_name s, replaces_id u, app_icon s, summary s, body s, actions as, hints a{sv}, expire_timeout i).
    const std::int32_t expires = notification.shown ? static_cast<std::int32_t>(notification.shown->count()) : -1;
    auto answer = bus.call({.destination = "org.freedesktop.Notifications",
                            .path = "/org/freedesktop/Notifications",
                            .interface = "org.freedesktop.Notifications",
                            .member = "Notify",
                            .signature = "susssasa{sv}i",
                            .arguments = {Value::of(notification.application), Value{std::uint32_t{0}}, Value::of(notification.icon),
                                          Value::of(notification.title), Value::of(notification.body), Value{std::move(actions)},
                                          Value{std::vector<Value>{}}, Value{expires}}});
    if (!answer) {
        if (answer.error().text.contains("ServiceUnknown")) return failure(Error::Code::unavailable, "no notification service on the session bus");
        return std::unexpected(answer.error());
    }
    if (answer->empty() || !std::holds_alternative<std::uint32_t>(answer->front().data)) return failure(Error::Code::failed, "Notify answered no id");
    return std::get<std::uint32_t>(answer->front().data);
}

}  // namespace mpal
