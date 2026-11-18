// omui-devtools atspi without a bus: the roles a screen reader speaks of, and the
// report — every check said, passed only when all passed and there was one.
import std;
import mcpplibs.mtest;
import omui.devtools.atspi;

using namespace mcpplibs::mtest;
namespace atspi = omui::devtools::atspi;

static UTest cases{"devtools.atspi", [] {
    "AT-SPI's role numbers by their names; one not spoken of by its number"_test = [] {
        expect(eq(atspi::role_name(43), std::string("button")) && eq(atspi::role_name(51), std::string("slider")) && eq(atspi::role_name(79), std::string("entry")));
        expect(eq(atspi::role_name(999), std::string("role 999")));
    };

    "the report: passed when every check did, never when there was none; as JSON with each check"_test = [] {
        atspi::Report report;
        expect(!report.ok()) << "no check is no pass";
        report.checks = {{true, "a window"}, {true, "the +1 button"}};
        report.trees["Tally"] = {{"Tally", "frame", 0}, {"+1", "button", 1}};
        report.goal_value = 10;
        expect(report.ok());
        const std::string written = report.json();
        expect(written.contains("\"ok\": true") && written.contains("\"what\": \"the +1 button\"") && written.contains("\"tally\"") && written.contains("\"goal_value\": 10"))
            << written;
        report.checks.push_back({false, "the count reads 1 afterwards"});
        expect(!report.ok());
    };
}};
