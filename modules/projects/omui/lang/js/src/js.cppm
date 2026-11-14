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
// fails throws an OmuiError. TypeScript runs as the JavaScript left when its
// types are blanked (omui.lang.ts, in this process, the Web's too; the
// generated .d.ts types the builders, tsc checks them in `omui check`).
export module omui.lang.js;

export import omui.lang.ts;
import std;
import omui.lang;
import omui.player;

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
    // The same, waited for, from the runtime's thread, which meanwhile does
    // what the script asks of it (Live Design's edits, D-149).
    [[nodiscard]] Result<void> reload_now(std::string code, std::string name = "main.js");
    // A module it imports by path changed (hot reload, Live Design): its new
    // text (none: the file's again). The next run evaluates it again — it and
    // the script, the other modules staying as they are.
    void module_changed(const std::filesystem::path& file, std::optional<std::string> text = std::nullopt);
    // Development mode: every element it builds from now carries where it was
    // built (`file:line:column`), so a node maps to its source and back.
    void keep_built_at(bool on);
    // The files of the modules it has imported by path.
    [[nodiscard]] std::vector<std::filesystem::path> modules() const;

    void on_error(std::function<void(std::string_view)> fn);

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;  // shared with the runs queued for it (they find it gone, not freed)
};

// JavaScript for the player: `lang = "js"`, *.js and *.mjs.
[[nodiscard]] player::Language javascript();
// TypeScript for the player: `lang = "ts"`, *.ts and *.mts, its types
// blanked when it is loaded (omui.lang.ts); the modules it imports by path
// ("./label") are loaded the same way as they are imported.
[[nodiscard]] player::Language typescript();

}  // namespace omui::lang::js

// The module's own, for its units: TypeScript's declarations for a type checker
// (declare.cpp; the TypeScript language's `declare`).
namespace omui::lang::js {
// The runtime's component catalogue (omuiCatalog's JSON) as the declaration writer's shapes.
[[nodiscard]] std::vector<ts::ComponentShape> components_of(const omui::lang::Json& catalog);
[[nodiscard]] std::expected<std::vector<std::filesystem::path>, std::string> declare_typescript(std::string_view catalog_json, std::string_view natives_json,
                                                                                                const std::filesystem::path& root, const std::filesystem::path& dir);
}
