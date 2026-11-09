// openmui.player — an application's views, components and scripts, run and
// reloaded in place (component-binding §9, §10.2).
//
//     omui::player::Player p(runtime, manifest, {omui::lang::js::typescript()});
//     if (auto r = p.start(); !r) …;
//     p.watch();   // saved files show at once
//
// Views and components are view documents (openmui.viewdoc); a script runs in
// a Language, a plugin the entry registers (the player knows none itself), on
// a thread of its own, over the C ABI. A file that changes is reloaded in
// place: a view keeps its nodes and state, a component its instances, a
// script its signals. A file with an error changes nothing; the error is kept
// in problems() and logged.
export module omui.player;

import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.semantic;

export namespace omui::player {

struct Script {
    std::string           lang;   // a registered Language's name: ts, js …
    std::filesystem::path entry;
};

// [package.metadata.openmui] of an application (the entry reads it).
struct Manifest {
    std::string                        id = "openmui-app";
    std::string                        title = "openmui";
    Size                               size{960, 640};
    std::filesystem::path              base;        // the paths below are relative to it
    std::vector<std::filesystem::path> components;  // component documents, defined first
    std::vector<std::filesystem::path> views;       // view documents, mounted at their files' stems
    std::vector<std::filesystem::path> themes;      // theme files (*.theme.omui.ts), added to the program's library
    std::vector<std::string>           theme;       // the composition to start with: a theme, then overlays
    std::filesystem::path              effects;     // a directory of the application's effects (*.effect.omui.ts)
    std::vector<Script>                scripts;
};

using Status = std::expected<void, std::string>;

// One script's machine (a JavaScript context …).
class Engine {
public:
    virtual ~Engine() = default;
    [[nodiscard]] virtual std::future<Status> run(std::string code, std::string name) = 0;
    // Runs the new text in place: the script's signals keep their values.
    [[nodiscard]] virtual std::future<Status> reload(std::string code, std::string name) = 0;
    // The same, waited for on the runtime's thread, which meanwhile does what
    // the script asks of it (a verb that edits a script: Live Design). A
    // language that cannot says so.
    [[nodiscard]] virtual Status reload_now(std::string code, std::string name) {
        (void)code;
        (void)name;
        return std::unexpected(std::string("this language runs its scripts again from their files only"));
    }
    // A module the script imports by path changed: its new text, or none (the
    // file's again); the next run evaluates it again. Default: no such modules.
    virtual void module_changed(const std::filesystem::path& file, std::optional<std::string> text) {
        (void)file;
        (void)text;
    }
    // Development mode: the elements its scripts build carry where they were
    // built, so a node maps to its source and back (design.locate). Default:
    // a language that cannot keeps none.
    virtual void keep_built_at(bool on) { (void)on; }
    // The files of the modules it has imported by path: watched as its own file is.
    [[nodiscard]] virtual std::vector<std::filesystem::path> modules() const { return {}; }
    // Nothing left to do on its thread, and nothing in flight to the runtime.
    [[nodiscard]] virtual bool idle() const = 0;
};

// Live Design's structured operations as edits of a script's text, at the
// builder call where an element was built (built_at: 1-based line and column): each
// answers the whole new text, or why it cannot (not a builder call there).
struct SourceEditor {
    // A property (as its contract names it) set to `value`, code (a JSON
    // value's text is); none: the property taken out.
    std::function<std::expected<std::string, std::string>(std::string_view text, std::size_t line, std::size_t column, std::string_view property,
                                                          std::optional<std::string_view> value)>
        set;
    // `element` (code) inserted among the call's children at `index` (none: the end).
    std::function<std::expected<std::string, std::string>(std::string_view text, std::size_t line, std::size_t column, std::string_view element,
                                                          std::optional<std::size_t> index)>
        insert;
    // The call taken out of the list it is in.
    std::function<std::expected<std::string, std::string>(std::string_view text, std::size_t line, std::size_t column)> remove;
};

struct Language {
    std::string              name;        // as manifests write it
    std::vector<std::string> extensions;  // .ts, .js …
    std::function<std::unique_ptr<Engine>(abi::OmuiRuntime)> make;
    // The source of an entry: the file, or what a compiler makes of it
    // (TypeScript with its types blanked). Empty: the file as it is.
    std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> load;
    // The same from text not yet in the file (Live Design's edits), named as
    // the file. Empty: the text as it is.
    std::function<std::expected<std::string, std::string>(std::string_view text, const std::filesystem::path& file)> load_text;
    // The language's API over the running application's components (their
    // catalogue, as omuiCatalog gives it), as its code writes it — what an
    // agent needs to write a script (design.api, D-145). Empty: none.
    std::function<std::string(std::string_view catalog_json)> describe;
    // The language's declarations for a type checker (`omui check`, W1 step
    // 5), written into `dir`: its API, a builder for every component of the
    // catalogue (omuiCatalog), the native modules (omuiNativeCatalog), and
    // the checker's configuration over the application's sources under
    // `root`. Answers the files it wrote. Empty: none.
    std::function<std::expected<std::vector<std::filesystem::path>, std::string>(
        std::string_view catalog_json, std::string_view natives_json, const std::filesystem::path& root, const std::filesystem::path& dir)>
        declare;
    // Structured operations as source edits (design.set, design.insert,
    // design.remove). Empty: the language's text is edited whole (design.edit).
    SourceEditor edit;
    // A data module of the language (`brand.theme.omui.ts`, D-139) as the
    // JSON snapshot it stands for, `{"theme": …}`: how the player reads an
    // application's themes. Empty: the language writes no data.
    std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> data;
};

class Player;
// A twin (D-149): the application again in this process, unseen and headless
// — its own runtime and player, the same manifest and languages — whose time
// the caller steps. What a preview or a variant looks like (a stand-in), the
// design's film, a motion measured: the application the person sees is never
// touched. The entry makes them (a headless host); the designer works in them.
class Twin {
public:
    virtual ~Twin() = default;
    [[nodiscard]] virtual Player& player() = 0;
    [[nodiscard]] virtual Runtime& runtime() = 0;
    // Its clock; a frame at a time on it (the next when none).
    [[nodiscard]] virtual seconds time() = 0;
    virtual void step(std::optional<seconds> at = std::nullopt) = 0;
    // Its scripts done, nothing moving.
    virtual void settle() = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>, std::string> png() = 0;
    // Its picture with a camera on `frame` (logical pixels): that region filling the window (a film's
    // close-up), the slot `fixed` (a caption) drawn over it as it is.
    [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>, std::string> framed_png(Rect frame, std::string_view fixed) = 0;
};
// A twin started (`design`: in development mode, its edits presented as the
// person's are), or why not.
using TwinFn = std::function<std::expected<std::unique_ptr<Twin>, std::string>(bool design)>;

// Live Design's designer (omui.designer, D-149): the design verbs, the
// history, previews and the timeline, made for a player in development mode
// when the program links one — it says so as it starts (provide_designer). A
// release links none: development mode then says it has no designer.
class Designer {
public:
    virtual ~Designer() = default;
};
using DesignerFactory = std::unique_ptr<Designer> (*)(Player& player);
void provide_designer(DesignerFactory factory) noexcept;

struct PlayerOptions {
    // Development mode (D-149): the application is its own designer. The
    // design.* verbs read the scripts' TypeScript, run new text within the
    // frame (its semantic changes answered, or its problems, nothing changed),
    // and commit it to the files or discard it. Left out of a release.
    bool design = false;
    // Twins of the application: previews, the variants board, the history's
    // pictures and film, motion measured (the designer's); without them those
    // say so.
    TwinFn twin;
};

class Player {
public:
    Player(Runtime& runtime, Manifest manifest, std::vector<Language> languages, PlayerOptions options = {});
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Components, then views, then scripts. Errors are reported together.
    Status start();
    // Reloads what changed on disk since the last look; answers what it
    // reloaded (its paths). Errors go to problems().
    std::vector<std::string> poll();
    // Runs these texts in place of the sources' (by path, as design.sources
    // names them), as design.edit runs an edit: what a stand-in shows.
    Status show(const std::map<std::string, std::string>& texts);

