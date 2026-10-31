// The stream: text arrives in pieces (a model's tokens) and is appended to the
// document, which re-parses only its tail; the words that just arrived fade
// in — a glyph motion on the newest block over just its new bytes, so what
// was there stays still. A status line says how fast and how little it costs.
export module stream;

import std;
import openmui;
import openmui.ui.markdown;

export namespace stream {

struct Model {
    omui::Signal<std::string> source{std::string()};
    omui::Signal<std::string> status{std::string("waiting for text")};
    bool                      fade = true;     // new words fade in (off: they just appear)
    bool                      ended = false;

    // A piece of the answer (on the UI thread).
    void append(std::string_view chunk);
    // The end of the input.
    void end();

    // What the status line shows.
    std::size_t   chunks = 0;
    std::size_t   parsed = 0;          // bytes the stream parsed, all appends together
    omui::seconds first{-1}, last{0};  // when the first and the latest piece came

    omui::Runtime* runtime = nullptr;
    std::vector<std::string> playing;  // the demo's tokens (play)
    double                   rate = 30;
    void play_from(std::size_t i);
    omui::md::Stream mirror;           // the document as the component has it (for the newest block)
};

// A built-in answer, cut into tokens as a model sends them.
[[nodiscard]] std::vector<std::string> demo_tokens();
// Plays `tokens` into the model at `per_second`, on the runtime's clock.
void play(Model& m, std::vector<std::string> tokens, double per_second);
// Reads standard input on a thread of its own and appends what arrives.
void read_stdin(Model& m);

[[nodiscard]] omui::AppSpec app(Model& m);

}  // namespace stream
