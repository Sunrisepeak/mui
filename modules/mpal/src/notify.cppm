// mpal:notify — telling the person something outside the application's view
// (the system's notifications): Linux's freedesktop Notifications over the
// session bus; elsewhere unsupported until its backend comes (README.md).
//
//     auto shown = mpal::notify({.application = "Notes", .title = "Saved", .body = "notes.md"});
//     if (!shown) … shown.error().code == mpal::Error::Code::unsupported …
export module mpal:notify;

import std;
import :error;
import :dbus;

export namespace mpal {

struct Notification {
    std::string application;  // who tells: the application's name
    std::string title, body;
    std::string icon;         // a name the system knows, or a file path ("": none)
    // Choices offered with it: {key, label}; the person's choice comes back by its key (not yet read).
    std::vector<std::pair<std::string, std::string>> actions;
    // How long it stays: none — the system's default; zero — until the person dismisses it.
    std::optional<std::chrono::milliseconds> shown;
};

// The system's id of the notification shown.
[[nodiscard]] std::expected<std::uint32_t, Error> notify(const Notification& notification);

// The freedesktop Notifications call on a given bus (Linux's session bus, a test's own).
[[nodiscard]] std::expected<std::uint32_t, Error> notify_on(dbus::Bus& bus, const Notification& notification);

}  // namespace mpal
