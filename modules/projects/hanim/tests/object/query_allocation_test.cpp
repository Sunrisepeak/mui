import std;
import hanim.object;
import mlibs.trace;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;

namespace {
std::atomic<std::size_t> allocations{0};
void* allocate(std::size_t size) noexcept {
    allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}
void* allocate_aligned(std::size_t size, std::align_val_t alignment) noexcept {
    allocations.fetch_add(1, std::memory_order_relaxed);
    const auto align = static_cast<std::size_t>(alignment);
    if (!align || (align & (align - 1))) return nullptr;
    const auto overhead = align - 1 + sizeof(void*);
    if (size > std::numeric_limits<std::size_t>::max() - overhead) return nullptr;
    auto* storage = std::malloc((size ? size : 1) + overhead);
    if (!storage) return nullptr;
    auto address = reinterpret_cast<std::uintptr_t>(storage) + sizeof(void*);
    address = (address + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    auto* aligned = reinterpret_cast<void*>(address);
    static_cast<void**>(aligned)[-1] = storage;
    return aligned;
}
void free_aligned(void* pointer) noexcept {
    if (pointer) std::free(static_cast<void**>(pointer)[-1]);
}
void* require(void* pointer) noexcept {
    if (!pointer) std::abort();
    return pointer;
}
}
// Replace every allocation family as a pair. In particular ASan's default
// nothrow new must not allocate a block that our free-based delete releases.
void* operator new(std::size_t size) { return require(allocate(size)); }
void* operator new[](std::size_t size) { return require(allocate(size)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return require(allocate_aligned(size, alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return require(allocate_aligned(size, alignment)); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate_aligned(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate_aligned(size, alignment); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { free_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { free_aligned(pointer); }

int main(int argc, char** argv) {
    Suite s{"hanim.object.query_allocations"};
    s.test("warm capture named/ref/resource queries allocate no scalar or array storage even while tracing", [] {
        mlibs::trace::start();
        const std::array edges{ShapeSegment::line({1, 0})};
        auto contour = Contour::copy({{}, edges, false}); check(contour.has_value());
        auto registry = ObjectRegistry::make(); check(registry.has_value());
        const auto object = registry->create({{{.id = "long-stable-part-key-not-a-display-label", .contours = {*contour}}}}); check(object.has_value());
        auto warm = registry->capture(*object); check(warm.has_value());
        const auto part = warm->find("long-stable-part-key-not-a-display-label"); check(part.has_value());
        const auto proof_before = allocations.load(std::memory_order_relaxed);
        ::operator delete(::operator new(1));
        ::operator delete[](::operator new[](1));
        ::operator delete(::operator new(1), std::size_t{1});
        ::operator delete[](::operator new[](1), std::size_t{1});
        ::operator delete(::operator new(1, std::nothrow), std::nothrow);
        ::operator delete[](::operator new[](1, std::nothrow), std::nothrow);
        constexpr auto aligned = std::align_val_t{64};
        auto* scalar = ::operator new(1, aligned);
        const bool alignment_correct = reinterpret_cast<std::uintptr_t>(scalar) % 64 == 0;
        ::operator delete(scalar, aligned);
        ::operator delete[](::operator new[](1, aligned), aligned);
        ::operator delete(::operator new(1, aligned), std::size_t{1}, aligned);
        ::operator delete[](::operator new[](1, aligned), std::size_t{1}, aligned);
        ::operator delete(::operator new(1, aligned, std::nothrow), aligned, std::nothrow);
        ::operator delete[](::operator new[](1, aligned, std::nothrow), aligned, std::nothrow);
        check(alignment_correct && allocations.load(std::memory_order_relaxed) - proof_before == 12,
            "scalar/array, throwing/nothrow and aligned allocation families are observed and paired");
        bool valid = true;
        double consumed = 0;
        const auto before = allocations.load(std::memory_order_relaxed);
        for (int i = 0; i < 2000; ++i) {
            auto snapshot = registry->capture(*object);
            if (!snapshot) { valid = false; continue; }
            const auto named = snapshot->find("long-stable-part-key-not-a-display-label");
            if (!named) { valid = false; continue; }
            const auto payload = snapshot->get(*named);
            if (!payload) { valid = false; continue; }
            const auto shape = (*payload)->contours.front().view();
            if (!shape) { valid = false; continue; }
            valid &= *named == *part && snapshot->revision() == 1 && registry->contains(*named) &&
                snapshot->require_capability("hanim.contours").has_value() && snapshot->capability_version("hanim.pixels") == 0;
            consumed += shape->segments.front().end.x;
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        const auto trace = mlibs::trace::stop();
        check(valid && consumed == 2000);
        check(after == before, std::format("{} scalar/array allocations across 2000 complete capture/query iterations", after - before));
        check(trace.dropped == 0);
        bool captured = false, created = false, owner_counter = false;
        for (const auto& event : trace.events) {
            if (std::string_view(event.category) != "hanim.object") continue;
            captured |= event.kind == mlibs::trace::Kind::begin && std::string_view(event.name) == "capture";
            created |= event.kind == mlibs::trace::Kind::begin && std::string_view(event.name) == "create";
            owner_counter |= event.kind == mlibs::trace::Kind::counter && std::string_view(event.name) == "owner.low" && event.value == static_cast<double>(object->owner);
        }
        check(captured && created && owner_counter, "existing trace infrastructure records object operations and exact owner halves");
    });
    return s.run(argc, argv);
}
