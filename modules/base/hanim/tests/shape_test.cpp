// A consumer of hanim alone: no renderer, UI, site or third-party geometry.
import std;
import hanim;
import openmui.testing;

using namespace omui::testing;
using namespace hanim;

namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
constexpr double k = 0.5522847498307936; // four-cubic circle, input itself is a Bezier approximation
constexpr std::array<ShapeSegment, 2> triangle{{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})}};
constexpr std::array<ShapeSegment, 4> circle{{
    ShapeSegment::cubic({k, -1}, {1, -k}, {1, 0}),
    ShapeSegment::cubic({1, k}, {k, 1}, {0, 1}),
    ShapeSegment::cubic({-k, 1}, {-1, k}, {-1, 0}),
    ShapeSegment::cubic({-1, -k}, {-k, -1}, {0, -1})
}};
ShapeContour tri() { return {{0, -1}, triangle}; }
ShapeContour disk() { return {{0, -1}, circle}; }
std::vector<ShapeCubic> sample(const PreparedShapeMorph& plan, double progress) {
    std::vector<ShapeCubic> result(plan.segment_count());
    check(plan.sample(progress, result).has_value());
    return result;
}
bool close(Vec2 a, Vec2 b, double epsilon = 1e-10) {
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon;
}
bool same(const ShapeCubic& a, const ShapeCubic& b) {
    return a.start == b.start && a.control1 == b.control1 && a.control2 == b.control2 && a.end == b.end;
}
void connected(std::span<const ShapeCubic> curves) {
    check(!curves.empty());
    for (std::size_t i = 0; i < curves.size(); ++i)
        check(curves[i].end == curves[(i + 1) % curves.size()].start, "single exactly closed contour");
}
double nearest(Vec2 p, std::span<const ShapeCubic> curves) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& c : curves) for (unsigned i = 0; i <= 256; ++i) {
        const auto q = c.point(static_cast<double>(i) / 256);
        const double dx = p.x - q.x, dy = p.y - q.y;
        best = std::min(best, dm::sqrt(dx * dx + dy * dy));
    }
    return best;
}
// Invert the monotone coordinates on each original quarter. This
// checks the actual Bezier locus to roundoff, rather than accepting a nearby
// sampled polygon or merely pinning our own normalized control output.
double circle_locus_error(Vec2 p) {
    double best = std::numeric_limits<double>::infinity();
    Vec2 start{0, -1};
    for (const auto& segment : circle) {
        const ShapeCubic original{start, segment.control1, segment.control2, segment.end};
        // Both coordinates are monotone. Near a tangent extremum one inverse
        // is ill-conditioned; the other remains a strong locus witness.
        for (unsigned axis = 0; axis < 2; ++axis) {
            auto coordinate = [axis](Vec2 q) { return axis == 0 ? q.x : q.y; };
            double lo = 0, hi = 1;
            const bool increasing = coordinate(original.end) > coordinate(original.start);
            for (unsigned i = 0; i < 60; ++i) {
                const double mid = lo + (hi - lo) * 0.5;
                const double v = coordinate(original.point(mid));
                if ((v < coordinate(p)) == increasing) lo = mid;
                else hi = mid;
            }
            const auto q = original.point(lo + (hi - lo) * 0.5);
            best = std::min(best, std::max(std::abs(p.x - q.x), std::abs(p.y - q.y)));
        }
        start = segment.end;
    }
    return best;
}
std::array<ShapeCubic, 4> circle_cubics() {
    std::array<ShapeCubic, 4> result{};
    Vec2 p{0, -1};
    for (std::size_t i = 0; i < circle.size(); ++i) {
        result[i] = {p, circle[i].control1, circle[i].control2, circle[i].end};
        p = circle[i].end;
    }
    return result;
}
}

