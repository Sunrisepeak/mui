// Notifications (mpal:notify): each system's backend, chosen with
// mcpplibs.os's platform questions — at compile time for a native build, when
// loaded for an ok image (openkal), which runs on every system — and the
// freedesktop call itself, which is protocol, not a platform's headers. A
// native build for macOS or Windows speaks to that system's centre
// (mpal:native), through mcpplibs.os's object models.
module mpal;

import std;
import mcpplibs.os;
import :native;

namespace mpal {

namespace os = mcpplibs::os;

std::expected<std::uint32_t, Error> notify(const Notification& notification) {
    if constexpr (os::is_macos || os::is_windows) {
        auto notifier = Notifier::open();
        if (!notifier) return std::unexpected(notifier.error());
        return notifier->show(notification);
    }
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

namespace {

constexpr std::string_view service = "org.freedesktop.Notifications";
constexpr std::string_view object = "/org/freedesktop/Notifications";

}  // namespace

std::expected<Notifier, Error> Notifier::open() {
    if constexpr (os::is_macos || os::is_windows) {
        auto centre = native::open();
        if (!centre) return std::unexpected(centre.error());
        Notifier notifier;
        notifier.centre_ = std::move(*centre);
        return notifier;
    }
    if constexpr (os::can_run_on(os::platform::linux))
        if (os::host() == os::platform::linux) {
            auto bus = dbus::Bus::session();
            if (!bus) return std::unexpected(bus.error());
            return on(std::move(*bus));
        }
    return failure(Error::Code::unsupported, std::format("notifications on {} are not written yet (mpal README)", os::name(os::host())));
}

std::expected<Notifier, Error> Notifier::listen() {
    auto notifier = open();
    if (notifier) notifier->every_ = true;
    return notifier;
}

std::expected<Notifier, Error> Notifier::listen(dbus::Bus bus) {
    auto notifier = on(std::move(bus));
    if (notifier) notifier->every_ = true;
    return notifier;
}

std::expected<Notifier, Error> Notifier::on(dbus::Bus bus) {
    // The service's two signals: ActionInvoked(u id, s key) and NotificationClosed(u id, u reason).
    if (auto matched = bus.add_match(std::format("type='signal',interface='{}',path='{}'", service, object)); !matched)
        return std::unexpected(matched.error());
    Notifier notifier;
    notifier.bus_.emplace(std::move(bus));
    return notifier;
}

std::expected<std::uint32_t, Error> Notifier::show(const Notification& notification) {
    if constexpr (os::is_macos || os::is_windows)
        if (centre_) return native::show(*centre_, notification);
    auto id = notify_on(*bus_, notification);
    if (id) shown_.insert(*id);
    return id;
}

std::expected<void, Error> Notifier::close(std::uint32_t id) {
    if constexpr (os::is_macos || os::is_windows)
        if (centre_) return native::close(*centre_, id);
    auto closed = bus_->call({.destination = std::string(service), .path = std::string(object), .interface = std::string(service),
                              .member = "CloseNotification", .signature = "u", .arguments = {dbus::Value{id}}});
    if (!closed) return std::unexpected(closed.error());
    return {};
}

std::expected<std::optional<Answer>, Error> Notifier::next(std::chrono::milliseconds wait) {
    if constexpr (os::is_macos || os::is_windows)
        if (centre_) return native::next(*centre_, wait);
    const auto until = std::chrono::steady_clock::now() + wait;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        auto signal = bus_->next_signal(std::max(left, std::chrono::milliseconds(0)));
        if (!signal) return std::unexpected(signal.error());
        if (!*signal) return std::optional<Answer>{};
        const dbus::Message& heard = **signal;
        if (heard.interface != service || heard.body.size() < 2) continue;
        const auto* id = std::get_if<std::uint32_t>(&heard.body[0].data);
        if (!id || (!every_ && !shown_.contains(*id))) continue;  // another application's
        if (heard.member == "ActionInvoked")
            if (const auto* key = std::get_if<std::string>(&heard.body[1].data)) return std::optional(Answer{*id, Answer::Kind::chosen, *key});
        if (heard.member == "NotificationClosed") {
            shown_.erase(*id);
            const auto* reason = std::get_if<std::uint32_t>(&heard.body[1].data);
            // 1 expired, 2 dismissed by the person, 3 closed by CloseNotification, 4 undefined.
            const Answer::Kind kind = !reason ? Answer::Kind::closed
                                      : *reason == 1 ? Answer::Kind::expired
                                      : *reason == 2 ? Answer::Kind::dismissed
                                                     : Answer::Kind::closed;
            return std::optional(Answer{*id, kind, {}});
        }
    }
}

}  // namespace mpal
