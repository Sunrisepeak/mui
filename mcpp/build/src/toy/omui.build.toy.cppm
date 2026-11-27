// openmui.build.toy — the build plugin for Bilibili Toy (D-84 §6.5): the pack
// format `dist-toy` (mcpp's pack_format mechanism). A program built for
// wasm32-emscripten becomes a Toy package — a small index.html, the program
// and the fonts under content-hashed names, relative paths, hash routes as
// the program's flags (omui-gen dist-toy says the rest). Without Toy's SDK
// it is a plain static page (the site's).
//
//     // build.mcpp
//     omui::build::dist_toy({.target = "omui-fight", .title = "格斗"});
//
//     mcpp pack -p <package> --target wasm32-emscripten --release --pkg-format dist-toy
//
// The package lands in <out_dir>/toy/<target>/, what is in it beside it in
// <target>.toy.xdef.json. Checking it with Toy's toy_doctor.py is
// omui-devtools check toy's; publishing is the publisher's (`toy create`).
export module omui.build.toy;

import std;
import mcpp;
export import omui.build.plan;

export namespace omui::build {

// Files the page fetches once the program is drawn (not before it starts):
// each of `dir`'s files ending in `suffix`, a hashed asset, written to
// `mount` (under /later/) in the program's file system — /later/.ready
// once all are there.
struct later_files {
    std::string dir;     // absolute
    std::string suffix;  // ".webp"
    std::string mount;   // "/later/fighters-art"
};

// A font cut down to the characters these text files and directories hold
// (absolute paths; directories recursively), with the characters people type
// (openmui.text's `<stem>.common.txt`) and the punctuation and Latin every page has.
struct font_subset {
    std::string              font;  // "NotoSansSC"
    std::vector<std::string> text;
};

struct toy_options {
    std::string              target;                 // the program target
    std::string              title;                  // the page's <title>
    std::vector<std::string> fonts;                  // openmui.text's default fonts by stem (Inter, NotoSansSC); empty: all
    std::string              background = "#000000"; // behind the canvas while it loads
    std::string              background_dark;        // that colour where the person prefers a dark appearance (a page that follows it)
    std::string              load = "eager";         // when the page takes its program: eager, idle, interaction (D-260)
    bool                     sdk = true;             // the page loads Toy's SDK (window.toy)
    // A world: the page of a program that draws documents it was not built with (one program for every page of a
    // site) — index.html a page waiting for its document, which a site's builder gives each of its pages
    // (omui-gen dist-toy --world; oweb's `engine`).
    bool                     world = false;
    std::vector<later_files> later;                  // fetched after the program is drawn
    std::vector<font_subset> subsets;                // fonts cut down to the program's characters
    // Fonts for some languages ({"NotoSansSC", {"zh"}}): before the program
    // starts only when the page opens in one; else when the program asks —
    // text with a glyph no face it has covers (the language switched to, a
    // word typed), or a reader wanting every face (Runtime::want_fonts).
    std::vector<std::pair<std::string, std::vector<std::string>>> font_languages;
    // Before a deferred font arrives, these literal UTF-8 characters use a tiny
    // subset of that same face. A page opened in its language loads the full face only.
    // With no language rule, every page starts with this subset, then takes the full face.
    std::vector<std::pair<std::string, std::string>> font_initial;
    // The characters these files and directories hold, added to a font's initial subset (its page's own text,
    // so the subset draws all of it).
    std::vector<font_subset> font_initial_text;
    // Fonts with an initial subset whose full face is fetched only when text asks for it (a character the
    // subset lacks, in that family or any), not once the program is drawn.
    std::vector<std::string> fonts_on_demand;
    // The page a document before its program runs (D-236; omui-gen dist-toy-document): the first screen's
    // projection, made beforehand by the program run natively (`<program> --static-html FILE`; `omui build --web`
    // runs it) — absolute, or relative to the package; packed without one while the file is not there yet.
    std::string              static_html;
    std::string              lang;         // the document's language (en, zh-CN)
    std::string              description;  // what a search result and a share card say
    std::string              canonical;    // the page's address
    std::string              image;        // a share card's picture
    std::vector<std::pair<std::string, std::string>> alternates;  // the page in another language: hreflang, address
    // The application's own files and directories (a directory's files, recursively), relative to the package: in
    // the program's file system before main, at the same paths under / — a TypeScript application's manifest and
    // scripts, which the player reads from its working directory. Carried in the page itself. Data modules
    // (omui.config.ts, *.xdef.ts) go as their snapshots (omui.config.json, *.xdef.json): a page does not evaluate them.
    std::vector<std::string> files;
    // A program in parts (a world's: the Web-program size design, 2026-11-27): `first` names another program target
    // of the package — the same program built without the part (its call to it left out) — and the packed program is
    // cut in two by what that one carries: its functions are the first part, the rest a part of its own, fetched only
    // by a page whose document holds what `needs` selects (a CSS selector: "code.language-view"), where main waits for
    // it; elsewhere a function of it called brings it in then. Empty: one program, whole.
    struct part_options {
        std::string first;
        std::string needs;
    };
    part_options             part;
    std::string              out_dir = std::string(mcpp::out_dir());
};

// Declares the format (always: so `--pkg-format` can name what is
// available) and, when the pack asks for it, the edge that makes the package.
bool dist_toy(toy_options opt);

}  // namespace omui::build
