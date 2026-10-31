// mgame.world — entities and their components as one block of plain data.
//
//     mgame::World<256, Position, Velocity, Brick> w;
//     const mgame::Entity e = w.create();
//     w.add(e, Position{…});
//     w.each<Position, Velocity>([](mgame::Entity, Position& p, Velocity& v) { … });   // by index
//     const auto saved = w;                                                          // a snapshot
//
// A column of N values for each component type, and a bit for each entity
// saying whether it has one. Entities are an index and a generation: a
// destroyed entity's index is taken again by a later one, whose generation
// differs, so an old handle no longer reaches it. A new entity takes the
// lowest free index, and iteration goes by index, so the same calls make the
// same world everywhere. Components are plain data without padding
// (has_unique_object_representations, or `no_padding` declared by a type of
// doubles), so the world is trivially copyable and its checksum is a hash of
// its bytes.
export module mgame.world;

import std;
import openmui.hash;

export namespace mgame {

struct Entity {
    std::uint32_t index = 0xffffffffu;
    std::uint32_t generation = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffffffu; }
    friend constexpr bool operator==(Entity a, Entity b) noexcept { return a.index == b.index && a.generation == b.generation; }
};

// Plain data with no padding: its bytes are its value, so a checksum of them
// is the same for the same value. A type with doubles (whose representations
// are not unique to the standard: ±0) says so itself with
// `static constexpr bool no_padding = true;` — its author vouches for it.
template <class C>
concept Component = std::is_trivially_copyable_v<C> && std::is_default_constructible_v<C> &&
                    (std::has_unique_object_representations_v<C> || requires { requires C::no_padding; });

namespace detail {

template <std::size_t N>
struct Bits {
    std::array<std::uint64_t, (N + 63) / 64> words{};
    [[nodiscard]] constexpr bool test(std::size_t i) const noexcept { return (words[i / 64] >> (i % 64)) & 1u; }
    constexpr void set(std::size_t i) noexcept { words[i / 64] |= std::uint64_t{1} << (i % 64); }
    constexpr void clear(std::size_t i) noexcept { words[i / 64] &= ~(std::uint64_t{1} << (i % 64)); }
};

template <Component C, std::size_t N>
struct Column {
    std::array<C, N> values{};
    Bits<N>          present{};
};

template <std::size_t N, class... Cs>
struct Columns : Column<Cs, N>... {};

}  // namespace detail

template <std::size_t N, Component... Cs>
class World {
    static_assert(N > 0 && N < 0xffffffffu, "a world holds 1 to 2^32 − 2 entities");

public:
    static constexpr std::size_t capacity = N;

    // A new entity: the lowest free index (none past capacity: an invalid one).
    [[nodiscard]] Entity create() noexcept {
        for (std::size_t w = 0; w < alive_.words.size(); ++w)
            if (const std::uint64_t free = ~alive_.words[w]; free != 0) {
                const std::size_t i = w * 64 + static_cast<std::size_t>(std::countr_zero(free));
                if (i >= N) break;
                alive_.set(i);
                ++count_;
                return {static_cast<std::uint32_t>(i), generation_[i]};
            }
        return {};
    }
    // Gone with its components; its index free for a later entity of the next generation.
    void destroy(Entity e) noexcept {
        if (!alive(e)) return;
        (column<Cs>().present.clear(e.index), ...);
        (void(column<Cs>().values[e.index] = Cs{}), ...);
        alive_.clear(e.index);
        ++generation_[e.index];
        --count_;
    }
    [[nodiscard]] bool alive(Entity e) const noexcept { return e.index < N && alive_.test(e.index) && generation_[e.index] == e.generation; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    // A component set on a living entity: the value in its column. A
    // destroyed entity's handle, or a stale one whose index another entity
    // now holds, changes nothing (nullptr; false).
    template <class C>
    C* add(Entity e, C value = {}) noexcept {
        if (!alive(e)) return nullptr;
        auto& c = column<C>();
        c.present.set(e.index);
        return &(c.values[e.index] = value);
    }
    template <class C>
    bool remove(Entity e) noexcept {
        if (!alive(e)) return false;
        auto& c = column<C>();
        c.present.clear(e.index);
        c.values[e.index] = C{};
        return true;
    }
    template <class C>
    [[nodiscard]] bool has(Entity e) const noexcept { return alive(e) && column<C>().present.test(e.index); }
    template <class C>
    [[nodiscard]] C* find(Entity e) noexcept { return has<C>(e) ? &column<C>().values[e.index] : nullptr; }
    template <class C>
    [[nodiscard]] const C* find(Entity e) const noexcept { return has<C>(e) ? &column<C>().values[e.index] : nullptr; }
    template <class C>
    [[nodiscard]] C& get(Entity e) noexcept { return column<C>().values[e.index]; }
    template <class C>
    [[nodiscard]] const C& get(Entity e) const noexcept { return column<C>().values[e.index]; }

    // Every living entity with all of Qs, by index: f(Entity, Qs&...).
    template <class... Qs, class F>
    void each(F&& f) {
        for (std::size_t w = 0; w < alive_.words.size(); ++w) {
            std::uint64_t m = alive_.words[w];
            ((m &= column<Qs>().present.words[w]), ...);
            while (m != 0) {
                const std::size_t i = w * 64 + static_cast<std::size_t>(std::countr_zero(m));
                m &= m - 1;
                f(Entity{static_cast<std::uint32_t>(i), generation_[i]}, column<Qs>().values[i]...);
            }
        }
    }
    template <class... Qs, class F>
    void each(F&& f) const {
        for (std::size_t w = 0; w < alive_.words.size(); ++w) {
            std::uint64_t m = alive_.words[w];
            ((m &= column<Qs>().present.words[w]), ...);
            while (m != 0) {
                const std::size_t i = w * 64 + static_cast<std::size_t>(std::countr_zero(m));
                m &= m - 1;
                f(Entity{static_cast<std::uint32_t>(i), generation_[i]}, column<Qs>().values[i]...);
            }
        }
    }

    // FNV-1a over the world's bytes: the same world, the same number, everywhere.
    [[nodiscard]] std::uint64_t checksum() const noexcept {
        omui::hash::Fnv1a h;
        auto bytes = [&h](const auto& v) { h.bytes(std::as_bytes(std::span(&v, 1))); };
        // Part by part: each part has no padding of its own (a column's values and its bits may have some between them).
        bytes(alive_);
        bytes(generation_);
        bytes(count_);
        ((bytes(column<Cs>().values), bytes(column<Cs>().present)), ...);
        return h.value();
    }

private:
    template <class C>
    [[nodiscard]] detail::Column<C, N>& column() noexcept { return static_cast<detail::Column<C, N>&>(columns_); }
    template <class C>
    [[nodiscard]] const detail::Column<C, N>& column() const noexcept { return static_cast<const detail::Column<C, N>&>(columns_); }

    detail::Bits<N>                   alive_{};
    std::array<std::uint32_t, N>      generation_{};
    std::uint64_t                     count_ = 0;
    detail::Columns<N, Cs...>         columns_{};
};

}  // namespace mgame
