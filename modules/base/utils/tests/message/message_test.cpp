// mlibs.message: words and values as std::format writes `{}`, without a locale.
import std;
import mcpplibs.mtest;
import mlibs.message;

using namespace mcpplibs::mtest;
namespace message = mlibs::message;

UTest cases{"mlibs.message", [] {
    "each `{}` the next value, written as std::format writes it"_test = [] {
        expect(eq(message::format("{} normals for {} positions", std::size_t{3}, 4), std::string("3 normals for 4 positions")));
        const std::string name = "floor";
        expect(eq(message::format("`{}` at {}: {} ({})", name, -12, 0.1, true), std::format("`{}` at {}: {} ({})", name, -12, 0.1, true)));
        for (const double value : {1.0, 0.5, 1e-7, 123456789.125, -3.25, 1e21})
            expect(eq(message::format("{}", value), std::format("{}", value))) << value;
        for (const float value : {0.1f, 3.0f}) expect(eq(message::format("{}", value), std::format("{}", static_cast<double>(value))));
        expect(eq(message::format("{}{}", 'x', std::string_view("y")), std::string("xy")));
        expect(eq(message::format("{}", std::uint64_t{18446744073709551615ull}), std::string("18446744073709551615")));
    };

    "braces doubled are braces; no values, the words alone"_test = [] {
        expect(eq(message::format("{{{}}} and }}", 7), std::string("{7} and }")));
        expect(eq(message::format("nothing to say"), std::string("nothing to say")));
    };
}};
