// openmui.build.embed — the build plugin for data files a program carries
// (D-73's embed plugin): each file becomes a partition holding its text
// (`omui-gen embed`), so a shipped program reads no file to have them. Theme
// packages and canvas programs use it for their own files: themes() and
// behaviors() are embed() with their directory, suffix and tag.
export module openmui.build.embed;

import std;
import mcpp;
export import openmui.build.plan;

export namespace openmui::build {

struct embed_options {
    std::string module;                                   // the package's module
    std::string ns;                                       // the namespace of the texts (default: the module's)
    std::string dir;                                      // where the files are
    std::string suffix;                                   // which: ".style.omui.toml"
    std::string tag;                                      // the partitions' and constants' suffix: <stem>_<tag>
    bool        index = false;                            // and <tag>_files, a table of them all (the module exports it)
    std::string out_dir = std::string(mcpp::out_dir());   // generated files go under out_dir/openmui
};

// Each file `<stem><suffix>` in `dir` as a partition `<module>:<stem>_<tag>`
// holding its text (`-` and `.` in the stem become `_`), and with `index`,
// `<module>:<tag>_files` a table of them all by file name (the module's
// interface exports it, as it does the others): a program carries its data
// files and reads none to have them.
[[nodiscard]] std::vector<edge> plan_embed(const embed_options& opt);
bool embed(embed_options opt);

// ── theme packages (openmui.theme) ──────────────────────────────────────────

struct theme_options {
    std::string module;                                   // the package's module (acme.themes)
    std::string ns;                                       // the namespace of the embedded texts (default: the module's)
    std::string themes = "themes";                        // the directory of *.theme.omui.toml
    std::string out_dir = std::string(mcpp::out_dir());
};

// Each theme file as a partition `<module>:<stem>_theme` holding its text:
// the package adds it to the program's library with
// omui::theme::library().add(<stem>_theme, "<stem>.theme.omui.toml").
bool themes(theme_options opt);

// ── declarative behaviours (canvas-kit) ─────────────────────────────────────

struct behavior_options {
    std::string module;                                   // the package's module (acme.wall)
    std::string ns;                                       // the namespace of the embedded texts (default: the module's)
    std::string behaviors = "behaviors";                  // the directory of *.behavior.omui.toml
    std::string out_dir = std::string(mcpp::out_dir());
};

// Each behaviour file as a partition `<module>:<stem>_behavior` holding its
// text: the program loads it with
// omui::world::load_behaviors(<stem>_behavior, "<stem>.behavior.omui.toml"),
// so a shipped program carries its behaviours and reads no file to have them.
bool behaviors(behavior_options opt);

}  // namespace openmui::build