    // ── The sources, as Live Design's designer (omui.designer) works with them ──
    // A source of the application: a script, or a module a script imports by path.
    struct Source {
        std::string path;         // from the manifest's directory, as the design verbs name it
        std::string language;     // its script's language
        std::string imported_by;  // a module: the script that imports it (empty for a script)
        bool        edited = false;  // runs an edit not written to its file
    };
    [[nodiscard]] std::vector<Source> sources() const;
    // The text a source runs now: its edit, or its file's.
    [[nodiscard]] std::optional<std::string> text(std::string_view path) const;
    // New text for a source, run at once with its state kept: the semantic
    // changes and the time — or its problems, and what ran before runs again.
    std::expected<Value, std::string> edit(std::string_view path, std::string_view text);
    // Every edit written to its file (the TypeScript stays the one source):
    // each file's text before. Every edit dropped: the texts dropped.
    std::expected<Value, std::string> commit_edits();
    Value discard_edits();
    // The language a source is written in (a module's: its script's).
    [[nodiscard]] const Language* language_of(std::string_view path) const;
    // A file as the sources name it, and a source's file.
    [[nodiscard]] std::string source_name(const std::filesystem::path& file) const;
    [[nodiscard]] std::filesystem::path source_file(std::string_view path) const;
    [[nodiscard]] Runtime& runtime() const noexcept;
    [[nodiscard]] const PlayerOptions& options() const noexcept;
    // The languages it runs scripts in (a twin of the application runs the same).
    [[nodiscard]] const std::vector<Language>& languages() const noexcept;
    // Polls on the runtime's timers, every `period`.
    void watch(seconds period = seconds(0.1));

    // The scripts are done for now (tests and `--headless` wait for it).
    [[nodiscard]] bool idle() const;
    [[nodiscard]] std::span<const std::string> problems() const noexcept;
    // Each language's declarations for a type checker, into `dir`, from what
    // this application registered (after start: its component documents
    // too). Answers the files written.
    [[nodiscard]] std::expected<std::vector<std::filesystem::path>, std::string> declare(const std::filesystem::path& dir) const;
    [[nodiscard]] const Manifest& manifest() const noexcept;
    [[nodiscard]] abi::OmuiRuntime handle() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::player
