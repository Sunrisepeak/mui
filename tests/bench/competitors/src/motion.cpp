// hanim's side of js/gsap-1000.mjs: motion/drive-1000's work (tests/bench),
// timed a frame at a time — 1000 driven channels of an Engine, every one
// retargeted every 30th frame, advanced a 60 Hz frame, every value read.
// 300 frames to warm up, 3000 measured; the same report as GSAP's side.
import std;
import mcpplibs.cmdline;
import hanim.core;
import hanim.engine;

int main(int argc, char** argv) {
    const auto line = mcpplibs::cmdline::App("omui-bench-motion")
        .description("hanim's side of the GSAP comparison: 1000 driven channels a frame")
        .option(mcpplibs::cmdline::Option("json").help("the report as JSON"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? 2 : 0;
    const bool json = parsed->is_flag_set("json");
    auto engine = hanim::Engine::make();
    if (!engine) return 1;
    static constexpr auto channel = hanim::ch::define<double>("bench.value");
    std::vector<hanim::Drive<double>> drives;
    for (std::uint64_t index = 0; index < 1000; ++index) {
        auto drive = engine->drive(index, channel);
        if (!drive) return 1;
        drives.push_back(*drive);
    }
    std::vector<double> values(engine->driven_lane_capacity());
    constexpr int warm_up = 300, measured = 3000;
    std::vector<double> times;
    times.reserve(measured);
    double checksum = 0;
    for (int frame = 0; frame < warm_up + measured; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        if (frame % 30 == 0)
            for (std::size_t index = 0; index < drives.size(); ++index)
                if (!drives[index].spring_to((frame / 30) % 2 ? 0.0 : 1.0 + static_cast<double>(index))) return 1;
        if (!engine->advance(hanim::seconds(1.0 / 60))) return 1;
        if (!engine->evaluate_driven(values)) return 1;
        for (const double value : values) checksum += value;
        const auto spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (frame >= warm_up) times.push_back(spent);
    }
    double mean = 0;
    for (const double time : times) mean += time;
    mean /= static_cast<double>(times.size());
    std::ranges::sort(times);
    const auto at = [&](double fraction) { return times[static_cast<std::size_t>(fraction * static_cast<double>(times.size() - 1))]; };
    if (json)
        std::println(R"({{"name":"hanim drive-1000","frames":{},"mean_ms":{:.4f},"p50_ms":{:.4f},"p95_ms":{:.4f},"max_ms":{:.4f},"checksum":{:.3f}}})", measured,
                     mean, at(0.5), at(0.95), times.back(), checksum);
    else
        std::println("hanim drive-1000: {} frames, mean {:.4f} ms, p50 {:.4f} ms, p95 {:.4f} ms, max {:.4f} ms", measured, mean, at(0.5), at(0.95),
                     times.back());
    return 0;
}
