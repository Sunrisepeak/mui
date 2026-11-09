// omui.agent:generation — runtime generation as an application ships (D-144,
// convergence plan W5): interface an Agent makes while the application runs,
// put into the surfaces the application declares, through stages each asked
// for and each readable —
//
//     draft → validated → prepared → previewed → committed
//
// A draft is a view document (or, where generation is open to code, code that
// makes one, evaluated sandboxed). It is checked against what the application
// registers, the limits and how open generation is; built without being shown;
// shown unseen for a picture before and after; then made its surface's next
// version, which undo takes back. Nothing a draft says runs before it is
// validated, and a draft that fails changes nothing.
//
//     omui::agent::Generation generation(runtime, {
//         .openness = omui::agent::Openness::data,
//         .surfaces = {{"extras", "cards the user's agent adds under the plan"}},
//     });
//
// The operations are generate.ops.omui.ts's (the application's verbs, served
// over the protocol and MCP); the class's functions are the same steps for a
// host's own code.
export module omui.agent:generation;

import std;
import omui.core;
import omui.view;
import omui.viewdoc;
import :protocol;

export namespace omui::agent {

// How open runtime generation is (the convergence plan §2.8): the developer
// sets it in omui.config.ts for development and a release, the user's
// settings and the session only narrow it.
//   off      nothing generated is taken
//   data     view documents only: no code, no verbs invoked
//   limited  code may make the document; only the components and verbs listed
//   all      code and documents, with every registered component and verb
enum class Openness : std::uint8_t { off, data, limited, all };
[[nodiscard]] std::string_view openness_name(Openness openness) noexcept;
[[nodiscard]] std::optional<Openness> parse_openness(std::string_view text) noexcept;

// A place generated interface may go: a slot the application's view shows
// (a view document's `slot`, a script's slot()), and what it is for.
struct Surface {
    std::string name;
    std::string doc;
};

// Code that makes a view document (limited, all): its language ("ts") and
// text in, the document's JSON out, or why not. The host gives it, sandboxed
// and bounded (the player's QuickJS: memory, time).
using EvaluateFn = std::function<std::expected<std::string, std::string>(std::string_view language, std::string_view code)>;

struct GenerationOptions {
    Openness             openness = Openness::data;
    std::vector<Surface> surfaces;
    // A draft's limits: its document's bytes, and its nodes (elements,
    // conditions, lists, slots).
    std::size_t max_bytes = 256 * 1024;
    std::size_t max_nodes = 2000;
    // limited: the components a draft may use and the verbs it may invoke.
    std::vector<std::string> components, verbs;
    // The previews' pictures: the runtime's (Runtime::capture_png, what its
    // host draws) unless given.
    CaptureFn  capture;
    EvaluateFn evaluate;
    // The host's signals a document's `"provided": true` ones bind to.
    std::function<std::optional<Signal<Value>>(std::string_view name)> provide;
    // Where the versions are kept (their documents and an index), to be
    // shown again when the application starts — each one checked again
    // first, against what is registered and how open generation is then; one
    // that no longer passes is not shown, and generate.surfaces says why.
    // Empty: kept only while it runs.
    std::filesystem::path directory;
};

// Registers the generate.* verbs with the runtime for as long as it lives.
class Generation {
public:
    Generation(Runtime& runtime, GenerationOptions options);
    ~Generation();
    Generation(const Generation&) = delete;
    Generation& operator=(const Generation&) = delete;

    // The verbs' steps (generate.ops.omui.ts says each one's arguments and answer).
    VerbResult surfaces() const;
    VerbResult draft(const Value& args);
    VerbResult preview(const Value& args);
    VerbResult commit(const Value& args);
    VerbResult restore(const Value& args);
    VerbResult versions(const Value& args) const;
    VerbResult drafts() const;
    VerbResult discard(const Value& args);

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;  // shared with what the history keeps to undo a commit
};

}  // namespace omui::agent
