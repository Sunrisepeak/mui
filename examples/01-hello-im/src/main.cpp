// Example 01: hello, immediate mode.
import std;
import omui;
import omui.example.hello_im;

int main(int argc, char** argv) {
    hello::State state;
    return omui::run(hello::app(state), argc, argv);
}
