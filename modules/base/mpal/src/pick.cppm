// mpal:pick — the person choosing files or folders the system holds (storage
// access): Linux's desktop portal (org.freedesktop.portal.FileChooser) over
// the session bus — a sandboxed application's only way to a file, and the
// desktop's own dialog for every other; elsewhere unsupported until its
// backend comes (README.md), and a host draws its own.
//
//     auto chosen = mpal::pick({.kind = mpal::Pick::Kind::open, .title = "Open a picture",
//                               .filters = {{"Pictures", {"png", "jpg"}}}});
//     if (chosen && chosen->empty()) … cancelled …
//
// The person takes their time: pick waits for them, so a program with an
// interface calls it on a thread of its own and posts the answer back.
export module mpal:pick;

import std;
import :error;
import :dbus;

export namespace mpal {

struct Filter {
    std::string              name;        // "Pictures"
    std::vector<std::string> extensions;  // {"png", "jpg"}; none: every file
};

struct Pick {
    enum class Kind : std::uint8_t { open, save, folder };
    Kind                  kind = Kind::open;
    std::string           title;
    std::vector<Filter>   filters;
    bool                  many = false;  // open: several files
    std::filesystem::path start;         // the folder it begins in ("": the system's choice)
    std::string           name;          // save: the name offered
    // The window it belongs to, as the portal names one ("x11:1a2b3c", "wayland:<handle>"; "": none).
    std::string           parent;
};

// Whether choosing files is here now (Linux: a desktop portal on the session bus); a host
// draws its own chooser when it is not.
[[nodiscard]] bool can_pick();
[[nodiscard]] bool can_pick_on(dbus::Bus& bus);

// What the person chose; none when they cancelled.
[[nodiscard]] std::expected<std::vector<std::filesystem::path>, Error> pick(const Pick& asked);

// The portal's chooser on a given bus (Linux's session bus, a test's own), waiting at most `wait`.
[[nodiscard]] std::expected<std::vector<std::filesystem::path>, Error> pick_on(dbus::Bus& bus, const Pick& asked,
                                                                               std::chrono::milliseconds wait = std::chrono::hours(24));

// A file:// URI's path (percent-decoded); none for another scheme.
[[nodiscard]] std::optional<std::filesystem::path> path_of_uri(std::string_view uri);

}  // namespace mpal
