// xengine.world:chunked — a world too large to copy whole: entities by archetype in chunks shared until written
// (the foundation plan's §7, M6.4; D-169: the world is a value, a copy is a branch).
//
//     xengine::Chunked<Position, Velocity, Health> world;
//     const auto hero = world.create();                  // 1, 2, 3 …: never taken again, the same every run
//     world.add(hero, Position{0, 0}), world.add(hero, Velocity{1, 0});
//     world.each<Position, const Velocity>([](auto id, Position& at, const Velocity& by) { at.x += by.x; });
//     const auto branch = world;                         // a pointer a chunk: a world of a million branches at once
//     world.changed_since(branch);                       // the chunks either has written since: what to compare
//
// Entities with the same components are an archetype; an archetype's entities are in chunks of `rows`, each
// component's values side by side. A chunk is shared between copies of the world until one of them writes to it,
// which copies that chunk alone (and the directory page that says where its entities are). So a snapshot, a
// rewind point or an agent's branch costs a pointer a chunk, and two branches say which chunks they differ in
// without looking at a value. Adding or taking a component moves the entity to its new archetype; the last row
// of its old archetype takes its place. Every step follows the order of the calls: the same calls make the same
// world, and `checksum()` says so.
export module xengine.world:chunked;

import std;
import mlibs.hash;
import :component;

export namespace xengine {

template <Component... Components>
class Chunked {
    static_assert(sizeof...(Components) <= 64, "an archetype is a 64-bit set of components");

public:
    using Id = std::uint64_t;  // 0 is none
    static constexpr std::size_t rows = 64;    // entities a chunk
    static constexpr std::size_t page = 1024;  // directory entries a page

    // A new entity with no components.
    Id create() {
        const Id id = ++last_;
        put_row(table_of(0), id);
        ++alive_;
        return id;
    }
    // Gone, its components with it. False where it is not (any more).
    bool destroy(Id id) {
        const Location at = where(id);
        if (!at.alive) return false;
        take_row(at);
        place(id, {});
        --alive_;
        return true;
    }
    [[nodiscard]] bool alive(Id id) const noexcept { return where(id).alive; }
    [[nodiscard]] std::size_t size() const noexcept { return alive_; }

    template <class Wanted>
    [[nodiscard]] bool has(Id id) const noexcept {
        const Location at = where(id);
        return at.alive && (tables_[at.table].mask & bit<Wanted>());
    }
    // The component given (set where it has one already). False where the entity is not.
    template <class Given>
    bool add(Id id, const Given& value) {
        const Location at = where(id);
        if (!at.alive) return false;
        const std::uint64_t mask = tables_[at.table].mask;
        const Location now = mask & bit<Given>() ? at : move_to(at, mask | bit<Given>());
        *writable<Given>(now) = value;
        return true;
    }
    template <class Taken>
    bool remove(Id id) {
        const Location at = where(id);
        if (!at.alive || !(tables_[at.table].mask & bit<Taken>())) return false;
        move_to(at, tables_[at.table].mask & ~bit<Taken>());
        return true;
    }
    template <class Wanted>
    [[nodiscard]] const Wanted* get(Id id) const noexcept {
        const Location at = where(id);
        if (!at.alive || !(tables_[at.table].mask & bit<Wanted>())) return nullptr;
        return &column<Wanted>(*tables_[at.table].chunks[at.chunk])[at.row];
    }
    // To write: its chunk made this world's own first.
    template <class Wanted>
    [[nodiscard]] Wanted* edit(Id id) {
        const Location at = where(id);
        if (!at.alive || !(tables_[at.table].mask & bit<Wanted>())) return nullptr;
        return writable<Wanted>(at);
    }

