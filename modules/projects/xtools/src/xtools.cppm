// xtools — the toolchain whole (convergence plan §4.5, D-203): every project's toolkit behind one tool kernel (T0).
// `run` is the command line: the program is it and nothing else, and a test asks it the same way.
export module xtools;

import std;
export import mlibs.agent;

export namespace xtools {

// A command line (its words, the program's name first): records to `out`, what went wrong to `errors`; the exit
// code. `mcp` serves the toolkits on `in` and `out` until `in` ends.
int run(std::span<const std::string> argv, std::istream& in, std::ostream& out, std::ostream& errors);

}  // namespace xtools
