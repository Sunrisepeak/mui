import std;
import hanim.object;
import mlibs.log;
import mlibs.trace;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace hanim;
using namespace mcpplibs::mtest;

namespace {
Contour line() {
    const std::array segments{ShapeSegment::line({10, 5})};
    auto result = Contour::copy({{1, 2}, segments, false}); expect(result.has_value());
    return std::move(*result);
}
ObjectDescription graph(Contour contour = line()) {
    return {{{.id = "root", .local = {.tx = 12}, .contours = {std::move(contour)}, .fill = Rgba{1, 0, 0, 1}},
             {.id = "child", .parent = "root"}}};
}
}

UTest cases{"hanim.object", [] {
    "capture owns geometry and graph after producer mutation update and destruction"_test = [] {
        Snapshot frozen;
        PartRef root;
        {
            std::vector<ShapeSegment> input{ShapeSegment::line({10, 5})};
            auto contour = Contour::copy({{1, 2}, input, false}); expect(contour.has_value());
            input.front().end = {999, 999}; input.clear(); input.shrink_to_fit();
            auto description = graph(*contour);
            auto registry = ObjectRegistry::make(); expect(registry.has_value());
            const auto object = registry->create(description); expect(object.has_value());
            auto captured = registry->capture(*object); expect(captured.has_value());
            frozen = *captured; root = frozen.root();
            description.parts[0].id = "mutated"; description.parts[0].local.tx = 999; description.parts[0].contours.clear();
            auto part = frozen.get(root); expect(part.has_value());
            expect((*part)->id == "root" && (*part)->local.tx == 12 && (*part)->fill->r == 1);
            auto shape = (*part)->contours.front().view(); expect(shape && !shape->closed && shape->segments.front().end == Vec2{10, 5});
            auto replacement = graph(); replacement.parts[0].local.tx = 24;
            expect(registry->update(*object, replacement) == 2);
            expect(registry->part(*object, "root") == root);
            auto now = registry->capture(*object); expect(now && now->revision() == 2);
            expect((*now->get(root))->local.tx == 24 && (*frozen.get(root))->local.tx == 12);
            expect(registry->destroy(*object).has_value() && !registry->contains(*object));
            expect(!registry->capture(*object) && registry->capture(*object).error() == ObjectError::stale_object);
        }
        expect(frozen.revision() == 1 && frozen.find("root") == root);
        expect((*frozen.get(root))->contours.front().view()->segments.front().end == Vec2{10, 5}) << "no producer or registry memory is borrowed";
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
            expect(good.load()) << "readonly leases outlive their registry and support concurrent reads";
        }
    };
    "foreign owner object generation and part generation are independently checked"_test = [] {
        auto a = ObjectRegistry::make({.max_objects = 1}), b = ObjectRegistry::make({.max_objects = 1}); expect(a && b);
        const auto ar = a->create(graph()), br = b->create(graph()); expect(ar && br);
        const auto ap = a->part(*ar, "root"), bp = b->part(*br, "root"); expect(ap && bp);
        auto as = a->capture(*ar); expect(as.has_value());
        expect(ar->slot == br->slot && ar->generation == br->generation && ar->owner != br->owner);
        expect(!b->contains(*ar) && b->capture(*ar).error() == ObjectError::foreign_owner);
        expect(as->get(*bp).error() == ObjectError::foreign_owner);
        auto wrong_object_generation = *ap; ++wrong_object_generation.object.generation;
        auto wrong_part_generation = *ap; ++wrong_part_generation.generation;
        expect(as->get(wrong_object_generation).error() == ObjectError::stale_object);
        expect(as->get(wrong_part_generation).error() == ObjectError::stale_part);
        expect(!a->contains(wrong_object_generation) && !a->contains(wrong_part_generation));
        expect(b->destroy(*ar).error() == ObjectError::foreign_owner && b->contains(*br));
        expect(a->destroy(*ar).has_value());
        const auto replacement = a->create(graph()); expect(replacement.has_value());
        expect(replacement->slot == ar->slot && replacement->generation != ar->generation);
        expect(!a->contains(*ar) && !a->contains(*ap));
        auto next = a->capture(*replacement); expect(next->get(*ap).error() == ObjectError::stale_object);
        expect(as->get(*ap).has_value()) << "a frozen endpoint still resolves its own old identity";
        ObjectRegistry moved = std::move(*a);
        expect(a->owner() == 0 && moved.contains(*replacement));
    };
    "named parts survive reordering and reparenting but deletion never resurrects a ref"_test = [] {
        auto registry = ObjectRegistry::make();
        ObjectDescription before{{{.id = "root"}, {.id = "track", .parent = "root"}, {.id = "thumb", .parent = "track"}, {.id = "label", .parent = "root"}}};
        auto object = registry->create(before); expect(object.has_value());
        const auto root = registry->part(*object, "root"), track = registry->part(*object, "track"), thumb = registry->part(*object, "thumb");
        const auto old = registry->capture(*object);
        ObjectDescription reordered{{{.id = "thumb", .parent = "root"}, {.id = "root"}, {.id = "label", .parent = "root"}, {.id = "track", .parent = "root"}}};
        expect(registry->update(*object, reordered) == 2);
        auto now = registry->capture(*object);
        expect(now->find("thumb") == thumb && now->find("track") == track && now->root() == *root);
        expect((*now->get(*thumb))->parent == std::optional<PartRef>(*root) && (*old->get(*thumb))->parent == std::optional<PartRef>(*track));
        reordered.parts.erase(reordered.parts.begin());
        expect(registry->update(*object, reordered) == 3 && !registry->contains(*thumb));
        auto removed = registry->capture(*object); expect(removed->get(*thumb).error() == ObjectError::stale_part);
        reordered.parts.push_back({.id = "thumb", .parent = "root"});
        expect(registry->update(*object, reordered) == 4);
        const auto added = registry->part(*object, "thumb"); expect(added && *added != *thumb);
        auto final = registry->capture(*object); expect(final->get(*thumb).error() == ObjectError::stale_part);
        expect(old->get(*thumb).has_value() && old->get(*added).error() == ObjectError::stale_part);
    };
    "invalid graphs data and names fail atomically without changing versions or refs"_test = [] {
        auto registry = ObjectRegistry::make({.max_depth = 2});
        const auto original = graph();
        auto object = registry->create(original); expect(object.has_value());
        const auto snapshot = registry->capture(*object); const auto child = registry->part(*object, "child");
        auto rejects = [&](ObjectDescription bad, ObjectError expected) {
            const auto result = registry->update(*object, bad);
            expect(!result && result.error() == expected) << std::string(describe(expected).name);
            expect(registry->revision(*object) == 1 && registry->part(*object, "child") == child);
            const auto current = registry->capture(*object);
            expect(current->parts().data() == snapshot->parts().data() && current->revision() == 1) << "failed preparation publishes no replacement";
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
        expect(!registry->update(*object, {}) && registry->revision(*object) == 1);
    };
    "limits apply per object and immutable contours count per referenced occurrence"_test = [] {
        auto registry = ObjectRegistry::make({.max_objects = 2, .max_parts = 2, .max_contours = 1, .max_segments = 1, .max_depth = 2});
        const auto first = registry->create(graph()), second = registry->create(graph()); expect(first && second);
        expect(registry->create(graph()).error() == ObjectError::capacity);
        auto too_many = graph(); too_many.parts[1].contours = too_many.parts[0].contours;
        expect(registry->update(*first, too_many).error() == ObjectError::capacity);
        too_many = graph(); too_many.parts.push_back({.id = "third", .parent = "root"});
        expect(registry->update(*first, too_many).error() == ObjectError::capacity);
        const std::array edges{ShapeSegment::line({1, 0}), ShapeSegment::line({2, 0})};
        auto longer = Contour::copy({{}, edges, false}); expect(longer.has_value());
        auto too_long = graph(*longer); expect(registry->update(*first, too_long).error() == ObjectError::capacity);
        expect(registry->revision(*first) == 1 && registry->revision(*second) == 1);
        expect(!ObjectRegistry::make({.max_parts = 0}));
    };
    "failed creation consumes neither a fresh nor a recycled object identity"_test = [] {
        auto registry = ObjectRegistry::make({.max_objects = 1, .max_parts = 2});
        expect(registry.has_value());
        auto bad = graph(); bad.parts[1].id = "root";
        expect(registry->create(bad).error() == ObjectError::duplicate_id && registry->size() == 0);
        const auto first = registry->create(graph()); expect(first.has_value());
        expect(first->slot == 0 && first->generation == 1);
        const auto frozen = registry->capture(*first);
        expect(registry->destroy(*first).has_value());
        for (int i = 0; i < 4; ++i) {
            expect(registry->create(bad).error() == ObjectError::duplicate_id);
            expect(registry->size() == 0 && registry->capture(*first).error() == ObjectError::stale_object);
        }
        const auto replacement = registry->create(graph()); expect(replacement.has_value());
        expect(replacement->slot == first->slot && replacement->generation == first->generation + 1) << "rejected preparation cannot consume a capacity-limited slot or advance its generation";
        expect(registry->size() == 1 && registry->contains(*replacement));
        expect(frozen->get(frozen->root()).has_value()) << "reusing live storage cannot change the retained old capture";
    };
    "bounded named-part churn reuses storage and rejected updates cannot advance identities"_test = [] {
        auto registry = ObjectRegistry::make({.max_objects = 1, .max_parts = 2});
        const auto object = registry->create(graph()); expect(object.has_value());
        const auto root = registry->part(*object, "root");
        auto child = *registry->part(*object, "child");
        const auto first = registry->capture(*object);
        for (int i = 0; i < 128; ++i) {
            const std::string child_name = i % 2 == 0 ? "other" : "child";
            auto next = graph(); next.parts[1].id = child_name;
            auto bad = next; bad.parts[1].parent = "missing";
            const auto before = registry->capture(*object);
            expect(registry->update(*object, bad).error() == ObjectError::missing_parent);
            const auto unchanged = registry->capture(*object);
            expect(unchanged->revision() == before->revision() && unchanged->parts().data() == before->parts().data());
            expect(registry->contains(child) && registry->part(*object, "root") == root);
            if (i % 2 == 0) std::ranges::reverse(next.parts);
            expect(registry->update(*object, next) == before->revision() + 1);
            const auto current = registry->part(*object, child_name); expect(current.has_value());
            expect(current->slot == child.slot && current->generation == child.generation + 1);
            expect(!registry->contains(child) && before->get(child).has_value());
            child = *current;
        }
        expect(registry->size() == 1 && registry->revision(*object) == 129);
        expect(first->find("child")->generation == 1 && first->get(child).error() == ObjectError::stale_part);
    };
    "failed updates leave free part identities identical to an uninterrupted consumer"_test = [] {
        auto changed = ObjectRegistry::make({.max_parts = 3}), control = ObjectRegistry::make({.max_parts = 3});
        const ObjectDescription initial{{{.id = "root"}, {.id = "a", .parent = "root"}, {.id = "b", .parent = "root"}}};
        const auto a = changed->create(initial), b = control->create(initial); expect(a && b);
        const auto retained = changed->capture(*a);
        const ObjectDescription root_only{{{.id = "root"}}};
        expect(changed->update(*a, root_only) == 2 && control->update(*b, root_only) == 2);
        const auto before = changed->capture(*a);
        const ObjectDescription bad{{{.id = "new"}, {.id = "c", .parent = "missing"}}};
        expect(changed->update(*a, bad).error() == ObjectError::missing_parent);
        expect(changed->capture(*a)->parts().data() == before->parts().data());
        // All keys change at full capacity; children may precede their root.
        const ObjectDescription replacement{{{.id = "c", .parent = "new"}, {.id = "d", .parent = "new"}, {.id = "new"}}};
        expect(changed->update(*a, replacement) == 3 && control->update(*b, replacement) == 3);
        for (std::string_view id : {"new", "c", "d"}) {
            const auto actual = changed->part(*a, id), expected = control->part(*b, id); expect(actual && expected);
            expect(actual->slot == expected->slot && actual->generation == expected->generation) << "a refused update cannot change later identity assignment relative to the same successful operations";
        }
        expect(retained->find("root").has_value() && retained->find("a").has_value() && retained->find("b").has_value());
        expect(!changed->contains(retained->root()) && before->find("root").has_value());
    };
    "capabilities describe actual owned domains and open contours remain open (a stroke morphs as a stroke)"_test = [] {
        auto registry = ObjectRegistry::make(); const auto ref = registry->create(graph());
        const auto snapshot = registry->capture(*ref);
        expect(snapshot->capability_version("hanim.parts") == 1 && snapshot->capability_version("hanim.contours") == 1);
        expect(snapshot->require_capability("hanim.solid-fill").has_value());
        expect(snapshot->require_capability("hanim.contours", 2).error() == ObjectError::unsupported_capability);
        expect(snapshot->require_capability("hanim.pixels").error() == ObjectError::unsupported_capability && snapshot->capability_version("hanim.mesh") == 0);
        const auto* part = *snapshot->get(snapshot->root()); const auto contour = part->contours.front().view();
        expect(contour && !contour->closed);
        // An open path morphs as a stroke and stays one: never silently converted into a fill (closed).
        const auto stroke = PreparedShapeMorph::prepare(*contour, *contour);
        expect(stroke.has_value() && !stroke->closed()) << "representation capture keeps an open path open";
        auto group = registry->create({{{.id = "group"}}}); const auto grouped = registry->capture(*group);
        expect(grouped->capability_version("hanim.contours") == 0 && grouped->capability_version("hanim.solid-fill") == 0);
        Snapshot absent; expect(!absent && absent.parts().empty() && absent.find("root").error() == ObjectError::uninitialized);
        expect(snapshot->get(grouped->root()).error() == ObjectError::different_object);
        Contour missing; expect(!missing.view() && missing.segment_count() == 0);
    };
    "contour resources validate used coordinates and preserve move-only representations"_test = [] {
        const auto move = Contour::copy({{1, 2}, {}, false});
        expect(move && move->view()->segments.empty() && !move->view()->closed);
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        expect(Contour::copy({{nan, 0}, {}, false}).error() == ObjectError::invalid_coordinate);
        const std::array bad_point{ShapeSegment::line({nan, 0})};
        expect(Contour::copy({{}, bad_point}).error() == ObjectError::invalid_coordinate);
        const std::array bad_control{ShapeSegment::quadratic({0, nan}, {1, 1})};
        expect(Contour::copy({{}, bad_control}).error() == ObjectError::invalid_coordinate);
        const std::array bad_verb{ShapeSegment{.verb = static_cast<ShapeVerb>(255)}};
        expect(Contour::copy({{}, bad_verb}).error() == ObjectError::invalid_verb);
        const std::array too_many{ShapeSegment::line({1, 1}), ShapeSegment::line({2, 2})};
        expect(Contour::copy({{}, too_many}, 1).error() == ObjectError::capacity);
    };
    "logger callbacks can read committed state but reentrant mutation is refused without another log"_test = [] {
        auto registry = ObjectRegistry::make(); const auto object = registry->create(graph());
        struct Observer { ObjectRegistry* registry; ObjectRef object; int called = 0; bool read = false, refused = false; } observer{&*registry, *object};
        expect(mlibs::log::set_log_filter("hanim.object=info,*=warning").has_value());
        mlibs::log::set_log_sink([](void* context, const mlibs::log::LogRecord& record) {
            if (record.category != "hanim.object" || !record.message.starts_with("updated")) return;
            auto& observer = *static_cast<Observer*>(context); ++observer.called;
            const auto snapshot = observer.registry->capture(observer.object);
            observer.read = snapshot && snapshot->revision() == 2 && observer.registry->size() == 1;
            const auto destroyed = observer.registry->destroy(observer.object);
            observer.refused = !destroyed && destroyed.error() == ObjectError::reentrant_mutation;
        }, &observer);
        const auto updated = registry->update(*object, graph());
        mlibs::log::set_log_sink(nullptr, nullptr); expect(mlibs::log::set_log_filter("*=info").has_value());
        expect(updated == 2 && observer.called == 1 && observer.read && observer.refused && registry->contains(*object));
    };
}};
