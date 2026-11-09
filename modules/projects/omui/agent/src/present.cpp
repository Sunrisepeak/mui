// An application drawn in another process's window (omui.agent:present).
module omui.agent;

import std;
import mcpplibs.os;
import omui.core;
import omui.view;
import xrender.vector;
import xrender.raster;
import omui.semantic;
import mlibs.text;
import mlibs.hash;

namespace omui::agent {

namespace {

// A message: its length (u32, the kind and the bytes), its kind, its bytes.
enum class Kind : char {
    frame = 'F', image = 'I', font = 'T', input = 'E', size = 'S', ask_state = 'Q', state = 'A', restore = 'R', ask_places = 'P', places = 'L',
    faces_known = 'K', face = 'B'
};

// A face's identity: its bytes' hash (the same file on both sides is the same face).
std::uint64_t identity_of(const mlibs::text::FontCollection& fonts, mlibs::text::FaceId face) {
    return mlibs::hash::fnv1a(std::as_bytes(fonts.face_data(face)));
}

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

// Whether two names of a source file name the same one: equal, or one the other's
// trailing components (separators either way).
bool same_source(std::string_view one, std::string_view other) {
    std::string left(one), right(other);
    std::ranges::replace(left, '\\', '/');
    std::ranges::replace(right, '\\', '/');
    if (left.size() < right.size()) std::swap(left, right);
    if (right.empty()) return false;
    return left == right || (left.ends_with(right) && left[left.size() - right.size() - 1] == '/');
}

}  // namespace

NodeId built_near(const Runtime& runtime, std::string_view file, int line) {
    NodeId best{};
    std::uint32_t best_line = 0;
    runtime.walk([&](NodeId node, int) {
        const std::source_location where = runtime.origin(node);
        if (where.line() == 0 || static_cast<int>(where.line()) > line || where.line() <= best_line) return;
        if (!same_source(where.file_name(), file)) return;
        const Rect drawn = runtime.drawn_bounds(node);
        if (drawn.width <= 0 || drawn.height <= 0) return;
        best = node;
        best_line = where.line();
    });
    return best;
}

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
    // The fonts it names, by what they are (face, weight, slant), once each: references are interned per
    // process. A face the host lacks (the application's own) goes first, its bytes once — after the
    // host has said which it has; before that, each is described again with every frame (the host's
    // own faces drawn at once, the application's when it is sent) and no bytes go.
    read_available();  // what the host has said it has
    std::set<std::uint32_t> described_now;
    for (const auto& command : frame.list->commands())
        if (const auto* glyphs = std::get_if<xrender::DrawGlyphs>(&command);
            glyphs && glyphs->font.id && !fonts_sent_.contains(glyphs->font.id) && described_now.insert(glyphs->font.id).second) {
            if (host_said_) fonts_sent_.insert(glyphs->font.id);
            const mlibs::text::FontRef font{glyphs->font.id};
            const mlibs::text::FaceId face = runtime_.fonts().face_of(font);
            auto known = identities_.find(face);
            if (known == identities_.end()) known = identities_.emplace(face, identity_of(runtime_.fonts(), face)).first;
            const std::uint64_t identity = known->second;
            if (host_said_ && !host_faces_.contains(identity) && faces_sent_.insert(identity).second) {
                const auto data = runtime_.fonts().face_data(face);
                std::string bytes;
                put64(bytes, identity);
                bytes.append(reinterpret_cast<const char*>(data.data()), data.size());
                out += message(Kind::face, bytes);
            }
            std::string described;
            put32(described, glyphs->font.id);
            put64(described, identity);
            put32(described, std::bit_cast<std::uint32_t>(runtime_.fonts().weight_of(font)));
            described.push_back(static_cast<char>(runtime_.fonts().slant_of(font)));
            out += message(Kind::font, described);
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
        } else if (next->first == Kind::ask_places) {
            Value::List answer;
            if (const auto asked = parse_json(next->second); asked && asked->list())
                for (const Value& place : *asked->list()) {
                    const auto* file = place.get("file");
                    const auto* line = place.get("line");
                    const NodeId found = file && line ? built_near(runtime_, file->as_string(), static_cast<int>(line->as_int())) : NodeId{};
                    if (!found) {
                        answer.emplace_back();
                        continue;
                    }
                    const Rect drawn = runtime_.drawn_bounds(found);
                    answer.push_back(Value{}
                                         .set("x", static_cast<double>(drawn.x))
                                         .set("y", static_cast<double>(drawn.y))
                                         .set("width", static_cast<double>(drawn.width))
                                         .set("height", static_cast<double>(drawn.height))
                                         .set("path", runtime_.path(found)));
                }
            (void)connection_.write(message(Kind::places, Value(std::move(answer)).json()));
        } else if (next->first == Kind::faces_known && next->second.size() >= 4) {
            const std::string_view bytes = next->second;
            const auto count = static_cast<std::size_t>(get(bytes, 0, 4));
            for (std::size_t index = 0; index < count && 4 + 8 * (index + 1) <= bytes.size(); ++index) host_faces_.insert(get(bytes, 4 + 8 * index, 8));
            host_said_ = true;
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

Presentation::Presentation(mcpplibs::os::ipc::connection connection, mlibs::text::FontCollection* fonts)
    : connection_(std::move(connection)), fonts_(fonts) {
    if (!fonts_) return;
    // The faces this side has, by identity: the application sends only those it lacks.
    std::string known;
    put32(known, static_cast<std::uint32_t>(fonts_->face_count()));
    for (std::size_t face = 0; face < fonts_->face_count(); ++face) {
        const std::uint64_t identity = identity_of(*fonts_, static_cast<mlibs::text::FaceId>(face));
        faces_[identity] = static_cast<std::uint32_t>(face);
        put64(known, identity);
    }
    (void)connection_.write(message(Kind::faces_known, known));
}

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
            } else if (next->first == Kind::face && next->second.size() > 8) {
                // A face the application has and this side had not: added, once.
                const std::string_view bytes = next->second;
                const std::uint64_t identity = get(bytes, 0, 8);
                if (fonts_ && !faces_.contains(identity))
                    if (auto added = fonts_->add_data(std::vector<std::uint8_t>(bytes.begin() + 8, bytes.end())))
                        faces_[identity] = static_cast<std::uint32_t>(*added);
            } else if (next->first == Kind::font && next->second.size() == 17) {
                const std::string_view bytes = next->second;
                if (fonts_)
                    if (const auto face = faces_.find(get(bytes, 4, 8)); face != faces_.end()) {
                        const auto weight = std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 12, 4)));
                        const auto slant = static_cast<mlibs::text::Slant>(bytes[16]);
                        font_map_[static_cast<std::uint32_t>(get(bytes, 0, 4))] = fonts_->font(static_cast<mlibs::text::FaceId>(face->second), weight, slant).id;
                    }
            } else if (next->first == Kind::places) {
                if (auto places = parse_json(next->second)) places_ = std::move(*places);
            } else if (next->first == Kind::state) {
                if (auto state = parse_json(next->second)) state_ = std::move(*state);
            } else if (next->first == Kind::frame) {
                xrender::DisplayList list;
                xrender::Remap remap;
                if (fonts_)
                    remap.font = [this](std::uint32_t id) {
                        const auto found = font_map_.find(id);
                        return found == font_map_.end() ? 0u : found->second;  // a font never described: none drawn
                    };
                if (xrender::decode(next->second, list, remap)) {
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
            // Asked again later (a signal came between, nothing yet); else the application is gone.
            if (chunk.error().code == mcpplibs::os::errc::again || chunk.error().code == mcpplibs::os::errc::timed_out) return nullptr;
            mlibs::log::info("present: the application's connection ended: {}", chunk.error().message());
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

bool Presentation::ask_places(const Value& places) {
    places_.reset();
    return connection_.write(message(Kind::ask_places, places.json())).has_value();
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
