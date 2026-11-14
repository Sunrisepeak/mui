// An Inspector workspace for hanim animation documents (Part 2 §4.3, H: the
// person looks, points, tunes): the stage drawn at any moment by the same
// renderer, a moment and an object pointed at to the steps that govern
// them, a step's numbers as controls, each change one step of history. The
// same operations are the workspace's verbs, so an Agent tunes as the person
// does, and the person sees what it changed.
export module omui.inspector:motion;
import std;
import omui;

export namespace omui::inspector::motion {

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Runtime* runtime = nullptr;  // borrowed; the host owns its lifetime
    Signal<std::int64_t> changed = signal(std::int64_t{0});
    Signal<double> time = signal(0.0);                   // the moment shown, in seconds
    Signal<std::string> selected = signal(std::string());  // the JSON pointer of the step being tuned
    Signal<std::string> problem = signal(std::string());

    // Read: snapshot, at, knobs, document. Write: open, seek, set, undo, redo, save.
    //   open {file} | {text}        a document (its problems refuse it; what was open stays)
    //   seek {time}                 the moment shown
    //   at {time?, object?}         the steps that govern it, innermost first (Studio::at)
    //   knobs {where}               a step's numbers, words and switches: key, JSON pointer, value
    //   set {where, value}          one value at a JSON pointer; the document read again whole,
    //                               refused with its problems (nothing changes) when it does not read
    //   undo, redo                  a set at a time
    //   save {file?}                the document as it is now
    [[nodiscard]] VerbResult perform(std::string_view operation, const Value& args = {});
    // The stage at the moment shown, fitted into `dest`.
    void draw(xrender::DisplayList& list, Rect dest);
    // The object drawn at `at` (in `dest`'s coordinates, the stage fitted into it) at the moment shown.
    [[nodiscard]] std::string object_at(Point at, Rect dest);
    [[nodiscard]] double duration() const;
    [[nodiscard]] Size stage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void add_verbs(AppSpec&, const std::shared_ptr<Session>&);
[[nodiscard]] Element view(const std::shared_ptr<Session>&, float width);
// The workbench over `file` (opened at setup; its problems shown).
[[nodiscard]] AppSpec app(const std::shared_ptr<Session>&, std::filesystem::path file);

}  // namespace omui::inspector::motion
