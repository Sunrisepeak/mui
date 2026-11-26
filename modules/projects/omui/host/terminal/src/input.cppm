// omui.host.terminal:input — a terminal's input bytes as the events every host delivers (terminal design §3 ①).
//
// Keys (ECMA-48's and xterm's sequences, their modifiers), text (UTF-8, a key and its text as a window's host
// gives them), the mouse (SGR mode 1006: buttons, drags, motion and the wheel, at the centre of the cell they name),
// pasted text (bracketed paste, mode 2004) and focus (mode 1004). Bytes come in pieces: a sequence cut at a piece's
// end is kept for the next, and an Escape alone is a key once nothing follows it (`flush`). Focus, hit testing,
// scrolling and an agent's operations never know they are in a terminal.
export module omui.host.terminal:input;

import std;
import omui.core;

export namespace omui::terminal {

// The grid a cell's position becomes a point by (the interface's logical pixels).
struct CellSize {
    float column = 8;
    float row = 16;
};

class Input {
public:
    explicit Input(CellSize cell = {}) : cell_(cell) {}

    // The events of the bytes given and any kept from before; a sequence cut short is kept for the next call.
    [[nodiscard]] std::vector<InputEvent> feed(std::string_view bytes);
    // What is kept, as it stands: an Escape alone is the key (a person pressed it, and nothing followed).
    [[nodiscard]] std::vector<InputEvent> flush();
    // Bytes are kept: a sequence not ended yet.
    [[nodiscard]] bool pending() const noexcept { return !kept_.empty(); }
    // Ctrl+C was pressed (ETX): the program's end, as a terminal's person asks for it.
    [[nodiscard]] bool interrupted() const noexcept { return interrupted_; }

private:
    // One event from the front of `bytes`: how many bytes it took (0: a sequence not ended yet).
    std::size_t decode(std::string_view bytes, std::vector<InputEvent>& out);
    std::size_t csi(std::string_view bytes, std::vector<InputEvent>& out);
    std::size_t mouse(std::string_view sequence, std::vector<InputEvent>& out);

    CellSize    cell_;
    std::string kept_;
    std::string pasting_;
    bool        in_paste_ = false;
    bool        interrupted_ = false;
    std::uint32_t buttons_ = 0;
};

}  // namespace omui::terminal
