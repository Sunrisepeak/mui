// mlibs.apart — data a program carries apart: a mark written before a region of a program's data that the program
// never reads itself (a shader's text a page's device compiles), so that the program's packer may take the region out
// of the program and the page give it back to whoever asks for it — fetched only where it is needed (the Web-program
// size design, §7). The program is the same with its regions in place: a build nobody packed reads them where they
// are.
//
//     // At build time, what a generator writes into the program's data:
//     const std::string carried = mlibs::apart::mark("wgsl", text.size()) + text;  // the region: `text`, after its mark
//     // At pack time, what the packer finds in the program's data (and takes out):
//     for (const mlibs::apart::Region& region : mlibs::apart::regions(data)) …
export module mlibs.apart;

import std;

export namespace mlibs::apart {

// What every mark begins with (a NUL first: no text a program reads begins so).
inline constexpr std::string_view prefix{"\0omui.apart\0", 12};

// The mark before `length` bytes of `kind` (letters, digits, `.`, `-`, `_`: a file's suffix): the prefix, the kind,
// a NUL, the length in decimal, a NUL.
[[nodiscard]] std::string mark(std::string_view kind, std::size_t length);

// A marked region of the data searched: where its mark begins, where its bytes begin, how many.
struct Region {
    std::string kind;
    std::size_t mark = 0;
    std::size_t at = 0;
    std::size_t length = 0;
};

// Every marked region of `data`, in order; a mark whose kind or length is not one, or whose bytes would run past
// the data's end, is not a mark.
[[nodiscard]] std::vector<Region> regions(std::span<const std::uint8_t> data);

}  // namespace mlibs::apart
