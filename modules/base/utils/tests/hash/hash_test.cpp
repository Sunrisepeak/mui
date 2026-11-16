// mlibs.hash against FNV's own reference vectors, and each way a value goes
// in written out by hand: what every pinned hash in the repository relies on.
// (mlibs.testing hashes with this package, so these checks are plain.)
import std;
import mlibs.hash;

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
    using mlibs::hash::Fnv1a;
    // FNV's test suite (www.isthe.com/chongo/tech/comp/fnv): 64- and 32-bit FNV-1a.
    check(mlibs::hash::fnv1a("") == 0xcbf29ce484222325ull, "64: the empty string is the offset basis");
    check(mlibs::hash::fnv1a("a") == 0xaf63dc4c8601ec8cull && mlibs::hash::fnv1a("foobar") == 0x85944171f73967e8ull, "64: a, foobar");
    check(mlibs::hash::fnv1a32("a") == 0xe40c292cu && mlibs::hash::fnv1a32("foobar") == 0xbf9cf968u, "32: a, foobar");
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
    check(Fnv1a(Fnv1a().text("ab").value()).text("c").value() == mlibs::hash::fnv1a("abc"), "going on from a hash");
    const std::array<std::byte, 3> abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    check(mlibs::hash::fnv1a(abc) == mlibs::hash::fnv1a("abc"), "bytes and text the same");
    check(Fnv1a().text("foobar").hex() == "85944171f73967e8", "hex: sixteen digits");
    struct Plain {
        std::uint32_t a = 1, b = 2;
    };
    check(Fnv1a().object(Plain{}).value() == Fnv1a().object(std::uint32_t{1}).object(std::uint32_t{2}).value(), "object: its own bytes");
    // SHA-256 and SHA-1 against FIPS 180's examples, a message across two blocks, and the RFC 6455
    // accept key's digest.
    check(mlibs::hash::sha256_hex("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256: the empty message");
    check(mlibs::hash::sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256: abc");
    check(mlibs::hash::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256: 448 bits, two blocks");
    check(mlibs::hash::sha256_hex(std::string(1000, 'a'))
              .starts_with("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"),
          "sha256: a thousand bytes");
    const auto sha1_hex = [](std::string_view text) {
        std::string out;
        for (const std::uint8_t byte : mlibs::hash::sha1(text))
            out += std::format("{:02x}", byte);
        return out;
    };
    check(sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1: abc");
    check(sha1_hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1: the empty message");
    check(sha1_hex("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11") ==
              "b37a4f2cc0624f1690f64606cf385945b2bec4ea",
          "sha1: RFC 6455's handshake example");
    std::println("hash: {} failed", failures);
    return failures == 0 ? 0 : 1;
}
