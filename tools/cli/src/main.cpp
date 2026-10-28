// omui — the openmui command line.
import std;
import openmui.cli;

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    return omui::cli::run(args, std::cout, std::cerr);
}
