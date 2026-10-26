import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

int main(int argc, char** argv) {
    Suite s{"spec.envelope"};

    s.test("needs-answer is an error record and exit code 2 (S-2)", [] {
        spec::Envelope e;
        spec::Value options;
        options.set("answer", spec::Value::List{"yes"});
        e.error(spec::error_code::needs_answer, "confirm deleting the note", options).result(spec::exit_code::needs_answer);
        check_eq(e.exit_code().value_or(-1), 2);
        check_eq(e.ndjson(), std::string(
            "{\"kind\":\"error\",\"code\":\"needs-answer\",\"message\":\"confirm deleting the note\",\"answer\":[\"yes\"]}\n"
            "{\"kind\":\"result\",\"exitCode\":2}\n"));
    });

    s.test("data and the last data record", [] {
        spec::Envelope e;
        e.phase("find").data(spec::Value(1)).data(spec::Value(2)).result(0);
        check(e.data_value() && e.data_value()->as_int() == 2);
        check(e.records().size() == 4);
    });

    s.test("actors in openas text form", [] {
        auto a = spec::Actor::parse("agent:planner;via=person:alice");
        check(a.has_value());
        check_eq(a->kind, std::string("agent"));
        check_eq(a->name, std::string("planner"));
        check(a->via.size() == 1 && a->via[0] == "person:alice");
        check_eq(a->text(), std::string("agent:planner;via=person:alice"));
        check(spec::Actor::parse("agent:x;budget=3").has_value(), "unknown keys are skipped");
        check(!spec::Actor::parse("planner").has_value());
        check(!spec::Actor::parse("agent:x;via=alice").has_value());
    });

    return s.run(argc, argv);
}
