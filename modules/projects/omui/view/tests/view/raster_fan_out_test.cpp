// Red 160: a window's CPU frame shares its rows out by their work. Through the runtime's raster pool (the frame crew
// of Tasks::shared), a whole window's frame is cut into bands drawn at once; a frame that changes a few rows — a
// button pressed, a row hovered — is one band, drawn by the caller, and wakes no thread. Either way the bytes are a
// serial render's.
import std;
import omui.core;
import omui.view;
import mrender.raster;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

constexpr int window_width = 1896, window_height = 1030;

// A list's frame: 28 rows of a title, a state and a button, the hovered one tinted.
void rows(mrender::DisplayList& list, int hovered) {
    list.clear();
    for (int row = 0; row < 28; ++row) {
        const float top = 8.0f + 36.0f * static_cast<float>(row);
        list.begin_node(static_cast<std::uint32_t>(row + 1));
        list.fill_rect({0, top, static_cast<float>(window_width), 36}, row == hovered ? Color::hex(0xeef2ff) : Color::hex(row % 2 ? 0xffffff : 0xf8fafc));
        list.fill_round_rect({16, top + 8, 640, 20}, mlibs::geom::CornerRadii::all(4), mrender::Paint::solid(Color::hex(0x334155)));
        list.fill_round_rect({1700, top + 4, 120, 28}, mlibs::geom::CornerRadii::all(6), mrender::Paint::solid(Color::hex(0x6366f1)));
        list.end_node();
    }
}

}  // namespace

static UTest cases{"view.raster_fan_out", [] {
    "a window's frame is shared out by its work; a few changed rows wake no thread"_test = [] {
        mrender::DisplayList list;
        rows(list, -1);
        mrender::raster::Surface serial(window_width, window_height), banded(window_width, window_height);
        mrender::CpuRenderer one, crew;
        mrender::DamageTracker damage;
        const mrender::CpuOptions alone{.background = Color::white()};
        mrender::CpuOptions shared{.background = Color::white(), .parallel = &raster_parallel()};

        (void)one.render(list, serial, {}, alone);
        shared.rows = damage.update(list, window_width, window_height, 1);
        FanOutStats before = Tasks::shared().frame_stats();
        const mrender::RenderReport whole = crew.render(list, banded, {}, shared);
        FanOutStats after = Tasks::shared().frame_stats();
        expect(std::ranges::equal(serial.bytes(), banded.bytes())) << "the whole frame";
        expect(whole.bands >= 2) << std::format("{} units: {} bands", whole.work, whole.bands);
        if (Tasks::shared().frame_width() > 1)
            expect(after.wakes - before.wakes >= 1 && after.wakes - before.wakes < Tasks::shared().frame_width())
                << std::format("{} helpers woken for {} bands", after.wakes - before.wakes, whole.bands);

        // A row hovered: its rows alone, one band, no helper.
        rows(list, 5);
        (void)one.render(list, serial, {}, alone);
        shared.rows = damage.update(list, window_width, window_height, 1);
        before = Tasks::shared().frame_stats();
        const mrender::RenderReport hover = crew.render(list, banded, {}, shared);
        after = Tasks::shared().frame_stats();
        expect(std::ranges::equal(serial.bytes(), banded.bytes())) << "the hovered row";
        expect(hover.rows < 64 && eq(hover.bands, std::size_t{1})) << std::format("{} rows, {} units, {} bands", hover.rows, hover.work, hover.bands);
        expect(eq(after.wakes - before.wakes, std::uint64_t{0})) << "no thread woken for a hovered row";
    };
}};
