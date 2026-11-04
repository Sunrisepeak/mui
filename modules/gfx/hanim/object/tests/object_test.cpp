import std;
import hanim.object;
import openmui.log;
import openmui.trace;
import openmui.testing;
import mcpplibs.os;

using namespace hanim;
using namespace omui::testing;

namespace {
Contour line() {
    const std::array segments{ShapeSegment::line({10, 5})};
    auto result = Contour::copy({{1, 2}, segments, false}); check(result.has_value());
    return std::move(*result);
}
ObjectDescription graph(Contour contour = line()) {
    return {{{.id = "root", .local = {.tx = 12}, .contours = {std::move(contour)}, .fill = Rgba{1, 0, 0, 1}},
             {.id = "child", .parent = "root"}}};
}
}

int main(int argc, char** argv) {
    Suite s{"hanim.object"};
    s.test("capture owns geometry and graph after producer mutation update and destruction", [] {
        Snapshot frozen;
        PartRef root;
        {
            std::vector<ShapeSegment> input{ShapeSegment::line({10, 5})};
            auto contour = Contour::copy({{1, 2}, input, false}); check(contour.has_value());
            input.front().end = {999, 999}; input.clear(); input.shrink_to_fit();
            auto description = graph(*contour);
            auto registry = ObjectRegistry::make(); check(registry.has_value());
            const auto object = registry->create(description); check(object.has_value());
            auto captured = registry->capture(*object); check(captured.has_value());
            frozen = *captured; root = frozen.root();
            description.parts[0].id = "mutated"; description.parts[0].local.tx = 999; description.parts[0].contours.clear();
            auto part = frozen.get(root); check(part.has_value());
            check((*part)->id == "root" && (*part)->local.tx == 12 && (*part)->fill->r == 1);
            auto shape = (*part)->contours.front().view(); check(shape && !shape->closed && shape->segments.front().end == Vec2{10, 5});
            auto replacement = graph(); replacement.parts[0].local.tx = 24;
            check(registry->update(*object, replacement) == 2);
            check(registry->part(*object, "root") == root);
            auto now = registry->capture(*object); check(now && now->revision() == 2);
            check((*now->get(root))->local.tx == 24 && (*frozen.get(root))->local.tx == 12);
            check(registry->destroy(*object).has_value() && !registry->contains(*object));
            check(!registry->capture(*object) && registry->capture(*object).error() == ObjectError::stale_object);
        }
        check(frozen.revision() == 1 && frozen.find("root") == root);
        check((*frozen.get(root))->contours.front().view()->segments.front().end == Vec2{10, 5}, "no producer or registry memory is borrowed");
        if constexpr (!mcpplibs::os::is_emscripten) {
            std::atomic<bool> good{true};
            std::array<std::jthread, 4> readers;
            for (auto& thread : readers) thread = std::jthread([frozen, root, &good] {
                for (int i = 0; i < 1000; ++i) {
                    auto part = frozen.get(root);
                    if (!part || (*part)->local.tx != 12 || (*part)->contours.front().view()->segments.front().end.x != 10) good = false;
                }
            });
            for (auto& thread : readers) thread.join();
            check(good.load(), "readonly leases outlive their registry and support concurrent reads");
        }
    });
    s.test("foreign owner object generation and part generation are independently checked", [] {
        auto a = ObjectRegistry::make({.max_objects = 1}), b = ObjectRegistry::make({.max_objects = 1}); check(a && b);
        const auto ar = a->create(graph()), br = b->create(graph()); check(ar && br);
        const auto ap = a->part(*ar, "root"), bp = b->part(*br, "root"); check(ap && bp);
        auto as = a->capture(*ar); check(as.has_value());
        check(ar->slot == br->slot && ar->generation == br->generation && ar->owner != br->owner);
        check(!b->contains(*ar) && b->capture(*ar).error() == ObjectError::foreign_owner);
        check(as->get(*bp).error() == ObjectError::foreign_owner);
        auto wrong_object_generation = *ap; ++wrong_object_generation.object.generation;
        auto wrong_part_generation = *ap; ++wrong_part_generation.generation;
        check(as->get(wrong_object_generation).error() == ObjectError::stale_object);
        check(as->get(wrong_part_generation).error() == ObjectError::stale_part);
        check(!a->contains(wrong_object_generation) && !a->contains(wrong_part_generation));
        check(b->destroy(*ar).error() == ObjectError::foreign_owner && b->contains(*br));
        check(a->destroy(*ar).has_value());
        const auto replacement = a->create(graph()); check(replacement.has_value());
        check(replacement->slot == ar->slot && replacement->generation != ar->generation);
        check(!a->contains(*ar) && !a->contains(*ap));
        auto next = a->capture(*replacement); check(next->get(*ap).error() == ObjectError::stale_object);
        check(as->get(*ap).has_value(), "a frozen endpoint still resolves its own old identity");
        ObjectRegistry moved = std::move(*a);
        check(a->owner() == 0 && moved.contains(*replacement));
    });
    s.test("named parts survive reordering and reparenting but deletion never resurrects a ref", [] {
        auto registry = ObjectRegistry::make();
        ObjectDescription before{{{.id = "root"}, {.id = "track", .parent = "root"}, {.id = "thumb", .parent = "track"}, {.id = "label", .parent = "root"}}};
        auto object = registry->create(before); check(object.has_value());
        const auto root = registry->part(*object, "root"), track = registry->part(*object, "track"), thumb = registry->part(*object, "thumb");
        const auto old = registry->capture(*object);
        ObjectDescription reordered{{{.id = "thumb", .parent = "root"}, {.id = "root"}, {.id = "label", .parent = "root"}, {.id = "track", .parent = "root"}}};
        check(registry->update(*object, reordered) == 2);
        auto now = registry->capture(*object);
        check(now->find("thumb") == thumb && now->find("track") == track && now->root() == *root);
        check((*now->get(*thumb))->parent == std::optional<PartRef>(*root) && (*old->get(*thumb))->parent == std::optional<PartRef>(*track));
        reordered.parts.erase(reordered.parts.begin());
        check(registry->update(*object, reordered) == 3 && !registry->contains(*thumb));
        auto removed = registry->capture(*object); check(removed->get(*thumb).error() == ObjectError::stale_part);
        reordered.parts.push_back({.id = "thumb", .parent = "root"});
        check(registry->update(*object, reordered) == 4);
        const auto added = registry->part(*object, "thumb"); check(added && *added != *thumb);
        auto final = registry->capture(*object); check(final->get(*thumb).error() == ObjectError::stale_part);
        check(old->get(*thumb).has_value() && old->get(*added).error() == ObjectError::stale_part);
    });
    s.test("invalid graphs data and names fail atomically without changing versions or refs", [] {
        auto registry = ObjectRegistry::make({.max_depth = 2});
        const auto original = graph();
        auto object = registry->create(original); check(object.has_value());
        const auto snapshot = registry->capture(*object); const auto child = registry->part(*object, "child");
        auto rejects = [&](ObjectDescription bad, ObjectError expected) {
            const auto result = registry->update(*object, bad);
            check(!result && result.error() == expected, std::string(object_error_name(expected)));
            check(registry->revision(*object) == 1 && registry->part(*object, "child") == child);
            const auto current = registry->capture(*object);
            check(current->parts().data() == snapshot->parts().data() && current->revision() == 1, "failed preparation publishes no replacement");
        };
        auto bad = original; bad.parts[1].id = "root"; rejects(bad, ObjectError::duplicate_id);
        bad = original; bad.parts[1].parent = "missing"; rejects(bad, ObjectError::missing_parent);
        bad = original; bad.parts[1].parent.clear(); rejects(bad, ObjectError::multiple_roots);
        bad = original; bad.parts[1].parent = "leaf"; bad.parts.push_back({.id = "leaf", .parent = "child"}); rejects(bad, ObjectError::cycle);
        bad = original; bad.parts.push_back({.id = "leaf", .parent = "child"}); rejects(bad, ObjectError::depth_limit);
        bad = original; bad.parts[1].id.assign(129, 'x'); rejects(bad, ObjectError::invalid_id);
        bad = original; bad.parts[1].parent.assign(129, 'x'); rejects(bad, ObjectError::invalid_id);
        bad = original; bad.parts[1].id = std::string("bad\0key", 7); rejects(bad, ObjectError::invalid_id);
        bad = original; bad.parts[0].local.sx = std::numeric_limits<double>::infinity(); rejects(bad, ObjectError::invalid_transform);
        bad = original; bad.parts[0].fill = Rgba{2, 0, 0, 1}; rejects(bad, ObjectError::invalid_color);
        bad = original; bad.parts[0].contours = {Contour{}}; rejects(bad, ObjectError::invalid_resource);
        check(!registry->update(*object, {}) && registry->revision(*object) == 1);
    });
    s.test("limits apply per object and immutable contours count per referenced occurrence", [] {
        auto registry = ObjectRegistry::make({.max_objects = 2, .max_parts = 2, .max_contours = 1, .max_segments = 1, .max_depth = 2});
        const auto first = registry->create(graph()), second = registry->create(graph()); check(first && second);
        check(registry->create(graph()).error() == ObjectError::capacity);
        auto too_many = graph(); too_many.parts[1].contours = too_many.parts[0].contours;
        check(registry->update(*first, too_many).error() == ObjectError::capacity);
        too_many = graph(); too_many.parts.push_back({.id = "third", .parent = "root"});
        check(registry->update(*first, too_many).error() == ObjectError::capacity);
        const std::array edges{ShapeSegment::line({1, 0}), ShapeSegment::line({2, 0})};
        auto longer = Contour::copy({{}, edges, false}); check(longer.has_value());
        auto too_long = graph(*longer); check(registry->update(*first, too_long).error() == ObjectError::capacity);
        check(registry->revision(*first) == 1 && registry->revision(*second) == 1);
        check(!ObjectRegistry::make({.max_parts = 0}));
    });
    s.test("capabilities describe actual owned domains and open contours remain open", [] {
        auto registry = ObjectRegistry::make(); const auto ref = registry->create(graph());
        const auto snapshot = registry->capture(*ref);
        check(snapshot->capability_version("hanim.parts") == 1 && snapshot->capability_version("hanim.contours") == 1);
        check(snapshot->require_capability("hanim.solid-fill").has_value());
        check(snapshot->require_capability("hanim.contours", 2).error() == ObjectError::unsupported_capability);
        check(snapshot->require_capability("hanim.pixels").error() == ObjectError::unsupported_capability && snapshot->capability_version("hanim.mesh") == 0);
        const auto* part = *snapshot->get(snapshot->root()); const auto contour = part->contours.front().view();
        check(contour && !contour->closed);
        const auto unsupported = PreparedShapeMorph::prepare(*contour, *contour);
        check(!unsupported && unsupported.error() == ShapeMorphError::open_contour, "representation capture does not silently convert an open path into a fill");
        auto group = registry->create({{{.id = "group"}}}); const auto grouped = registry->capture(*group);
        check(grouped->capability_version("hanim.contours") == 0 && grouped->capability_version("hanim.solid-fill") == 0);
        Snapshot absent; check(!absent && absent.parts().empty() && absent.find("root").error() == ObjectError::uninitialized);
        check(snapshot->get(grouped->root()).error() == ObjectError::different_object);
        Contour missing; check(!missing.view() && missing.segment_count() == 0);
    });
    s.test("contour resources validate used coordinates and preserve move-only representations", [] {
        const auto move = Contour::copy({{1, 2}, {}, false});
        check(move && move->view()->segments.empty() && !move->view()->closed);
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        check(Contour::copy({{nan, 0}, {}, false}).error() == ObjectError::invalid_coordinate);
        const std::array bad_point{ShapeSegment::line({nan, 0})};
        check(Contour::copy({{}, bad_point}).error() == ObjectError::invalid_coordinate);
        const std::array bad_control{ShapeSegment::quadratic({0, nan}, {1, 1})};
        check(Contour::copy({{}, bad_control}).error() == ObjectError::invalid_coordinate);
        const std::array bad_verb{ShapeSegment{.verb = static_cast<ShapeVerb>(255)}};
        check(Contour::copy({{}, bad_verb}).error() == ObjectError::invalid_verb);
        const std::array too_many{ShapeSegment::line({1, 1}), ShapeSegment::line({2, 2})};
        check(Contour::copy({{}, too_many}, 1).error() == ObjectError::capacity);
    });
    s.test("logger callbacks can read committed state but reentrant mutation is refused without another log", [] {
        auto registry = ObjectRegistry::make(); const auto object = registry->create(graph());
        struct Observer { ObjectRegistry* registry; ObjectRef object; int called = 0; bool read = false, refused = false; } observer{&*registry, *object};
        check(omui::set_log_filter("hanim.object=info,*=warning").has_value());
        omui::set_log_sink([](void* context, const omui::LogRecord& record) {
            if (record.category != "hanim.object" || !record.message.starts_with("updated")) return;
            auto& observer = *static_cast<Observer*>(context); ++observer.called;
            const auto snapshot = observer.registry->capture(observer.object);
            observer.read = snapshot && snapshot->revision() == 2 && observer.registry->size() == 1;
            const auto destroyed = observer.registry->destroy(observer.object);
            observer.refused = !destroyed && destroyed.error() == ObjectError::reentrant_mutation;
        }, &observer);
        const auto updated = registry->update(*object, graph());
        omui::set_log_sink(nullptr, nullptr); check(omui::set_log_filter("*=info").has_value());
        check(updated == 2 && observer.called == 1 && observer.read && observer.refused && registry->contains(*object));
    });
    return s.run(argc, argv);
}
