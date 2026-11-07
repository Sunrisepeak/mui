module hanim.rhi.cts;

import std;
import hanim.core;
import hanim.rhi;

namespace hanim::gpu::cts {
namespace rhi = mlibs::rhi;
namespace {
// A polygon of `corners` corners round (x, y), turned by `turn`.
std::vector<ShapeSegment> polygon(int corners, double radius, double turn, Vec2& start) {
    std::vector<ShapeSegment> edges;
    for (int corner = 0; corner <= corners; ++corner) {
        const double angle = turn + 2 * std::numbers::pi * corner / corners;
        const Vec2 at{radius * std::cos(angle), radius * std::sin(angle)};
        if (corner == 0)
            start = at;
        else
            edges.push_back(ShapeSegment::line(at));
    }
    edges.pop_back();
    return edges;
}
std::vector<PreparedShapeMorph> plans(std::size_t count, PointPath path = {}) {
    std::vector<PreparedShapeMorph> result;
    for (std::size_t index = 0; index < count; ++index) {
        Vec2 from_start{}, to_start{};
        const auto from =
            polygon(3 + static_cast<int>(index % 5), 20 + index % 13, .1 * index, from_start);
        const auto to =
            polygon(4 + static_cast<int>(index % 7), 30 + index % 11, -.07 * index, to_start);
        auto plan =
            PreparedShapeMorph::prepare({from_start, from}, {to_start, to}, {.point_path = path});
        if (plan)
            result.push_back(std::move(*plan));
    }
    return result;
}
// Every control point on the device against the CPU sampler, the largest difference.
double largest_difference(rhi::Device& device, const std::vector<PreparedShapeMorph>& morphs,
                          double progress) {
    auto batch = ShapeBatch::prepare(device, morphs);
    if (!batch)
        return 1e9;
    auto output =
        device.create(rhi::BufferDesc{.size = static_cast<std::uint32_t>((*batch)->floats() * 4),
                                      .usage = rhi::BufferUsage::compute_write});
    if (!output)
        return 1e9;
    rhi::CommandList commands;
    if (!(*batch)->record(commands, progress, *output) || !device.submit(commands)) {
        device.destroy(*output);
        return 1e9;
    }
    std::vector<float> sampled((*batch)->floats());
    if (!device.read(*output, 0, std::as_writable_bytes(std::span(sampled)))) {
        device.destroy(*output);
        return 1e9;
    }
    double largest = 0;
    for (std::size_t plan = 0; plan < morphs.size(); ++plan) {
        std::vector<ShapeCubic> cubics(morphs[plan].segment_count());
        if (!morphs[plan].sample(progress, cubics))
            return 1e9;
        const auto first = (*batch)->first_segments()[plan];
        for (std::size_t segment = 0; segment < cubics.size(); ++segment) {
            const auto& cubic = cubics[segment];
            const std::array points{cubic.start, cubic.control1, cubic.control2, cubic.end};
            for (std::size_t point = 0; point < 4; ++point) {
                const auto at = ((first + segment) * 4 + point) * 2;
                largest = std::max({largest, std::abs(sampled[at] - points[point].x),
                                    std::abs(sampled[at + 1] - points[point].y)});
            }
        }
    }
    device.destroy(*output);
    return largest;
}

struct Bench {
    rhi::Device& device;
    Totals totals;
};
}  // namespace

mlibs::cts::Report run(rhi::Device& device, mlibs::cts::Environment environment,
                       const mlibs::cts::Policy& policy, Totals* totals) {
    if (environment.subject.empty())
        environment.subject = device.caps().device;
    mlibs::cts::Suite<Bench> suite{"hanim.shape.gpu", "1"};
    auto scenario = [&](std::string id, auto body) {
        suite.add({.id = std::move(id),
                   .contract = "hanim.shape.gpu@1",
                   .level = mlibs::cts::Level::tolerance,
                   .needs = {"compute"},
                   .body = [body](Bench& bench, mlibs::cts::Check& check) {
                       const auto failure = body(bench);
                       check.that(failure.empty(), failure);
                   }});
    };
    const auto compare = [](Bench& bench, const std::vector<PreparedShapeMorph>& morphs,
                            double progress) -> std::string {
        const double difference = largest_difference(bench.device, morphs, progress);
        ++bench.totals.samples;
        for (const auto& morph : morphs)
            bench.totals.compared_points += morph.segment_count() * 4;
        bench.totals.largest_difference = std::max(bench.totals.largest_difference, difference);
        // 32-bit floats on coordinates up to ~40.
        return difference < 1e-4 ? std::string()
                                 : std::format("at {} the device differs from the CPU by {}",
                                               progress, difference);
    };
    scenario("shape.batch.straight", [=](Bench& bench) -> std::string {
        ++bench.totals.scenarios;
        const auto morphs = plans(1000);
        for (const double progress : {0.0, .25, .5, .8, 1.0})
            if (auto failure = compare(bench, morphs, progress); !failure.empty())
                return failure;
        return {};
    });
    scenario("shape.batch.curved", [=](Bench& bench) -> std::string {
        ++bench.totals.scenarios;
        return compare(bench, plans(200, PointPath::bend(.4)), .5);
    });
    scenario("shape.batch.refusals", [](Bench& bench) -> std::string {
        ++bench.totals.scenarios;
        auto& device = bench.device;
        const auto refused = ShapeBatch::prepare(device, plans(2, PointPath::jitter(2, 10)));
        if (refused || refused.error().code != rhi::Errc::unsupported)
            return "noise was not refused as unsupported";
        auto mixed = plans(1);
        mixed.push_back(plans(1, PointPath::bend(.2)).front());
        if (ShapeBatch::prepare(device, mixed))
            return "a batch of two point paths was accepted";
        if (ShapeBatch::prepare(device, std::span<const PreparedShapeMorph>{}))
            return "an empty batch was accepted";
        auto batch = ShapeBatch::prepare(device, plans(3));
        if (!batch)
            return batch.error().message;
        auto small =
            device.create(rhi::BufferDesc{.size = 4, .usage = rhi::BufferUsage::compute_write});
        if (!small)
            return small.error().message;
        rhi::CommandList commands;
        const bool too_small = !(*batch)->record(commands, .5, *small) && commands.size() == 0;
        const bool nonfinite =
            !(*batch)->record(commands, std::numeric_limits<double>::quiet_NaN(), *small) &&
            commands.size() == 0;
        device.destroy(*small);
        bench.totals.validation_checks += 5;
        return too_small && nonfinite ? std::string() : "a refused record appended commands";
    });
    Bench bench{device, {}};
    const auto has = [&](std::string_view capability) {
        return capability == "compute" && device.caps().compute;
    };
    auto report = suite.run(bench, has, policy, std::move(environment));
    if (totals)
        *totals = bench.totals;
    return report;
}

}  // namespace hanim::gpu::cts
