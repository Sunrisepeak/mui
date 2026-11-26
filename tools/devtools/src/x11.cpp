module omui.devtools.x11;

import std;
import mcpplibs.os;
import mlibs.hash;

namespace omui::devtools::x11 {

namespace {

namespace ipc = mcpplibs::os::ipc;

// Core requests and events (the X protocol, version 11).
constexpr std::uint8_t change_window_attributes = 2, configure_window = 12, get_geometry = 14,
                       query_tree = 15, intern_atom = 16, get_atom_name = 17, get_property = 20,
                       query_pointer = 38, translate_coordinates = 40, get_input_focus = 43,
                       get_image = 73, query_extension = 98;
constexpr std::uint8_t create_notify = 16, destroy_notify = 17, unmap_notify = 18, map_notify = 19,
                       configure_notify = 22;
constexpr std::uint32_t structure_notify_mask = 1u << 17, substructure_notify_mask = 1u << 19,
                        event_mask_attribute = 1u << 11;
// RANDR's requests (version 1.5): its version, the screen's resources, an output, a CRTC, the
// monitors.
constexpr std::uint8_t randr_query_version = 0, randr_get_output_info = 9,
                       randr_get_crtc_info = 20, randr_get_screen_resources_current = 25,
                       randr_get_monitors = 42;
constexpr std::uint8_t raw_rectangles = 0;  // DAMAGE's level: every drawing reported
constexpr std::uint8_t button_press = 4, button_release = 5, motion_notify = 6;
constexpr std::uint8_t z_pixmap = 2;

// A request being written: little-endian fields, padded to four bytes, its length in words filled
// in.
class Request {
public:
    Request(std::uint8_t major, std::uint8_t data) {
        bytes_ = {static_cast<char>(major), static_cast<char>(data), 0, 0};
    }
    Request& u8(std::uint8_t value) {
        bytes_.push_back(static_cast<char>(value));
        return *this;
    }
    Request& u16(std::uint16_t value) {
        return u8(static_cast<std::uint8_t>(value)).u8(static_cast<std::uint8_t>(value >> 8));
    }
    Request& u32(std::uint32_t value) {
        return u16(static_cast<std::uint16_t>(value)).u16(static_cast<std::uint16_t>(value >> 16));
    }
    Request& pad(std::size_t count) {
        bytes_.append(count, '\0');
        return *this;
    }
    Request& text(std::string_view value) {
        bytes_ += value;
        return pad((4 - value.size() % 4) % 4);
    }
    [[nodiscard]] std::string done() {
        pad((4 - bytes_.size() % 4) % 4);
        const auto words = static_cast<std::uint16_t>(bytes_.size() / 4);
        bytes_[2] = static_cast<char>(words & 0xff);
        bytes_[3] = static_cast<char>(words >> 8);
        return std::move(bytes_);
    }
private:
    std::string bytes_;
};

std::uint16_t u16_at(std::string_view bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[at]) |
                                      static_cast<std::uint8_t>(bytes[at + 1]) << 8);
}
std::int16_t i16_at(std::string_view bytes, std::size_t at) {
    return static_cast<std::int16_t>(u16_at(bytes, at));
}
std::uint32_t u32_at(std::string_view bytes, std::size_t at) {
    return u16_at(bytes, at) | static_cast<std::uint32_t>(u16_at(bytes, at + 2)) << 16;
}

}  // namespace

struct Display::State {
    ipc::connection socket;
    std::string input;  // read, not yet taken
    std::deque<Event> events;
    std::uint16_t sequence = 0;
    std::uint32_t id_base = 0, id_mask = 0, next_id = 1;
    std::uint32_t root = 0;
    Rect screen;
    std::uint8_t damage_major = 0, damage_event = 0, xtest_major = 0;
    std::uint32_t wm_pid = 0, cardinal = 6;
    std::uint8_t min_keycode = 8, max_keycode = 255;

    std::expected<void, std::string> send(std::string bytes) {
        ++sequence;
        if (auto written = socket.write(bytes); !written)
            return std::unexpected(std::format("the display: {}", written.error().message()));
        return {};
    }

