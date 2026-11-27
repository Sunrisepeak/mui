// mlibs.compose: a slot filled, left empty, and said.
import std;
import mlibs.compose;
import mlibs.error;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace compose = mlibs::compose;

namespace {

struct Greeter {
    std::string_view (*greet)() = nullptr;
};
struct Decoder {
    std::string_view format;
};

inline constexpr compose::Slot<Greeter> greeter{{.id = "test.greeter",
                                                 .version = 2,
                                                 .summary = "says hello",
                                                 .absent = "install test::hello at the program's root"}};
inline constexpr compose::Slot<Decoder> decoders{{.id = "test.decoder", .cardinality = compose::Cardinality::many, .summary = "reads a format"}};

std::string_view hello() { return "hello"; }
std::string_view hi() { return "hi"; }

}  // namespace

UTest cases{"compose", [] {
    "a slot of one: installed, read, and a second refused"_test = [] {
        compose::Composition composition;
        expect(composition.get(greeter) == nullptr) << "absent until installed";
        static const Greeter first{&hello};
        static const Greeter second{&hi};
        expect(composition.install(greeter, first, "hello").has_value());
        const Greeter* got = composition.get(greeter);
        expect(got == &first && got->greet() == "hello");
        expect(composition.provider(greeter.info) == "hello");
        const auto again = composition.install(greeter, second, "hi");
        expect(!again.has_value() && again.error().code == compose::Errc::occupied) << "one provider";
        expect(again.error().message.contains("hello") && again.error().message.contains("hi")) << "names both";
        expect(composition.get(greeter) == &first) << "the first stays";
    };

    "a slot of many: every provider, in the order installed"_test = [] {
        compose::Composition composition;
        static const Decoder png{"png"}, jpeg{"jpeg"};
        expect(composition.all(decoders).empty());
        expect(composition.install(decoders, png, "png").has_value() && composition.install(decoders, jpeg, "jpeg").has_value());
        const std::vector<const Decoder*> all = composition.all(decoders);
        expect(all.size() == 2u && all[0]->format == "png" && all[1]->format == "jpeg");
    };

    "absent is one state: unsaid, left out, the platform's — each refused with the slot and the way out"_test = [] {
        compose::Composition composition;
        const compose::Error unsaid = composition.missing(greeter.info);
        expect(unsaid.code == compose::Errc::absent && unsaid.kind() == mlibs::error::Kind::refused);
        expect(unsaid.message.contains("test.greeter") && unsaid.message.contains("install test::hello")) << unsaid.message;
        expect(unsaid.where == "test.greeter");
        composition.leave_out(greeter.info, compose::Absence::left_out);
        expect(composition.get(greeter) == nullptr);
        expect(composition.missing(greeter.info).message.contains("built without")) << composition.missing(greeter.info).message;
        composition.leave_out(greeter.info, compose::Absence::platform);
        const compose::Error platform = composition.missing(greeter.info);
        expect(platform.message.contains("platform") && !platform.message.contains("install test::hello")) << "no way out a platform lacks";
    };

    "frozen: nothing more is installed, what is there is read"_test = [] {
        compose::Composition composition;
        static const Greeter first{&hello};
        static const Decoder png{"png"};
        expect(composition.install(greeter, first, "hello").has_value());
        composition.freeze();
        expect(composition.frozen());
        const auto late = composition.install(decoders, png, "png");
        expect(!late.has_value() && late.error().code == compose::Errc::frozen);
        expect(composition.get(greeter) == &first && composition.all(decoders).empty());
        expect(composition.missing(decoders.info).code == compose::Errc::absent);
    };

    "describe: what is installed, and what is absent with why and the way out"_test = [] {
        compose::Composition composition;
        static const Decoder png{"png"};
        expect(composition.install(decoders, png, "png").has_value());
        (void)composition.missing(greeter.info);  // asked for and missing
        const std::vector<compose::Described> said = composition.describe();
        expect(said.size() == 2u);
        expect(said[0].slot == "test.decoder" && said[0].providers == std::vector<std::string>{"png"} && said[0].way.empty());
        expect(said[1].slot == "test.greeter" && said[1].version == 2 && said[1].providers.empty());
        expect(said[1].absence == compose::Absence::unsaid && said[1].way == "install test::hello at the program's root");
        expect(said[1].summary == "says hello");
    };

    "two compositions in one process are apart; the process has one of its own"_test = [] {
        compose::Composition one, other;
        static const Greeter first{&hello};
        expect(one.install(greeter, first, "hello").has_value());
        expect(other.get(greeter) == nullptr);
        expect(&compose::process() == &compose::process());
    };
}};
