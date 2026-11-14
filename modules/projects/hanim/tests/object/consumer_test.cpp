// An unrelated native host consumes only the public object/core API.
// No UI Runtime, renderer, application model callback or Engine is implied.
import std;
import hanim.object;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;

namespace {
constexpr double k = 0.5522847498307936;
struct HostShape {
    Vec2 start{0, -1};
    std::vector<ShapeSegment> edges;
    Transform2D transform;
    Rgba color;
};
struct HostAdapter {
    ObjectRegistry& objects;
    ObjectRef ref;
    int publications = 0;
    std::expected<ObjectRef, ObjectError> publish(const HostShape& source) {
        auto contour = Contour::copy({source.start, source.edges, true});
        if (!contour) return std::unexpected(contour.error());
        ObjectDescription description{{{.id = "group"}, {.id = "outline", .parent = "group", .local = source.transform,
            .contours = {*contour}, .fill = source.color}}};
        if (ref) {
            auto changed = objects.update(ref, description);
            if (!changed) return std::unexpected(changed.error());
        } else {
            auto created = objects.create(description);
            if (!created) return std::unexpected(created.error());
            ref = *created;
        }
        ++publications;
        return ref;
    }
};
bool same(const ShapeCubic& a, const ShapeCubic& b) {
    return a.start == b.start && a.control1 == b.control1 && a.control2 == b.control2 && a.end == b.end;
}
}

UTest cases{"hanim.object.consumer", [] {
    "a synthetic host captures named endpoints and prepares one real triangle-to-circle contour"_test = [] {
        auto registry = ObjectRegistry::make(); expect(registry.has_value()); if (!registry) return;
        HostAdapter adapter{*registry};
        HostShape host{.edges = {ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})}, .color = {1, 0, 0, 1}};
        const auto object = adapter.publish(host); expect(object.has_value());
        auto a = registry->capture(*object); expect(a.has_value());
        const auto outline = a->find("outline"); expect(outline.has_value());
        host.edges = {ShapeSegment::cubic({k, -1}, {1, -k}, {1, 0}), ShapeSegment::cubic({1, k}, {k, 1}, {0, 1}),
            ShapeSegment::cubic({-k, 1}, {-1, k}, {-1, 0}), ShapeSegment::cubic({-1, -k}, {-k, -1}, {0, -1})};
        host.transform.tx = 100; host.color = {0, 0, 1, 1};
        expect(adapter.publish(host) == object);
        auto b = registry->capture(*object); expect(b.has_value());
        expect(a->revision() == 1 && b->revision() == 2 && b->find("outline") == outline);
        expect(a->require_capability("hanim.contours").has_value() && b->require_capability("hanim.solid-fill").has_value());
        const auto* from = *a->get(*outline); const auto* to = *b->get(*outline);
        const auto av = from->contours.front().view(), bv = to->contours.front().view(); expect(av && bv);
        auto plan = PreparedShapeMorph::prepare(*av, *bv); expect(plan.has_value()); if (!plan) return;
        std::vector<ShapeCubic> start(plan->segment_count()), middle(start.size()), finish(start.size()), again(start.size());
        expect(plan->sample(0, start).has_value() && plan->sample(0.5, middle).has_value() && plan->sample(1, finish).has_value());
        bool changed = false;
        for (std::size_t i = 0; i < middle.size(); ++i) {
            changed |= !same(middle[i], start[i]) && !same(middle[i], finish[i]);
            expect(middle[i].end == middle[(i + 1) % middle.size()].start) << "the midpoint is one connected current contour";
        }
        expect(changed && middle.size() >= 4) << "this consumes a prepared geometric correspondence, not two endpoint copies";
        const auto transform = lerp(from->local, to->local, 0.5);
        const auto color = lerp(*from->fill, *to->fill, 0.5);
        expect(transform.tx == 50 && color.a == 1 && color != *from->fill && color != *to->fill);
        expect(adapter.publications == 2) << "sampling does not call the host's publishing function";
        host.edges.clear(); host.edges.shrink_to_fit();
        expect(registry->destroy(*object).has_value());
        // The endpoint captures can be released too: the core prepared plan
        // has already copied the geometry it needs into owned coefficients.
        a = Snapshot{}; b = Snapshot{};
        for (double progress : {1.0, 0.0, 0.25, 0.75, 0.5}) expect(plan->sample(progress, again).has_value());
        for (std::size_t i = 0; i < middle.size(); ++i) expect(same(middle[i], again[i])) << "replayed midpoint survives host/object/capture resource release";
        expect(adapter.publications == 2);
    };
}};