    // At least `count` bytes in `input`, waiting at most until `deadline` (forever when none).
    std::expected<bool, std::string> fill(std::size_t count,
                                          std::optional<Clock::time_point> deadline) {
        while (input.size() < count) {
            std::chrono::milliseconds wait{1000};
            if (deadline) {
                const auto left =
                    std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now());
                if (left.count() <= 0)
                    wait = std::chrono::milliseconds{0};
                else
                    wait = left;
            }
            auto chunk = socket.read_for(wait, std::size_t{1} << 22);
            if (!chunk)
                return std::unexpected(std::format("the display: {}", chunk.error().message()));
            if (!*chunk) {
                if (deadline && Clock::now() >= *deadline)
                    return false;
                continue;
            }
            if ((*chunk)->empty())
                return std::unexpected("the display closed the connection");
            input += **chunk;
        }
        return true;
    }

    // An event of 32 bytes, kept in the queue.
    void keep(std::string_view bytes) {
        Event event;
        event.at = Clock::now();
        const std::uint8_t code = static_cast<std::uint8_t>(bytes[0]) & 0x7f;
        if (damage_event && code == damage_event) {
            event.kind = Event::Kind::damaged;
            event.window = u32_at(bytes, 4);
            event.area = {i16_at(bytes, 16), i16_at(bytes, 18), u16_at(bytes, 20),
                          u16_at(bytes, 22)};
        } else if (code == create_notify) {
            event.kind = Event::Kind::created;
            event.parent = u32_at(bytes, 4);
            event.window = u32_at(bytes, 8);
            event.area = {i16_at(bytes, 12), i16_at(bytes, 14), u16_at(bytes, 16),
                          u16_at(bytes, 18)};
        } else if (code == map_notify || code == unmap_notify || code == destroy_notify) {
            event.kind = code == map_notify     ? Event::Kind::mapped
                         : code == unmap_notify ? Event::Kind::unmapped
                                                : Event::Kind::destroyed;
            event.parent = u32_at(bytes, 4);
            event.window = u32_at(bytes, 8);
        } else if (code == configure_notify) {
            event.kind = Event::Kind::configured;
            event.parent = u32_at(bytes, 4);
            event.window = u32_at(bytes, 8);
            event.area = {i16_at(bytes, 16), i16_at(bytes, 18), u16_at(bytes, 20),
                          u16_at(bytes, 22)};
        } else {
            return;  // another event: not one this client asked about
        }
        events.push_back(event);
    }

    // The reply to the request just sent (its whole bytes), events met on the way kept; an error is
    // said.
    std::expected<std::string, std::string> reply() {
        while (true) {
            auto filled = fill(32, std::nullopt);
            if (!filled)
                return std::unexpected(filled.error());
            const std::uint8_t kind = static_cast<std::uint8_t>(input[0]);
            if (kind == 0) {
                const std::uint16_t of = u16_at(input, 2);
                const auto code = static_cast<std::uint8_t>(input[1]);
                input.erase(0, 32);
                if (of == sequence)
                    return std::unexpected(
                        std::format("the display refused a request (error {})", code));
                continue;  // an earlier request's error: nothing waits for it
            }
            if (kind == 1) {
                const std::size_t size = 32 + std::size_t{u32_at(input, 4)} * 4;
                filled = fill(size, std::nullopt);
                if (!filled)
                    return std::unexpected(filled.error());
                std::string out = input.substr(0, size);
                input.erase(0, size);
                if (u16_at(out, 2) == sequence)
                    return out;
                continue;
            }
            keep(std::string_view(input).substr(0, 32));
            input.erase(0, 32);
        }
    }

    std::expected<std::string, std::string> ask(std::string request) {
        if (auto sent = send(std::move(request)); !sent)
            return std::unexpected(sent.error());
        return reply();
    }

    std::expected<std::uint32_t, std::string> atom(std::string_view name) {
        auto answer = ask(Request(intern_atom, 0)
                              .u16(static_cast<std::uint16_t>(name.size()))
                              .pad(2)
                              .text(name)
                              .done());
        if (!answer)
            return std::unexpected(answer.error());
        return u32_at(*answer, 8);
    }

    // An extension's major opcode and first event, or why it is not there.
    std::expected<std::pair<std::uint8_t, std::uint8_t>, std::string>
    extension(std::string_view name) {
        auto answer = ask(Request(query_extension, 0)
                              .u16(static_cast<std::uint16_t>(name.size()))
                              .pad(2)
                              .text(name)
                              .done());
        if (!answer)
            return std::unexpected(answer.error());
        if (!(*answer)[8])
            return std::unexpected(std::format("the display has no {} extension", name));
        return std::pair{static_cast<std::uint8_t>((*answer)[9]),
                         static_cast<std::uint8_t>((*answer)[10])};
    }

    std::uint32_t make_id() {
        const int shift = std::countr_zero(id_mask);
        return id_base | ((next_id++ << shift) & id_mask);
    }
};

