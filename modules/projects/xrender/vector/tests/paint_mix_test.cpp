import std;
import mlibs.geom;
import xrender.vector;
import mlibs.testing;
using namespace mlibs::testing;
namespace render = xrender;
namespace geom = mlibs::geom;
namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv) {
    Suite suite{"render.paint-mix"};
    suite.test("prepared material owns leaf data and display-list recording owns its sampled mixture", [] {
        std::array<render::GradientStop, 2> stops{{{0, geom::Color{1, 0, 0, 1}}, {1, geom::Color{0, 0, 1, 1}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient; gradient.gradient.stops = stops;
        render::Material material; material.normal = {9}; material.key = {0.2f, 0.3f, 0.4f, 1};
        render::Paint image; image.kind = render::Paint::Kind::image; image.image = {3}; image.material = &material;
        render::DisplayList list;
        {
            const auto plan = render::PreparedPaintMix::prepare(gradient, image);
            check(plan.has_value()); if (!plan) return;
            stops[0].color = geom::Color::black(); material.key = geom::Color::white();
            const auto paint = plan->record(0.25, list);
            check(paint.has_value()); if (!paint) return;
            list.fill_rect({0, 0, 10, 10}, *paint);
        }
        const auto& paint = std::get<render::FillRect>(list.commands()[0]).paint;
        check(paint.kind == render::Paint::Kind::mix); check_eq(paint.mix.size(), std::size_t{2});
        check(paint.mix[0].paint.gradient.stops[0].color == geom::Color{1, 0, 0, 1});
        check(paint.mix[1].paint.material->key == geom::Color{0.2f, 0.3f, 0.4f, 1});
        check(paint.mix[0].weight == 0.75f && paint.mix[1].weight == 0.25f);
        render::DisplayList copy; copy.append(list); const auto expected = copy.hash(); list.clear();
        check_eq(copy.hash(), expected);
        check(std::get<render::FillRect>(copy.commands()[0]).paint.mix[0].paint.gradient.stops[0].color.r == 1);
    });
    suite.test("retarget flattens and deduplicates leaf brushes instead of nesting layers", [] {
        const std::array<geom::Color, 3> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
        auto plan = render::PreparedPaintMix::prepare(render::Paint::solid(colors[0]), render::Paint::solid(colors[1]));
        check(plan.has_value()); if (!plan) return;
        std::array<render::WeightedPaint, render::max_paint_mix_terms> buffer;
        for (unsigned i = 0; i < 12; ++i) {
            check(plan->sample(0.37, buffer).has_value());
            render::Paint snapshot; snapshot.kind = render::Paint::Kind::mix;
            snapshot.mix = std::span(buffer).first(plan->term_count());
            auto next = render::PreparedPaintMix::prepare(snapshot, render::Paint::solid(colors[(i + 2) % colors.size()]));
            check(next.has_value()); if (!next) return;
            plan = std::move(next);
            check(plan->term_count() <= 3);
            check(plan->sample(0, buffer).has_value());
            double total = 0;
            for (const auto& term : std::span(buffer).first(plan->term_count())) {
                total += term.weight; check(term.paint.kind != render::Paint::Kind::mix);
            }
            check_near(total, 1, 1e-6);
        }
    });
    suite.test("invalid weights, nested mixtures, sample inputs and explicit budgets report errors", [] {
        const auto leaf = render::Paint::solid(geom::Color::white());
        std::array<render::WeightedPaint, 2> terms{{{leaf, 0.5f}, {leaf, 0.5f}}};
        render::Paint mix; mix.kind = render::Paint::Kind::mix; mix.mix = terms;
        check(render::validate_paint_mix(mix).has_value());
        terms[0].weight = -1; check(!render::validate_paint_mix(mix));
        terms[0].weight = std::numeric_limits<float>::quiet_NaN(); check(!render::validate_paint_mix(mix));
        terms[0].weight = 0.1f; check(!render::validate_paint_mix(mix));
        terms[0].weight = 0.5f; terms[0].paint.kind = render::Paint::Kind::mix;
        check(render::validate_paint_mix(mix).error() == render::PaintMixError::nested_mix);
        check(render::PreparedPaintMix::prepare(leaf, leaf, {.max_terms = 0}).error() == render::PaintMixError::invalid_options);
        const auto other = render::Paint::solid(geom::Color::black());
        check(render::PreparedPaintMix::prepare(leaf, other, {.max_terms = 1}).error() == render::PaintMixError::term_budget);
        const auto plan = render::PreparedPaintMix::prepare(leaf, other);
        check(plan.has_value()); if (!plan) return;
        std::array<render::WeightedPaint, 1> small{{{leaf, 0.3f}}};
        check(!plan->sample(0.3, small)); check(small[0].weight == 0.3f);
        check(!plan->sample(std::numeric_limits<double>::infinity(), terms));
        render::DisplayList output;
        check(!output.record_paint_mix(0, [](auto) {}));
        check(!output.record_paint_mix(2, [](auto out) { out[0].weight = 1; out[1].weight = 1; }));
        check(output.empty());
    });
    suite.test("a seventeenth distinct material reports a strict budget error and leaves the prior plan usable", [] {
        std::array<render::WeightedPaint, render::max_paint_mix_terms> terms;
        for (std::size_t i = 0; i < terms.size(); ++i)
            terms[i] = {render::Paint::solid({static_cast<float>(i) / 16, 0.2f, 0.3f, 1}), 1.0f / 16};
        render::Paint input; input.kind = render::Paint::Kind::mix; input.mix = terms;
        const auto prior = render::PreparedPaintMix::prepare(input, terms[0].paint);
        check(prior.has_value()); if (!prior) return;
        std::array<render::WeightedPaint, render::max_paint_mix_terms> before, after;
        check(prior->sample(0.37, before).has_value());
        const auto rejected = render::PreparedPaintMix::prepare(input, render::Paint::solid({0.5f, 0.8f, 0.9f, 1}));
        check(!rejected && rejected.error() == render::PaintMixError::term_budget);
        check_eq(prior->term_count(), std::size_t{16});
        check(prior->sample(0.37, after).has_value());
        for (std::size_t i = 0; i < before.size(); ++i) {
            check(before[i].paint.color == after[i].paint.color); check(before[i].weight == after[i].weight);
        }
    });

    suite.test("warm caller-owned sampling and recording allocate no scalar heap memory", [] {
        const auto plan = render::PreparedPaintMix::prepare(render::Paint::solid(geom::Color::white()), render::Paint::solid(geom::Color::black()));
        check(plan.has_value()); if (!plan) return;
        render::DisplayList output; output.reserve(8);
        auto frame = [&](double t) {
            output.clear(); const auto paint = plan->record(t, output);
            if (!paint) return false;
            output.fill_rect({0, 0, 10, 10}, *paint); return true;
        };
        check(frame(0.3)); check(frame(0.5));
        const auto before = allocations.load(std::memory_order_relaxed);
        bool okay = true;
        for (unsigned i = 0; i < 1000; ++i) okay = frame(static_cast<double>(i % 1000) / 1000) && okay;
        const auto after = allocations.load(std::memory_order_relaxed);
        check(okay); check_eq(after, before);
    });
    return suite.run(argc, argv);
}
