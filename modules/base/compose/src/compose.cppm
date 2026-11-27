// mlibs.compose — what a program is made of, chosen at its root (D-238).
//
// A slot is a contract others may provide at: typed, versioned, declared once
// by its owner as a constant. A provider is an implementation of it — a table
// of functions, a stage, a pack of data — in any package. A composition is
// the value that says which providers a program installed; the program's root
// (an application, an SDK, a tool, a test) installs them by name, and nothing
// installs itself.
//
//     // the owner (omui.accessibility)
//     struct Service { std::string_view name; Attach* attach; };
//     inline constexpr mlibs::compose::Slot<Service> service_slot{{
//         .id = "omui.accessibility.service", .summary = "the platform's assistive-technology service",
//         .absent = "build with omui.sdk's feature `accessibility-bridge`, or call omui::accessibility::bridge::install()"}};
//
//     // a provider (omui.accessibility.bridge), called by the root
//     void install() { static const Service service{…}; (void)mlibs::compose::process().install(service_slot, service, "accesskit"); }
//
//     // the owner, where it prepares (never per node or per pixel)
//     if (const Service* service = mlibs::compose::process().get(service_slot)) … else return std::unexpected(process().missing(service_slot.info));
//
// Absent is unsupported: a provider left out of a build, one never installed,
// and a platform that has none are one state, read on one path, and the
// failure says the way out. What is not installed is not named, so the linker
// leaves it out of the program — a provider's table is therefore made when it
// is installed, never initialised at program start, and holds only what the
// owner calls.
//
// A composition is a value: a process has a default one (`process`), and a
// test, a second world or a tool may make its own. Frozen, it is read without
// a lock.
export module mlibs.compose;

import std;
import mlibs.error;

export namespace mlibs::compose {

// How many providers a slot takes: one (a service), or many (decoders, components, packs), in the order installed.
enum class Cardinality : std::uint8_t { one, many };

// What an owner says of its slot. `id` is `<owner>.<name>`; `absent` is the way out a program without a
// provider is told — what to depend on, build with or call.
struct SlotInfo {
    std::string_view id;
    int              version = 1;
    Cardinality      cardinality = Cardinality::one;
    std::string_view summary = {};
    std::string_view absent = {};
};

// A slot whose providers are `Service`s. Declared once, by its owner, as a constant.
template <class Service>
struct Slot {
    SlotInfo info;
};

enum class Errc : std::uint8_t { absent, occupied, frozen };

constexpr mlibs::error::Description describe(Errc code) noexcept {
    using Kind = mlibs::error::Kind;
    switch (code) {
        case Errc::absent:
            return {"compose.absent", Kind::refused, "nothing is installed at this slot",
                    "install a provider of the slot at the program's root; the message says which"};
        case Errc::occupied:
            return {"compose.occupied", Kind::refused, "the slot takes one provider and has one",
                    "install one provider of this slot; `describe` says which is installed"};
        case Errc::frozen:
            return {"compose.frozen", Kind::refused, "the composition is frozen",
                    "install providers at the program's root, before the composition is frozen"};
    }
    return {"compose.?", Kind::internal};
}
using Error = mlibs::error::Error<Errc>;

// Why a slot is empty: nobody said, the program's root left it out, or the platform has no provider.
enum class Absence : std::uint8_t { unsaid, left_out, platform };

[[nodiscard]] constexpr std::string_view name(Absence absence) noexcept {
    switch (absence) {
        case Absence::unsaid: return "unsaid";
        case Absence::left_out: return "left-out";
        case Absence::platform: return "platform";
    }
    return "?";
}

// One slot of a composition, as `describe` says it: what is installed, in order, or why nothing is.
struct Described {
    std::string              slot;
    int                      version = 1;
    Cardinality              cardinality = Cardinality::one;
    std::string              summary;
    std::vector<std::string> providers;
    Absence                  absence = Absence::unsaid;  // meaningful when `providers` is empty
    std::string              way;                        // the way out, when empty
};

class Composition {
public:
    Composition();
    ~Composition();
    Composition(const Composition&) = delete;
    Composition& operator=(const Composition&) = delete;

    // Installs `service` at `slot` as `provider`. `service` outlives the composition (a provider's table is
    // a static of its `install`). Refused: a slot of one that has one, a frozen composition.
    template <class Service>
    std::expected<void, Error> install(const Slot<Service>& slot, const Service& service, std::string_view provider) {
        return install_erased(slot.info, &service, provider);
    }
    // The slot's provider (the first installed), or null: absent.
    template <class Service>
    [[nodiscard]] const Service* get(const Slot<Service>& slot) const noexcept {
        return static_cast<const Service*>(first(slot.info));
    }
    // Every provider of the slot, in the order installed. For an owner to keep where it prepares.
    template <class Service>
    [[nodiscard]] std::vector<const Service*> all(const Slot<Service>& slot) const {
        std::vector<const Service*> out;
        for (const void* service : every(slot.info)) out.push_back(static_cast<const Service*>(service));
        return out;
    }
    // The name the slot's provider was installed by, or "".
    [[nodiscard]] std::string_view provider(const SlotInfo& slot) const noexcept;

    // Says why the slot is empty: the root left it out, or the platform has none. Nothing changes for a
    // slot that has a provider.
    void leave_out(const SlotInfo& slot, Absence why);
    // The failure of a caller that needs the slot: which slot, why it is empty, the way out.
    [[nodiscard]] Error missing(const SlotInfo& slot) const;

    // No provider is installed after this; reads take no lock.
    void freeze() noexcept;
    [[nodiscard]] bool frozen() const noexcept;

    // Every slot this composition was told of — installed at, left out, or asked for and missing — by id.
    [[nodiscard]] std::vector<Described> describe() const;

private:
    std::expected<void, Error> install_erased(const SlotInfo& slot, const void* service, std::string_view provider);
    [[nodiscard]] const void* first(const SlotInfo& slot) const noexcept;
    [[nodiscard]] std::vector<const void*> every(const SlotInfo& slot) const;

    struct State;
    std::unique_ptr<State> state_;
};

// The process's composition: what `main`'s root installed.
[[nodiscard]] Composition& process() noexcept;

}  // namespace mlibs::compose
