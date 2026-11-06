// openmui.hash against FNV's own reference vectors, and each way a value goes
// in written out by hand: what every pinned hash in the repository relies on.
// (openmui.testing hashes with this package, so these checks are plain.)
import std;
import openmui.hash;

namespace {

int failures = 0;
void check(bool ok, std::string_view what) {
    if (!ok) {
        ++failures;
        std::println("  FAIL  {}", what);
    }
}

std::uint64_t by_hand(std::span<const std::uint8_t> b) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const std::uint8_t x : b) h = (h ^ x) * 0x100000001b3ull;
    return h;
}

}  // namespace

int main() {
    using omui::hash::Fnv1a;
    // FNV's test suite (www.isthe.com/chongo/tech/comp/fnv): 64- and 32-bit FNV-1a.
    check(omui::hash::fnv1a("") == 0xcbf29ce484222325ull, "64: the empty string is the offset basis");
    check(omui::hash::fnv1a("a") == 0xaf63dc4c8601ec8cull && omui::hash::fnv1a("foobar") == 0x85944171f73967e8ull, "64: a, foobar");
    check(omui::hash::fnv1a32("a") == 0xe40c292cu && omui::hash::fnv1a32("foobar") == 0xbf9cf968u, "32: a, foobar");
    // Numbers: their little-endian bytes.
    const std::array<std::uint8_t, 8> le{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    check(Fnv1a().u64(0x0102030405060708ull).value() == by_hand(le), "u64: eight bytes, least significant first");
    check(Fnv1a().le(0x0a0b0c0d, 4).value() == by_hand(std::array<std::uint8_t, 4>{0x0d, 0x0c, 0x0b, 0x0a}), "le: four bytes");
    check(Fnv1a().f32(1.0f).value() == Fnv1a().u64(0x3f800000u).value(), "f32: its bits, widened");
    check(Fnv1a().f64(1.0).value() == Fnv1a().u64(0x3ff0000000000000ull).value(), "f64: its bits");
    // A string and its length: one of a sequence.
    check(Fnv1a().str("ab").str("c").value() != Fnv1a().str("a").str("bc").value(), "str: kept apart");
    check(Fnv1a().str("ab").value() == Fnv1a().text("ab").u64(2).value(), "str: the bytes, then the length");
    // A word: one multiply.
    check(Fnv1a().word(5).value() == ((0xcbf29ce484222325ull ^ 5) * 0x100000001b3ull), "word: folded at once");
    // Going on from a value; the same bytes as one run.
    check(Fnv1a(Fnv1a().text("ab").value()).text("c").value() == omui::hash::fnv1a("abc"), "going on from a hash");
    const std::array<std::byte, 3> abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    check(omui::hash::fnv1a(abc) == omui::hash::fnv1a("abc"), "bytes and text the same");
    check(Fnv1a().text("foobar").hex() == "85944171f73967e8", "hex: sixteen digits");
    struct Plain {
        std::uint32_t a = 1, b = 2;
    };
    check(Fnv1a().object(Plain{}).value() == Fnv1a().object(std::uint32_t{1}).object(std::uint32_t{2}).value(), "object: its own bytes");
    std::println("hash: {} failed", failures);
    return failures == 0 ? 0 : 1;
}
