// An application drawn in another process's window (omui.agent:present).
module omui.agent;

import std;
import mcpplibs.os;
import omui.core;
import omui.view;
import xrender.vector;
import xrender.raster;
import omui.semantic;

namespace omui::agent {

namespace {

// A message: its length (u32, the kind and the bytes), its kind, its bytes.
enum class Kind : char { frame = 'F', image = 'I', input = 'E', size = 'S', ask_state = 'Q', state = 'A', restore = 'R' };

void put32(std::string& out, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
}
void put64(std::string& out, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
}
std::uint64_t get(std::string_view bytes, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[at + index])) << (8 * index);
    return value;
}

std::string message(Kind kind, std::string_view bytes) {
    std::string out;
    put32(out, static_cast<std::uint32_t>(bytes.size() + 1));
    out.push_back(static_cast<char>(kind));
    out.append(bytes);
    return out;
}

// The next whole message at the front of `pending`: its kind and bytes, taken out.
std::optional<std::pair<Kind, std::string>> next_message(std::string& pending) {
    if (pending.size() < 5) return std::nullopt;
    const auto length = static_cast<std::size_t>(get(pending, 0, 4));
    if (pending.size() < 4 + length || length == 0) return std::nullopt;
    std::pair<Kind, std::string> out{static_cast<Kind>(pending[4]), pending.substr(5, length - 1)};
    pending.erase(0, 4 + length);
    return out;
}

// Every image a list refers to: drawn, painted with, or a material's normal map.
void images_of(const xrender::DisplayList& list, std::set<std::uint32_t>& out) {
    const auto paint = [&](const xrender::Paint& p, auto& self) -> void {
        if (p.kind == xrender::Paint::Kind::image || p.kind == xrender::Paint::Kind::image_mask) out.insert(p.image.id);
        if (p.material && p.material->normal.id) out.insert(p.material->normal.id);
        for (const auto& term : p.mix) self(term.paint, self);
    };
    for (const auto& command : list.commands())
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (requires { c.paint; }) paint(c.paint, paint);
                if constexpr (std::same_as<T, xrender::DrawImage>) out.insert(c.image.id);
            },
            command);
    out.erase(0);
}

}  // namespace

Presenter::Presenter(Runtime& runtime, mcpplibs::os::ipc::connection connection) : runtime_(runtime), connection_(std::move(connection)) {}

bool Presenter::send(const FrameOutput& frame) {
    if (!frame.list || !connection_.valid()) return false;
    std::set<std::uint32_t> wanted;
    images_of(*frame.list, wanted);
    std::string out;
    for (const std::uint32_t id : wanted) {
        const xrender::ImageRef ref{id};
        const auto revision = runtime_.images().revision(ref);
        if (const auto known = sent_.find(id); known != sent_.end() && known->second == revision) continue;
        const auto* surface = runtime_.images().get(ref);
        if (!surface) continue;
        std::string image;
        put32(image, id);
        put64(image, revision);
        put32(image, static_cast<std::uint32_t>(surface->width()));
        put32(image, static_cast<std::uint32_t>(surface->height()));
        const auto bytes = surface->bytes();
        image.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out += message(Kind::image, image);
        sent_[id] = revision;
    }
    out += message(Kind::frame, xrender::encode(*frame.list));
    return connection_.write(out).has_value();
}

void Presenter::read_available() {
    while (connection_.valid()) {
        auto chunk = connection_.read_for(std::chrono::milliseconds(0));
        if (!chunk || !*chunk || (*chunk)->empty()) break;
        pending_ += **chunk;
    }
    while (auto next = next_message(pending_)) {
        if (next->first == Kind::input) {
            if (const auto events = parse_json(next->second); events && events->list())
                for (const Value& event : *events->list())
                    if (auto made = event_from_value(event)) input_.push_back(std::move(*made));
        } else if (next->first == Kind::ask_state) {
            (void)connection_.write(message(Kind::state, semantic::Tree(runtime_).state().json()));
        } else if (next->first == Kind::restore) {
            if (const auto state = parse_json(next->second)) {
                Invocation host;
                host.actor = {"person", "developer", {}};
                (void)semantic::Tree(runtime_).restore(*state, host);
            }
        } else if (next->first == Kind::size && next->second.size() == 12) {
            const auto width = std::bit_cast<float>(static_cast<std::uint32_t>(get(next->second, 0, 4)));
            const auto height = std::bit_cast<float>(static_cast<std::uint32_t>(get(next->second, 4, 4)));
            const auto scale = std::bit_cast<float>(static_cast<std::uint32_t>(get(next->second, 8, 4)));
            size_ = std::pair{Size{width, height}, scale};
        }
    }
}

std::vector<InputEvent> Presenter::input() {
    read_available();
    return std::exchange(input_, {});
}

std::optional<std::pair<Size, float>> Presenter::resized() {
    read_available();
    return std::exchange(size_, std::nullopt);
}

Presentation::Presentation(mcpplibs::os::ipc::connection connection) : connection_(std::move(connection)) {}

const xrender::DisplayList* Presentation::receive(std::chrono::milliseconds wait) {
    const auto until = std::chrono::steady_clock::now() + wait;
    while (!gone_) {
        while (auto next = next_message(pending_)) {
            if (next->first == Kind::image && next->second.size() >= 20) {
                const std::string_view bytes = next->second;
                const auto id = static_cast<std::uint32_t>(get(bytes, 0, 4));
                const auto revision = get(bytes, 4, 8);
                const auto width = static_cast<int>(get(bytes, 12, 4)), height = static_cast<int>(get(bytes, 16, 4));
                if (bytes.size() != 20 + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) continue;
                auto surface = std::make_shared<xrender::raster::Surface>(width, height);
                std::memcpy(surface->pixels().data(), bytes.data() + 20, bytes.size() - 20);
                images_.put({id}, std::move(surface), revision);
            } else if (next->first == Kind::state) {
                if (auto state = parse_json(next->second)) state_ = std::move(*state);
            } else if (next->first == Kind::frame) {
                xrender::DisplayList list;
                if (xrender::decode(next->second, list)) {
                    list_ = std::move(list);
                    ++frames_;
                    return &list_;
                }
            }
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() < 0) return nullptr;
        auto chunk = connection_.read_for(left);
        if (!chunk) {
            gone_ = true;
            return nullptr;
        }
        if (!*chunk) return nullptr;  // nothing within the wait
        if ((*chunk)->empty()) {
            gone_ = true;
            return nullptr;
        }
        pending_ += **chunk;
    }
    return nullptr;
}

bool Presentation::send_input(std::span<const InputEvent> events) {
    Value::List list;
    for (const InputEvent& event : events) list.push_back(to_value(event));
    return connection_.write(message(Kind::input, Value(std::move(list)).json())).has_value();
}

bool Presentation::ask_state() {
    state_.reset();
    return connection_.write(message(Kind::ask_state, {})).has_value();
}

bool Presentation::restore(const Value& state) { return connection_.write(message(Kind::restore, state.json())).has_value(); }

bool Presentation::resize(Size size, float scale) {
    std::string bytes;
    put32(bytes, std::bit_cast<std::uint32_t>(size.width));
    put32(bytes, std::bit_cast<std::uint32_t>(size.height));
    put32(bytes, std::bit_cast<std::uint32_t>(scale));
    return connection_.write(message(Kind::size, bytes)).has_value();
}

}  // namespace omui::agent
