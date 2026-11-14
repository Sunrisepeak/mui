// Example 01: hello, immediate mode.
import std;
import omui;
import hello;

int main(int argc, char** argv) {
    hello::State state;
    return omui::run(hello::app(state), argc, argv);
}
