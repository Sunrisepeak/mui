// gongfu.data — characters and rules as files (D-85 §8.3).
//
//     auto kata = gongfu::data::read_fighter(text, "kata.fighter.gongfu.toml");
//     if (!kata) for (const auto& d : kata.error()) std::println("{}", d.text());
//     //   kata.fighter.gongfu.toml:14:10: `5LP`: frames 4–30 of a move of 12 … 
//
// A fighter file names its body and its moves; a system file the game's
// rules. Every problem is reported at once, each with its line and column —
// what the format checks (keys, types) and what gongfu::compile checks
// (frames inside the move, cancels into moves that exist) alike — and a key
// misspelt with the nearest one. Moves are asked for in numpad notation:
//
//     LP, 2LK, 6HP             a button, with a direction held (2 down, 6 forward)
//     236P, 623K, 63214HP      a motion, then a button (P any punch, K any kick)
//     [4]6P, [2]8K, [4:60]6P   a charge (back or down, 40 frames or as given), then the release
//     LP+LK                    buttons together
export module gongfu.data;

import std;
export import gongfu;

export namespace gongfu::data {

struct Diagnostic {
    std::string file;
    int         line = 0, column = 0;
    std::string message;
    std::string hint;
    [[nodiscard]] std::string text() const;
};

// The hint for a key no table of its kind has: the nearest that is ("did you
// mean `damage`?"), else all of them. Other gongfu formats (a look) use it too.
[[nodiscard]] std::string nearest_hint(std::string_view name, std::span<const std::string_view> keys);

// A move's input in numpad notation, and back.
[[nodiscard]] std::expected<Command, std::string> parse_command(std::string_view notation);
[[nodiscard]] std::string notation(const Command& command);

[[nodiscard]] std::expected<Fighter, std::vector<Diagnostic>> read_fighter(std::string_view source, std::string_view file);
[[nodiscard]] std::expected<SystemDef, std::vector<Diagnostic>> read_system(std::string_view source, std::string_view file);

// A fighter's baked hurtboxes, `<name>.boxes.gongfu.toml` (G-5): per move,
// the frames on which its limbs reach out of its stance's box. gongfu.rules
// writes it from the look and the fighter (`from` is their hash) and checks
// at build time that it is what they make now; nobody edits it by hand.
struct Baked {
    std::string fighter;
    std::string from;  // "fnv1a:<16 hex>" of the fighter's and the look's files
    std::vector<std::pair<std::string, std::vector<HurtDef>>> moves;
};
[[nodiscard]] std::expected<Baked, std::vector<Diagnostic>> read_boxes(std::string_view source, std::string_view file);
[[nodiscard]] std::string write_boxes(const Baked& baked);
// The fighter with its baked hurtboxes: a move named there that the fighter
// does not have, or that has hurtboxes written by hand, is a problem.
[[nodiscard]] std::expected<Fighter, std::vector<Diagnostic>> read_fighter(std::string_view source, std::string_view file, std::string_view boxes,
                                                                         std::string_view boxes_file);

}  // namespace gongfu::data