    // Every entity that has all of `Wanted`, archetype by archetype (in the order first made), chunk by chunk,
    // row by row: `visit(id, Wanted&...)`. Where every one of `Wanted` is `const`, nothing is copied; otherwise
    // each chunk visited is made this world's own once, however many of its rows are written.
    template <class... Wanted, class Visit>
    void each(Visit&& visit) {
        constexpr std::uint64_t needed = (bit<std::remove_const_t<Wanted>>() | ... | 0);
        constexpr bool writes = (!std::is_const_v<Wanted> || ...);
        for (Table& table : tables_) {
            if ((table.mask & needed) != needed) continue;
            for (std::shared_ptr<Chunk>& held : table.chunks) {
                if constexpr (writes) own(held);
                Chunk& chunk = *held;
                for (std::uint32_t row = 0; row < chunk.count; ++row)
                    visit(chunk.ids[row], column<std::remove_const_t<Wanted>>(chunk)[row]...);
            }
        }
    }
    template <class... Wanted, class Visit>
    void each(Visit&& visit) const {
        constexpr std::uint64_t needed = (bit<std::remove_const_t<Wanted>>() | ... | 0);
        for (const Table& table : tables_) {
            if ((table.mask & needed) != needed) continue;
            for (const std::shared_ptr<Chunk>& held : table.chunks) {
                const Chunk& chunk = *held;
                for (std::uint32_t row = 0; row < chunk.count; ++row)
                    visit(chunk.ids[row], column<std::remove_const_t<Wanted>>(chunk)[row]...);
            }
        }
    }

    // What two copies of one world share and what they do not: chunks and directory pages that are the same
    // memory, and those that are this world's alone (written since they were one, or new).
    struct Sharing {
        std::size_t chunks_shared = 0, chunks_own = 0;
        std::size_t pages_shared = 0, pages_own = 0;
    };
    [[nodiscard]] Sharing changed_since(const Chunked& other) const {
        Sharing out;
        std::vector<const Chunk*> theirs;
        for (const Table& table : other.tables_)
            for (const auto& chunk : table.chunks) theirs.push_back(chunk.get());
        std::ranges::sort(theirs);
        for (const Table& table : tables_)
            for (const auto& chunk : table.chunks)
                ++(std::ranges::binary_search(theirs, chunk.get()) ? out.chunks_shared : out.chunks_own);
        for (std::size_t index = 0; index < directory_.size(); ++index)
            ++(index < other.directory_.size() && directory_[index] == other.directory_[index] ? out.pages_shared : out.pages_own);
        return out;
    }
    [[nodiscard]] std::size_t chunks() const noexcept {
        std::size_t count = 0;
        for (const Table& table : tables_) count += table.chunks.size();
        return count;
    }
    // Every entity's id and components in the world's order, and the ids made so far, hashed: the same world,
    // the same number, everywhere.
    [[nodiscard]] std::uint64_t checksum() const noexcept {
        mlibs::hash::Fnv1a hash;
        const auto bytes = [&hash](const auto* first, std::size_t count) { hash.bytes(std::as_bytes(std::span(first, count))); };
        bytes(&last_, 1), bytes(&alive_, 1);
        for (const Table& table : tables_) {
            bytes(&table.mask, 1);
            for (const auto& held : table.chunks) {
                const Chunk& chunk = *held;
                bytes(chunk.ids.data(), chunk.count);
                ((table.mask & bit<Components>() ? bytes(column<Components>(chunk).data(), chunk.count) : void()), ...);
            }
        }
        return hash.value();
    }

private:
    template <class Value>
    using Values = std::array<Value, rows>;
    struct Chunk {
        std::uint32_t                                      count = 0;
        std::array<Id, rows>                               ids{};
        std::tuple<std::shared_ptr<Values<Components>>...> columns;  // none where the archetype has not that one
    };
    struct Table {
        std::uint64_t                       mask = 0;
        std::vector<std::shared_ptr<Chunk>> chunks;
    };
    struct Location {
        std::uint32_t table = 0, chunk = 0, row = 0;
        bool          alive = false;
    };
    using Page = std::array<Location, page>;

    template <class Wanted>
    static constexpr std::uint64_t bit() noexcept {
        constexpr std::size_t index = [] {
            std::size_t at = 0, found = sizeof...(Components);
            ((std::is_same_v<Wanted, Components> ? void(found = at) : void(), ++at), ...);
            return found;
        }();
        static_assert(index < sizeof...(Components), "a component this world does not hold");
        return std::uint64_t{1} << index;
    }
    template <class Wanted>
    static Values<Wanted>& column(Chunk& chunk) noexcept { return *std::get<std::shared_ptr<Values<Wanted>>>(chunk.columns); }
    template <class Wanted>
    static const Values<Wanted>& column(const Chunk& chunk) noexcept { return *std::get<std::shared_ptr<Values<Wanted>>>(chunk.columns); }

