import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.verb"};

    s.test("names", [] {
        check(omui::valid_verb_name("note.delete"));
        check(omui::valid_verb_name("app.open_file2"));
        check(!omui::valid_verb_name("Note.delete"));
        check(!omui::valid_verb_name("note..delete"));
        check(!omui::valid_verb_name(".note"));
        check(!omui::valid_verb_name("note."));
        check(!omui::valid_verb_name("note.1st"));
    });

    s.test("S-1: an irreversible verb states its answer", [] {
        omui::Verbs verbs;
        auto r = verbs.add({.name = "note.delete", .effect = omui::Effect::irreversible},
                           [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; });
        check(!r.has_value());
        check(r.error().find("S-1") != std::string::npos);
    });

    s.test("S-2: needs-answer at once, then runs with the answer", [] {
        omui::Verbs verbs;
        int deleted = 0;
        check(verbs.add({.name = "note.delete", .summary = "Delete the note", .effect = omui::Effect::irreversible, .answer = "yes"},
                        [&](const omui::Value& args) -> omui::VerbResult {
                            ++deleted;
                            return omui::Value(args.get("id") ? args.get("id")->as_string() : "");
                        }).has_value());
        omui::Value args;
        args.set("id", "n1");
        auto first = verbs.invoke("note.delete", {.args = args, .actor = {"agent", "planner", {"person:alice"}}});
        check_eq(first.exit_code().value_or(-1), 2);
        check_eq(deleted, 0);
        check(first.ndjson().find("\"needs-answer\"") != std::string::npos);
        auto second = verbs.invoke("note.delete", {.args = args, .answer = "yes"});
        check_eq(second.exit_code().value_or(-1), 0);
        check_eq(deleted, 1);
        check_eq(second.data_value()->as_string(), std::string_view("n1"));
        check(verbs.journal().size() == 2);
        check_eq(verbs.journal()[0].actor, std::string("agent:planner;via=person:alice"));
        check_eq(verbs.journal()[0].exit_code, 2);
    });

    s.test("unknown verbs and failures", [] {
        omui::Verbs verbs;
        check_eq(verbs.invoke("nope", {}).exit_code().value_or(-1), 1);
        check(verbs.add({.name = "x.fail"}, [](const omui::Value&) -> omui::VerbResult { return std::unexpected("disk full"); }).has_value());
        auto e = verbs.invoke("x.fail", {});
        check_eq(e.exit_code().value_or(-1), 1);
        check(e.ndjson().find("disk full") != std::string::npos);
        check(!verbs.add({.name = "x.fail"}, [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; }).has_value(),
              "a name is registered once");
        check(verbs.remove("x.fail") && verbs.find("x.fail") == nullptr);
    });

    s.test("S-13: a dry run is checked as the call would be, answers its outcome, and runs nothing", [] {
        omui::Verbs verbs;
        int ran = 0;
        check(verbs.add({.name = "note.delete", .effect = omui::Effect::irreversible, .answer = "yes"}, [&](const omui::Value&) -> omui::VerbResult {
                        ++ran;
                        return omui::Value{};
                    }).has_value());
        auto outcome = [](const omui::Envelope& e) { return std::string(e.data_value()->get("outcome")->as_string()); };
        auto asks = verbs.invoke("note.delete", {.dry_run = true});
        check(asks.exit_code() == 0 && outcome(asks) == "needs-answer" && asks.data_value()->get("answer")->as_string() == "yes", asks.ndjson());
        auto runs = verbs.invoke("note.delete", {.answer = "yes", .dry_run = true});
        check(outcome(runs) == "run" && runs.data_value()->get("effect")->as_string() == "irreversible" &&
                  !runs.data_value()->get("undoable")->as_bool(),
              runs.ndjson());
        auto none = verbs.invoke("note.nothing", {.dry_run = true});
        check(none.exit_code() == 0 && outcome(none) == "fails" && none.data_value()->get("error")->get("code")->as_string() == "not-found", none.ndjson());
        check(ran == 0 && verbs.journal().empty(), "nothing ran, nothing journaled");
    });

    return s.run(argc, argv);
}
