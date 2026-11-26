// market: the live market board of tests/bench's core/live-dashboard in a window of 1896 × 1030
// (the size NSH's Linux Stocks numbers were taken in) — 22 quotes with a live sparkline each, a
// 40-level order book and a 30-row tape, every one of them changing on every tick, a tick every
// 1/120 s. The same board as the scene's (its `Market`), as an application: what the window
// presents is measured from outside (omui-devtools bench apps).
import std;
import omui;

namespace {

using namespace omui;

struct Market {
    static constexpr std::size_t symbols = 22, points = 120, levels = 20, trades = 30;

    std::array<std::array<float, points>, symbols> history{};  // a ring a symbol, 0..1
    std::size_t head = 0;
    std::uint64_t ticks = 0;
    std::vector<Signal<std::string>> price, change, bid, ask, tape;
    std::vector<Signal<double>> bid_depth, ask_depth;

    Market() {
        for (std::size_t symbol = 0; symbol < symbols; ++symbol)
            price.push_back(signal(std::string())), change.push_back(signal(std::string()));
        for (std::size_t level = 0; level < levels; ++level) {
            bid.push_back(signal(std::string())), ask.push_back(signal(std::string()));
            bid_depth.push_back(signal(0.0)), ask_depth.push_back(signal(0.0));
        }
        for (std::size_t trade = 0; trade < trades; ++trade)
            tape.push_back(signal(std::string()));
        for (std::size_t warm = 0; warm < points; ++warm)
            tick();
    }

    // A wave a symbol, of whole numbers only: the same on every machine.
    [[nodiscard]] static std::uint64_t wave(std::uint64_t at, std::uint64_t period) {
        const std::uint64_t phase = at % (2 * period);
        return phase < period ? phase : 2 * period - phase;
    }

    void tick() {
        ++ticks;
        head = (head + 1) % points;
        for (std::size_t symbol = 0; symbol < symbols; ++symbol) {
            const std::uint64_t period = 37 + symbol * 11;
            const std::uint64_t level =
                wave(ticks * (3 + symbol % 5) + symbol * 17, period) * 1000 / period +
                wave(ticks * 7 + symbol, 13) * 6;
            history[symbol][head] = static_cast<float>(level) / 1080.0f;
            const std::uint64_t cents = 10000 + symbol * 731 + level * 3;
            price[symbol].set(std::format("{}.{:02}", cents / 100, cents % 100));
            change[symbol].set(std::format("{}{}.{:02}%", level >= 500 ? "+" : "−",
                                           (level >= 500 ? level - 500 : 500 - level) / 100,
                                           (level >= 500 ? level - 500 : 500 - level) % 100));
        }
        for (std::size_t level = 0; level < levels; ++level) {
            const std::uint64_t bids = 100 + wave(ticks * 5 + level * 29, 61) * 40,
                                asks = 100 + wave(ticks * 3 + level * 31, 53) * 45;
            bid[level].set(std::format("{}.{:02}  {}", 182, 50 - level, bids));
            ask[level].set(std::format("{}.{:02}  {}", 182, 51 + level, asks));
            bid_depth[level].set(static_cast<double>(bids) / 2600.0);
            ask_depth[level].set(static_cast<double>(asks) / 2500.0);
        }
        for (std::size_t trade = trades - 1; trade > 0; --trade)
            tape[trade].set(tape[trade - 1].peek());
        tape[0].set(std::format("{:02}:{:02}.{:03}   182.{:02}   {}", ticks / 7200 % 60,
                                ticks / 120 % 60, ticks % 120 * 1000 / 120, 40 + ticks % 20,
                                100 + wave(ticks * 13, 97) * 10));
    }

    [[nodiscard]] View view() {
        std::vector<Element> watch, book, prints;
        for (std::size_t symbol = 0; symbol < symbols; ++symbol)
            watch.push_back(
                row({.id = std::format("s{}", symbol), .padding = 6, .gap = 10, .align = "center"},
                    {
                        text({.id = "name",
                              .text = std::format("SYM{:02}", symbol),
                              .style = "label",
                              .width = 64}),
                        painter({.id = "line",
                                 .height = 28,
                                 .grow = 1,
                                 .role = Role::figure,
                                 .label = std::format("SYM{:02} today", symbol),
                                 .paint = [this, symbol](
                                              PaintContext& paint) { sparkline(paint, symbol); }}),
                        text({.id = "price", .text = price[symbol], .align = "end", .width = 84}),
                        text({.id = "change",
                              .text = change[symbol],
                              .style = "caption",
                              .align = "end",
                              .width = 72}),
                    }));
        for (std::size_t level = 0; level < levels; ++level)
            book.push_back(
                row({.id = std::format("l{}", level), .gap = 8, .align = "center"},
                    {
                        progress({.id = "bids", .value = bid_depth[level], .grow = 1}),
                        text({.id = "bid", .text = bid[level], .style = "code", .width = 120}),
                        text({.id = "ask", .text = ask[level], .style = "code", .width = 120}),
                        progress({.id = "asks", .value = ask_depth[level], .grow = 1}),
                    }));
        for (std::size_t trade = 0; trade < trades; ++trade)
            prints.push_back(
                text({.id = std::format("t{}", trade), .text = tape[trade], .style = "code"}));
        return row({.id = "board", .padding = 12, .gap = 12, .background = "background"},
                   {
                       column({.id = "watch", .width = 620, .gap = 2}, std::move(watch)),
                       column({.id = "book", .gap = 4, .grow = 1}, std::move(book)),
                       column({.id = "tape", .width = 360, .gap = 2}, std::move(prints)),
                   });
    }
private:
    void sparkline(PaintContext& paint, std::size_t symbol) const {
        const float width = paint.size.width, height = paint.size.height;
        mrender::Path path;
        for (std::size_t point = 0; point < points; ++point) {
            const float value = history[symbol][(head + 1 + point) % points];
            const Point at{static_cast<float>(point) * width / static_cast<float>(points - 1),
                           height - 2 - value * (height - 4)};
            if (point == 0)
                path.move_to(at);
            else
                path.line_to(at);
        }
        paint.list.stroke_path(path, {.width = 1.5f},
                               mrender::Paint::solid(paint.theme.color("accent")));
    }
};

// A tick, then the next one 1/120 s later; the sparklines asked to paint again.
void every_tick(Runtime& runtime, Market& market) {
    market.tick();
    for (std::size_t symbol = 0; symbol < Market::symbols; ++symbol)
        if (const NodeId line = runtime.find(std::format("main/watch/s{}/line", symbol)))
            runtime.invalidate_paint(line);
    runtime.after(seconds(1.0 / 120.0), [&runtime, &market] { every_tick(runtime, market); });
}

}  // namespace

int main(int argc, char** argv) {
    Market market;
    AppSpec app{.id = "dev.omui.bench.market", .title = "Market", .size = {1896, 1030}};
    app.view = [&market] { return market.view(); };
    app.setup = [&market](Runtime& runtime) {
        runtime.after(seconds(1.0 / 120.0), [&runtime, &market] { every_tick(runtime, market); });
    };
    return run(std::move(app), argc, argv);
}
