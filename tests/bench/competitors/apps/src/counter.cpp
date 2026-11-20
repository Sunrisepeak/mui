// counter: "Count: N" and a button that adds one — the counter of tests/bench's core/* scenes, in a
// window.
import std;
import omui;

int main(int argc, char** argv) {
    using namespace omui;
    const Signal<std::int32_t> count = signal(std::int32_t{0});
    AppSpec app{.id = "dev.omui.bench.counter", .title = "Counter", .size = {800, 600}};
    app.view = [count] {
        return column({.id = "counter", .padding = 24, .gap = 12, .background = "background"},
                      {
                          text({.id = "value",
                                .text = computed(
                                    [count] { return std::format("Count: {}", count.get()); }),
                                .style = "title"}),
                          button({.id = "inc",
                                  .text = "Increment",
                                  .on_activated = [count] { count.set(count.peek() + 1); }}),
                      });
    };
    return run(std::move(app), argc, argv);
}
