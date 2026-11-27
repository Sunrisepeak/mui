// oweb-reader: the page a browser has, drawn (oweb.reader) — the program every page of a site loads, its live
// examples run where a page holds one (the world's part only such a page fetches: build.mcpp).
import oweb.reader;

int main(int argc, char** argv) { return oweb::reader::program(argc, argv, &oweb::shell::install_examples); }
