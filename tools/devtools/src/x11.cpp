module omui.devtools.x11;

import std;
import mcpplibs.os;
import mlibs.hash;

namespace omui::devtools::x11 {

namespace {

namespace ipc = mcpplibs::os::ipc;

// Core requests and events (the X protocol, version 11).
constexpr std::uint8_t change_window_attributes = 2, configure_window = 12, get_geometry = 14,
                       intern_atom = 16, get_property = 20, get_input_focus = 43, get_image = 73,
                       query_extension = 98;
constexpr std::uint8_t create_notify = 16, destroy_notify = 17, unmap_notify = 18, map_notify = 19,
                       configure_notify = 22;
constexpr std::uint32_t substructure_notify_mask = 1u << 19, event_mask_attribute = 1u << 11;
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
    // The setup: little-endian, protocol 11.0, no authorisation (the harness's own server).
    std::string setup{'l', '\0', 11, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
    state.screen = {0, 0, u16_at(reply, screen + 20), u16_at(reply, screen + 22)};
    return display;
}

std::uint32_t Display::root() const noexcept {
    return state_->root;
}
Rect Display::screen() const noexcept {
    return state_->screen;
}

std::expected<void, std::string> Display::watch() {
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
    constexpr std::uint8_t raw_rectangles = 0;
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

std::expected<Display::Image, std::string> Display::image(Rect area) {
    State& state = *state_;
    auto answer = state.ask(Request(get_image, z_pixmap)
                                .u32(state.root)
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
