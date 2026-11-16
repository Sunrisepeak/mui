// mlibs.xdef: a data file's name and format identifier, and a reader's first check of a snapshot.
import std;
import mcpplibs.mtest;
import mlibs.xdef;

using namespace mcpplibs::mtest;
namespace xdef = mlibs::xdef;

UTest cases{"mlibs.xdef", [] {
    "a data file's name: <name>.<kind>.xdef.<ts|json|jsonl>, and the earlier container's"_test = [] {
        const auto rise = xdef::parse_name("rise.animation.xdef.json");
        expect(rise && rise->name == "rise" && rise->kind == "animation" && rise->container == "xdef" && rise->extension == "json");
        // The earlier suffix composed, not written: a migration of this repository leaves this test as it is.
        const auto dotted = xdef::parse_name(std::format("caption.button.contract.{}.ts", xdef::earlier_container));
        expect(dotted && dotted->name == "caption.button" && dotted->kind == "contract" && dotted->container == "omui");
        expect(!xdef::parse_name("README.md")) << "not data";
        expect(!xdef::parse_name("skill.omui.md")) << "markdown is no container's";
        expect(!xdef::parse_name("claims.xdef.json")) << "a name and a kind both";
        expect(!xdef::parse_name("a.Kind.xdef.json")) << "a kind is lowercase";
    };

    "a format identifier: <kind>@<major>, the major from 1"_test = [] {
        const auto ops = xdef::parse_format("ops@1");
        expect(ops && ops->same({"ops", 1}));
        expect(xdef::format_id({"animation", 3}) == "animation@3");
        expect(!xdef::parse_format("ops"));
        expect(!xdef::parse_format("ops@0"));
        expect(!xdef::parse_format("ops@1x"));
    };

    "a reader's first check: the snapshot's $format names its kind at its major"_test = [] {
        expect(!xdef::format_problem("matter@1", "matter", 1));
        expect(xdef::format_problem(std::nullopt, "matter", 1).value_or("").contains("no \"$format\""));
        expect(xdef::format_problem("ops@1", "matter", 1).value_or("").contains("a ops snapshot, not a matter one"));
        expect(xdef::format_problem("matter@2", "matter", 1).value_or("").contains("major version 2"));
        expect(xdef::format_problem("matter", "matter", 1).value_or("").contains("no format identifier"));
    };
}};