Display::Display() : state_(std::make_unique<State>()) {}
Display::Display(Display&&) noexcept = default;
Display& Display::operator=(Display&&) noexcept = default;
Display::~Display() = default;

std::expected<Display, std::string> Display::open(int number) {
    auto socket = ipc::connection::connect("/tmp/.X11-unix", std::format("X{}", number));
    if (!socket)
        return std::unexpected(
            std::format("display :{} cannot be reached: {}", number, socket.error().message()));
    Display display;
    State& state = *display.state_;
    state.socket = std::move(*socket);
    // The setup: little-endian, protocol 11.0; the session's cookie where it has one for this
    // display (a desktop's server asks for it), none otherwise (the harness's own server).
    const auto home = mcpplibs::os::env::get("HOME");
    const std::string authority = mcpplibs::os::env::get("XAUTHORITY").value_or(
        home ? *home + "/.Xauthority" : std::string{});
    const std::string entries =
        authority.empty() ? std::string{} : mcpplibs::os::fs::read(authority).value_or("");
    std::string hostname;  // this machine's name, as Xauthority's local entries are keyed
    if (!entries.empty()) {
        mcpplibs::os::process::spawn_options asked{.program = "hostname"};
        asked.err = mcpplibs::os::process::stdio::null;
        if (auto said = mcpplibs::os::process::run(std::move(asked));
            said && said->status.shell_code() == 0)
            hostname = said->out;
        while (!hostname.empty() && std::isspace(static_cast<unsigned char>(hostname.back())))
            hostname.pop_back();
    }
    const std::optional<std::string> secret =
        entries.empty() ? std::nullopt : cookie(entries, number, hostname);
    constexpr std::string_view scheme = "MIT-MAGIC-COOKIE-1";
    std::string setup{'l', '\0', 11, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (secret) {
        setup[6] = static_cast<char>(scheme.size());
        setup[8] = static_cast<char>(secret->size());
        setup += scheme;
        setup.append((4 - scheme.size() % 4) % 4, '\0');
        setup += *secret;
        setup.append((4 - secret->size() % 4) % 4, '\0');
    }
    if (auto written = state.socket.write(setup); !written)
        return std::unexpected(std::format("display :{}: {}", number, written.error().message()));
    auto filled = state.fill(8, std::nullopt);
    if (!filled)
        return std::unexpected(filled.error());
    const std::size_t size = 8 + std::size_t{u16_at(state.input, 6)} * 4;
    filled = state.fill(size, std::nullopt);
    if (!filled)
        return std::unexpected(filled.error());
    const std::string reply = state.input.substr(0, size);
    state.input.erase(0, size);
    if (reply[0] != 1)
        return std::unexpected(std::format("display :{} refused the connection: {}", number,
                                           reply.substr(8, static_cast<std::uint8_t>(reply[1]))));
    state.id_base = u32_at(reply, 12);
    state.id_mask = u32_at(reply, 16);
    const std::size_t vendor = u16_at(reply, 24), formats = static_cast<std::uint8_t>(reply[29]);
    const std::size_t screen = 40 + (vendor + 3) / 4 * 4 + formats * 8;
    state.root = u32_at(reply, screen);
    state.min_keycode = static_cast<std::uint8_t>(reply[34]);
    state.max_keycode = static_cast<std::uint8_t>(reply[35]);
    state.screen = {0, 0, u16_at(reply, screen + 20), u16_at(reply, screen + 22)};
    return display;
}

std::uint32_t Display::root() const noexcept {
    return state_->root;
}
Rect Display::screen() const noexcept {
    return state_->screen;
}

std::expected<void, std::string> Display::watch(bool screen) {
    State& state = *state_;
    const auto damage = state.extension("DAMAGE");
    if (!damage)
        return std::unexpected(damage.error());
    const auto test = state.extension("XTEST");
    if (!test)
        return std::unexpected(test.error());
    state.damage_major = damage->first;
    state.damage_event = damage->second;
    state.xtest_major = test->first;
    // Each extension's version is asked before its first use (the protocol's rule).
    if (auto answer = state.ask(Request(state.damage_major, 0).u32(1).u32(1).done()); !answer)
        return std::unexpected(answer.error());
    if (auto answer = state.ask(Request(state.xtest_major, 0).u8(2).pad(1).u16(2).done()); !answer)
        return std::unexpected(answer.error());
    auto pid = state.atom("_NET_WM_PID");
    if (!pid)
        return std::unexpected(pid.error());
    state.wm_pid = *pid;
    if (auto sent = state.send(Request(change_window_attributes, 0)
                                   .u32(state.root)
                                   .u32(event_mask_attribute)
                                   .u32(substructure_notify_mask)
                                   .done());
        !sent)
        return sent;
    // The root's damage is the screen's: every window drawn on it is reported (no compositor: one
    // pixmap).
    if (screen)
        if (auto sent = state.send(Request(state.damage_major, 1)
                                       .u32(state.make_id())
                                       .u32(state.root)
                                       .u8(raw_rectangles)
                                       .pad(3)
                                       .done());
            !sent)
            return sent;
    return sync();
}

std::expected<void, std::string> Display::follow(std::uint32_t window) {
    // Asked without waiting: a window gone already answers with an error nobody waits for.
    return state_->send(Request(change_window_attributes, 0)
                            .u32(window)
                            .u32(event_mask_attribute)
                            .u32(structure_notify_mask)
                            .done());
}

std::expected<void, std::string> Display::damage(std::uint32_t window) {
    // One object a call: a window's damage goes with it (a later program's window may reuse its
    // number).
    State& state = *state_;
    if (auto sent = state.send(Request(state.damage_major, 1)
                                   .u32(state.make_id())
                                   .u32(window)
                                   .u8(raw_rectangles)
                                   .pad(3)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<std::array<std::int32_t, 2>, std::string> Display::origin(std::uint32_t window) {
    State& state = *state_;
    auto answer = state.ask(
        Request(translate_coordinates, 0).u32(window).u32(state.root).u16(0).u16(0).done());
    if (!answer)
        return std::unexpected(answer.error());
    return std::array<std::int32_t, 2>{i16_at(*answer, 12), i16_at(*answer, 14)};
}

std::expected<std::uint32_t, std::string> Display::top_level(std::uint32_t window) {
    State& state = *state_;
    for (int depth = 0; depth < 32; ++depth) {
        auto answer = state.ask(Request(query_tree, 0).u32(window).done());
        if (!answer)
            return std::unexpected(answer.error());
        const std::uint32_t parent = u32_at(*answer, 12);
        if (parent == state.root || parent == 0)
            return window;
        window = parent;
    }
    return std::unexpected("a window nested deeper than 32 levels");
}

std::expected<std::vector<std::uint32_t>, std::string> Display::children(std::uint32_t window) {
    auto answer = state_->ask(Request(query_tree, 0).u32(window).done());
    if (!answer)
        return std::unexpected(answer.error());
    const std::size_t count = u16_at(*answer, 16);
    std::vector<std::uint32_t> out;
    for (std::size_t at = 0; at < count && 32 + at * 4 + 4 <= answer->size(); ++at)
        out.push_back(u32_at(*answer, 32 + at * 4));
    return out;
}

std::expected<std::uint32_t, std::string> Display::window_of(std::uint64_t process) {
    if (!state_->wm_pid) {
        auto pid = state_->atom("_NET_WM_PID");
        if (!pid)
            return std::unexpected(pid.error());
        state_->wm_pid = *pid;
    }
    std::uint32_t found = 0;
    std::int64_t biggest = 0;
    const auto consider = [&](std::uint32_t window) {
        const auto owner = process_of(window);
        if (!owner || *owner != process)
            return false;
        const auto place = geometry(window);
        if (!place || place->width < 64 || place->height < 64)
            return false;
        const std::int64_t area = std::int64_t{place->width} * place->height;
        if (area > biggest)
            biggest = area, found = window;
        return true;
    };
    auto tops = children(state_->root);
    if (!tops)
        return std::unexpected(tops.error());
    for (const std::uint32_t top : *tops) {
        if (consider(top))
            continue;
        if (const auto inside = children(top))
            for (const std::uint32_t child : *inside)
                (void)consider(child);
    }
    return found;
}

std::expected<Display::Pointer, std::string> Display::where_pointer() {
    State& state = *state_;
    auto answer = state.ask(Request(query_pointer, 0).u32(state.root).done());
    if (!answer)
        return std::unexpected(answer.error());
    return Pointer{.x = i16_at(*answer, 16), .y = i16_at(*answer, 18), .over = u32_at(*answer, 12)};
}

std::expected<std::string, std::string> Display::resources() {
    State& state = *state_;
    const auto name = state.atom("RESOURCE_MANAGER");
    if (!name)
        return std::unexpected(name.error());
    constexpr std::uint32_t string_type = 31, words = 1u << 16;  // up to 256 KiB
    auto answer = state.ask(
        Request(get_property, 0).u32(state.root).u32(*name).u32(string_type).u32(0).u32(words).done());
    if (!answer)
        return std::unexpected(answer.error());
    if (static_cast<std::uint8_t>((*answer)[1]) != 8)
        return std::string{};
    const std::size_t size = u32_at(*answer, 16);
    return answer->substr(32, std::min(size, answer->size() - 32));
}

std::expected<std::vector<Monitor>, std::string> Display::monitors() {
    State& state = *state_;
    const auto randr = state.extension("RANDR");
    if (!randr)
        return std::vector<Monitor>{};
    const std::uint8_t major = randr->first;
    auto version = state.ask(Request(major, randr_query_version).u32(1).u32(5).done());
    if (!version)
        return std::unexpected(version.error());
    if (u32_at(*version, 8) < 1 || (u32_at(*version, 8) == 1 && u32_at(*version, 12) < 5))
        return std::vector<Monitor>{};
    // Each mode's rate, from the screen's resources: its dot clock over its total size.
    auto resources =
        state.ask(Request(major, randr_get_screen_resources_current).u32(state.root).done());
    if (!resources)
        return std::unexpected(resources.error());
    const std::uint32_t configured = u32_at(*resources, 12);
    const std::size_t crtcs = u16_at(*resources, 16), outputs = u16_at(*resources, 18),
                      modes = u16_at(*resources, 20);
    std::map<std::uint32_t, double> rate;
    for (std::size_t at = 0; at < modes; ++at) {
        const std::size_t mode = 32 + (crtcs + outputs) * 4 + at * 32;
        if (mode + 32 > resources->size())
            break;
        const double clock = u32_at(*resources, mode + 8);
        const double total = static_cast<double>(u16_at(*resources, mode + 16)) *
                             static_cast<double>(u16_at(*resources, mode + 24));
        if (total > 0)
            rate[u32_at(*resources, mode)] = clock / total;
    }
    auto listed = state.ask(Request(major, randr_get_monitors).u32(state.root).u8(1).pad(3).done());
    if (!listed)
        return std::unexpected(listed.error());
    std::vector<Monitor> out;
    std::size_t at = 32;
    const std::size_t count = u32_at(*listed, 12);
    for (std::size_t one = 0; one < count && at + 24 <= listed->size(); ++one) {
        Monitor monitor;
        const std::uint32_t name = u32_at(*listed, at);
        monitor.primary = (*listed)[at + 4] != 0;
        const std::size_t output_count = u16_at(*listed, at + 6);
        monitor.area = {i16_at(*listed, at + 8), i16_at(*listed, at + 10), u16_at(*listed, at + 12),
                        u16_at(*listed, at + 14)};
        const std::uint32_t output = output_count > 0 ? u32_at(*listed, at + 24) : 0;
        at += 24 + output_count * 4;
        if (auto said = state.ask(Request(get_atom_name, 0).u32(name).done()))
            monitor.name = said->substr(32, u16_at(*said, 8));
        // Its output's CRTC, the CRTC's mode, the mode's rate.
        if (output) {
            auto info = state.ask(
                Request(major, randr_get_output_info).u32(output).u32(configured).done());
            if (info && u32_at(*info, 12) != 0) {
                auto crtc = state.ask(Request(major, randr_get_crtc_info)
                                          .u32(u32_at(*info, 12))
                                          .u32(configured)
                                          .done());
                if (crtc)
                    monitor.hertz = rate[u32_at(*crtc, 20)];
            }
        }
        out.push_back(std::move(monitor));
    }
    return out;
}

std::expected<void, std::string> Display::activate(std::uint32_t window) {
    State& state = *state_;
    const auto active = state.atom("_NET_ACTIVE_WINDOW");
    if (!active)
        return std::unexpected(active.error());
    constexpr std::uint8_t send_event = 25, client_message = 33;
    constexpr std::uint32_t redirect_and_notify = (1u << 20) | (1u << 19), pager = 2;
    if (auto sent = state.send(Request(send_event, 0)
                                   .u32(state.root)
                                   .u32(redirect_and_notify)
                                   .u8(client_message)
                                   .u8(32)
                                   .u16(0)
                                   .u32(window)
                                   .u32(*active)
                                   .u32(pager)
                                   .u32(0)
                                   .u32(0)
                                   .u32(0)
                                   .u32(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<std::uint32_t, std::string> Display::input_focus() {
    auto answer = state_->ask(Request(get_input_focus, 0).done());
    if (!answer)
        return std::unexpected(answer.error());
    return u32_at(*answer, 8);
}

std::expected<std::uint8_t, std::string> Display::keycode(std::uint32_t keysym) {
    State& state = *state_;
    constexpr std::uint8_t get_keyboard_mapping = 101;
    const auto count = static_cast<std::uint8_t>(state.max_keycode - state.min_keycode + 1);
    auto answer =
        state.ask(Request(get_keyboard_mapping, 0).u8(state.min_keycode).u8(count).pad(2).done());
    if (!answer)
        return std::unexpected(answer.error());
    const std::size_t per = static_cast<std::uint8_t>((*answer)[1]);
    for (std::size_t at = 0; per > 0 && at < count; ++at)
        if (32 + (at * per + 1) * 4 <= answer->size() && u32_at(*answer, 32 + at * per * 4) == keysym)
            return static_cast<std::uint8_t>(state.min_keycode + at);
    return std::uint8_t{0};
}

std::expected<void, std::string> Display::key(std::uint8_t keycode, bool down) {
    State& state = *state_;
    constexpr std::uint8_t key_press = 2, key_release = 3;
    if (auto sent = state.send(Request(state.xtest_major, 2)
                                   .u8(down ? key_press : key_release)
                                   .u8(keycode)
                                   .pad(2)
                                   .u32(0)
                                   .u32(0)
                                   .pad(8)
                                   .u16(0)
                                   .u16(0)
                                   .pad(7)
                                   .u8(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<void, std::string> Display::keep_above(std::uint32_t window) {
    State& state = *state_;
    const auto property = state.atom("_NET_WM_STATE");
    const auto above = state.atom("_NET_WM_STATE_ABOVE");
    if (!property)
        return std::unexpected(property.error());
    if (!above)
        return std::unexpected(above.error());
    // A ClientMessage to the root, as EWMH says: add, the state, none, a pager's request.
    constexpr std::uint8_t send_event = 25, client_message = 33;
    constexpr std::uint32_t redirect_and_notify = (1u << 20) | (1u << 19);
    if (auto sent = state.send(Request(send_event, 0)
                                   .u32(state.root)
                                   .u32(redirect_and_notify)
                                   .u8(client_message)
                                   .u8(32)
                                   .u16(0)
                                   .u32(window)
                                   .u32(*property)
                                   .u32(1)
                                   .u32(*above)
                                   .u32(0)
                                   .u32(2)
                                   .u32(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<void, std::string> Display::move(std::uint32_t window, std::int32_t x,
                                               std::int32_t y) {
    State& state = *state_;
    const auto moving = state.atom("_NET_MOVERESIZE_WINDOW");
    if (!moving)
        return std::unexpected(moving.error());
    // North-west gravity, x and y given, a pager's request.
    constexpr std::uint32_t north_west = 1, given_x = 1u << 8, given_y = 1u << 9, pager = 2u << 12;
    constexpr std::uint8_t send_event = 25, client_message = 33;
    constexpr std::uint32_t redirect_and_notify = (1u << 20) | (1u << 19);
    if (auto sent = state.send(Request(send_event, 0)
                                   .u32(state.root)
                                   .u32(redirect_and_notify)
                                   .u8(client_message)
                                   .u8(32)
                                   .u16(0)
                                   .u32(window)
                                   .u32(*moving)
                                   .u32(north_west | given_x | given_y | pager)
                                   .u32(static_cast<std::uint32_t>(x))
                                   .u32(static_cast<std::uint32_t>(y))
                                   .u32(0)
                                   .u32(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<std::optional<Event>, std::string> Display::next(std::chrono::milliseconds wait) {
    State& state = *state_;
    const auto deadline = Clock::now() + wait;
    while (state.events.empty()) {
        auto filled = state.fill(32, deadline);
        if (!filled)
            return std::unexpected(filled.error());
        if (!*filled)
            return std::optional<Event>{};
        const std::uint8_t kind = static_cast<std::uint8_t>(state.input[0]);
        if (kind == 1) {  // a reply nobody waits for (none is sent without waiting): passed over
            const std::size_t size = 32 + std::size_t{u32_at(state.input, 4)} * 4;
            if (auto whole = state.fill(size, std::nullopt); !whole)
                return std::unexpected(whole.error());
            state.input.erase(0, size);
            continue;
        }
        if (kind != 0)
            state.keep(std::string_view(state.input).substr(0, 32));
        state.input.erase(0, 32);
    }
    Event event = state.events.front();
    state.events.pop_front();
    return event;
}

std::expected<Display::Image, std::string> Display::image(Rect area, std::uint32_t drawable) {
    State& state = *state_;
    auto answer = state.ask(Request(get_image, z_pixmap)
                                .u32(drawable ? drawable : state.root)
                                .u16(static_cast<std::uint16_t>(area.x))
                                .u16(static_cast<std::uint16_t>(area.y))
                                .u16(static_cast<std::uint16_t>(area.width))
                                .u16(static_cast<std::uint16_t>(area.height))
                                .u32(0xffffffffu)
                                .done());
    if (!answer)
        return std::unexpected(answer.error());
    Image out;
    out.at = Clock::now();
    const std::size_t count =
        static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height);
    if (answer->size() < 32 + count * 4)
        return std::unexpected("the display's image is not 32 bits a pixel");
    out.pixels.resize(count);
    std::memcpy(out.pixels.data(), answer->data() + 32, count * 4);
    for (auto& pixel : out.pixels)
        pixel &= 0x00ffffffu;
    return out;
}

std::expected<Rect, std::string> Display::geometry(std::uint32_t window) {
    auto answer = state_->ask(Request(get_geometry, 0).u32(window).done());
    if (!answer)
        return std::unexpected(answer.error());
    return Rect{i16_at(*answer, 12), i16_at(*answer, 14), u16_at(*answer, 16), u16_at(*answer, 18)};
}

std::expected<std::uint64_t, std::string> Display::process_of(std::uint32_t window) {
    State& state = *state_;
    auto answer = state.ask(
        Request(get_property, 0).u32(window).u32(state.wm_pid).u32(0).u32(0).u32(1).done());
    if (!answer)
        return std::unexpected(answer.error());
    if (static_cast<std::uint8_t>((*answer)[1]) != 32 || u32_at(*answer, 16) < 1)
        return std::uint64_t{0};
    return std::uint64_t{u32_at(*answer, 32)};
}

std::expected<void, std::string> Display::resize(std::uint32_t window, std::int32_t width,
                                                 std::int32_t height) {
    constexpr std::uint16_t width_bit = 1u << 2, height_bit = 1u << 3;
    if (auto sent = state_->send(Request(configure_window, 0)
                                     .u32(window)
                                     .u16(width_bit | height_bit)
                                     .pad(2)
                                     .u32(static_cast<std::uint32_t>(width))
                                     .u32(static_cast<std::uint32_t>(height))
                                     .done());
        !sent)
        return sent;
    return sync();
}

std::expected<void, std::string> Display::pointer(std::int32_t x, std::int32_t y) {
    State& state = *state_;
    if (auto sent = state.send(Request(state.xtest_major, 2)
                                   .u8(motion_notify)
                                   .u8(0)  // absolute
                                   .pad(2)
                                   .u32(0)  // now
                                   .u32(state.root)
                                   .pad(8)
                                   .u16(static_cast<std::uint16_t>(x))
                                   .u16(static_cast<std::uint16_t>(y))
                                   .pad(7)
                                   .u8(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<void, std::string> Display::button(bool down) {
    State& state = *state_;
    if (auto sent = state.send(Request(state.xtest_major, 2)
                                   .u8(down ? button_press : button_release)
                                   .u8(1)
                                   .pad(2)
                                   .u32(0)
                                   .u32(0)
                                   .pad(8)
                                   .u16(0)
                                   .u16(0)
                                   .pad(7)
                                   .u8(0)
                                   .done());
        !sent)
        return sent;
    return sync();
}

std::expected<void, std::string> Display::wheel(bool down) {
    State& state = *state_;
    const std::uint8_t notch = down ? 5 : 4;
    for (const std::uint8_t type : {button_press, button_release})
        if (auto sent = state.send(Request(state.xtest_major, 2)
                                       .u8(type)
                                       .u8(notch)
                                       .pad(2)
                                       .u32(0)
                                       .u32(0)
                                       .pad(8)
                                       .u16(0)
                                       .u16(0)
                                       .pad(7)
                                       .u8(0)
                                       .done());
            !sent)
            return sent;
    return {};
}

std::expected<void, std::string> Display::sync() {
    auto answer = state_->ask(Request(get_input_focus, 0).done());
    if (!answer)
        return std::unexpected(answer.error());
    return {};
}

std::optional<std::string> cookie(std::string_view xauthority, int number,
                                  std::string_view hostname) {
    // Entries one after another: a family (two bytes, big-endian), then four counted strings —
    // the address, the display's number, the scheme's name, its data.
    constexpr std::uint16_t local = 256, wild = 65535;
    const std::string display = std::to_string(number);
    std::optional<std::string> found;
    std::size_t at = 0;
    const auto counted = [&](std::string_view& out) {
        if (at + 2 > xauthority.size())
            return false;
        const std::size_t size = static_cast<std::uint8_t>(xauthority[at]) << 8 |
                                 static_cast<std::uint8_t>(xauthority[at + 1]);
        if (at + 2 + size > xauthority.size())
            return false;
        out = xauthority.substr(at + 2, size);
        at += 2 + size;
        return true;
    };
    while (at + 2 <= xauthority.size()) {
        const std::uint16_t family = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(xauthority[at]) << 8 |
            static_cast<std::uint8_t>(xauthority[at + 1]));
        at += 2;
        std::string_view address, entry_number, name, data;
        if (!counted(address) || !counted(entry_number) || !counted(name) || !counted(data))
            break;
        if (name != "MIT-MAGIC-COOKIE-1" || (!entry_number.empty() && entry_number != display))
            continue;
        // This machine's own entry first; a wildcard's where it has none (another host's local
        // entry is that host's).
        if (family == local && address == hostname)
            return std::string(data);
        if (family == wild && !found)
            found = std::string(data);
    }
    return found;
}

std::uint64_t hash(std::span<const std::uint32_t> pixels) noexcept {
    mlibs::hash::Fnv1a value;
    for (const std::uint32_t pixel : pixels)
        value.le(pixel, 3);  // the colour's three bytes, not the unused fourth
    return value.value();
}

bool uniform(std::span<const std::uint32_t> pixels) noexcept {
    return std::ranges::all_of(pixels, [&](std::uint32_t pixel) {
        return (pixel & 0x00ffffffu) == (pixels.front() & 0x00ffffffu);
    });
}

}  // namespace omui::devtools::x11
