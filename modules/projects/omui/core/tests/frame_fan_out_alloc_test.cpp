// A frame's fan-out allocates nothing (red 160): once the crew's helpers have started, the chunks of a frame are
// shared out and joined without the heap — on the caller's thread and on the helpers'. Its own program: it counts
// every allocation by replacing `operator new`.
import std;
import omui.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
std::atomic<std::uint64_t> allocations{0};
}

void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    std::abort();  // core code uses no exceptions; running out of memory ends the run
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

static UTest cases{"core.frame_fan_out", [] {
    "a frame's fan-out allocates nothing once its helpers are started"_test = [] {
        omui::Tasks tasks(16);
        std::array<std::uint32_t, 64> rows{};
        const auto draw = [&](std::size_t chunks) {
            tasks.parallel_for(chunks, 1, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) rows[i] += 1;
            }, omui::Priority::frame);
        };
        draw(64);  // the helpers start: their threads are the crew's, made once
        const std::uint64_t before = allocations.load();
        for (int frame = 0; frame < 1000; ++frame) draw(1 + static_cast<std::size_t>(frame) % 64);
        const std::uint64_t made = allocations.load() - before;
        expect(eq(made, std::uint64_t{0})) << std::format("{} allocations in 1000 frames", made);
        std::uint64_t drawn = 0, asked = 64;
        for (std::uint32_t row : rows) drawn += row;
        for (int frame = 0; frame < 1000; ++frame) asked += 1 + static_cast<std::uint64_t>(frame) % 64;
        expect(eq(drawn, asked)) << "every chunk drawn once";
    };
}};