int main(int argc, char** argv) {
    Suite s{"hanim.shape"};
    s.test("triangle to circle is one moving cubic contour with original endpoint geometry", [] {
        const auto prepared = PreparedShapeMorph::prepare(tri(), disk());
        check(prepared.has_value());
        if (!prepared) return;
        const auto a = sample(*prepared, 0), b = sample(*prepared, 1), middle = sample(*prepared, 0.5);
        check(prepared->segment_count() >= 4);
        connected(a); connected(b); connected(middle);
        bool moved = false;
        for (std::size_t i = 0; i < middle.size(); ++i) {
            check(close(middle[i].control1, {(a[i].control1.x + b[i].control1.x) * 0.5,
                                             (a[i].control1.y + b[i].control1.y) * 0.5}));
            moved = moved || (!same(middle[i], a[i]) && !same(middle[i], b[i]));
            for (double t : {0.0, 0.2, 0.5, 0.8, 1.0}) {
                const auto p = a[i].point(t);
                const double left = std::abs(p.y + 2 * p.x + 1);
                const double right = std::abs(p.y - 2 * p.x + 1);
                check(std::min({left, right, std::abs(p.y - 1)}) < 1e-10, "triangle sides stay exact straight lines");
                check(circle_locus_error(b[i].point(t)) < 1e-10, "target retains original Bezier locus, not sampled chords");
            }
        }
        check(moved, "intermediate controls differ from both endpoints; no pair of faded fixed shapes");
        for (Vec2 corner : {Vec2{0, -1}, Vec2{-1, 1}, Vec2{1, 1}}) {
            bool retained = false;
            for (const auto& c : a) retained = retained || close(c.start, corner);
            check(retained, "original sharp vertex is a subdivision boundary");
        }
    });

    s.test("quadratic degree elevation and subdivision preserve every original curved endpoint", [] {
        const std::array<ShapeSegment, 2> lens{{ShapeSegment::quadratic({2, -2}, {4, 0}), ShapeSegment::quadratic({2, 2}, {0, 0})}};
        const auto prepared = PreparedShapeMorph::prepare({{0, 0}, lens}, disk(), {.align_start = false});
        check(prepared.has_value());
        if (!prepared) return;
        const auto result = sample(*prepared, 0);
        const auto quadratic = [](double t, bool bottom) {
            return Vec2{4 * t, (bottom ? 4 : -4) * t * (1 - t)};
        };
        for (const auto& c : result) for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const auto p = c.point(t);
            const auto expected = quadratic(p.x / 4, p.y >= 0);
            check(close(p, expected), "endpoint is the exact quadratic locus, not a polyline");
        }
        check(nearest({2, -1}, result) < 0.01 && nearest({2, 1}, result) < 0.01, "both arcs survive");
    });

    s.test("cubic controls remain exact when no subdivision is needed", [] {
        const auto prepared = PreparedShapeMorph::prepare(disk(), disk(), {.align_start = false});
        check(prepared.has_value());
        if (!prepared) return;
        const auto result = sample(*prepared, 0);
        const auto originals = circle_cubics();
        check_eq(result.size(), originals.size());
        for (std::size_t i = 0; i < result.size(); ++i) check(same(result[i], originals[i]));
    });

    s.test("opposite winding and shifted start align without rotating the shape", [] {
        const std::array<ShapeSegment, 3> a{{ShapeSegment::line({1, -1}), ShapeSegment::line({1, 1}), ShapeSegment::line({-1, 1})}};
        const std::array<ShapeSegment, 3> b{{ShapeSegment::line({1, -1}), ShapeSegment::line({-1, -1}), ShapeSegment::line({-1, 1})}};
        const auto prepared = PreparedShapeMorph::prepare({{-1, -1}, a}, {{1, 1}, b});
        check(prepared.has_value());
        if (!prepared) return;
        check(prepared->target_reversed());
        check(prepared->target_start_fraction() > 0);
        const auto first = sample(*prepared, 0), last = sample(*prepared, 1), mid = sample(*prepared, 0.5);
        for (std::size_t i = 0; i < first.size(); ++i) {
            check(close(first[i].start, last[i].start));
            check(close(first[i].control1, last[i].control1));
            check(close(first[i].control2, last[i].control2));
            check(close(first[i].end, last[i].end));
            check(close(first[i].start, mid[i].start), "identical squares do not collapse due to phase or winding");
        }
    });

    s.test("alignment can be disabled and preserves explicit traversal", [] {
        const std::array<ShapeSegment, 2> backwards{{ShapeSegment::line({1, 1}), ShapeSegment::line({-1, 1})}};
        const auto prepared = PreparedShapeMorph::prepare(tri(), {{0, -1}, backwards}, {.align_direction = false, .align_start = false});
        check(prepared.has_value());
        if (!prepared) return;
        check(!prepared->target_reversed());
        check_bits(prepared->target_start_fraction(), 0.0);
        const auto a = sample(*prepared, 0), b = sample(*prepared, 1);
        check(a.front().control1.x < 0 && b.front().control1.x > 0);
    });

    s.test("concave star to curved contour keeps all ten protected source corners", [] {
        std::array<Vec2, 10> vertices{};
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const double angle = static_cast<double>(i) * dm::pi / 5;
            const double radius = i % 2 ? 0.45 : 1.0;
            vertices[i] = {radius * dm::sin(angle), -radius * dm::cos(angle)};
        }
        std::array<ShapeSegment, 9> star{};
        for (std::size_t i = 0; i < star.size(); ++i) star[i] = ShapeSegment::line(vertices[i + 1]);
        const auto prepared = PreparedShapeMorph::prepare({vertices[0], star}, disk());
        check(prepared.has_value());
        if (!prepared) return;
        const auto result = sample(*prepared, 0);
        for (auto vertex : vertices) {
            bool retained = false;
            for (const auto& c : result) retained = retained || close(c.start, vertex);
            check(retained);
        }
        connected(sample(*prepared, 0.37));
    });

    s.test("arbitrary seek, reverse sampling and a re-prepared intermediate are continuous", [] {
        const auto prepared = PreparedShapeMorph::prepare(tri(), disk());
        check(prepared.has_value());
        if (!prepared) return;
        const auto forward = sample(*prepared, 0.37);
        for (double t : {1.0, 0.9, 0.1, 0.0, 0.8}) (void)sample(*prepared, t);
        const auto again = sample(*prepared, 0.37);
        for (std::size_t i = 0; i < forward.size(); ++i) check(same(forward[i], again[i]), "seek order cannot mutate the plan");
        std::vector<ShapeSegment> segments;
        for (const auto& c : forward) segments.push_back(ShapeSegment::cubic(c.control1, c.control2, c.end));
        const auto retarget = PreparedShapeMorph::prepare({forward.front().start, segments}, tri(), {.align_start = false});
        check(retarget.has_value());
        if (!retarget) return;
        const auto restart = sample(*retarget, 0);
        for (const auto& c : restart) for (double t : {0.0, 0.5, 1.0})
            check(nearest(c.point(t), forward) < 0.004, "retarget starts on sampled geometry");
        // Geometry continuity only: this primitive does not promise velocity continuity.
        const auto low = sample(*prepared, -1), high = sample(*prepared, 2);
        const auto first = sample(*prepared, 0), last = sample(*prepared, 1);
        for (std::size_t i = 0; i < low.size(); ++i) { check(same(low[i], first[i])); check(same(high[i], last[i])); }
    });

    s.test("open, empty, degenerate and invalid commands return explicit errors", [] {
        check(PreparedShapeMorph::prepare({{0, 0}, {}}, disk()).error() == ShapeMorphError::empty_contour);
        check(PreparedShapeMorph::prepare({{0, -1}, triangle, false}, disk()).error() == ShapeMorphError::open_contour);
        const std::array<ShapeSegment, 1> point{{ShapeSegment::line({0, 0})}};
        check(PreparedShapeMorph::prepare({{0, 0}, point}, disk()).error() == ShapeMorphError::degenerate_contour);
        const std::array<ShapeSegment, 2> line{{ShapeSegment::line({1, 1}), ShapeSegment::line({2, 2})}};
        check(PreparedShapeMorph::prepare({{0, 0}, line}, disk()).error() == ShapeMorphError::degenerate_contour);
        auto invalid = triangle;
        invalid[0].verb = static_cast<ShapeVerb>(99);
        check(PreparedShapeMorph::prepare({{0, -1}, invalid}, disk()).error() == ShapeMorphError::invalid_verb);
        check_eq(shape_morph_error_name(ShapeMorphError::open_contour), std::string_view("open_contour"));
    });

    s.test("nonfinite coordinates, unsafe numeric range and invalid option budgets fail preparation", [] {
        auto invalid = circle;
        invalid[0].control2.x = std::numeric_limits<double>::infinity();
        check(PreparedShapeMorph::prepare(tri(), {{0, -1}, invalid}).error() == ShapeMorphError::nonfinite_coordinate);
        invalid = circle;
        invalid[0].control1.y = std::numeric_limits<double>::quiet_NaN();
        check(PreparedShapeMorph::prepare(tri(), {{0, -1}, invalid}).error() == ShapeMorphError::nonfinite_coordinate);
        check(PreparedShapeMorph::prepare({{1e200, 0}, triangle}, disk()).error() == ShapeMorphError::numerical_range);
        check(PreparedShapeMorph::prepare(tri(), disk(), {.max_input_segments = 0}).error() == ShapeMorphError::invalid_options);
        check(PreparedShapeMorph::prepare(tri(), disk(), {.arc_samples = 257}).error() == ShapeMorphError::invalid_options);
        check(PreparedShapeMorph::prepare(tri(), disk(), {.alignment_samples = 3}).error() == ShapeMorphError::invalid_options);
    });

    s.test("implicit close and correspondence subdivisions count against segment budgets", [] {
        check(PreparedShapeMorph::prepare(tri(), disk(), {.max_input_segments = 2}).error() == ShapeMorphError::segment_budget);
        check(PreparedShapeMorph::prepare(tri(), disk(), {.max_output_segments = 3}).error() == ShapeMorphError::segment_budget);
        auto exact = PreparedShapeMorph::prepare(tri(), disk());
        check(exact.has_value());
        if (!exact) return;
        check(PreparedShapeMorph::prepare(tri(), disk(), {.max_output_segments = exact->segment_count()}).has_value());
        check(PreparedShapeMorph::prepare(tri(), disk(), {.max_output_segments = exact->segment_count() - 1}).error() == ShapeMorphError::segment_budget);
    });

    s.test("invalid seek and undersized buffers fail atomically", [] {
        const auto prepared = PreparedShapeMorph::prepare(tri(), disk());
        check(prepared.has_value());
        if (!prepared) return;
        const ShapeCubic sentinel{{7, 8}, {9, 10}, {11, 12}, {13, 14}};
        std::vector<ShapeCubic> output(prepared->segment_count() + 1, sentinel);
        check(prepared->sample(0.5, std::span(output).first(prepared->segment_count() - 1)).error() == ShapeMorphError::output_too_small);
        check(prepared->sample(std::numeric_limits<double>::quiet_NaN(), output).error() == ShapeMorphError::invalid_progress);
        check(prepared->sample(std::numeric_limits<double>::infinity(), output).error() == ShapeMorphError::invalid_progress);
        for (const auto& c : output) check(same(c, sentinel));
        check(prepared->sample(0.5, output).has_value());
        check(same(output.back(), sentinel), "caller storage beyond the written span stays intact");
        PreparedShapeMorph empty;
        check(empty.sample(0.5, output).error() == ShapeMorphError::unprepared);
    });

    s.test("uniform tiny and large scales retain geometry without squared-length underflow", [] {
        for (double scale : {1e-200, 1e90}) {
            auto source = triangle;
            auto target = circle;
            for (auto* segments : {static_cast<ShapeSegment*>(source.data()), target.data()}) {
                const auto count = segments == source.data() ? source.size() : target.size();
                for (std::size_t i = 0; i < count; ++i) {
                    for (auto* p : {&segments[i].end, &segments[i].control1, &segments[i].control2}) {
                        p->x *= scale; p->y *= scale;
                    }
                }
            }
            const auto prepared = PreparedShapeMorph::prepare({{0, -scale}, source}, {{0, -scale}, target});
            check(prepared.has_value());
            if (!prepared) continue;
            const auto result = sample(*prepared, 1);
            connected(result);
            for (const auto& cubic : result) for (double t : {0.0, 0.3, 0.7, 1.0}) {
                const auto point = cubic.point(t);
                check(circle_locus_error({point.x / scale, point.y / scale}) < 1e-10,
                      "shape is invariant under a finite uniform scale");
            }
        }
    });

    s.test("a positive edge below perimeter precision is rejected rather than silently removed", [] {
        const std::array<ShapeSegment, 4> tiny_edge{{
            ShapeSegment::line({1, 0}), ShapeSegment::line({1, 1e-20}),
            ShapeSegment::line({1, 1}), ShapeSegment::line({0, 1})}};
        const auto result = PreparedShapeMorph::prepare({{0, 0}, tiny_edge}, disk());
        check(!result.has_value());
        if (!result) check(result.error() == ShapeMorphError::numerical_range);
    });

    s.test("prepared contour samples have a cross-toolchain conformance fingerprint", [] {
        Hash digest;
        for (auto options : {ShapeMorphOptions{}, ShapeMorphOptions{.align_direction = false, .align_start = false}}) {
            const auto prepared = PreparedShapeMorph::prepare(tri(), disk(), options);
            check(prepared.has_value());
            if (!prepared) return;
            digest.u64(prepared->segment_count());
            digest.f64(prepared->target_start_fraction());
            std::vector<ShapeCubic> output(prepared->segment_count());
            for (unsigned i = 0; i <= 64; ++i) {
                check(prepared->sample(static_cast<double>(i) / 64, output).has_value());
                for (const auto& c : output)
                    for (auto p : {c.start, c.control1, c.control2, c.end}) { digest.f64(p.x); digest.f64(p.y); }
            }
        }
        std::println("    prepared-shape fingerprint {}", digest.hex());
        check_eq(digest.hex(), std::string("37b21beebb9bc81e"));
    });

    s.test("warm sampling performs no scalar or array allocation and has immutable storage", [] {
        const auto prepared = PreparedShapeMorph::prepare(tri(), disk());
        check(prepared.has_value());
        if (!prepared) return;
        std::vector<ShapeCubic> output(prepared->segment_count());
        const auto* storage = output.data();
        const auto probe_before = allocations.load(std::memory_order_relaxed);
        auto* scalar_probe = ::operator new(1);
        auto* array_probe = ::operator new[](1);
        ::operator delete(scalar_probe); ::operator delete[](array_probe);
        check_eq(allocations.load(std::memory_order_relaxed) - probe_before, std::size_t{2});
        const auto before = allocations.load(std::memory_order_relaxed);
        bool all_ok = true;
        for (unsigned i = 0; i < 10000; ++i)
            all_ok = all_ok && prepared->sample(static_cast<double>((i * 37) % 1001) / 1000, output).has_value();
        const auto after = allocations.load(std::memory_order_relaxed);
        check(all_ok);
        check_eq(after, before);
        check(output.data() == storage);
    });

    return s.run(argc, argv);
}
