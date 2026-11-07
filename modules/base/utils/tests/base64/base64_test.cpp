// mlibs.base64 against RFC 4648's test vectors (§10), both ways, and the
// errors decoding reports.
import std;
import mlibs.base64;
import mlibs.testing;

using namespace mlibs::testing;

namespace {
std::vector<std::uint8_t> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"base64"};
    suite.test("RFC 4648's vectors, encoded and decoded back", [] {
        const std::array<std::pair<std::string_view, std::string_view>, 7> vectors{{
            {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}}};
        for (const auto& [plain, coded] : vectors) {
            check_eq(mlibs::base64::encode(bytes_of(plain)), std::string(coded));
            const auto back = mlibs::base64::decode(coded);
            check(back.has_value() && *back == bytes_of(plain), std::format("decode {}", coded));
        }
    });
    suite.test("every byte value goes there and back; wrapping and missing padding are read", [] {
        std::vector<std::uint8_t> all(256);
        for (std::size_t value = 0; value < all.size(); ++value) all[value] = static_cast<std::uint8_t>(value);
        const std::string coded = mlibs::base64::encode(all);
        check(mlibs::base64::decode(coded).value_or(std::vector<std::uint8_t>{}) == all);
        check(mlibs::base64::decode("Zm9v\nYmE").value_or(std::vector<std::uint8_t>{}) == bytes_of("fooba"));
    });
    suite.test("what is not base64 is an error naming where", [] {
        const auto wrong = mlibs::base64::decode("Zm9v*mFy");
        check(!wrong.has_value() && wrong.error().contains("at 4"), "a symbol outside the alphabet");
        check(!mlibs::base64::decode("Zm9vY").has_value(), "a lone symbol at the end");
        check(!mlibs::base64::decode("Zg==Zg==").has_value(), "data after padding");
    });
    return suite.run(argc, argv);
}
