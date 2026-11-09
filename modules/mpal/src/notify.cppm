// mpal:notify — telling the person something outside the application's view
// (the system's notifications): Linux's freedesktop Notifications over the
// session bus; elsewhere unsupported until its backend comes (README.md).
//
//     auto shown = mpal::notify({.application = "Notes", .title = "Saved", .body = "notes.md"});
//     if (!shown) … shown.error().code == mpal::Error::Code::unsupported …
//
// The person's answer (an action chosen, the notification dismissed) comes
// back to a Notifier, which keeps the conversation open:
//
//     auto notes = mpal::Notifier::open();
//     auto id = notes->show({.application = "Notes", .title = "Saved", .actions = {{"open", "Open"}}});
//     if (auto answer = notes->next(std::chrono::seconds(1)); answer && *answer && (*answer)->key == "open") …
export module mpal:notify;

import std;
import :error;
import :dbus;

export namespace mpal {

struct Notification {
    std::string application;  // who tells: the application's name
    std::string title, body;
    std::string icon;         // a name the system knows, or a file path ("": none)
    // Choices offered with it: {key, label}; the person's choice comes back by its key
    // (a Notifier's Answer). The key "default" is the notification itself, clicked.
    std::vector<std::pair<std::string, std::string>> actions;
    // How long it stays: none — the system's default; zero — until the person dismisses it.
    std::optional<std::chrono::milliseconds> shown;
};

// What became of a notification shown: the person chose an action, or it went
// away (dismissed by the person, expired, closed by the application).
struct Answer {
    enum class Kind : std::uint8_t { chosen, dismissed, expired, closed };
    std::uint32_t id = 0;  // the notification's, as show() gave it
    Kind          kind = Kind::chosen;
    std::string   key;  // chosen: the action's key
};

// The system's id of the notification shown (L0: told and forgotten).
[[nodiscard]] std::expected<std::uint32_t, Error> notify(const Notification& notification);

// The freedesktop Notifications call on a given bus (Linux's session bus, a test's own).
[[nodiscard]] std::expected<std::uint32_t, Error> notify_on(dbus::Bus& bus, const Notification& notification);

// Notifications and their answers: one conversation with the system's service.
// Only the answers to its own notifications come back.
class Notifier {
public:
    // The system's service (Linux: the session bus); unsupported where its backend is not written.
    [[nodiscard]] static std::expected<Notifier, Error> open();
    // The freedesktop service on a given bus.
    [[nodiscard]] static std::expected<Notifier, Error> on(dbus::Bus bus);
    // One that hears the answers to every notification, its own or not: a connection
    // that only listens (a thread of its own, while another shows), the caller telling which are its.
    [[nodiscard]] static std::expected<Notifier, Error> listen();
    [[nodiscard]] static std::expected<Notifier, Error> listen(dbus::Bus bus);

    [[nodiscard]] std::expected<std::uint32_t, Error> show(const Notification& notification);
    // Takes a notification shown back (its answer: closed).
    [[nodiscard]] std::expected<void, Error> close(std::uint32_t id);
    // The next answer, waiting at most `wait`; none when none came.
    [[nodiscard]] std::expected<std::optional<Answer>, Error> next(std::chrono::milliseconds wait);

private:
    std::optional<dbus::Bus>     bus_;
    std::set<std::uint32_t>      shown_;
    bool                         every_ = false;
};

}  // namespace mpal
