// mlibs.build.embed — the build plugin for data files a program carries (D-73): each file becomes a partition
// holding its text (`xdef embed`, mlibs.xdef's emitter), so a shipped program reads no file to have them; with
// `index`, one table of them all. A TypeScript data module is its owner's to carry, evaluated with its kinds: omui's
// build plugin (omui.build.embed) plans those on top of these edges.
export module mlibs.build.embed;

import std;
import mcpp;
export import mlibs.build.plan;

export namespace mlibs::build {

struct embed_options {
    std::string module;     // the package's module
    std::string ns;         // the namespace of the texts (default: the module's)
    std::string dir;        // where the files are
    std::string suffix;     // which: ".locale.xdef.json"
    std::string tag;        // the partitions' and constants' suffix: <stem>_<tag>
    bool        index = false;  // and <tag>_files, a table of them all (the module exports it)
    // The files left out (a feature the build does not have): no partition a file, and with `index` an empty
    // table, so the code reading it builds the same and finds nothing.
    bool        left_out = false;
    std::string out_dir = std::string(mcpp::out_dir());  // generated files go under out_dir/<subdir>
    std::string subdir = "embed";
};

// Each file `<stem><suffix>` in `dir` as a partition `<module>:<stem>_<tag>` holding its text (`-` and `.` in the stem
// become `_`), made by `xdef embed`; with `index`, `<module>:<tag>_files`, a table of them all by file name (a data
// module, `*.xdef.ts`, listed by its snapshot's name, `.xdef.json`). Edges, for a plugin to adjust before submitting.
[[nodiscard]] std::vector<edge> plan_embed(const embed_options& opt);
// The edges planned and submitted; false (and why, on stderr) when nothing could be.
bool embed(embed_options opt);

}  // namespace mlibs::build
