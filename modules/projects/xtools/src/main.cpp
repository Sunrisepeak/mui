// xtools — the toolchain whole: its command line (xtools::run), and nothing else.
import std;
import xtools;

int main(int argc, char** argv) {
    const std::vector<std::string> arguments(argv, argv + argc);
    return xtools::run(arguments, std::cin, std::cout, std::cerr);
}
