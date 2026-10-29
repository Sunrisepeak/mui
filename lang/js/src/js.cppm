// openmui.lang.js — JavaScript and TypeScript on the Script Binding Profile
// (component-binding §5.2, §7), with QuickJS-ng.
//
//     import { signal, fmt, column, text, button, mount } from "openmui";
//     const count = signal(0);
//     mount("main", column({ gap: 8 }, [
//       text({ text: fmt("{} clicks", count) }),
//       button({ id: "inc", text: "+1", onActivated: () => count.update(v => v + 1) }),
//     ]));
//
// The module `openmui` exports a builder for every registered component (in
// camel case: textBox), `signal` and expressions (`fmt`, `select`, `eq` …,
// and methods: count.add(1)), regions (`when`, `each`, `slot`), `mount`,
// `verb` / `invoke`, `model`, `find` / `act` / `snapshot`, `tr`, `log`. Props
// are camel case (maxWidth is max_width), handlers `on<Event>`. A call that
// fails throws an OmuiError. TypeScript is compiled to JavaScript before it
// runs (esbuild at build time; the generated .d.ts types the builders).
export module openmui.lang.js;

import std;
import openmui.lang;
import openmui.player;

export namespace omui::lang::js {

class Script {
public:
    explicit Script(Session& session);
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // Runs an ES module on the script thread; the future answers when it ran
    // (its top-level promise settled).
    [[nodiscard]] std::future<Result<void>> run(std::string code, std::string name = "main.js");
    // Runs it again in place: signals keep their values (the n-th signal() of
    // the module is the last run's, or the one of the same key), views are
    // reconciled, handlers replaced.
    [[nodiscard]] std::future<Result<void>> reload(std::string code, std::string name = "main.js");

    void on_error(std::function<void(std::string_view)> fn);

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;  // shared with the runs queued for it (they find it gone, not freed)
};

// JavaScript for the player: `lang = "js"`, *.js and *.mjs.
[[nodiscard]] player::Language javascript();
// TypeScript for the player: `lang = "ts"`, *.ts and *.tsx, compiled with
// esbuild (found on PATH, or OMUI_ESBUILD) when it is loaded; `openmui` stays
// an import, the rest of the program's imports are bundled.
[[nodiscard]] player::Language typescript();
// What esbuild makes of a TypeScript file: one ES module.
[[nodiscard]] std::expected<std::string, std::string> compile_typescript(const std::filesystem::path& file);
// What esbuild makes of TypeScript source (an editor's text, `name` in its
// messages): the module transformed, its imports left as they are.
[[nodiscard]] std::expected<std::string, std::string> compile_typescript_source(std::string_view source, std::string_view name = "main.ts");

}  // namespace omui::lang::js
