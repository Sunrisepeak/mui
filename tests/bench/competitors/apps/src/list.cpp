// list: the 10,000-row list of tests/bench's core/list-scroll in a window of 1896 × 1030 — three
// parts a row (a title, a state, a button), virtual (list_view). The harness scrolls it with the
// wheel and measures what the window presents from outside (omui-devtools bench apps).
import std;
import omui;

int main(int argc, char** argv) {
    using namespace omui;
    AppSpec app{.id = "dev.omui.bench.list", .title = "Orders", .size = {1896, 1030}};
    app.setup = [](Runtime& runtime) {
        std::vector<Row> rows;
        for (int i = 0; i < 10000; ++i)
            rows.push_back(
                {std::format("o{}", i),
                 Value{}.set("title", std::format("Order {} — {} shares at {}.{:02}", i,
                                                  100 + i % 900, 10 + i % 400, i % 100))});
        (void)runtime.model("orders").push(std::move(rows));
    };
    app.view = [] {
        return column({.id = "page", .background = "background"},
                      {list_view({.id = "orders",
                                  .estimate = 36.0,
                                  .model = "orders",
                                  .height = Dim(mlibs::layout::pct(100))},
                                 [](const Row& entry, std::size_t index) {
                                     return row({.id = "line",
                                                 .padding = 8,
                                                 .gap = 12,
                                                 .align = "center"},
                                                {text({.id = "title",
                                                       .text = Model::label_of(entry),
                                                       .grow = 1}),
                                                 text({.id = "state",
                                                       .text = index % 3 == 0 ? "filled" : "open",
                                                       .style = "caption"}),
                                                 button({.id = "open", .text = "Open"})});
                                 })});
    };
    return run(std::move(app), argc, argv);
}
