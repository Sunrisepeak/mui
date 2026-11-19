// hello: a title and a button, nothing else (the same app as Slint's, Flutter's and NSH's).
import std;
import omui;

int main(int argc, char** argv) {
    using namespace omui;
    AppSpec app{.id = "dev.omui.bench.hello", .title = "Hello", .size = {800, 600}};
    app.view = [] {
        return column({.id = "hello", .padding = 24, .gap = 12, .background = "background"},
                      {
                          text({.id = "title", .text = "Hello, world", .style = "title"}),
                          button({.id = "click", .text = "Click me"}),
                      });
    };
    return run(std::move(app), argc, argv);
}
