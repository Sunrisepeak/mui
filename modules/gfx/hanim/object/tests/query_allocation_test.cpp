import std;
import hanim.object;
import openmui.trace;
import openmui.testing;

using namespace hanim;
using namespace omui::testing;

namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main(int argc, char** argv) {
    Suite s{"hanim.object.query_allocations"};
    s.test("warm capture named/ref/resource queries allocate no scalar or array storage even while tracing", [] {
        omui::trace::start();
        const std::array edges{ShapeSegment::line({1, 0})};
        auto contour = Contour::copy({{}, edges, false}); check(contour.has_value());
        auto registry = ObjectRegistry::make(); check(registry.has_value());
        const auto object = registry->create({{{.id = "long-stable-part-key-not-a-display-label", .contours = {*contour}}}}); check(object.has_value());
        auto warm = registry->capture(*object); check(warm.has_value());
        const auto part = warm->find("long-stable-part-key-not-a-display-label"); check(part.has_value());
        const auto proof_before = allocations.load(std::memory_order_relaxed);
        auto* scalar = ::operator new(1); auto* array = ::operator new[](1);
        ::operator delete(scalar); ::operator delete[](array);
        check(allocations.load(std::memory_order_relaxed) - proof_before == 2, "both allocation entry points are observed");
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
        const auto trace = omui::trace::stop();
        check(valid && consumed == 2000);
        check(after == before, std::format("{} scalar/array allocations across 2000 complete capture/query iterations", after - before));
        check(trace.dropped == 0);
        bool captured = false, created = false, owner_counter = false;
        for (const auto& event : trace.events) {
            if (std::string_view(event.category) != "hanim.object") continue;
            captured |= event.kind == omui::trace::Kind::begin && std::string_view(event.name) == "capture";
            created |= event.kind == omui::trace::Kind::begin && std::string_view(event.name) == "create";
            owner_counter |= event.kind == omui::trace::Kind::counter && std::string_view(event.name) == "owner.low" && event.value == static_cast<double>(object->owner);
        }
        check(captured && created && owner_counter, "existing trace infrastructure records object operations and exact owner halves");
    });
    return s.run(argc, argv);
}
