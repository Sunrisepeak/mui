// omui.agent:a2ui — A2UI as an input format of runtime generation (W5 step
// 3, D-137: the model of agent interaction is openas's, A2UI one adapter).
// An agent that speaks A2UI streams its messages; each surface that has
// begun rendering becomes a view document, again after every change, which
// generate.a2ui drafts (and commits, when asked) like any other draft.
//
// Read: A2UI v0.8's server-to-client messages — surfaceUpdate (components
// as a flat list by id), dataModelUpdate (the data they bind to),
// beginRendering (the root) and deleteSurface. Components: Text, Image,
// Row, Column, List, Card, Button, CheckBox, TextField, Slider, Divider.
// Values are literals or data paths: a path becomes a signal (a two-way
// binding where the component takes input). A Button's action is emitted
// as `action` {name, surfaceId, sourceComponentId, context} — what
// generate.actions hands back to the agent. Text an agent sent is marked
// `remote` (S-3). What is not read yet (Tabs, Modal, MultipleChoice,
// DateTimeInput, Video, AudioPlayer, Icon, templates) is named as a problem
// with where it is; nothing is guessed.
export module omui.agent:a2ui;

import std;
import omui.core;

export namespace omui::agent {

class A2uiReader {
public:
    struct Rendered {
        std::string surface;   // the A2UI surface
        std::string document;  // its view document (JSON)
    };
    struct Read {
        std::vector<Rendered>    rendered;  // the surfaces that changed and render
        std::vector<std::string> deleted;
        std::vector<std::string> problems;  // each with the message and component it is about
    };

    // Messages: one, a JSON array of them, or JSON lines (a stream's chunk,
    // a line a message). The reader keeps every surface's components and
    // data between calls.
    [[nodiscard]] Read feed(std::string_view messages);

private:
    struct Surface {
        std::map<std::string, Value, std::less<>> components;  // by id: {"Text": {…}}
        std::map<std::string, Value, std::less<>> data;        // signal name → value
        std::string                                root;
        bool                                       rendering = false;
    };
    std::map<std::string, Surface, std::less<>> surfaces_;

    void apply(const Value& message, std::size_t index, std::set<std::string>& changed, Read& out);
    [[nodiscard]] std::expected<std::string, std::vector<std::string>> document(std::string_view name, const Surface& surface) const;
};

}  // namespace omui::agent
