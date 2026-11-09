import std;
import omui.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"core.verb", [] {

    "names"_test = [] {
        expect(omui::valid_verb_name("note.delete"));
        expect(omui::valid_verb_name("app.open_file2"));
        expect(!omui::valid_verb_name("Note.delete"));
        expect(!omui::valid_verb_name("note..delete"));
        expect(!omui::valid_verb_name(".note"));
        expect(!omui::valid_verb_name("note."));
        expect(!omui::valid_verb_name("note.1st"));
    };

    "S-1: an irreversible verb states its answer"_test = [] {
        omui::Verbs verbs;
        auto r = verbs.add({.name = "note.delete", .effect = omui::Effect::irreversible},
                           [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; });
        expect(!r.has_value());
        expect(r.error().find("S-1") != std::string::npos);
    };

    "S-2: needs-answer at once, then runs with the answer"_test = [] {
        omui::Verbs verbs;
        int deleted = 0;
        expect(verbs.add({.name = "note.delete", .summary = "Delete the note", .effect = omui::Effect::irreversible, .answer = "yes"},
                        [&](const omui::Value& args) -> omui::VerbResult {
                            ++deleted;
                            return omui::Value(args.get("id") ? args.get("id")->as_string() : "");
                        }).has_value());
        omui::Value args;
        args.set("id", "n1");
        auto first = verbs.invoke("note.delete", {.args = args, .actor = {"agent", "planner", {"person:alice"}}});
        expect(eq(first.exit_code().value_or(-1), 2));
        expect(eq(deleted, 0));
        expect(first.ndjson().find("\"needs-answer\"") != std::string::npos);
        auto second = verbs.invoke("note.delete", {.args = args, .answer = "yes"});
        expect(eq(second.exit_code().value_or(-1), 0));
        expect(eq(deleted, 1));
        expect(eq(second.data_value()->as_string(), std::string_view("n1")));
        expect(verbs.journal().size() == 2);
        expect(eq(verbs.journal()[0].actor, std::string("agent:planner;via=person:alice")));
        expect(eq(verbs.journal()[0].exit_code, 2));
    };

    "unknown verbs and failures"_test = [] {
        omui::Verbs verbs;
        expect(eq(verbs.invoke("nope", {}).exit_code().value_or(-1), 1));
        expect(verbs.add({.name = "x.fail"}, [](const omui::Value&) -> omui::VerbResult { return std::unexpected("disk full"); }).has_value());
        auto e = verbs.invoke("x.fail", {});
        expect(eq(e.exit_code().value_or(-1), 1));
        expect(e.ndjson().find("disk full") != std::string::npos);
        expect(!verbs.add({.name = "x.fail"}, [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; }).has_value()) << "a name is registered once";
        expect(verbs.remove("x.fail") && verbs.find("x.fail") == nullptr);
    };

    "S-13: a dry run is checked as the call would be, answers its outcome, and runs nothing"_test = [] {
        omui::Verbs verbs;
        int ran = 0;
        expect(verbs.add({.name = "note.delete", .effect = omui::Effect::irreversible, .answer = "yes"}, [&](const omui::Value&) -> omui::VerbResult {
                        ++ran;
                        return omui::Value{};
                    }).has_value());
        auto outcome = [](const omui::Envelope& e) { return std::string(e.data_value()->get("outcome")->as_string()); };
        auto asks = verbs.invoke("note.delete", {.dry_run = true});
        expect(asks.exit_code() == 0 && outcome(asks) == "needs-answer" && asks.data_value()->get("answer")->as_string() == "yes") << asks.ndjson();
        auto runs = verbs.invoke("note.delete", {.answer = "yes", .dry_run = true});
        expect(outcome(runs) == "run" && runs.data_value()->get("effect")->as_string() == "irreversible" &&
                  !runs.data_value()->get("undoable")->as_bool()) << runs.ndjson();
        auto none = verbs.invoke("note.nothing", {.dry_run = true});
        expect(none.exit_code() == 0 && outcome(none) == "fails" && none.data_value()->get("error")->get("code")->as_string() == "not-found") << none.ndjson();
        expect(ran == 0 && verbs.journal().empty()) << "nothing ran, nothing journaled";
        // A reversible verb whose function records its own way back says so; one that does not, does not.
        expect(verbs.add({.name = "pack.show", .effect = omui::Effect::reversible, .undoable = true}, [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; }).has_value());
        expect(verbs.add({.name = "pack.poke", .effect = omui::Effect::reversible}, [](const omui::Value&) -> omui::VerbResult { return omui::Value{}; }).has_value());
        expect(verbs.invoke("pack.show", {.dry_run = true}).data_value()->get("undoable")->as_bool()) << "pack.show: undoable";
        expect(!verbs.invoke("pack.poke", {.dry_run = true}).data_value()->get("undoable")->as_bool()) << "pack.poke: not";
    };
}};