    [[nodiscard]] Location where(Id id) const noexcept {
        if (id == 0 || id > last_) return {};
        const std::size_t index = static_cast<std::size_t>(id - 1);
        if (index / page >= directory_.size()) return {};
        const auto& held = directory_[index / page];
        return held ? (*held)[index % page] : Location{};
    }
    void place(Id id, Location at) {
        const std::size_t index = static_cast<std::size_t>(id - 1);
        if (directory_.size() <= index / page) directory_.resize(index / page + 1);
        std::shared_ptr<Page>& held = directory_[index / page];
        if (!held) held = std::make_shared<Page>();
        else if (held.use_count() > 1) held = std::make_shared<Page>(*held);
        (*held)[index % page] = at;
    }
    // A chunk this world alone holds, its columns too.
    static void own(std::shared_ptr<Chunk>& held) {
        if (held.use_count() == 1) return;
        auto copy = std::make_shared<Chunk>(*held);
        const auto apart = [](auto& values) {
            if (values) values = std::make_shared<std::remove_cvref_t<decltype(*values)>>(*values);
        };
        std::apply([&](auto&... values) { (apart(values), ...); }, copy->columns);
        held = std::move(copy);
    }
    template <class Wanted>
    Wanted* writable(Location at) {
        std::shared_ptr<Chunk>& held = tables_[at.table].chunks[at.chunk];
        own(held);
        return &column<Wanted>(*held)[at.row];
    }
    std::uint32_t table_of(std::uint64_t mask) {
        for (std::uint32_t index = 0; index < tables_.size(); ++index)
            if (tables_[index].mask == mask) return index;
        tables_.push_back({mask, {}});
        return static_cast<std::uint32_t>(tables_.size() - 1);
    }
    // A row after the table's last (in a new chunk where that one is full), said in the directory.
    Location put_row(std::uint32_t table_index, Id id) {
        Table& table = tables_[table_index];
        if (table.chunks.empty() || table.chunks.back()->count == rows) {
            auto chunk = std::make_shared<Chunk>();
            ((table.mask & bit<Components>() ? void(std::get<std::shared_ptr<Values<Components>>>(chunk->columns) = std::make_shared<Values<Components>>()) : void()), ...);
            table.chunks.push_back(std::move(chunk));
        }
        std::shared_ptr<Chunk>& held = table.chunks.back();
        own(held);
        const std::uint32_t row = held->count++;
        held->ids[row] = id;
        const Location at{table_index, static_cast<std::uint32_t>(table.chunks.size() - 1), row, true};
        place(id, at);
        return at;
    }
    // The row taken out: the table's last row moves into it (and the directory says so).
    void take_row(Location at) {
        Table& table = tables_[at.table];
        std::shared_ptr<Chunk>& last = table.chunks.back();
        std::shared_ptr<Chunk>& hole = table.chunks[at.chunk];
        own(last), own(hole);
        const std::uint32_t last_row = last->count - 1;
        if (hole != last || at.row != last_row) {
            hole->ids[at.row] = last->ids[last_row];
            ((table.mask & bit<Components>() ? void(column<Components>(*hole)[at.row] = column<Components>(*last)[last_row]) : void()), ...);
            place(hole->ids[at.row], {at.table, at.chunk, at.row, true});
        }
        if (--last->count == 0) table.chunks.pop_back();
    }
    // The entity at `from` to the archetype of `mask`, keeping the components both have; where it is now.
    Location move_to(Location from, std::uint64_t mask) {
        const Id id = tables_[from.table].chunks[from.chunk]->ids[from.row];
        const std::uint32_t target = table_of(mask);  // may grow tables_: indices from here, not references
        const Location to = put_row(target, id);
        const std::uint64_t kept = tables_[from.table].mask & mask;
        const Chunk& source = *tables_[from.table].chunks[from.chunk];
        Chunk& destination = *tables_[to.table].chunks[to.chunk];
        ((kept & bit<Components>() ? void(column<Components>(destination)[to.row] = column<Components>(source)[from.row]) : void()), ...);
        take_row(from);  // another table: `to` stays where it is
        return to;
    }

    std::vector<Table>                 tables_;
    std::vector<std::shared_ptr<Page>> directory_;
    Id                                 last_ = 0;
    std::size_t                        alive_ = 0;
};

}  // namespace xengine
